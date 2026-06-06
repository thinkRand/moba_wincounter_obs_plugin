#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void trigger_set_victory_scene(const char *name);
void trigger_set_defeat_scene(const char *name);

void trigger_on_victory(uint64_t now_ms);
void trigger_on_defeat(uint64_t now_ms);

void trigger_tick(uint64_t now_ms);

#ifdef __cplusplus
}
#endif
