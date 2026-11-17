#include "ui/frame_strip.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <string>

// FLTK
#include <FL/Fl.H>
#include <FL/Fl_Image.H>
#include <FL/fl_draw.H>

#include "ui/theme.h"

// A slot is wide enough for a 16:9 thumbnail; a narrower one is centred in
// it, a wider one cut at its sides
static constexpr int SLOT_W = FRAME_THUMBNAIL_HEIGHT * 16 / 9;
static constexpr int GAP = 8;
// Space above the thumbnails, for the frame around the picked one
static constexpr int TOP = 4;
static constexpr int FRAME_W = 3;
// Pixels a notch of the mouse wheel scrolls
static constexpr int WHEEL_STEP = SLOT_W / 2;

FrameStrip::FrameStrip(int x, int y, int w, int h) : Fl_Widget(x, y, w, h){
    box(FL_FLAT_BOX);
    color(FL_BACKGROUND_COLOR);
}

FrameStrip::~FrameStrip() = default;

void FrameStrip::frames(const std::vector<ScreenFrame>* new_frames, double new_captured_at, int new_picked){
    shown = new_frames;
    captured_at = new_captured_at;
    picked = new_picked;
    images.clear();
    if (shown)
        for (const ScreenFrame& frame : *shown){
            const Image& thumb = frame.thumbnail;
            images.push_back(thumb.empty() ? nullptr
                             : std::make_unique<Fl_RGB_Image>(thumb.rgb.data(), thumb.width, thumb.height, 3));
        }
    scroll_to_picked();
    redraw();
}

int FrameStrip::row_w() const {
    const int count = shown ? static_cast<int>(shown->size()) : 0;
    return count == 0 ? 0 : count * SLOT_W + (count - 1) * GAP + 2 * FRAME_W;
}

void FrameStrip::clamp_scroll(){
    scroll = std::clamp(scroll, 0, std::max(0, row_w() - w()));
}

void FrameStrip::scroll_to_picked(){
    if (picked >= 0){
        const int left = picked * (SLOT_W + GAP);
        const int right = left + SLOT_W + 2 * FRAME_W;
        if (left < scroll) scroll = left;
        else if (right > scroll + w()) scroll = right - w();
    }
    clamp_scroll();
}

// How long before the capture time is, as the line under its thumbnail
static std::string when_label(double captured_at, double time){
    // The one taken with the shortcut has its time to the dot
    const double before = captured_at - time;
    if (before < 0.01) return "When pressed";
    if (before < 0.5) return "Just before";
    return std::format("{:.0f} s before", before);
}

void FrameStrip::draw(){
    draw_box();
    if (!shown) return;
    fl_push_clip(x(), y(), w(), h());
    const int thumb_y = y() + TOP;
    for (size_t i = 0; i < shown->size(); ++i){
        const int slot_x = x() + FRAME_W - scroll + static_cast<int>(i) * (SLOT_W + GAP);
        if (slot_x + SLOT_W + FRAME_W < x() || slot_x - FRAME_W > x() + w()) continue;
        const bool is_picked = static_cast<int>(i) == picked;

        // The picked one in a blue frame, the rest in a thin grey one
        if (is_picked){
            fl_color(ACCENT);
            fl_rectf(slot_x - FRAME_W, thumb_y - FRAME_W, SLOT_W + 2 * FRAME_W, FRAME_THUMBNAIL_HEIGHT + 2 * FRAME_W);
        } else {
            fl_color(fl_darker(FL_BACKGROUND_COLOR));
            fl_rect(slot_x - 1, thumb_y - 1, SLOT_W + 2, FRAME_THUMBNAIL_HEIGHT + 2);
        }
        fl_color(fl_darker(FL_BACKGROUND_COLOR));
        fl_rectf(slot_x, thumb_y, SLOT_W, FRAME_THUMBNAIL_HEIGHT);
        if (Fl_RGB_Image* image = images[i].get()){
            fl_push_clip(slot_x, thumb_y, SLOT_W, FRAME_THUMBNAIL_HEIGHT);
            image->draw(slot_x + (SLOT_W - image->w()) / 2, thumb_y + (FRAME_THUMBNAIL_HEIGHT - image->h()) / 2);
            fl_pop_clip();
        }

        fl_font(is_picked ? FL_HELVETICA_BOLD : FL_HELVETICA, 12);
        fl_color(is_picked ? ACCENT : DIM);
        fl_draw(when_label(captured_at, (*shown)[i].time).c_str(), slot_x, thumb_y + FRAME_THUMBNAIL_HEIGHT + FRAME_W,
                SLOT_W, 18, FL_ALIGN_CENTER);
    }
    fl_pop_clip();
}

int FrameStrip::handle(int event){
    switch (event){
    case FL_ENTER:
    case FL_MOVE:
        // For the tooltip
        return 1;
    case FL_PUSH: {
        if (!shown || Fl::event_button() != FL_LEFT_MOUSE) return 1;
        const int along = Fl::event_x() - x() - FRAME_W + scroll;
        const int index = along / (SLOT_W + GAP);
        // Not in the gap between two
        if (along < 0 || along % (SLOT_W + GAP) >= SLOT_W || index >= static_cast<int>(shown->size())) return 1;
        if (index != picked){
            picked = index;
            scroll_to_picked();
            redraw();
            if (pick_listener) pick_listener(index);
        }
        return 1;
    }
    case FL_MOUSEWHEEL: {
        if (row_w() <= w()) return 0;
        // Either way the wheel turns, or a touchpad's sideways swipe
        const int notches = Fl::event_dx() != 0 ? Fl::event_dx() : Fl::event_dy();
        scroll += notches * WHEEL_STEP;
        clamp_scroll();
        redraw();
        return 1;
    }
    default:
        return Fl_Widget::handle(event);
    }
}
