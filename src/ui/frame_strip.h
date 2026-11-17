#pragma once

#include <functional>
#include <memory>
#include <utility>
#include <vector>

// FLTK
#include <FL/Fl_Widget.H>

#include "core/screen_frames.h"

class Fl_RGB_Image;

// A row of screenshots' thumbnails, oldest on the left, each with how long
// before the capture it was taken, to pick a card's picture from. The one
// picked has a blue frame. Clicking another picks it; the mouse wheel
// scrolls the row when they don't all fit.
class FrameStrip : public Fl_Widget {
public:
    // How tall it wants to be: the thumbnails, and a line under them
    static constexpr int HEIGHT = FRAME_THUMBNAIL_HEIGHT + 30;

    FrameStrip(int x, int y, int w, int h);
    // Defined in frame_strip.cpp, where Fl_RGB_Image is complete
    ~FrameStrip() override;

    // Shows frames, with picked picked. Only borrows them: they have to live
    // until the next call, or the widget dies. captured_at is when the
    // capture was, which the times are told from. nullptr shows nothing.
    void frames(const std::vector<ScreenFrame>* frames, double captured_at, int picked);
    // Called with the index of the one clicked
    void on_pick(std::function<void(int)> listener) { pick_listener = std::move(listener); }

    int handle(int event) override;

protected:
    void draw() override;

private:
    const std::vector<ScreenFrame>* shown = nullptr;
    double captured_at = 0;
    int picked = -1;
    // Pixels the row is scrolled to the left
    int scroll = 0;
    std::function<void(int)> pick_listener;
    // One per frame. They only borrow the thumbnails' pixels.
    std::vector<std::unique_ptr<Fl_RGB_Image>> images;

    // How wide the whole row is
    int row_w() const;
    // Scrolls so the picked one is in view
    void scroll_to_picked();
    void clamp_scroll();
};
