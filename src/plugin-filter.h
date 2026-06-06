// plugin-filter.h – OBS filter declaration for MLBB stats
#pragma once

#include <obs-module.h>
#include "plugin-data.h"

#ifdef __cplusplus
extern "C" {
#endif

extern struct obs_source_info moba_wincounter_info;

void plugin_set_victory_source(const char *name);
void plugin_set_defeat_source(const char *name);
void plugin_set_victory_label(const char *label);
void plugin_set_defeat_label(const char *label);
void plugin_set_manual_wins(int wins);
void plugin_set_manual_losses(int losses);
void plugin_set_language_key(const char *key);
void plugin_set_game_key(const char *key);

void plugin_read_counts_from_sources(void);
void plugin_ensure_text_sources(void);
void plugin_update_text_sources(void);
void plugin_set_filter_source(obs_source_t *source);
bool plugin_is_counts_loaded(void);
void plugin_set_counts_loaded(bool loaded);

#ifdef __cplusplus
}
#endif
