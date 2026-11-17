#include "core/screen_frames.h"

#include <cmath>
#include <stdexcept>
#include <utility>

Image ScreenFrame::picture() const {
    if (image) return *image;
    if (jpeg) return decode_jpeg(*jpeg);
    return {};
}

std::vector<std::uint8_t> ScreenFrame::jpeg_bytes(int quality) const {
    if (jpeg) return *jpeg;
    if (image) return encode_jpeg(*image, quality);
    throw std::runtime_error("The screenshot has no picture");
}

ScreenFrame compress_frame(const Image& image, double time){
    if (image.empty()) throw std::runtime_error("The screenshot is empty");
    const Image smaller = shrink_to_height(image, FRAME_MAX_HEIGHT);
    ScreenFrame frame;
    frame.time = time;
    frame.thumbnail = shrink_to_height(smaller, FRAME_THUMBNAIL_HEIGHT);
    frame.jpeg = std::make_shared<const std::vector<std::uint8_t>>(encode_jpeg(smaller, FRAME_QUALITY));
    return frame;
}

ScreenFrame keep_frame(Image image, double time){
    ScreenFrame frame;
    frame.time = time;
    if (image.empty()) return frame;
    frame.thumbnail = shrink_to_height(image, FRAME_THUMBNAIL_HEIGHT);
    frame.image = std::make_shared<const Image>(std::move(image));
    return frame;
}

ScreenFrame jpeg_frame(std::vector<std::uint8_t> jpeg, double time){
    const Image image = decode_jpeg(jpeg);
    ScreenFrame frame;
    frame.time = time;
    frame.thumbnail = shrink_to_height(image, FRAME_THUMBNAIL_HEIGHT);
    frame.jpeg = std::make_shared<const std::vector<std::uint8_t>>(std::move(jpeg));
    return frame;
}

int nearest_frame(std::span<const ScreenFrame> frames, double time){
    int best = -1;
    double best_distance = 0;
    for (size_t i = 0; i < frames.size(); ++i){
        const double distance = std::fabs(frames[i].time - time);
        if (best < 0 || distance <= best_distance){
            best = static_cast<int>(i);
            best_distance = distance;
        }
    }
    return best;
}
