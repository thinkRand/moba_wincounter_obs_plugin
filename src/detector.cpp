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
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>

constexpr int kMinMatchCount = 10;
// Lowe's ratio threshold from the SIFT paper — standard value
constexpr float kRatioThreshold = 0.7f;
constexpr int kMinInlierCount = 10;

struct DetectorHandle {
	cv::Ptr<cv::SIFT> sift;
	cv::Ptr<cv::FlannBasedMatcher> matcher;
	cv::Mat victory_img;
	cv::Mat defeat_img;
	cv::Mat victory_des;
	cv::Mat defeat_des;
	std::vector<cv::KeyPoint> victory_kp;
	std::vector<cv::KeyPoint> defeat_kp;
	bool initialized;
	std::string debug_dir;
};

void *detector_create(const char *template_dir)
{
	DetectorHandle *handle = new DetectorHandle();
	handle->sift = cv::SIFT::create();
	handle->matcher = cv::FlannBasedMatcher::create();
	handle->initialized = false;

	if (!template_dir || !template_dir[0]) {
		std::fprintf(stderr, "[mlbb_stats] No template directory provided\n");
		delete handle;
		return nullptr;
	}

	std::string v_path = std::string(template_dir) + "/victory.png";
	std::string d_path = std::string(template_dir) + "/defeat.png";

	handle->victory_img = cv::imread(v_path, cv::IMREAD_GRAYSCALE);
	handle->defeat_img = cv::imread(d_path, cv::IMREAD_GRAYSCALE);

	if (handle->victory_img.empty()) {
		std::fprintf(stderr, "[mlbb_stats] Failed to load victory.png from %s\n",
			    template_dir);
		delete handle;
		return nullptr;
	}
	if (handle->defeat_img.empty()) {
		std::fprintf(stderr, "[mlbb_stats] Failed to load defeat.png from %s\n",
			    template_dir);
		delete handle;
		return nullptr;
	}

	handle->sift->detectAndCompute(handle->victory_img, cv::noArray(),
					handle->victory_kp, handle->victory_des);
	handle->sift->detectAndCompute(handle->defeat_img, cv::noArray(),
					handle->defeat_kp, handle->defeat_des);

	handle->initialized = true;

	const char *debug_env = std::getenv("MLBB_DEBUG_DIR");
	if (debug_env && debug_env[0]) {
		handle->debug_dir = debug_env;
		std::filesystem::create_directories(handle->debug_dir);
	}

	return handle;
}

void detector_destroy(void *handle)
{
	delete static_cast<DetectorHandle *>(handle);
}

void detector_get_template_info(void *handle_ptr,
                                struct TemplateInfo *victory,
                                struct TemplateInfo *defeat)
{
	DetectorHandle *handle = static_cast<DetectorHandle *>(handle_ptr);
	if (handle) {
		if (victory) {
			victory->width = handle->victory_img.cols;
			victory->height = handle->victory_img.rows;
			victory->keypoints = (int)handle->victory_kp.size();
		}
		if (defeat) {
			defeat->width = handle->defeat_img.cols;
			defeat->height = handle->defeat_img.rows;
			defeat->keypoints = (int)handle->defeat_kp.size();
		}
	}
}

static bool is_valid_homography(const cv::Mat &H, int template_w, int template_h)
{
	if (H.empty())
		return false;

	std::vector<cv::Point2f> src_corners = {
		cv::Point2f(0.0f, 0.0f),
		cv::Point2f(0.0f, static_cast<float>(template_h - 1)),
		cv::Point2f(static_cast<float>(template_w - 1),
			    static_cast<float>(template_h - 1)),
		cv::Point2f(static_cast<float>(template_w - 1), 0.0f)
	};

	std::vector<cv::Point2f> dst_corners(4);
	cv::perspectiveTransform(src_corners, dst_corners, H);

	double template_area = static_cast<double>(template_w) *
			       static_cast<double>(template_h);
	double area = cv::contourArea(dst_corners);

	if (area < template_area * 0.3 || area > template_area * 3.0)
		return false;

	if (!cv::isContourConvex(dst_corners))
		return false;

	return true;
}

static int match_template_sift(DetectorHandle *handle,
			       const cv::Mat &img_gray,
			       const std::vector<cv::KeyPoint> &template_kp,
			       const cv::Mat &template_des,
			       int template_w, int template_h,
			       const std::vector<cv::KeyPoint> &frame_kp,
			       const cv::Mat &frame_des,
			       int *out_good_matches)
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
		if (matches[i][0].distance <
		    kRatioThreshold * matches[i][1].distance) {
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

enum MatchResult detector_process_frame(void *handle_ptr,
					const uint8_t *y_plane,
					int width, int height, int stride,
					struct FrameMatchInfo *info)
{
	DetectorHandle *handle = static_cast<DetectorHandle *>(handle_ptr);
	if (!handle || !handle->initialized || !y_plane ||
	    width <= 0 || height <= 0)
		return MATCH_UNKNOWN;

	cv::Mat img_gray(height, width, CV_8UC1, (void *)y_plane, stride);

	// Count keypoints on frame for diagnostics
	std::vector<cv::KeyPoint> frame_kp;
	cv::Mat frame_des;
	handle->sift->detectAndCompute(img_gray, cv::noArray(), frame_kp, frame_des);

	// Reset match info
	if (info) {
		std::memset(info, 0, sizeof(*info));
		info->result = MATCH_UNKNOWN;
		info->frame_keypoints = (int)frame_kp.size();
	}

	int good_v = 0, good_d = 0;
	int inliers_v = 0, inliers_d = 0;

	if (!frame_des.empty() && frame_kp.size() >= 2) {
		inliers_v = match_template_sift(handle, img_gray,
						handle->victory_kp,
						handle->victory_des,
						handle->victory_img.cols,
						handle->victory_img.rows,
						frame_kp, frame_des,
						&good_v);
		inliers_d = match_template_sift(handle, img_gray,
						handle->defeat_kp,
						handle->defeat_des,
						handle->defeat_img.cols,
						handle->defeat_img.rows,
						frame_kp, frame_des,
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

	if (!handle->debug_dir.empty() &&
	    (result != MATCH_UNKNOWN)) {
		std::time_t now = std::time(nullptr);
		char time_buf[32];
		std::strftime(time_buf, sizeof(time_buf), "%Y%m%d_%H%M%S",
			      std::localtime(&now));

		char filename[512];
		std::snprintf(filename, sizeof(filename), "%s/%s_%s_%dx%d.png",
			      handle->debug_dir.c_str(), time_buf,
			      match_result_to_string(result),
			      width, height);

		cv::imwrite(filename, img_gray);
		std::fprintf(stderr, "[mlbb_stats] Saved debug frame: %s\n", filename);
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
