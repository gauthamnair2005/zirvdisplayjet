#ifndef ZIRVIUM_DRIVERS_DISPLAYJET_DISPLAYJET_H
#define ZIRVIUM_DRIVERS_DISPLAYJET_DISPLAYJET_H

#include <stdint.h>
#include <stdbool.h>

#define DJ_MAX_SURFACES      32
#define DJ_NAME_LEN          64
#define DJ_CONNECTOR_NAME    64

typedef enum {
    DJ_FORMAT_RGBX8888 = 0,
} dj_format_t;

typedef struct {
    uint32_t id;
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    uint8_t  bpp;
    dj_format_t format;
    void    *buffer;
    size_t   buffer_size;
    int      owner_pid;
    bool     used;
} dj_surface_t;

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

/* DisplayJet syscall numbers */
#define SYS_DJ_CONNECT         110
#define SYS_DJ_DISCONNECT      111
#define SYS_DJ_CREATE_SURFACE  112
#define SYS_DJ_DESTROY_SURFACE 113
#define SYS_DJ_PRESENT         114
#define SYS_DJ_GET_MODE        115
#define SYS_DJ_SURFACE_WRITE   116
#define SYS_DJ_SURFACE_READ    117
#define SYS_DJ_LIST_SURFACES   118

/* Kernel driver API */
void displayjet_init(void);
void displayjet_set_framebuffer(void *fb_virt, uint32_t width, uint32_t height, uint32_t stride, uint8_t bpp);
int  displayjet_connect(int pid);
int  displayjet_disconnect(int pid);
int  displayjet_create_surface(uint32_t width, uint32_t height, uint32_t *out_id);
int  displayjet_destroy_surface(uint32_t id);
int  displayjet_present(uint32_t id);
int  displayjet_get_mode(dj_display_mode_t *mode);
int  displayjet_surface_write(uint32_t id, const void *data, size_t size);
int  displayjet_surface_read(uint32_t id, void *data, size_t size);
int  displayjet_list_surfaces(dj_surface_info_t *infos, uint32_t *count);

#endif
