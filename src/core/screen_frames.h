#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "core/image.h"

// Screenshots kept every few seconds next to the audio buffer, so a card's
// picture can be picked from around the line instead of only the moment the
// shortcut was pressed, which is often a bit late.

// How tall a kept screenshot is at most. A 4K screen is made smaller: it's
// then about a third of a megabyte as JPEG, and a card's picture is no
// taller than 480 pixels anyway.
inline constexpr int FRAME_MAX_HEIGHT = 1080;
inline constexpr int FRAME_QUALITY = 90;
// How tall the thumbnails to pick from are, in pixels
inline constexpr int FRAME_THUMBNAIL_HEIGHT = 72;

// One screenshot, and when it was taken. Copies share the picture, so
// they're cheap to hand from window to window.
struct ScreenFrame {
    // When, in seconds since 1970 (UTC)
    double time = 0;
    // FRAME_THUMBNAIL_HEIGHT tall
    Image thumbnail;
    // The picture: either as JPEG bytes, or already decoded, e.g. the one
    // taken when the shortcut was pressed, kept as sharp as it was
    std::shared_ptr<const std::vector<std::uint8_t>> jpeg;
    std::shared_ptr<const Image> image;

    // The picture, decoded if needed. Throws std::runtime_error if its
    // JPEG is broken.
    Image picture() const;
    // As JPEG bytes, encoded at quality if needed. Throws
    // std::runtime_error if there's no picture.
    std::vector<std::uint8_t> jpeg_bytes(int quality) const;
};

// A frame of a screenshot taken every few seconds: made no taller than
// FRAME_MAX_HEIGHT and kept as JPEG. Slow-ish (a tenth of a second for a
// 4K screen), so meant for a thread of its own. Throws std::runtime_error
// if image is empty.
ScreenFrame compress_frame(const Image& image, double time);

// A frame of image as it is, decoded, e.g. the screenshot taken when the
// shortcut was pressed. Empty image gives a frame without a picture.
ScreenFrame keep_frame(Image image, double time);

// A frame of a JPEG file's bytes, e.g. read back from the stash. Throws
// std::runtime_error if they aren't one.
ScreenFrame jpeg_frame(std::vector<std::uint8_t> jpeg, double time);

// The index of the frame taken nearest to time, the later one on a tie.
// -1 if there are none.
int nearest_frame(std::span<const ScreenFrame> frames, double time);
