#include <obs-module.h>
#include "plugin-data.h"
#include "plugin-filter.h"
#include "plugin-support.h"

void *filter_create(obs_data_t *settings, obs_source_t *context)
{
	plugin_set_filter_source(context);

	// Read settings directly in create — OBS 32+ may not call filter_update
	// when loading a saved scene collection.
	plugin_set_victory_source(
		obs_data_get_string(settings, "victory_source_name"));
	plugin_set_victory_label(
		obs_data_get_string(settings, "victory_label"));
	plugin_set_defeat_source(
		obs_data_get_string(settings, "defeat_source_name"));
	plugin_set_defeat_label(
		obs_data_get_string(settings, "defeat_label"));

	obs_log(LOG_INFO, "filter_create: src=%p", (void *)context);

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
	return "MLBB Stats Filter";
}

static struct obs_source_frame *filter_video(void *data,
					    struct obs_source_frame *frame)
{
	UNUSED_PARAMETER(data);
	return frame;
}

static obs_properties_t *filter_get_properties(void *data)
{
	obs_properties_t *props = obs_properties_create();

	obs_properties_add_text(props, "victory_source_name",
		"Victory Source Name", OBS_TEXT_DEFAULT);
	obs_properties_add_text(props, "victory_label",
		"Victory Label (use {w} {d})", OBS_TEXT_DEFAULT);
	obs_properties_add_text(props, "override_wins",
		"Override Wins (empty = keep)", OBS_TEXT_DEFAULT);

	obs_properties_add_text(props, "defeat_source_name",
		"Defeat Source Name", OBS_TEXT_DEFAULT);
	obs_properties_add_text(props, "defeat_label",
		"Defeat Label (use {w} {d})", OBS_TEXT_DEFAULT);
	obs_properties_add_text(props, "override_losses",
		"Override Losses (empty = keep)", OBS_TEXT_DEFAULT);

	return props;
}

static void filter_update(void *data, obs_data_t *settings)
{
	UNUSED_PARAMETER(data);

	plugin_set_victory_source(
		obs_data_get_string(settings, "victory_source_name"));
	plugin_set_victory_label(
		obs_data_get_string(settings, "victory_label"));
	plugin_set_defeat_source(
		obs_data_get_string(settings, "defeat_source_name"));
	plugin_set_defeat_label(
		obs_data_get_string(settings, "defeat_label"));

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

struct obs_source_info mlbb_stats_filter_info = {
	.id = "mlbb_stats_filter",
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
