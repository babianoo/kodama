#include "ui/subtitle_track.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <string>

// FLTK
#include <FL/Fl.H>
#include <FL/Fl_Window.H>
#include <FL/fl_draw.H>

#include "core/subtitles.h"
#include "ui/theme.h"

// Pixels the mouse has to move for a press to be a drag, not a click
static constexpr int DRAG_START = 3;
// Seconds a notch of the mouse wheel moves the subtitles
static constexpr double WHEEL_STEP = 0.1;
// Space inside a line's box, before its text
static constexpr int TEXT_PAD = 4;

SubtitleTrack::SubtitleTrack(int x, int y, int w, int h) : Fl_Widget(x, y, w, h){
    box(FL_FLAT_BOX);
    // Like the waveform's
    color(fl_lighter(FL_BACKGROUND_COLOR));
}

void SubtitleTrack::lines(const std::vector<SubtitleLine>* new_lines){
    lines_ = new_lines;
    highlight_begin = highlight_end = 0;
    redraw();
}

void SubtitleTrack::duration(double seconds){
    duration_ = seconds;
    redraw();
}

void SubtitleTrack::sync(std::optional<double> video_at_end){
    sync_ = video_at_end;
    redraw();
}

void SubtitleTrack::highlight(size_t begin, size_t end){
    highlight_begin = begin;
    highlight_end = end;
    redraw();
}

int SubtitleTrack::x_of(double video_time) const {
    // The capture's start, in the video
    const double start = *sync_ - duration_;
    return x() + static_cast<int>(std::lround((video_time - start) * w() / duration_));
}

double SubtitleTrack::time_at(int px) const {
    return *sync_ - duration_ + static_cast<double>(px - x()) * duration_ / w();
}

std::optional<size_t> SubtitleTrack::line_at(int px) const {
    if (!lines_ || !sync_ || duration_ <= 0 || w() <= 0) return std::nullopt;
    const double t = time_at(px);
    // The last one that started before it, of those still on then: the one
    // drawn on top
    std::optional<size_t> found;
    for (size_t i = 0; i < lines_->size(); ++i){
        const SubtitleLine& line = (*lines_)[i];
        if (line.start > t) break;
        if (line.end >= t) found = i;
    }
    return found;
}

void SubtitleTrack::shift(double seconds){
    if (!sync_) return;
    sync_ = *sync_ + seconds;
    redraw();
    if (sync_listener) sync_listener();
}

int SubtitleTrack::handle(int event){
    const bool usable = lines_ && sync_ && duration_ > 0 && w() > 0;
    switch (event){
    case FL_ENTER:
    case FL_MOVE:
        window()->cursor(usable ? FL_CURSOR_WE : FL_CURSOR_DEFAULT);
        return 1;    // Needed to get FL_MOVE and FL_LEAVE
    case FL_LEAVE:
        window()->cursor(FL_CURSOR_DEFAULT);
        return 1;

    case FL_PUSH:
        if (Fl::event_button() != FL_LEFT_MOUSE || !usable) return 0;
        drag_x = Fl::event_x();
        drag_sync = *sync_;
        dragged = false;
        return 1;    // Needed to get FL_DRAG and FL_RELEASE
    case FL_DRAG: {
        if (!usable) return 1;
        const int dx = Fl::event_x() - drag_x;
        if (!dragged && std::abs(dx) < DRAG_START) return 1;
        dragged = true;
        // Dragging them right shows them later: the capture is then
        // earlier in the video
        const double target = drag_sync - dx * duration_ / w();
        shift(target - *sync_);
        return 1;
    }
    case FL_RELEASE:
        if (!usable || dragged) return 1;
        if (const auto line = line_at(Fl::event_x()); line && pick_listener) pick_listener(*line);
        return 1;

    case FL_MOUSEWHEEL:
        if (!usable || Fl::event_dy() == 0) return 0;
        // Down moves them right, like dragging that way
        shift(-Fl::event_dy() * WHEEL_STEP);
        return 1;
    }
    return Fl_Widget::handle(event);
}

void SubtitleTrack::draw(){
    draw_box();
    if (!lines_) return;
    if (!sync_ || duration_ <= 0 || w() <= 0){
        fl_font(FL_HELVETICA_ITALIC, 13);
        fl_color(DIM);
        fl_draw("Not synced yet: transcribe a line, and it's looked for in the subtitles.",
                x(), y(), w(), h(), FL_ALIGN_CENTER | FL_ALIGN_CLIP);
        return;
    }

    const double from = *sync_ - duration_, to = *sync_;
    // The ones on screen. A line that starts before the one before it ends
    // goes in a second row, so both can be read.
    struct Shown { size_t index; int row; };
    std::vector<Shown> shown;
    double row0_end = -1e9;
    for (size_t i = 0; i < lines_->size(); ++i){
        const SubtitleLine& line = (*lines_)[i];
        if (line.start >= to) break;
        if (line.end <= from) continue;
        const int row = line.start < row0_end ? 1 : 0;
        if (row == 0) row0_end = line.end;
        shown.push_back({i, row});
    }
    const bool two_rows = std::ranges::any_of(shown, [](const Shown& s){ return s.row == 1; });
    const int row_h = two_rows ? h() / 2 : h();

    fl_push_clip(x(), y(), w(), h());
    fl_font(FL_HELVETICA, two_rows ? 11 : 13);
    for (const Shown& s : shown){
        const SubtitleLine& line = (*lines_)[s.index];
        const bool picked = s.index >= highlight_begin && s.index < highlight_end;
        const int bx = std::max(x(), x_of(line.start));
        const int bx_end = std::min(x() + w(), x_of(line.end));
        const int by = y() + s.row * row_h + 1;
        const int bw = std::max(1, bx_end - bx - 1), bh = row_h - 2;
        fl_color(fl_color_average(ACCENT, color(), picked ? 0.45f : 0.15f));
        fl_rectf(bx, by, bw, bh);
        fl_color(fl_color_average(ACCENT, color(), picked ? 0.9f : 0.4f));
        fl_rect(bx, by, bw, bh);

        // On one line, cut off at the box's end
        std::string text = line.text;
        std::ranges::replace(text, '\n', ' ');
        fl_push_clip(bx + TEXT_PAD, by, std::max(0, bw - 2 * TEXT_PAD), bh);
        fl_color(picked ? FL_FOREGROUND_COLOR : DIM);
        // Not fl_draw()'s boxed form, which would take '@' for a symbol
        fl_draw(text.c_str(), bx + TEXT_PAD, by + (bh + fl_height()) / 2 - fl_descent());
        fl_pop_clip();
    }
    fl_pop_clip();
}
