#include <thread>
#include <mutex>
#include <condition_variable>
#include <deque>
#include <vector>
#include <cstdint>

#include <obs-module.h>
#include "worker.h"
#include "detector.h"
#include "scene-trigger.h"
#include "plugin-data.h"
#include "plugin-filter.h"
#include "atomic_ops.h"
#include "plugin-support.h"

struct FrameTask {
    std::vector<uint8_t> data;
    int width;
    int height;
    int stride;
    uint64_t timestamp_ns;
};

struct WorkerHandle {
    void *detector;
    uint64_t cooldown_ms;

    std::thread thread;
    std::mutex mutex;
    std::condition_variable cv;
    std::deque<FrameTask> queue;
    bool shutdown;

    enum MatchResult last_result;
    uint64_t last_result_time_ms;
};

static void worker_thread_func(WorkerHandle *w)
{
    while (true) {
        FrameTask task;
        {
            std::unique_lock<std::mutex> lock(w->mutex);
            w->cv.wait(lock, [w]() {
                return w->shutdown || !w->queue.empty();
            });
            if (w->shutdown && w->queue.empty())
                return;
            task = std::move(w->queue.front());
            w->queue.pop_front();
        }

        uint64_t now_ms = task.timestamp_ns / 1000000;

        struct FrameMatchInfo match_info;
        enum MatchResult result = detector_process_frame(
            w->detector,
            task.data.data(),
            task.width, task.height, task.stride,
            &match_info);

        if (result == MATCH_VICTORY || result == MATCH_DEFEAT) {
            if (result == w->last_result) {
                w->last_result_time_ms = now_ms;
            } else if ((now_ms - w->last_result_time_ms) > w->cooldown_ms) {
                if (result == MATCH_VICTORY) {
                    atomic_increment_long(&g_state.wins);
                    trigger_on_victory(now_ms);
                } else {
                    atomic_increment_long(&g_state.losses);
                    trigger_on_defeat(now_ms);
                }

                obs_queue_task(OBS_TASK_UI,
                               [](void *) { plugin_update_text_sources(); },
                               NULL, false);

                w->last_result = result;
                w->last_result_time_ms = now_ms;

                obs_log(LOG_INFO,
                        "counted: w:%ld/l:%ld",
                        atomic_load_long(&g_state.wins),
                        atomic_load_long(&g_state.losses));
            }
        } else {
            w->last_result = MATCH_UNKNOWN;
        }

        trigger_tick(now_ms);
    }
}

void *worker_create(void *detector, uint64_t cooldown_ms)
{
    WorkerHandle *w = new WorkerHandle();
    w->detector = detector;
    w->cooldown_ms = cooldown_ms;
    w->shutdown = false;
    w->last_result = MATCH_UNKNOWN;
    w->last_result_time_ms = 0;
    w->thread = std::thread(worker_thread_func, w);
    return w;
}

void worker_destroy(void *handle_ptr)
{
    if (!handle_ptr)
        return;
    WorkerHandle *w = static_cast<WorkerHandle *>(handle_ptr);
    {
        std::lock_guard<std::mutex> lock(w->mutex);
        w->shutdown = true;
    }
    w->cv.notify_one();
    if (w->thread.joinable())
        w->thread.join();
    delete w;
}

bool worker_push_frame(void *handle_ptr, const uint8_t *y_plane,
                       int width, int height, int stride,
                       uint64_t timestamp_ns)
{
    WorkerHandle *w = static_cast<WorkerHandle *>(handle_ptr);
    if (!w)
        return false;

    FrameTask task;
    task.width = width;
    task.height = height;
    task.stride = stride;
    task.timestamp_ns = timestamp_ns;

    size_t size = static_cast<size_t>(stride) *
                  static_cast<size_t>(height);
    task.data.assign(y_plane, y_plane + size);

    {
        std::lock_guard<std::mutex> lock(w->mutex);
        if (w->shutdown)
            return false;
        if (w->queue.size() >= 2)
            return false;
        w->queue.push_back(std::move(task));
    }
    w->cv.notify_one();
    return true;
}
