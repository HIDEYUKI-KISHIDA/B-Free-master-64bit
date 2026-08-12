#ifndef VIRTIO_GPU_H
#define VIRTIO_GPU_H

#include <stdint.h>
#include "gpu_backend.h"

/* Returns 0 when virtio-gpu scanout is configured; updates *fbinfo. */
int virtio_gpu_activate(const bfree_gpu_device_info_t *device, tk2gpu_fbinfo_t *fbinfo);

/* Optional page-flip / flush via VIRTIO_GPU_CMD_RESOURCE_FLUSH. */
int virtio_gpu_resource_flush(uint32_t resource_id, uint32_t x, uint32_t y,
                              uint32_t w, uint32_t h);

int virtio_gpu_active(void);

#endif /* VIRTIO_GPU_H */
