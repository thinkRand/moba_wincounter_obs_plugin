#include <obs-module.h>
#include <obs-frontend-api.h>
#include <string.h>
#include "scene-trigger.h"
#include "plugin-support.h"

#define ANIM_DURATION_MS 5000

static char s_victory_source[128] = {0};
static char s_defeat_source[128] = {0};

static volatile uint64_t s_anim_end_ms = 0;

void trigger_set_victory_scene(const char *name)
{
	strncpy(s_victory_source, name ? name : "", sizeof(s_victory_source) - 1);
	s_victory_source[sizeof(s_victory_source) - 1] = '\0';
}

void trigger_set_defeat_scene(const char *name)
{
	strncpy(s_defeat_source, name ? name : "", sizeof(s_defeat_source) - 1);
	s_defeat_source[sizeof(s_defeat_source) - 1] = '\0';
}

struct item_find {
	const char *target_name;
	bool visible;
};

static bool toggle_item_cb(obs_scene_t *scene, obs_sceneitem_t *item, void *param)
{
	(void)scene;
	struct item_find *f = (struct item_find *)param;
	obs_source_t *src = obs_sceneitem_get_source(item);
	const char *name = obs_source_get_name(src);
	obs_log(LOG_INFO, "toggle_item_cb: checking item '%s' against target '%s'", name ? name : "(null)",
		f->target_name);
	if (strcmp(name, f->target_name) == 0) {
		obs_log(LOG_INFO, "toggle_item_cb: found match, setting visible=%d", f->visible);
		obs_sceneitem_set_visible(item, f->visible);
		return false;
	}
	return true;
}

static void set_source_visible(const char *name, bool visible)
{
	if (!name || !name[0]) {
		obs_log(LOG_INFO, "set_source_visible: name is empty, skipping");
		return;
	}

	obs_source_t *cur = obs_frontend_get_current_scene();
	if (!cur) {
		obs_log(LOG_INFO, "set_source_visible: obs_frontend_get_current_scene() returned NULL");
		return;
	}
	obs_scene_t *scene = obs_scene_from_source(cur);
	if (!scene) {
		obs_log(LOG_INFO,
			"set_source_visible: obs_scene_from_source() returned NULL (current scene is not a scene?)");
		obs_source_release(cur);
		return;
	}

	obs_log(LOG_INFO, "set_source_visible: name='%s' visible=%d current_scene='%s'", name, visible,
		obs_source_get_name(cur));

	struct item_find f = {.target_name = name, .visible = visible};
	obs_scene_enum_items(scene, toggle_item_cb, &f);
	obs_source_release(cur);
}

struct show_param {
	char name[128];
	char other_name[128];
};

static void do_show(void *param)
{
	struct show_param *sp = (struct show_param *)param;

	set_source_visible(sp->other_name, false);
	set_source_visible(sp->name, true);

	obs_log(LOG_INFO, "animation shown: '%s'", sp->name);
	bfree(sp);
}

static void do_hide(void *param)
{
	(void)param;
	set_source_visible(s_victory_source, false);
	set_source_visible(s_defeat_source, false);
	obs_log(LOG_INFO, "animations hidden");
}

static void queue_show(const char *name, const char *other_name, uint64_t now_ms)
{
	if (!name || !name[0])
		return;

	s_anim_end_ms = now_ms + ANIM_DURATION_MS;

	struct show_param *sp = bzalloc(sizeof(*sp));
	strncpy(sp->name, name, sizeof(sp->name) - 1);
	sp->name[sizeof(sp->name) - 1] = '\0';
	if (other_name) {
		strncpy(sp->other_name, other_name, sizeof(sp->other_name) - 1);
		sp->other_name[sizeof(sp->other_name) - 1] = '\0';
	}

	obs_queue_task(OBS_TASK_UI, do_show, sp, false);
}

void trigger_on_victory(uint64_t now_ms)
{
	if (!s_victory_source[0])
		return;
	queue_show(s_victory_source, s_defeat_source, now_ms);
	obs_log(LOG_INFO, "queued victory animation");
}

void trigger_on_defeat(uint64_t now_ms)
{
	if (!s_defeat_source[0])
		return;
	queue_show(s_defeat_source, s_victory_source, now_ms);
	obs_log(LOG_INFO, "queued defeat animation");
}

void trigger_tick(uint64_t now_ms)
{
	uint64_t end = s_anim_end_ms;
	if (end && now_ms >= end) {
		s_anim_end_ms = 0;
		obs_queue_task(OBS_TASK_UI, do_hide, NULL, false);
	}
}
