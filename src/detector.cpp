#include "detector.h"

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/features2d.hpp>
#include <opencv2/flann.hpp>
#include <opencv2/calib3d.hpp>
#include <opencv2/imgcodecs.hpp>

#include <cstring>
#include <string>
#include <vector>
#include <map>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <algorithm>

constexpr int kMinMatchCount = 10;
constexpr float kRatioThreshold = 0.7f;
constexpr int kMinInlierCount = 10;

struct GameTemplates {
	cv::Mat victory_img;
	cv::Mat defeat_img;
	cv::Mat victory_des;
	cv::Mat defeat_des;
	std::vector<cv::KeyPoint> victory_kp;
	std::vector<cv::KeyPoint> defeat_kp;
};

struct LanguageData {
	std::map<std::string, GameTemplates> games;
	std::vector<std::string> game_order;
};

struct DetectorHandle {
	cv::Ptr<cv::SIFT> sift;
	cv::Ptr<cv::FlannBasedMatcher> matcher;
	std::map<std::string, LanguageData> languages;
	std::vector<std::string> language_order;
	std::string current_language;
	std::string current_game;
	std::atomic<GameTemplates *> active;
	bool initialized;
	std::string debug_dir;
};

static bool load_game_templates(DetectorHandle *handle, const std::string &lang_key, const std::string &game_key,
				const std::string &victory_path, const std::string &defeat_path)
{
	GameTemplates gt;
	gt.victory_img = cv::imread(victory_path, cv::IMREAD_GRAYSCALE);
	gt.defeat_img = cv::imread(defeat_path, cv::IMREAD_GRAYSCALE);

	if (gt.victory_img.empty() || gt.defeat_img.empty())
		return false;

	handle->sift->detectAndCompute(gt.victory_img, cv::noArray(), gt.victory_kp, gt.victory_des);
	handle->sift->detectAndCompute(gt.defeat_img, cv::noArray(), gt.defeat_kp, gt.defeat_des);

	handle->languages[lang_key].games[game_key] = std::move(gt);
	handle->languages[lang_key].game_order.push_back(game_key);

	const auto &lang_data = handle->languages[lang_key];
	const auto &stored = lang_data.games.find(game_key)->second;
	std::fprintf(stderr, "[moba_wincounter] Loaded '%s/%s': victory=%dx%d %dkp, defeat=%dx%d %dkp\n",
		     lang_key.c_str(), game_key.c_str(), stored.victory_img.cols, stored.victory_img.rows,
		     (int)stored.victory_kp.size(), stored.defeat_img.cols, stored.defeat_img.rows,
		     (int)stored.defeat_kp.size());
	return true;
}

static bool load_templates_from_dir(DetectorHandle *handle, const std::string &lang_key,
				    const std::filesystem::path &dir)
{
	namespace fs = std::filesystem;

	int count = 0;
	for (const auto &entry : fs::directory_iterator(dir)) {
		if (!entry.is_regular_file())
			continue;

		std::string filename = entry.path().filename().string();
		const std::string suffix = "_victory.png";
		if (filename.size() <= suffix.size() || filename.substr(filename.size() - suffix.size()) != suffix)
			continue;

		std::string game_key = filename.substr(0, filename.size() - suffix.size());
		if (game_key.empty())
			continue;

		std::string defeat_name = game_key + "_defeat.png";
		fs::path defeat_path = dir / defeat_name;
		if (!fs::is_regular_file(defeat_path)) {
			std::fprintf(stderr, "[moba_wincounter] Skipping '%s/%s': missing '%s'\n", lang_key.c_str(),
				     filename.c_str(), defeat_name.c_str());
			continue;
		}

		if (load_game_templates(handle, lang_key, game_key, entry.path().string(), defeat_path.string())) {
			count++;
		}
	}

	if (count == 0) {
		std::fprintf(stderr,
			     "[moba_wincounter] In '%s': no '*_victory.png' files found. "
			     "Trying legacy 'victory.png' / 'defeat.png'\n",
			     lang_key.c_str());
		std::string v_path = (dir / "victory.png").string();
		std::string d_path = (dir / "defeat.png").string();
		if (load_game_templates(handle, lang_key, "mlbb", v_path, d_path)) {
			count = 1;
		}
	}

	return count > 0;
}

void *detector_create(const char *template_dir)
{
	DetectorHandle *handle = new DetectorHandle{};
	handle->sift = cv::SIFT::create();
	handle->matcher = cv::FlannBasedMatcher::create();
	handle->initialized = false;
	handle->active.store(nullptr);

	if (!template_dir || !template_dir[0]) {
		std::fprintf(stderr, "[moba_wincounter] No template directory provided\n");
		delete handle;
		return nullptr;
	}

	namespace fs = std::filesystem;
	fs::path dir(template_dir);
	if (!fs::is_directory(dir)) {
		std::fprintf(stderr, "[moba_wincounter] Template directory not found: %s\n", template_dir);
		delete handle;
		return nullptr;
	}

	int total_loaded = 0;

	// Scan for language subdirectories
	bool has_subdirs = false;
	for (const auto &entry : fs::directory_iterator(dir)) {
		if (entry.is_directory()) {
			has_subdirs = true;
			break;
		}
	}

	if (has_subdirs) {
		for (const auto &entry : fs::directory_iterator(dir)) {
			if (!entry.is_directory())
				continue;

			std::string lang_key = entry.path().filename().string();
			if (lang_key.empty() || lang_key[0] == '.')
				continue;

			if (load_templates_from_dir(handle, lang_key, entry.path())) {
				handle->language_order.push_back(lang_key);
				total_loaded++;
			}
		}
	} else {
		// Backward compatibility: flat templates treated as English
		if (load_templates_from_dir(handle, "en", dir)) {
			handle->language_order.push_back("en");
			total_loaded++;
		}
	}

	if (total_loaded == 0) {
		std::fprintf(stderr, "[moba_wincounter] No valid templates found in %s\n", template_dir);
		delete handle;
		return nullptr;
	}

	handle->current_language = handle->language_order[0];
	const auto &first_lang = handle->languages[handle->current_language];
	if (!first_lang.game_order.empty()) {
		handle->current_game = first_lang.game_order[0];
		handle->active.store(&handle->languages[handle->current_language].games[handle->current_game]);
	}
	handle->initialized = true;

	const char *debug_env = std::getenv("MLBB_DEBUG_DIR");
	if (debug_env && debug_env[0]) {
		handle->debug_dir = debug_env;
		std::filesystem::create_directories(handle->debug_dir);
	}

	return handle;
}

void detector_destroy(void *handle_ptr)
{
	DetectorHandle *handle = static_cast<DetectorHandle *>(handle_ptr);
	if (handle) {
		handle->active.store(nullptr);
		delete handle;
	}
}

int detector_get_language_count(void *handle_ptr)
{
	DetectorHandle *handle = static_cast<DetectorHandle *>(handle_ptr);
	if (!handle)
		return 0;
	return (int)handle->language_order.size();
}

const char *detector_get_language_key(void *handle_ptr, int index)
{
	DetectorHandle *handle = static_cast<DetectorHandle *>(handle_ptr);
	if (!handle || index < 0 || index >= (int)handle->language_order.size())
		return nullptr;
	return handle->language_order[index].c_str();
}

bool detector_select_language(void *handle_ptr, const char *lang_key)
{
	DetectorHandle *handle = static_cast<DetectorHandle *>(handle_ptr);
	if (!handle || !lang_key)
		return false;

	auto lang_it = handle->languages.find(std::string(lang_key));
	if (lang_it == handle->languages.end())
		return false;

	handle->current_language = lang_key;
	auto &lang_data = lang_it->second;

	// Try to keep the same game; fall back to first game in language
	auto game_it = lang_data.games.find(handle->current_game);
	if (game_it != lang_data.games.end()) {
		handle->active.store(&game_it->second);
	} else if (!lang_data.game_order.empty()) {
		handle->current_game = lang_data.game_order[0];
		game_it = lang_data.games.find(handle->current_game);
		if (game_it != lang_data.games.end()) {
			handle->active.store(&game_it->second);
		}
	}

	return true;
}

const char *detector_get_current_language(void *handle_ptr)
{
	DetectorHandle *handle = static_cast<DetectorHandle *>(handle_ptr);
	if (!handle)
		return nullptr;
	return handle->current_language.c_str();
}

int detector_get_game_count(void *handle_ptr)
{
	DetectorHandle *handle = static_cast<DetectorHandle *>(handle_ptr);
	if (!handle)
		return 0;

	auto lang_it = handle->languages.find(handle->current_language);
	if (lang_it == handle->languages.end())
		return 0;

	return (int)lang_it->second.game_order.size();
}

const char *detector_get_game_key(void *handle_ptr, int index)
{
	DetectorHandle *handle = static_cast<DetectorHandle *>(handle_ptr);
	if (!handle)
		return nullptr;

	auto lang_it = handle->languages.find(handle->current_language);
	if (lang_it == handle->languages.end())
		return nullptr;

	const auto &order = lang_it->second.game_order;
	if (index < 0 || index >= (int)order.size())
		return nullptr;

	return order[index].c_str();
}

bool detector_select_game(void *handle_ptr, const char *game_key)
{
	DetectorHandle *handle = static_cast<DetectorHandle *>(handle_ptr);
	if (!handle || !game_key)
		return false;

	auto lang_it = handle->languages.find(handle->current_language);
	if (lang_it == handle->languages.end())
		return false;

	auto game_it = lang_it->second.games.find(std::string(game_key));
	if (game_it == lang_it->second.games.end())
		return false;

	handle->current_game = game_key;
	handle->active.store(&game_it->second);
	return true;
}

const char *detector_get_current_game(void *handle_ptr)
{
	DetectorHandle *handle = static_cast<DetectorHandle *>(handle_ptr);
	if (!handle)
		return nullptr;
	return handle->current_game.c_str();
}

void detector_get_template_info(void *handle_ptr, struct TemplateInfo *victory, struct TemplateInfo *defeat)
{
	DetectorHandle *handle = static_cast<DetectorHandle *>(handle_ptr);
	if (!handle)
		return;

	GameTemplates *active = handle->active.load();
	if (!active)
		return;

	if (victory) {
		victory->width = active->victory_img.cols;
		victory->height = active->victory_img.rows;
		victory->keypoints = (int)active->victory_kp.size();
	}
	if (defeat) {
		defeat->width = active->defeat_img.cols;
		defeat->height = active->defeat_img.rows;
		defeat->keypoints = (int)active->defeat_kp.size();
	}
}

static bool is_valid_homography(const cv::Mat &H, int template_w, int template_h)
{
	if (H.empty())
		return false;

	std::vector<cv::Point2f> src_corners = {
		cv::Point2f(0.0f, 0.0f), cv::Point2f(0.0f, static_cast<float>(template_h - 1)),
		cv::Point2f(static_cast<float>(template_w - 1), static_cast<float>(template_h - 1)),
		cv::Point2f(static_cast<float>(template_w - 1), 0.0f)};

	std::vector<cv::Point2f> dst_corners(4);
	cv::perspectiveTransform(src_corners, dst_corners, H);

	double template_area = static_cast<double>(template_w) * static_cast<double>(template_h);
	double area = cv::contourArea(dst_corners);

	if (area < template_area * 0.3 || area > template_area * 3.0)
		return false;

	if (!cv::isContourConvex(dst_corners))
		return false;

	return true;
}

static int match_template_sift(DetectorHandle *handle, const cv::Mat &img_gray,
			       const std::vector<cv::KeyPoint> &template_kp, const cv::Mat &template_des,
			       int template_w, int template_h, const std::vector<cv::KeyPoint> &frame_kp,
			       const cv::Mat &frame_des, int *out_good_matches)
{
	if (template_des.empty() || img_gray.empty())
		return 0;

	if (frame_des.empty() || template_des.rows < 2 || frame_des.rows < 2)
		return 0;

	std::vector<std::vector<cv::DMatch>> matches;
	handle->matcher->knnMatch(template_des, frame_des, matches, 2);

	std::vector<cv::DMatch> good;
	for (size_t i = 0; i < matches.size(); i++) {
		if (matches[i].size() < 2)
			continue;
		if (matches[i][0].distance < kRatioThreshold * matches[i][1].distance) {
			good.push_back(matches[i][0]);
		}
	}

	if (out_good_matches)
		*out_good_matches = (int)good.size();

	if ((int)good.size() < kMinMatchCount)
		return 0;

	std::vector<cv::Point2f> src_pts, dst_pts;
	for (size_t i = 0; i < good.size(); i++) {
		src_pts.push_back(template_kp[good[i].queryIdx].pt);
		dst_pts.push_back(frame_kp[good[i].trainIdx].pt);
	}

	cv::Mat H = cv::findHomography(src_pts, dst_pts, cv::RANSAC, 5.0);
	if (!is_valid_homography(H, template_w, template_h))
		return 0;

	std::vector<cv::Point2f> src_proj;
	cv::perspectiveTransform(src_pts, src_proj, H);

	int inliers = 0;
	for (size_t i = 0; i < src_proj.size(); i++) {
		float dx = src_proj[i].x - dst_pts[i].x;
		float dy = src_proj[i].y - dst_pts[i].y;
		if (std::sqrt(dx * dx + dy * dy) <= 5.0f)
			inliers++;
	}

	return inliers;
}

enum MatchResult detector_process_frame(void *handle_ptr, const uint8_t *y_plane, int width, int height, int stride,
					struct FrameMatchInfo *info)
{
	DetectorHandle *handle = static_cast<DetectorHandle *>(handle_ptr);
	if (!handle || !handle->initialized || !y_plane || width <= 0 || height <= 0)
		return MATCH_UNKNOWN;

	GameTemplates *active = handle->active.load();
	if (!active)
		return MATCH_UNKNOWN;

	cv::Mat img_gray(height, width, CV_8UC1, (void *)y_plane, stride);

	std::vector<cv::KeyPoint> frame_kp;
	cv::Mat frame_des;
	handle->sift->detectAndCompute(img_gray, cv::noArray(), frame_kp, frame_des);

	if (info) {
		std::memset(info, 0, sizeof(*info));
		info->result = MATCH_UNKNOWN;
		info->frame_keypoints = (int)frame_kp.size();
	}

	int good_v = 0, good_d = 0;
	int inliers_v = 0, inliers_d = 0;

	if (!frame_des.empty() && frame_kp.size() >= 2) {
		inliers_v = match_template_sift(handle, img_gray, active->victory_kp, active->victory_des,
						active->victory_img.cols, active->victory_img.rows, frame_kp, frame_des,
						&good_v);
		inliers_d = match_template_sift(handle, img_gray, active->defeat_kp, active->defeat_des,
						active->defeat_img.cols, active->defeat_img.rows, frame_kp, frame_des,
						&good_d);
	}

	if (info) {
		info->victory_good_matches = good_v;
		info->victory_inliers = inliers_v;
		info->defeat_good_matches = good_d;
		info->defeat_inliers = inliers_d;
	}

	enum MatchResult result;
	if (inliers_v < kMinInlierCount && inliers_d < kMinInlierCount)
		result = MATCH_UNKNOWN;
	else if (inliers_v >= kMinInlierCount && inliers_d < kMinInlierCount)
		result = MATCH_VICTORY;
	else if (inliers_d >= kMinInlierCount && inliers_v < kMinInlierCount)
		result = MATCH_DEFEAT;
	else
		result = (inliers_v >= inliers_d) ? MATCH_VICTORY : MATCH_DEFEAT;

	if (info)
		info->result = result;

	if (!handle->debug_dir.empty() && (result != MATCH_UNKNOWN)) {
		std::time_t now = std::time(nullptr);
		char time_buf[32];
		std::strftime(time_buf, sizeof(time_buf), "%Y%m%d_%H%M%S", std::localtime(&now));

		char filename[512];
		std::snprintf(filename, sizeof(filename), "%s/%s_%s_%dx%d.png", handle->debug_dir.c_str(), time_buf,
			      match_result_to_string(result), width, height);

		cv::imwrite(filename, img_gray);
		std::fprintf(stderr, "[moba_wincounter] Saved debug frame: %s\n", filename);
	}

	return result;
}

const char *match_result_to_string(enum MatchResult result)
{
	switch (result) {
	case MATCH_VICTORY:
		return "victory";
	case MATCH_DEFEAT:
		return "defeat";
	default:
		return "unknown";
	}
}
