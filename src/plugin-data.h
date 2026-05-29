// plugin-data.h – consolidated state for the MLBB stats filter
#pragma once

#include <stdint.h>
#include <obs-module.h>
#include "detector.h"

#ifdef __cplusplus
extern "C" {
#endif

struct plugin_state {
    void *detector;
    uint64_t last_process_ts;
    int processed_count;
    long wins;
    long losses;
    int base_width;
    int base_height;
    enum MatchResult last_result;
    uint64_t last_result_time_ms;

    // Text source configuration
    char victory_source_name[128];
    char defeat_source_name[128];
    char victory_label[256];
    char defeat_label[256];

    // Active filter instance (last one created owns the state)
    obs_source_t *active_filter_source;

    // Cooldown between counting the same result (ms)
    uint64_t cooldown_ms;

    // Whether counts have been loaded from text sources
    bool counts_loaded;
};

extern struct plugin_state g_state;

#ifdef __cplusplus
}
#endif
