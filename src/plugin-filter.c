#include <obs-module.h>
#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#endif
#include "plugin-data.h"
#include "plugin-filter.h"
#include "plugin-support.h"
#include "scene-trigger.h"
#include "detector.h"

void *filter_create(obs_data_t *settings, obs_source_t *context)
{
	plugin_set_filter_source(context);

	// Read settings directly in create — OBS 32+ may not call filter_update
	// when loading a saved scene collection.
	plugin_set_victory_source(obs_data_get_string(settings, "victory_source_name"));
	plugin_set_victory_label(obs_data_get_string(settings, "victory_label"));
	plugin_set_defeat_source(obs_data_get_string(settings, "defeat_source_name"));
	plugin_set_defeat_label(obs_data_get_string(settings, "defeat_label"));

	trigger_set_victory_scene(obs_data_get_string(settings, "victory_trigger_scene"));
	trigger_set_defeat_scene(obs_data_get_string(settings, "defeat_trigger_scene"));

	plugin_set_language_key(obs_data_get_string(settings, "language_key"));
	plugin_set_game_key(obs_data_get_string(settings, "game_key"));

	obs_log(LOG_INFO, "filter_create: src=%p lang='%s' game='%s'", (void *)context, g_state.language_key,
		g_state.game_key);

	return context;
}

void filter_destroy(void *data)
{
	obs_source_t *source = data;
	if (g_state.active_filter_source == source)
		g_state.active_filter_source = NULL;
}

const char *filter_get_name(void *type_data)
{
	return "MOBA WinCounter";
}

static struct obs_source_frame *filter_video(void *data, struct obs_source_frame *frame)
{
	UNUSED_PARAMETER(data);
	return frame;
}

static bool language_modified(obs_properties_t *props, obs_property_t *property, obs_data_t *settings)
{
	UNUSED_PARAMETER(property);

	const char *lang = obs_data_get_string(settings, "language_key");
	plugin_set_language_key(lang);

	obs_property_t *game_prop = obs_properties_get(props, "game_key");
	obs_property_list_clear(game_prop);

	if (g_state.detector) {
		int n = detector_get_game_count(g_state.detector);
		for (int i = 0; i < n; i++) {
			const char *key = detector_get_game_key(g_state.detector, i);
			obs_property_list_add_string(game_prop, key, key);
		}
	}

	return false;
}

static bool support_button_clicked(obs_properties_t *props, obs_property_t *property, void *data)
{
	UNUSED_PARAMETER(props);
	UNUSED_PARAMETER(property);
	UNUSED_PARAMETER(data);

	const char *url = "https://ko-fi.com/thinkrand";

#ifdef _WIN32
	ShellExecuteA(NULL, "open", url, NULL, NULL, SW_SHOWNORMAL);
#elif defined(__APPLE__)
	char cmd[512];
	snprintf(cmd, sizeof(cmd), "open \"%s\"", url);
	system(cmd);
#else
	char cmd[512];
	snprintf(cmd, sizeof(cmd), "xdg-open \"%s\"", url);
	system(cmd);
#endif

	return false;
}

static obs_properties_t *filter_get_properties(void *data)
{
	obs_properties_t *props = obs_properties_create();

	obs_property_t *lang_list = obs_properties_add_list(props, "language_key", "Language", OBS_COMBO_TYPE_LIST,
							    OBS_COMBO_FORMAT_STRING);
	if (g_state.detector) {
		int n = detector_get_language_count(g_state.detector);
		for (int i = 0; i < n; i++) {
			const char *key = detector_get_language_key(g_state.detector, i);
			obs_property_list_add_string(lang_list, key, key);
		}
	}
	obs_property_set_modified_callback(lang_list, language_modified);

	obs_property_t *game_list =
		obs_properties_add_list(props, "game_key", "Game", OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	if (g_state.detector) {
		int n = detector_get_game_count(g_state.detector);
		for (int i = 0; i < n; i++) {
			const char *key = detector_get_game_key(g_state.detector, i);
			obs_property_list_add_string(game_list, key, key);
		}
	}

	obs_properties_add_text(props, "victory_source_name", "Victory Source Name", OBS_TEXT_DEFAULT);
	obs_properties_add_text(props, "victory_label", "Victory Label (use {w} {d})", OBS_TEXT_DEFAULT);
	obs_properties_add_text(props, "override_wins", "Override Wins (empty = keep)", OBS_TEXT_DEFAULT);

	obs_properties_add_text(props, "defeat_source_name", "Defeat Source Name", OBS_TEXT_DEFAULT);
	obs_properties_add_text(props, "defeat_label", "Defeat Label (use {w} {d})", OBS_TEXT_DEFAULT);
	obs_properties_add_text(props, "override_losses", "Override Losses (empty = keep)", OBS_TEXT_DEFAULT);

	obs_properties_add_text(props, "victory_trigger_scene", "Trigger Scene on Victory", OBS_TEXT_DEFAULT);
	obs_properties_add_text(props, "defeat_trigger_scene", "Trigger Scene on Defeat", OBS_TEXT_DEFAULT);

	obs_properties_add_button(props, "support_button", "Support on Ko-fi", support_button_clicked);

	return props;
}

static void filter_update(void *data, obs_data_t *settings)
{
	UNUSED_PARAMETER(data);

	plugin_set_victory_source(obs_data_get_string(settings, "victory_source_name"));
	plugin_set_victory_label(obs_data_get_string(settings, "victory_label"));
	plugin_set_defeat_source(obs_data_get_string(settings, "defeat_source_name"));
	plugin_set_defeat_label(obs_data_get_string(settings, "defeat_label"));

	trigger_set_victory_scene(obs_data_get_string(settings, "victory_trigger_scene"));
	trigger_set_defeat_scene(obs_data_get_string(settings, "defeat_trigger_scene"));

	plugin_set_language_key(obs_data_get_string(settings, "language_key"));
	plugin_set_game_key(obs_data_get_string(settings, "game_key"));

	const char *ow = obs_data_get_string(settings, "override_wins");
	if (ow && ow[0]) {
		plugin_set_manual_wins(atoi(ow));
		obs_data_set_string(settings, "override_wins", "");
	}

	const char *ol = obs_data_get_string(settings, "override_losses");
	if (ol && ol[0]) {
		plugin_set_manual_losses(atoi(ol));
		obs_data_set_string(settings, "override_losses", "");
	}

	plugin_ensure_text_sources();
	plugin_read_counts_from_sources();
	plugin_set_counts_loaded(true);
}

struct obs_source_info moba_wincounter_info = {
	.id = "moba_wincounter",
	.type = OBS_SOURCE_TYPE_FILTER,
	.output_flags = OBS_SOURCE_VIDEO,
	.get_name = filter_get_name,
	.create = filter_create,
	.destroy = filter_destroy,
	.get_properties = filter_get_properties,
	.update = filter_update,
	.filter_video = filter_video,
	.icon_type = OBS_ICON_TYPE_CUSTOM,
};
