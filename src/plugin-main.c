#include <obs-module.h>
#include <string.h>
#include "atomic_ops.h"
#include "plugin-support.h"
#include "plugin-filter.h"
#include "detector.h"
#include "scene-trigger.h"
#include "worker.h"

OBS_DECLARE_MODULE()

// Ignore repeat detections of the same result for 3 minutes.
// MLBB matches last ~15 min on average (as little as ~6 min with surrender),
// so a new result cannot appear within 3 min of the previous one.
// This prevents counting the same match result multiple times
// as the result screen lingers or re-appears.
#define COOLDOWN_MS 180000

struct plugin_state g_state = {0};

static void update_text_sources(void);
static void deferred_init_text_sources(void *param);
static int try_extract_count(const char *label, const char *text, const char *token);

static inline long atomic_read(volatile long *ptr)
{
	return atomic_load_long(ptr);
}

static void on_raw_video_frame(void *param, struct video_data *frame)
{
	(void)param;

	if (!g_state.active_filter_source)
		return;

	if (!g_state.counts_loaded)
		obs_queue_task(OBS_TASK_UI, deferred_init_text_sources, NULL, false);

	uint64_t now_ms = frame->timestamp / 1000000;
	if (now_ms - g_state.last_process_ts < 1000)
		return;
	g_state.last_process_ts = now_ms;

	int stride = (int)frame->linesize[0];
	const uint8_t *y_plane = frame->data[0];

	if (!y_plane || g_state.base_width <= 0 || g_state.base_height <= 0)
		return;

	worker_push_frame(g_state.worker, y_plane, g_state.base_width, g_state.base_height, stride, frame->timestamp);
}

bool plugin_is_counts_loaded(void)
{
	return g_state.counts_loaded;
}
void plugin_set_counts_loaded(bool loaded)
{
	g_state.counts_loaded = loaded;
}

static void deferred_init_text_sources(void *param)
{
	UNUSED_PARAMETER(param);
	if (g_state.counts_loaded)
		return;
	plugin_ensure_text_sources();
	plugin_read_counts_from_sources();
	plugin_set_counts_loaded(true);
}

void plugin_update_text_sources(void)
{
	update_text_sources();
}

void plugin_set_language_key(const char *key)
{
	if (!key || !key[0])
		return;
	if (strcmp(g_state.language_key, key) == 0)
		return;
	strncpy(g_state.language_key, key, sizeof(g_state.language_key) - 1);
	g_state.language_key[sizeof(g_state.language_key) - 1] = '\0';
	if (g_state.detector) {
		detector_select_language(g_state.detector, key);
		obs_log(LOG_INFO, "language changed to '%s', game='%s'", key,
			detector_get_current_game(g_state.detector));
	}
}

void plugin_set_game_key(const char *key)
{
	if (!key || !key[0])
		return;
	if (strcmp(g_state.game_key, key) == 0)
		return;
	strncpy(g_state.game_key, key, sizeof(g_state.game_key) - 1);
	g_state.game_key[sizeof(g_state.game_key) - 1] = '\0';
	if (g_state.detector) {
		detector_select_game(g_state.detector, key);
		obs_log(LOG_INFO, "active game changed to '%s'", key);
	}
}

void plugin_set_filter_source(obs_source_t *source)
{
	g_state.active_filter_source = source;
}

void plugin_set_victory_source(const char *name)
{
	strncpy(g_state.victory_source_name, name ? name : "", sizeof(g_state.victory_source_name) - 1);
	g_state.victory_source_name[sizeof(g_state.victory_source_name) - 1] = '\0';
}

void plugin_set_defeat_source(const char *name)
{
	strncpy(g_state.defeat_source_name, name ? name : "", sizeof(g_state.defeat_source_name) - 1);
	g_state.defeat_source_name[sizeof(g_state.defeat_source_name) - 1] = '\0';
}

void plugin_set_victory_label(const char *label)
{
	strncpy(g_state.victory_label, label ? label : "", sizeof(g_state.victory_label) - 1);
	g_state.victory_label[sizeof(g_state.victory_label) - 1] = '\0';
}

void plugin_set_defeat_label(const char *label)
{
	strncpy(g_state.defeat_label, label ? label : "", sizeof(g_state.defeat_label) - 1);
	g_state.defeat_label[sizeof(g_state.defeat_label) - 1] = '\0';
}

void plugin_set_manual_wins(int wins)
{
	atomic_store_long(&g_state.wins, wins);
	update_text_sources();
}

void plugin_set_manual_losses(int losses)
{
	atomic_store_long(&g_state.losses, losses);
	update_text_sources();
}

static void replace_token(char *out, size_t out_sz, const char *tmpl, const char *token, int value)
{
	const char *pos = strstr(tmpl, token);
	if (!pos) {
		strncpy(out, tmpl, out_sz - 1);
		out[out_sz - 1] = '\0';
		return;
	}
	size_t prefix_len = (size_t)(pos - tmpl);
	if (prefix_len >= out_sz) {
		out[0] = '\0';
		return;
	}
	memcpy(out, tmpl, prefix_len);
	int written = snprintf(out + prefix_len, out_sz - prefix_len, "%d", value);
	if (written < 0)
		return;
	size_t remaining = out_sz - prefix_len - (size_t)written;
	const char *rest = pos + strlen(token);
	strncpy(out + prefix_len + written, rest, remaining - 1);
	out[out_sz - 1] = '\0';
}

static void push_text(obs_source_t *source, const char *text)
{
	obs_data_t *s = obs_source_get_settings(source);
	if (s) {
		obs_data_set_string(s, "text", text);
		obs_source_update(source, s);
		obs_data_release(s);
	}
}

// ——— Victory/defeat helpers (factor out copy-paste) ———

static void update_single_source(const char *source_name, const char *label, const char *tok1, long val1,
				 const char *tok2, long val2)
{
	if (!source_name[0] || !label[0])
		return;
	char tmp[256];
	replace_token(tmp, sizeof(tmp), label, tok1, (int)val1);
	char final[256];
	replace_token(final, sizeof(final), tmp, tok2, (int)val2);
	obs_source_t *src = obs_get_source_by_name(source_name);
	if (src) {
		push_text(src, final);
		obs_source_release(src);
	}
}

static void ensure_single_source(const char *source_name, const char *label, const char *tok_primary,
				 const char *tok_secondary, const char *log_label)
{
	if (!source_name[0] || !label[0])
		return;
	obs_source_t *src = obs_get_source_by_name(source_name);
	if (!src)
		return;
	obs_data_t *s = obs_source_get_settings(src);
	if (s) {
		const char *text = obs_data_get_string(s, "text");
		if (!text || !text[0] || try_extract_count(label, text, tok_primary) < 0) {
			char tmp[256];
			replace_token(tmp, sizeof(tmp), label, tok_primary, 0);
			char final[256];
			replace_token(final, sizeof(final), tmp, tok_secondary, 0);
			push_text(src, final);
			obs_log(LOG_INFO, "wrote default %s text: '%s'", log_label, final);
		}
		obs_data_release(s);
	}
	obs_source_release(src);
}

static void read_single_count(const char *source_name, const char *label, const char *token, volatile long *counter,
			      const char *count_label)
{
	if (!source_name[0] || !label[0])
		return;
	obs_source_t *src = obs_get_source_by_name(source_name);
	if (!src)
		return;
	obs_data_t *s = obs_source_get_settings(src);
	if (s) {
		const char *text = obs_data_get_string(s, "text");
		if (text && text[0]) {
			int v = try_extract_count(label, text, token);
			if (v >= 0) {
				atomic_store_long(counter, v);
			} else {
				obs_log(LOG_WARNING, "Can't extract %s: source='%s' label='%s' text='%s'", count_label,
					source_name, label, text);
			}
		}
		obs_data_release(s);
	}
	obs_source_release(src);
}

static void update_text_sources(void)
{
	long w = atomic_read(&g_state.wins);
	long l = atomic_read(&g_state.losses);
	update_single_source(g_state.victory_source_name, g_state.victory_label, "{w}", w, "{d}", l);
	update_single_source(g_state.defeat_source_name, g_state.defeat_label, "{d}", l, "{w}", w);
}

void plugin_ensure_text_sources(void)
{
	ensure_single_source(g_state.victory_source_name, g_state.victory_label, "{w}", "{d}", "victory");
	ensure_single_source(g_state.defeat_source_name, g_state.defeat_label, "{d}", "{w}", "defeat");
}

static int try_extract_count(const char *label, const char *text, const char *token)
{
	if (!label || !text || !token)
		return -1;

	char fmt[512];
	size_t pos = 0;
	const char *lp = label;
	int found = 0;

	while (*lp && pos < sizeof(fmt) - 8) {
		if (lp[0] == '{' && lp[2] == '}' && (lp[1] == 'w' || lp[1] == 'd')) {
			if (strncmp(lp, token, 3) == 0) {
				memcpy(fmt + pos, "%d", 2);
				pos += 2;
				found = 1;
			} else {
				memcpy(fmt + pos, "%*d", 3);
				pos += 3;
			}
			lp += 3;
		} else {
			fmt[pos++] = *lp++;
		}
	}
	memcpy(fmt + pos, " %1s", 5);
	fmt[pos + 4] = '\0';

	if (!found)
		return -1;

	int val = -1;
	char trailing[2] = {0};
	if (sscanf(text, fmt, &val, trailing) == 1)
		return val;
	return -1;
}

void plugin_read_counts_from_sources(void)
{
	obs_log(LOG_INFO, "reading counts from text sources...");
	read_single_count(g_state.victory_source_name, g_state.victory_label, "{w}", &g_state.wins, "wins");
	read_single_count(g_state.defeat_source_name, g_state.defeat_label, "{d}", &g_state.losses, "losses");
	obs_log(LOG_INFO, "read counts from text sources: w:%ld/l:%ld", atomic_read(&g_state.wins),
		atomic_read(&g_state.losses));
}

bool obs_module_load(void)
{
	obs_log(LOG_INFO, "plugin loaded successfully (version %s)", PLUGIN_VERSION);

	g_state.cooldown_ms = COOLDOWN_MS;

	obs_register_source(&moba_wincounter_info);

	video_t *video = obs_get_video();
	if (video) {
		g_state.base_width = (int)video_output_get_width(video);
		g_state.base_height = (int)video_output_get_height(video);
		obs_log(LOG_INFO, "OBS base resolution: %dx%d", g_state.base_width, g_state.base_height);
	}

	const char *env_templates = getenv("MLBB_TEMPLATES_DIR");
	char *templates_path = NULL;
	if (env_templates && env_templates[0]) {
		templates_path = bstrdup(env_templates);
	} else {
		templates_path = obs_module_file("templates");
	}
	obs_log(LOG_INFO, "Loading templates from: %s", templates_path ? templates_path : "(null)");

	g_state.detector = detector_create(templates_path);
	if (!g_state.detector) {
		obs_log(LOG_ERROR, "detector creation failed (no valid templates)");
		if (templates_path)
			bfree(templates_path);
		return false;
	}

	{
		int lang_count = detector_get_language_count(g_state.detector);
		obs_log(LOG_INFO, "loaded %d language(s)", lang_count);
		for (int li = 0; li < lang_count; li++) {
			const char *lk = detector_get_language_key(g_state.detector, li);
			detector_select_language(g_state.detector, lk);
			int game_count = detector_get_game_count(g_state.detector);
			obs_log(LOG_INFO, "  language '%s': %d game(s)", lk, game_count);
			for (int gi = 0; gi < game_count; gi++) {
				const char *gk = detector_get_game_key(g_state.detector, gi);
				detector_select_game(g_state.detector, gk);
				struct TemplateInfo vi, di;
				detector_get_template_info(g_state.detector, &vi, &di);
				obs_log(LOG_INFO, "    [%d] '%s': victory=%dx%d %dkp, defeat=%dx%d %dkp", gi, gk,
					vi.width, vi.height, vi.keypoints, di.width, di.height, di.keypoints);
			}
		}
		// Restore defaults
		detector_select_language(g_state.detector, detector_get_language_key(g_state.detector, 0));
		detector_select_game(g_state.detector, detector_get_game_key(g_state.detector, 0));
		strncpy(g_state.language_key, detector_get_current_language(g_state.detector),
			sizeof(g_state.language_key) - 1);
		obs_log(LOG_INFO, " active: lang='%s' game='%s'", g_state.language_key,
			detector_get_current_game(g_state.detector));
	}
	if (templates_path)
		bfree(templates_path);

	g_state.worker = worker_create(g_state.detector, g_state.cooldown_ms);

	obs_add_raw_video_callback(NULL, on_raw_video_frame, NULL);
	obs_log(LOG_INFO, "raw video callback registered");

	return true;
}

void obs_module_unload(void)
{
	obs_remove_raw_video_callback(on_raw_video_frame, NULL);
	worker_destroy(g_state.worker);
	g_state.worker = NULL;
	detector_destroy(g_state.detector);
	g_state.detector = NULL;
	obs_log(LOG_INFO, "plugin unloaded");
}
