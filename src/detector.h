#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

enum MatchResult {
    MATCH_UNKNOWN = 0,
    MATCH_VICTORY,
    MATCH_DEFEAT,
};

struct TemplateInfo {
    int width;
    int height;
    int keypoints;
};

struct FrameMatchInfo {
    enum MatchResult result;
    int victory_good_matches;
    int victory_inliers;
    int defeat_good_matches;
    int defeat_inliers;
    int frame_keypoints;
};

void *detector_create(const char *template_dir);
void detector_destroy(void *handle);

int detector_get_language_count(void *handle);
const char *detector_get_language_key(void *handle, int index);
bool detector_select_language(void *handle, const char *lang_key);
const char *detector_get_current_language(void *handle);

int detector_get_game_count(void *handle);
const char *detector_get_game_key(void *handle, int index);
bool detector_select_game(void *handle, const char *game_key);
const char *detector_get_current_game(void *handle);

void detector_get_template_info(void *handle, struct TemplateInfo *victory,
                                struct TemplateInfo *defeat);
enum MatchResult detector_process_frame(void *handle, const uint8_t *y_plane,
                                        int width, int height, int stride,
                                        struct FrameMatchInfo *info);

const char *match_result_to_string(enum MatchResult result);

#ifdef __cplusplus
}
#endif
