#ifndef _DISPLAYJET_H
#define _DISPLAYJET_H

#include <stdint.h>
#include <stddef.h>

#define DJ_MAX_SURFACES      32
#define DJ_CONNECTOR_NAME    64

typedef enum {
    DJ_FORMAT_RGBX8888 = 0,
} dj_format_t;

typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    uint8_t  bpp;
    bool     connected;
    char     connector_name[DJ_CONNECTOR_NAME];
} dj_display_mode_t;

typedef struct {
    uint32_t id;
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    uint8_t  bpp;
} dj_surface_info_t;

/* Syscall numbers */
#define SYS_DJ_CONNECT         100
#define SYS_DJ_DISCONNECT      101
#define SYS_DJ_CREATE_SURFACE  102
#define SYS_DJ_DESTROY_SURFACE 103
#define SYS_DJ_PRESENT         104
#define SYS_DJ_GET_MODE        105
#define SYS_DJ_SURFACE_WRITE   106
#define SYS_DJ_SURFACE_READ    107
#define SYS_DJ_LIST_SURFACES   108

/* Syscall wrappers (for userspace) */
static inline int dj_connect(void) {
    int ret;
    asm volatile("int $0x80" : "=a"(ret) : "a"(SYS_DJ_CONNECT) : "memory");
    return ret;
}

static inline int dj_disconnect(void) {
    int ret;
    asm volatile("int $0x80" : "=a"(ret) : "a"(SYS_DJ_DISCONNECT) : "memory");
    return ret;
}

static inline int dj_create_surface(uint32_t width, uint32_t height, uint32_t *out_id) {
    int ret;
    asm volatile("int $0x80" : "=a"(ret) : "a"(SYS_DJ_CREATE_SURFACE), "b"((long)width), "c"((long)height), "d"((long)out_id) : "memory");
    return ret;
}

static inline int dj_destroy_surface(uint32_t id) {
    int ret;
    asm volatile("int $0x80" : "=a"(ret) : "a"(SYS_DJ_DESTROY_SURFACE), "b"((long)id) : "memory");
    return ret;
}

static inline int dj_present(uint32_t id) {
    int ret;
    asm volatile("int $0x80" : "=a"(ret) : "a"(SYS_DJ_PRESENT), "b"((long)id) : "memory");
    return ret;
}

static inline int dj_get_mode(dj_display_mode_t *mode) {
    int ret;
    asm volatile("int $0x80" : "=a"(ret) : "a"(SYS_DJ_GET_MODE), "b"((long)mode) : "memory");
    return ret;
}

static inline int dj_surface_write(uint32_t id, const void *data, size_t size) {
    int ret;
    asm volatile("int $0x80" : "=a"(ret) : "a"(SYS_DJ_SURFACE_WRITE), "b"((long)id), "c"((long)data), "d"((long)size) : "memory");
    return ret;
}

static inline int dj_surface_read(uint32_t id, void *data, size_t size) {
    int ret;
    asm volatile("int $0x80" : "=a"(ret) : "a"(SYS_DJ_SURFACE_READ), "b"((long)id), "c"((long)data), "d"((long)size) : "memory");
    return ret;
}

static inline int dj_list_surfaces(dj_surface_info_t *infos, uint32_t *count) {
    int ret;
    asm volatile("int $0x80" : "=a"(ret) : "a"(SYS_DJ_LIST_SURFACES), "b"((long)infos), "c"((long)count) : "memory");
    return ret;
}

#endif
