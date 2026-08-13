#ifndef GUI_SERVER_FBDEV_H
#define GUI_SERVER_FBDEV_H

#include <stdint.h>

#ifndef TK2GPU_FBINFO_T
#define TK2GPU_FBINFO_T
typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t bpp;
    uint32_t pitch;
    uint64_t phys_addr;
    uint64_t size;
} tk2gpu_fbinfo_t;
#endif

int fbdev_init(void);
void *fbdev_base(void);
uint32_t fbdev_width(void);
uint32_t fbdev_height(void);
uint32_t fbdev_pitch(void);
uint32_t fbdev_bpp(void);
int fbdev_get_info(tk2gpu_fbinfo_t *out);
int fbdev_ioctl(int cmd, void *arg);
void fbdev_present(void);

#endif /* GUI_SERVER_FBDEV_H */
