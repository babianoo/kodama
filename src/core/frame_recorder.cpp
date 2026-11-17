#include "core/frame_recorder.h"

#include <chrono>
#include <exception>
#include <format>
#include <iostream>
#include <utility>

#include "core/screenshot.h"

// The clock, in seconds since 1970 (UTC)
static double clock_now(){
    return std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();
}

FrameRecorder::~FrameRecorder(){
    if (thread.joinable()){
        thread.request_stop();
        wake.notify_all();
    }
    // thread then joins as it goes
}

void FrameRecorder::configure(bool on, int new_interval, int new_keep_seconds){
    if (!on){
        if (thread.joinable()){
            thread.request_stop();
            wake.notify_all();
            thread.join();
            thread = {};
        }
        const std::lock_guard lock(mutex);
        kept.clear();
        interval = 0;
        return;
    }
    {
        const std::lock_guard lock(mutex);
        if (interval == new_interval && keep_seconds == new_keep_seconds && thread.joinable()) return;
        interval = new_interval;
        keep_seconds = new_keep_seconds;
    }
    // A new interval starts counting now
    if (thread.joinable()) wake.notify_all();
    else thread = std::jthread([this](std::stop_token stop){ run(stop); });
}

std::vector<ScreenFrame> FrameRecorder::frames(double since, double until) const {
    const std::lock_guard lock(mutex);
    std::vector<ScreenFrame> found;
    for (const ScreenFrame& frame : kept)
        if (frame.time >= since && frame.time <= until) found.push_back(frame);
    return found;
}

void FrameRecorder::run(std::stop_token stop){
    std::unique_lock lock(mutex);
    while (!stop.stop_requested()){
        // Woken early by configure(), to wait the new interval instead
        const int waiting_for = interval;
        const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(waiting_for);
        if (wake.wait_until(lock, stop, until, [&]{ return interval != waiting_for; })) continue;
        if (stop.stop_requested()) break;

        // Without the lock: frames() isn't held up while it's taken
        lock.unlock();
        const double now = clock_now();
        ScreenFrame frame;
        std::string error;
        try {
            frame = compress_frame(take_screenshot(true), now);
        } catch (const std::exception& e){
            error = e.what();
        }
        lock.lock();

        if (error.empty()){
            kept.push_back(std::move(frame));
            last_error.clear();
        } else if (error != last_error){
            std::cerr << std::format("Not keeping screenshots: {}\n", error);
            last_error = error;
        }
        // One older than what's kept too: a line can start before the
        // first frame of it
        const double oldest = now - keep_seconds - interval;
        while (!kept.empty() && kept.front().time < oldest) kept.pop_front();
    }
}
