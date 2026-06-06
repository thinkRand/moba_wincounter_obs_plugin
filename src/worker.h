#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void *worker_create(void *detector, uint64_t cooldown_ms);
void worker_destroy(void *handle);
bool worker_push_frame(void *handle, const uint8_t *y_plane,
                       int width, int height, int stride,
                       uint64_t timestamp_ns);

#ifdef __cplusplus
}
#endif
