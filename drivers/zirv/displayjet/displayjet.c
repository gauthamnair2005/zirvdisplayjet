#include "displayjet.h"
#include "drivers/pci/pci.h"
#include "drivers/zirv/device.h"
#include "kernel/console.h"
#include "kernel/mm/pmm.h"
#include "kernel/mm/vmm.h"
#include "arch/x64/cpu.h"
#include <string.h>
#include <stddef.h>

/* ── Bochs VBE hardware interface (embedded, no external dependency) ──────── */
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

/* ── Public API ─────────────────────────────────────────────────────────── */
void displayjet_init(void)
{
    memset(&g_dj, 0, sizeof(g_dj));
    g_dj.compositor_pid = -1;

    kputs("[displayjet] Initialising DisplayJet display driver\n");

    if (hw_init() != 0) {
        kputs("[displayjet] No compatible display hardware found\n");
        return;
    }

    console_enable_fb(g_dj.fb_virt, g_dj.width, g_dj.height,
                      g_dj.stride, g_dj.bpp);

    zirv_register_device(DEV_CLASS_DISPLAY_GPU, DEV_CLASS_DISPLAY_GPU,
                         "DisplayJet (bochs VBE backend)", NULL);

    kputs("[displayjet] DisplayJet ready — waiting for compositor\n");
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
    memset(s->buffer, 0, s->buffer_size);

    *out_id = s->id;
    return 0;
}

int displayjet_destroy_surface(uint32_t id)
{
    dj_surface_t *s = surface_by_id(id);
    if (!s) return -1;

    if (s->buffer) {
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

    memcpy(g_dj.fb_virt, s->buffer, copy_size);
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
    memcpy(mode->connector_name, "DisplayJet (bochs VBE)", 22);
    mode->connector_name[22] = '\0';
    return 0;
}

int displayjet_surface_write(uint32_t id, const void *data, size_t size)
{
    dj_surface_t *s = surface_by_id(id);
    if (!s || !s->buffer || !data) return -1;
    if (size > s->buffer_size) size = s->buffer_size;
    memcpy(s->buffer, data, size);
    return (int)size;
}

int displayjet_surface_read(uint32_t id, void *data, size_t size)
{
    dj_surface_t *s = surface_by_id(id);
    if (!s || !s->buffer || !data) return -1;
    if (size > s->buffer_size) size = s->buffer_size;
    memcpy(data, s->buffer, size);
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
