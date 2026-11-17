#pragma once

#include <condition_variable>
#include <deque>
#include <mutex>
#include <stop_token>
#include <string>
#include <thread>
#include <vector>

#include "core/screen_frames.h"

// Takes a screenshot every few seconds on a thread of its own, and keeps
// the ones of the last seconds, like the audio buffer keeps what was
// played. Each is made smaller and compressed (see compress_frame()), so
// 30 seconds of them take a few megabytes.
//
// On Linux it only takes them the quick ways (grim, KWin), never through
// the screenshot portal, which is slow and saves a file each time: where
// only the portal works, e.g. on GNOME, it keeps none.
//
// Its methods can be called from any thread.
class FrameRecorder {
public:
    FrameRecorder() = default;
    // Stops the thread and waits for it, at most the time one screenshot
    // takes
    ~FrameRecorder();

    FrameRecorder(const FrameRecorder&) = delete;
    FrameRecorder& operator=(const FrameRecorder&) = delete;

    // Takes one every interval seconds, keeping those of the last
    // keep_seconds, or stops and forgets them all if on is false. The first
    // comes interval seconds after it starts. Changing nothing does nothing.
    void configure(bool on, int interval, int keep_seconds);

    // The frames taken from since to until (in seconds since 1970), oldest
    // first
    std::vector<ScreenFrame> frames(double since, double until) const;

private:
    mutable std::mutex mutex;
    std::condition_variable_any wake;
    std::deque<ScreenFrame> kept;
    // Read by the thread under mutex
    int interval = 0;
    int keep_seconds = 0;
    // Said once, not every few seconds
    std::string last_error;

    std::jthread thread;

    void run(std::stop_token stop);
};
