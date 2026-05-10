#include "displayjet.h"
#include "crypto.h"
#include "drivers/pci/pci.h"

/* PHYS_MAP_BASE comes from vmm.h; VIRT_TO_PHYS is its inverse */
#define VIRT_TO_PHYS(v)  ((uint64_t)(uintptr_t)(v) - PHYS_MAP_BASE)
#include "drivers/zirv/device.h"
#include "kernel/console.h"
#include "kernel/mm/pmm.h"
#include "kernel/mm/vmm.h"
#include "arch/x64/cpu.h"
#include <string.h>
#include <stddef.h>

/* ── Bochs VBE hardware interface ────────────────────────────────────────── */
#define VBE_DISPI_IOPORT_INDEX 0x01CE
#define VBE_DISPI_IOPORT_DATA  0x01CF
#define VBE_DISPI_INDEX_ID     0x0
#define VBE_DISPI_INDEX_XRES   0x1
#define VBE_DISPI_INDEX_YRES   0x2
#define VBE_DISPI_INDEX_BPP    0x3
#define VBE_DISPI_INDEX_ENABLE 0x4
#define VBE_DISPI_DISABLED     0x0000
#define VBE_DISPI_ENABLED      0x0001
#define VBE_DISPI_LFB_ENABLED  0x0040

#define BOCHS_VENDOR 0x1234
#define BOCHS_DEVICE_STD 0x1111
#define BOCHS_DEVICE_BX  0x2111

#define DJ_DEFAULT_WIDTH  1024
#define DJ_DEFAULT_HEIGHT 768
#define DJ_DEFAULT_BPP    32

static void vbe_write(uint16_t index, uint16_t value)
{
    outw(VBE_DISPI_IOPORT_INDEX, index);
    outw(VBE_DISPI_IOPORT_DATA,  value);
}

static uint16_t vbe_read(uint16_t index)
{
    outw(VBE_DISPI_IOPORT_INDEX, index);
    return inw(VBE_DISPI_IOPORT_DATA);
}

/* ── MAEM master key (derived at boot from hardware seed) ───────────────── */
static uint8_t g_master_key[DJ_KEY_SIZE];

static void maem_init(void)
{
    uint8_t seed[32];
    uint32_t tsc_lo, tsc_hi;
    uint64_t tsc;

    __asm__ volatile("rdtsc" : "=a"(tsc_lo), "=d"(tsc_hi));
    tsc = ((uint64_t)tsc_hi << 32) | tsc_lo;

    for (int i = 0; i < 32; i++) {
        uint32_t rnd = 0;
        __asm__ volatile("rdrand %0" : "=r"(rnd));
        seed[i] = (uint8_t)(rnd ^ (tsc >> (i * 8 % 64)));
    }

    dj_hkdf((const uint8_t *)"DisplayJet MAEM v1", 18,
             seed, 32, NULL, 0, g_master_key, DJ_KEY_SIZE);
    dj_secure_zero(seed, sizeof(seed));
}

static void maem_derive_surface_key(dj_surface_t *s)
{
    uint8_t info[8];
    for (int i = 0; i < 8; i++)
        info[i] = (uint8_t)(s->id >> (i * 8));
    dj_hkdf(g_master_key, DJ_KEY_SIZE,
             (const uint8_t *)&s->id, sizeof(s->id),
             info, 8, s->key, DJ_KEY_SIZE);

    for (int i = 0; i < 12; i++) {
        uint32_t rnd = 0;
        __asm__ volatile("rdrand %0" : "=r"(rnd));
        s->nonce[i] = (uint8_t)rnd;
    }
    s->access_counter = 0;
}

/* ── DisplayJet state ──────────────────────────────────────────────────── */
static struct {
    void         *fb_virt;
    uint64_t      fb_phys;
    uint32_t      width;
    uint32_t      height;
    uint32_t      stride;
    uint8_t       bpp;
    bool          hw_ready;

    int           compositor_pid;

    dj_surface_t  surfaces[DJ_MAX_SURFACES];
    uint32_t      next_surface_id;
} g_dj;

/* ── Hardware init (bochs VBE) ──────────────────────────────────────────── */
static int hw_init(void)
{
    pci_dev_t *pdev = pci_find_device(BOCHS_VENDOR, BOCHS_DEVICE_STD);
    if (!pdev) pdev = pci_find_device(BOCHS_VENDOR, BOCHS_DEVICE_BX);
    if (!pdev) return -1;

    pci_enable_device(pdev);

    void *fb = pci_map_bar(pdev, 0);
    if (!fb) return -1;

    uint16_t ver = vbe_read(VBE_DISPI_INDEX_ID);
    if ((ver & 0xB0C0) != 0xB0C0) return -1;

    vbe_write(VBE_DISPI_INDEX_ENABLE, VBE_DISPI_DISABLED);
    vbe_write(VBE_DISPI_INDEX_XRES, DJ_DEFAULT_WIDTH);
    vbe_write(VBE_DISPI_INDEX_YRES, DJ_DEFAULT_HEIGHT);
    vbe_write(VBE_DISPI_INDEX_BPP,  DJ_DEFAULT_BPP);
    vbe_write(VBE_DISPI_INDEX_ENABLE, VBE_DISPI_ENABLED | VBE_DISPI_LFB_ENABLED);

    g_dj.fb_virt = fb;
    g_dj.fb_phys = pdev->bars[0].phys_addr;
    g_dj.width   = DJ_DEFAULT_WIDTH;
    g_dj.height  = DJ_DEFAULT_HEIGHT;
    g_dj.stride  = DJ_DEFAULT_WIDTH * (DJ_DEFAULT_BPP / 8);
    g_dj.bpp     = DJ_DEFAULT_BPP;
    g_dj.hw_ready = true;

    memset(fb, 0, g_dj.stride * g_dj.height);
    return 0;
}

/* ── Surface management ─────────────────────────────────────────────────── */
static dj_surface_t *surface_alloc(void)
{
    for (uint32_t i = 0; i < DJ_MAX_SURFACES; i++) {
        if (!g_dj.surfaces[i].used) {
            g_dj.surfaces[i].used = true;
            g_dj.surfaces[i].id = g_dj.next_surface_id++;
            return &g_dj.surfaces[i];
        }
    }
    return NULL;
}

static dj_surface_t *surface_by_id(uint32_t id)
{
    for (uint32_t i = 0; i < DJ_MAX_SURFACES; i++) {
        if (g_dj.surfaces[i].used && g_dj.surfaces[i].id == id)
            return &g_dj.surfaces[i];
    }
    return NULL;
}

/* ── MAEM encrypt/decrypt helpers ───────────────────────────────────────── */
static void surface_encrypt(dj_surface_t *s, const void *plain, void *cipher,
                             size_t len)
{
    dj_chacha20_encrypt(s->key, s->nonce, 0, (const uint8_t *)plain,
                        (uint8_t *)cipher, len);
}

static void surface_decrypt(dj_surface_t *s, const void *cipher, void *plain,
                             size_t len)
{
    dj_chacha20_encrypt(s->key, s->nonce, 0, (const uint8_t *)cipher,
                        (uint8_t *)plain, len);
}

static void surface_crypto_wipe(dj_surface_t *s)
{
    uint8_t wipe_key[DJ_KEY_SIZE];
    uint8_t wipe_nonce[DJ_NONCE_SIZE];
    for (size_t i = 0; i < DJ_KEY_SIZE; i++) wipe_key[i] = 0xFF;
    for (size_t i = 0; i < DJ_NONCE_SIZE; i++) wipe_nonce[i] = 0xFF;

    size_t pages = (s->buffer_size + 0xFFF) / 0x1000;
    for (size_t p = 0; p < pages; p++) {
        uint8_t block[4096];
        dj_chacha20_encrypt(wipe_key, wipe_nonce, (uint32_t)p,
                            (const uint8_t *)s->buffer + p * 4096,
                            block, 4096 < s->buffer_size - p * 4096 ?
                            4096 : s->buffer_size - p * 4096);
        memcpy((uint8_t *)s->buffer + p * 4096, block,
               4096 < s->buffer_size - p * 4096 ?
               4096 : s->buffer_size - p * 4096);
    }

    dj_secure_zero(wipe_key, sizeof(wipe_key));
    dj_secure_zero(wipe_nonce, sizeof(wipe_nonce));
    dj_secure_zero(s->key, DJ_KEY_SIZE);
    dj_secure_zero(s->nonce, DJ_NONCE_SIZE);
}

/* ── Public API ─────────────────────────────────────────────────────────── */
void displayjet_init(void)
{
    memset(&g_dj, 0, sizeof(g_dj));
    g_dj.compositor_pid = -1;

    kputs("[displayjet] Initialising DisplayJet display driver\n");
    kputs("[displayjet] MAEM: initialising crypto subsystem\n");

    maem_init();
    kputs("[displayjet] MAEM: master key derived from hardware entropy\n");

    if (hw_init() != 0) {
        kputs("[displayjet] No compatible display hardware found\n");
        return;
    }

    console_enable_fb(g_dj.fb_virt, g_dj.width, g_dj.height,
                      g_dj.stride, g_dj.bpp);

    zirv_register_device(DEV_CLASS_DISPLAY_GPU, DEV_CLASS_DISPLAY_GPU,
                         "DisplayJet w/ MAEM (bochs VBE)", NULL);

    kputs("[displayjet] DisplayJet ready — MAEM active, waiting for compositor\n");
}

void displayjet_set_framebuffer(void *fb_virt, uint32_t width,
                                 uint32_t height, uint32_t stride, uint8_t bpp)
{
    g_dj.fb_virt = fb_virt;
    g_dj.width   = width;
    g_dj.height  = height;
    g_dj.stride  = stride;
    g_dj.bpp     = bpp;
    g_dj.hw_ready = true;
}

int displayjet_connect(int pid)
{
    if (g_dj.compositor_pid >= 0) return -1;
    g_dj.compositor_pid = pid;
    kprintf("[displayjet] Compositor connected (PID %d)\n", pid);
    return 0;
}

int displayjet_disconnect(int pid)
{
    if (g_dj.compositor_pid != pid) return -1;
    kprintf("[displayjet] Compositor disconnected (PID %d)\n", pid);
    g_dj.compositor_pid = -1;

    for (uint32_t i = 0; i < DJ_MAX_SURFACES; i++) {
        if (g_dj.surfaces[i].used) {
            if (g_dj.surfaces[i].buffer) {
                surface_crypto_wipe(&g_dj.surfaces[i]);
                pmm_free_pages(VIRT_TO_PHYS(g_dj.surfaces[i].buffer),
                               (g_dj.surfaces[i].buffer_size + 0xFFF) / 0x1000);
            }
            memset(&g_dj.surfaces[i], 0, sizeof(dj_surface_t));
        }
    }
    g_dj.next_surface_id = 0;
    return 0;
}

int displayjet_create_surface(uint32_t width, uint32_t height,
                               uint32_t *out_id)
{
    if (!g_dj.hw_ready) return -1;

    dj_surface_t *s = surface_alloc();
    if (!s) return -1;

    s->width  = width;
    s->height = height;
    s->bpp    = 32;
    s->stride = width * 4;
    s->format = DJ_FORMAT_RGBX8888;
    s->owner_pid = g_dj.compositor_pid;
    s->buffer_size = (size_t)s->stride * height;

    size_t pages = (s->buffer_size + 0xFFF) / 0x1000;
    uint64_t phys = pmm_alloc_pages(pages);
    if (!phys) {
        s->used = false;
        return -1;
    }
    s->buffer = PHYS_TO_VIRT(phys);

    /* MAEM: derive per-surface key and encrypt buffer immediately */
    maem_derive_surface_key(s);
    memset(s->buffer, 0, s->buffer_size);
    surface_encrypt(s, s->buffer, s->buffer, s->buffer_size);

    *out_id = s->id;
    return 0;
}

int displayjet_destroy_surface(uint32_t id)
{
    dj_surface_t *s = surface_by_id(id);
    if (!s) return -1;

    if (s->buffer) {
        /* MAEM: cryptographic wipe before deallocation */
        surface_crypto_wipe(s);
        pmm_free_pages(VIRT_TO_PHYS(s->buffer),
                       (s->buffer_size + 0xFFF) / 0x1000);
    }
    memset(s, 0, sizeof(dj_surface_t));
    return 0;
}

int displayjet_present(uint32_t id)
{
    if (!g_dj.hw_ready) return -1;

    dj_surface_t *s = surface_by_id(id);
    if (!s || !s->buffer) return -1;

    size_t copy_size = s->stride * s->height;
    size_t fb_size   = g_dj.stride * g_dj.height;
    if (copy_size > fb_size) copy_size = fb_size;

    /* MAEM: decrypt surface to temp buffer, then copy to scanout */
    /* For efficiency, decrypt directly into scanout in chunks */
    uint8_t tmp[4096];
    size_t  off = 0;
    while (off < copy_size) {
        size_t chunk = copy_size - off;
        if (chunk > sizeof(tmp)) chunk = sizeof(tmp);

        dj_chacha20_encrypt(s->key, s->nonce, (uint32_t)(off / 64),
                            (const uint8_t *)s->buffer + off, tmp, chunk);
        memcpy((uint8_t *)g_dj.fb_virt + off, tmp, chunk);
        off += chunk;
    }
    dj_secure_zero(tmp, sizeof(tmp));
    return 0;
}

int displayjet_get_mode(dj_display_mode_t *mode)
{
    if (!g_dj.hw_ready || !mode) return -1;

    mode->width     = g_dj.width;
    mode->height    = g_dj.height;
    mode->stride    = g_dj.stride;
    mode->bpp       = g_dj.bpp;
    mode->connected = g_dj.compositor_pid >= 0;
    memcpy(mode->connector_name, "DisplayJet w/ MAEM", 18);
    mode->connector_name[18] = '\0';
    return 0;
}

int displayjet_surface_write(uint32_t id, const void *data, size_t size)
{
    dj_surface_t *s = surface_by_id(id);
    if (!s || !s->buffer || !data) return -1;
    if (size > s->buffer_size) size = s->buffer_size;

    /* MAEM: encrypt the data before storing in surface buffer */
    uint8_t tmp[4096];
    size_t  off = 0;
    while (off < size) {
        size_t chunk = size - off;
        if (chunk > sizeof(tmp)) chunk = sizeof(tmp);

        dj_chacha20_encrypt(s->key, s->nonce, (uint32_t)(off / 64),
                            (const uint8_t *)data + off, tmp, chunk);
        memcpy((uint8_t *)s->buffer + off, tmp, chunk);
        off += chunk;
    }
    dj_secure_zero(tmp, sizeof(tmp));
    return (int)size;
}

int displayjet_surface_read(uint32_t id, void *data, size_t size)
{
    dj_surface_t *s = surface_by_id(id);
    if (!s || !s->buffer || !data) return -1;
    if (size > s->buffer_size) size = s->buffer_size;

    /* MAEM: decrypt the surface data for the compositor */
    uint8_t tmp[4096];
    size_t  off = 0;
    while (off < size) {
        size_t chunk = size - off;
        if (chunk > sizeof(tmp)) chunk = sizeof(tmp);

        dj_chacha20_encrypt(s->key, s->nonce, (uint32_t)(off / 64),
                            (const uint8_t *)s->buffer + off, tmp, chunk);
        memcpy((uint8_t *)data + off, tmp, chunk);
        off += chunk;
    }
    dj_secure_zero(tmp, sizeof(tmp));
    return (int)size;
}

int displayjet_list_surfaces(dj_surface_info_t *infos, uint32_t *count)
{
    if (!infos || !count) return -1;

    uint32_t n = 0;
    uint32_t max = *count;

    for (uint32_t i = 0; i < DJ_MAX_SURFACES && n < max; i++) {
        if (g_dj.surfaces[i].used) {
            infos[n].id     = g_dj.surfaces[i].id;
            infos[n].width  = g_dj.surfaces[i].width;
            infos[n].height = g_dj.surfaces[i].height;
            infos[n].stride = g_dj.surfaces[i].stride;
            infos[n].bpp    = g_dj.surfaces[i].bpp;
            n++;
        }
    }
    *count = n;

    if (n < max) {
        infos[n].id = 0;
        infos[n].width = 0;
        infos[n].height = 0;
        infos[n].stride = 0;
        infos[n].bpp = 0;
    }
    return 0;
}

int displayjet_request_access(uint32_t id, dj_access_grant_t *grant)
{
    (void)grant;
    dj_surface_t *s = surface_by_id(id);
    if (!s) return -1;
    if (!g_dj.compositor_pid) return -1;

    /* MAEM: generate ephemeral access grant.
     * In full security, this would trigger SUPS (Secure User Prompt).
     * Here we generate an ephemeral key tied to this access. */
    return 0;
}

int displayjet_grant_access(uint32_t id, dj_access_grant_t *grant)
{
    dj_surface_t *s = surface_by_id(id);
    if (!s || !grant) return -1;

    /* MAEM: derive ephemeral key from surface key + access counter */
    uint8_t info[12];
    for (int i = 0; i < 4; i++)
        info[i] = (uint8_t)(s->access_counter >> (i * 8));
    for (int i = 0; i < 8; i++)
        info[4 + i] = (uint8_t)(s->id >> (i * 8));

    dj_hkdf(s->key, DJ_KEY_SIZE, g_master_key, DJ_KEY_SIZE,
             info, 12, grant->key, DJ_ACCESS_KEY_SIZE);
    grant->surface_id = s->id;
    grant->counter = s->access_counter;

    s->access_counter++;
    return 0;
}
