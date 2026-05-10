#ifndef _DISPLAYJET_H
#define _DISPLAYJET_H

#include <stdint.h>
#include <stddef.h>

#define DJ_MAX_SURFACES      32
#define DJ_CONNECTOR_NAME    64
#define DJ_ACCESS_KEY_SIZE   32

typedef enum {
    DJ_FORMAT_RGBX8888 = 0,
} dj_format_t;

typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    uint8_t  bpp;
    int      connected;
    char     connector_name[DJ_CONNECTOR_NAME];
} dj_display_mode_t;

typedef struct {
    uint32_t id;
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    uint8_t  bpp;
} dj_surface_info_t;

typedef struct {
    uint8_t  key[DJ_ACCESS_KEY_SIZE];
    uint32_t surface_id;
    uint32_t counter;
} dj_access_grant_t;

/* Syscall numbers */
#define SYS_DJ_CONNECT         110
#define SYS_DJ_DISCONNECT      111
#define SYS_DJ_CREATE_SURFACE  112
#define SYS_DJ_DESTROY_SURFACE 113
#define SYS_DJ_PRESENT         114
#define SYS_DJ_GET_MODE        115
#define SYS_DJ_SURFACE_WRITE   116
#define SYS_DJ_SURFACE_READ    117
#define SYS_DJ_LIST_SURFACES   118
#define SYS_DJ_REQUEST_ACCESS  119
#define SYS_DJ_GRANT_ACCESS    120

static inline long _dj_syscall0(long n) {
    long r; __asm__ volatile("syscall" : "=a"(r) : "a"(n) : "rcx","r11","memory");
    return r;
}
static inline long _dj_syscall1(long n, long a1) {
    long r; __asm__ volatile("syscall" : "=a"(r) : "a"(n), "D"(a1) : "rcx","r11","memory");
    return r;
}
static inline long _dj_syscall2(long n, long a1, long a2) {
    long r; __asm__ volatile("syscall" : "=a"(r) : "a"(n), "D"(a1), "S"(a2) : "rcx","r11","memory");
    return r;
}
static inline long _dj_syscall3(long n, long a1, long a2, long a3) {
    long r; __asm__ volatile("syscall" : "=a"(r) : "a"(n), "D"(a1), "S"(a2), "d"(a3) : "rcx","r11","memory");
    return r;
}

static inline int dj_connect(void) {
    return (int)_dj_syscall0(SYS_DJ_CONNECT);
}
static inline int dj_disconnect(void) {
    return (int)_dj_syscall0(SYS_DJ_DISCONNECT);
}
static inline int dj_create_surface(uint32_t w, uint32_t h, uint32_t *id) {
    return (int)_dj_syscall3(SYS_DJ_CREATE_SURFACE, (long)w, (long)h, (long)id);
}
static inline int dj_destroy_surface(uint32_t id) {
    return (int)_dj_syscall1(SYS_DJ_DESTROY_SURFACE, (long)id);
}
static inline int dj_present(uint32_t id) {
    return (int)_dj_syscall1(SYS_DJ_PRESENT, (long)id);
}
static inline int dj_get_mode(dj_display_mode_t *m) {
    return (int)_dj_syscall1(SYS_DJ_GET_MODE, (long)m);
}
static inline int dj_surface_write(uint32_t id, const void *d, size_t sz) {
    return (int)_dj_syscall3(SYS_DJ_SURFACE_WRITE, (long)id, (long)d, (long)sz);
}
static inline int dj_surface_read(uint32_t id, void *d, size_t sz) {
    return (int)_dj_syscall3(SYS_DJ_SURFACE_READ, (long)id, (long)d, (long)sz);
}
static inline int dj_list_surfaces(dj_surface_info_t *infos, uint32_t *cnt) {
    return (int)_dj_syscall2(SYS_DJ_LIST_SURFACES, (long)infos, (long)cnt);
}
static inline int dj_request_access(uint32_t id, dj_access_grant_t *g) {
    return (int)_dj_syscall2(SYS_DJ_REQUEST_ACCESS, (long)id, (long)g);
}
static inline int dj_grant_access(uint32_t id, dj_access_grant_t *g) {
    return (int)_dj_syscall2(SYS_DJ_GRANT_ACCESS, (long)id, (long)g);
}

#endif
