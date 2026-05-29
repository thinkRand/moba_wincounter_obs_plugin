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
#include <sys/stat.h>
#include <direct.h>

#define MIN_MATCH_COUNT 10
#define RATIO_THRESH 0.7f
#define MIN_INLIER_COUNT 8

struct DetectorHandle {
	cv::Ptr<cv::SIFT> sift;
	cv::Ptr<cv::FlannBasedMatcher> matcher;
	cv::Mat victory_img;
	cv::Mat defeat_img;
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
		return handle;
	}

	std::string v_path = std::string(template_dir) + "/victory.png";
	std::string d_path = std::string(template_dir) + "/defeat.png";

	handle->victory_img = cv::imread(v_path, cv::IMREAD_GRAYSCALE);
	handle->defeat_img = cv::imread(d_path, cv::IMREAD_GRAYSCALE);

	if (handle->victory_img.empty()) {
		std::fprintf(stderr, "[mlbb_stats] Failed to load victory.png from %s\n",
			    template_dir);
		return handle;
	}
	if (handle->defeat_img.empty()) {
		std::fprintf(stderr, "[mlbb_stats] Failed to load defeat.png from %s\n",
			    template_dir);
		return handle;
	}

	handle->initialized = true;
	std::fprintf(stderr, "[mlbb_stats] Loaded templates from %s\n", template_dir);

	const char *debug_env = std::getenv("MLBB_DEBUG_DIR");
	if (debug_env && debug_env[0]) {
		handle->debug_dir = debug_env;
#ifdef _MSC_VER
		_mkdir(debug_env);
#else
		mkdir(debug_env, 0755);
#endif
		std::fprintf(stderr, "[mlbb_stats] Debug frames will be saved to: %s\n",
			    debug_env);
	}

	return handle;
}

void detector_destroy(void *handle)
{
	delete static_cast<DetectorHandle *>(handle);
}

static int match_template_sift(DetectorHandle *handle,
			       const cv::Mat &img_gray,
			       const cv::Mat &template_img)
{
	if (template_img.empty() || img_gray.empty())
		return 0;

	std::vector<cv::KeyPoint> kp1, kp2;
	cv::Mat des1, des2;

	handle->sift->detectAndCompute(template_img, cv::noArray(), kp1, des1);
	handle->sift->detectAndCompute(img_gray, cv::noArray(), kp2, des2);

	if (des1.empty() || des2.empty() ||
	    des1.rows < 2 || des2.rows < 2)
		return 0;

	std::vector<std::vector<cv::DMatch>> matches;
	handle->matcher->knnMatch(des1, des2, matches, 2);

	std::vector<cv::DMatch> good;
	for (size_t i = 0; i < matches.size(); i++) {
		if (matches[i].size() < 2)
			continue;
		if (matches[i][0].distance <
		    RATIO_THRESH * matches[i][1].distance) {
			good.push_back(matches[i][0]);
		}
	}

	if ((int)good.size() < MIN_MATCH_COUNT)
		return (int)good.size();

	std::vector<cv::Point2f> src_pts, dst_pts;
	for (size_t i = 0; i < good.size(); i++) {
		src_pts.push_back(kp1[good[i].queryIdx].pt);
		dst_pts.push_back(kp2[good[i].trainIdx].pt);
	}

	cv::Mat H = cv::findHomography(src_pts, dst_pts, cv::RANSAC, 5.0);
	if (H.empty())
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

const char *detector_process_frame(void *handle_ptr,
				   const uint8_t *y_plane,
				   int width, int height, int stride)
{
	DetectorHandle *handle = static_cast<DetectorHandle *>(handle_ptr);
	if (!handle || !handle->initialized || !y_plane ||
	    width <= 0 || height <= 0)
		return "unknown";

	cv::Mat img_gray(height, width, CV_8UC1, (void *)y_plane, stride);

	int inliers_v = match_template_sift(handle, img_gray,
					    handle->victory_img);
	int inliers_d = match_template_sift(handle, img_gray,
					    handle->defeat_img);

	const char *result;
	if (inliers_v < MIN_INLIER_COUNT && inliers_d < MIN_INLIER_COUNT)
		result = "unknown";
	else if (inliers_v >= MIN_INLIER_COUNT && inliers_d < MIN_INLIER_COUNT)
		result = "victory";
	else if (inliers_d >= MIN_INLIER_COUNT && inliers_v < MIN_INLIER_COUNT)
		result = "defeat";
	else
		result = (inliers_v >= inliers_d) ? "victory" : "defeat";

	if (!handle->debug_dir.empty() &&
	    (strcmp(result, "victory") == 0 || strcmp(result, "defeat") == 0)) {
		static int frame_counter = 0;
		frame_counter++;

		std::time_t now = std::time(nullptr);
		char time_buf[32];
		std::strftime(time_buf, sizeof(time_buf), "%Y%m%d_%H%M%S",
			      std::localtime(&now));

		char filename[512];
		std::snprintf(filename, sizeof(filename), "%s/%s_%s_%dx%d.png",
			      handle->debug_dir.c_str(), time_buf, result,
			      width, height);

		cv::imwrite(filename, img_gray);
		std::fprintf(stderr, "[mlbb_stats] Saved debug frame: %s\n", filename);
	}

	return result;
}
