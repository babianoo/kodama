#pragma once

#include <cstddef>
#include <functional>
#include <optional>
#include <utility>
#include <vector>

// FLTK
#include <FL/Fl_Widget.H>

struct SubtitleLine;

// A strip under the waveform with the subtitles shown during the capture,
// each a box from where it starts to where it ends, lined up with the
// waveform above: the same x and width, and the capture's last second on
// the right.
//
// Which part of the video the capture is comes from sync(): the time in
// the video at the capture's end. Without one it only says how to get one.
// Dragging the strip sideways moves the subtitles, which changes it, and
// so does the mouse wheel, a tenth of a second a notch. Clicking a line
// picks it.
class SubtitleTrack : public Fl_Widget {
public:
    SubtitleTrack(int x, int y, int w, int h);

    // Only borrows lines: they have to live until the next call, or the
    // widget dies. nullptr for none.
    void lines(const std::vector<SubtitleLine>* lines);
    // How long the capture is, in seconds
    void duration(double seconds);

    // The time in the video, in seconds, at the capture's end; none if
    // that isn't known. Setting it doesn't call on_sync.
    std::optional<double> sync() const { return sync_; }
    void sync(std::optional<double> video_at_end);
    // Called while the user drags the subtitles or turns the wheel over
    // them, with the new sync()
    void on_sync(std::function<void()> listener) { sync_listener = std::move(listener); }

    // Lines begin up to end are drawn as picked; begin == end for none
    void highlight(size_t begin, size_t end);
    // Called with a line's index when the user clicks it
    void on_pick(std::function<void(size_t)> listener) { pick_listener = std::move(listener); }

    int handle(int event) override;

protected:
    void draw() override;

private:
    const std::vector<SubtitleLine>* lines_ = nullptr;
    double duration_ = 0;
    std::optional<double> sync_;
    size_t highlight_begin = 0, highlight_end = 0;
    std::function<void()> sync_listener;
    std::function<void(size_t)> pick_listener;

    // Where a drag started, and the sync then
    int drag_x = 0;
    double drag_sync = 0;
    bool dragged = false;

    // Where a time in the video is across the widget, and the other way
    // round. Only with a sync.
    int x_of(double video_time) const;
    double time_at(int px) const;
    // The line under px, if there's one there
    std::optional<size_t> line_at(int px) const;
    // Moves the sync by seconds and tells on_sync
    void shift(double seconds);
};
