// Tests for core/stash.h, in a folder of its own under the system's temp
// folder. No framework, like sample_ring_test.cpp.

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "core/stash.h"

namespace fs = std::filesystem;

static int failures = 0;

#define CHECK(condition) do { \
    if (!(condition)){ \
        std::fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #condition); \
        ++failures; \
    } \
} while (0)

static Image gradient(int width, int height){
    Image image{.width = width, .height = height, .rgb = {}};
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x){
            image.rgb.push_back(static_cast<std::uint8_t>(x * 255 / width));
            image.rgb.push_back(static_cast<std::uint8_t>(y * 255 / height));
            image.rgb.push_back(128);
        }
    return image;
}

int main(){
    const fs::path dir = fs::temp_directory_path() / "hearstash-stash-test" / "stash";
    fs::remove_all(dir.parent_path());

    // Nothing there yet
    CHECK(list_stash(dir).empty());

    std::vector<float> audio(4800);
    for (size_t i = 0; i < audio.size(); ++i) audio[i] = 0.5f * std::sin(static_cast<float>(i) * 0.05f);
    const Image screenshot = gradient(320, 180);

    StashCapture capture{.audio = audio, .rate = 48000, .selection_seconds = 8, .screenshot = &screenshot,
                         .screenshot_error = "", .app = "mpv", .time = 1793012345};
    const StashEntry first = stash_line(dir, capture);
    CHECK(!first.id.empty());
    CHECK(std::abs(first.seconds - 0.1) < 1e-9);
    CHECK(first.bytes > 0);
    CHECK(fs::exists(dir / first.id / "stash.json"));

    // Same second, no screenshot: a folder of its own
    capture.screenshot = nullptr;
    capture.screenshot_error = "The portal said no";
    capture.app = "";
    const StashEntry second = stash_line(dir, capture);
    CHECK(second.id == first.id + "-2");

    // A later one, which lists first
    capture.time += 60;
    const StashEntry third = stash_line(dir, capture);

    // A folder cut off while saving, and a broken one, are skipped
    fs::create_directories(dir / "half-written");
    std::ofstream(dir / "half-written" / "audio.wav") << "RIFF";
    fs::create_directories(dir / "broken");
    std::ofstream(dir / "broken" / "stash.json") << "{not json";

    const std::vector<StashEntry> entries = list_stash(dir);
    CHECK(entries.size() == 3);
    if (entries.size() == 3){
        CHECK(entries[0].id == third.id);
        CHECK(entries[1].id == second.id);
        CHECK(entries[2].id == first.id);
        CHECK(entries[2].app == "mpv");
        CHECK(entries[2].rate == 48000);
        CHECK(entries[2].selection_seconds == 8);
        CHECK(std::abs(entries[2].seconds - 0.1) < 1e-9);
        CHECK(entries[1].screenshot_error == "The portal said no");
    }

    // The audio comes back as it was saved, to 16 bits
    const StashedLine line = load_stashed(dir, first);
    CHECK(line.rate == 48000);
    CHECK(line.audio.size() == audio.size());
    bool close = line.audio.size() == audio.size();
    for (size_t i = 0; close && i < audio.size(); ++i) close = std::abs(line.audio[i] - audio[i]) < 1.0f / 30000;
    CHECK(close);
    CHECK(line.selection_seconds == 8);
    CHECK(line.screenshot.width == 320 && line.screenshot.height == 180);
    CHECK(line.screenshot_error.empty());

    const Image thumbnail = load_stash_thumbnail(dir, first);
    CHECK(thumbnail.height == STASH_THUMBNAIL_HEIGHT);
    CHECK(thumbnail.width == 160);

    const StashedLine without = load_stashed(dir, second);
    CHECK(without.screenshot.empty());
    CHECK(without.screenshot_error == "The portal said no");
    CHECK(load_stash_thumbnail(dir, second).empty());
    CHECK(line.frames.empty());

    // With screenshots kept every few seconds before it: a compressed one,
    // and one kept as it was, which is compressed when saved
    {
        const std::vector<ScreenFrame> frames{compress_frame(gradient(1920, 1080), 1793012400.25),
                                              keep_frame(gradient(320, 180), 1793012403.25)};
        CHECK(frames[0].thumbnail.height == FRAME_THUMBNAIL_HEIGHT);
        CHECK(frames[0].picture().height == 1080);
        StashCapture with = capture;
        with.screenshot = &screenshot;
        with.screenshot_error = "";
        with.time = 1793012405;
        with.precise_time = 1793012405.75;
        with.frames = frames;
        const StashEntry framed = stash_line(dir, with);
        CHECK(fs::exists(dir / framed.id / "frame-01.jpg"));
        CHECK(fs::exists(dir / framed.id / "frame-02.jpg"));

        const StashedLine loaded = load_stashed(dir, framed);
        CHECK(loaded.frames.size() == 2);
        if (loaded.frames.size() == 2){
            // Told from the whole second it was stashed in, as the capture
            // window is
            CHECK(std::abs(loaded.frames[0].time - (1793012405 - 5.5)) < 1e-6);
            CHECK(std::abs(loaded.frames[1].time - (1793012405 - 2.5)) < 1e-6);
            CHECK(loaded.frames[0].picture().width == 1920);
            CHECK(loaded.frames[1].picture().width == 320);
            CHECK(loaded.frames[1].thumbnail.height == FRAME_THUMBNAIL_HEIGHT);
        }
        delete_stashed(dir, framed.id);
    }

    // The nearest, the later on a tie
    {
        const std::vector<ScreenFrame> none;
        CHECK(nearest_frame(none, 5) == -1);
        std::vector<ScreenFrame> frames(3);
        frames[0].time = 10;
        frames[1].time = 13;
        frames[2].time = 16;
        CHECK(nearest_frame(frames, 0) == 0);
        CHECK(nearest_frame(frames, 12) == 1);
        CHECK(nearest_frame(frames, 14.5) == 2);
        CHECK(nearest_frame(frames, 99) == 2);
    }

    // A transcript added later, which a line starts without
    CHECK(first.transcript.empty() && first.transcript_error.empty());
    // With the words whisper wasn't sure of: 猫, and one that isn't in the
    // text, which is left out
    const std::vector<UnsureWord> unsure{{0, 3, 0.25f}, {6, 99, 0.1f}};
    set_stash_transcript(dir, first.id, "猫が好き", "", unsure);
    set_stash_transcript(dir, second.id, "", "No whisper model");
    {
        const std::vector<StashEntry> again = list_stash(dir);
        CHECK(again.size() == 3);
        if (again.size() == 3){
            CHECK(again[2].transcript == "猫が好き");
            CHECK(again[2].transcript_error.empty());
            CHECK((again[2].transcript_unsure == std::vector<UnsureWord>{{0, 3, 0.25f}}));
            CHECK(again[1].transcript.empty());
            CHECK(again[1].transcript_error == "No whisper model");
            CHECK(again[1].transcript_unsure.empty());
            const StashedLine loaded = load_stashed(dir, again[2]);
            CHECK(loaded.transcript.text == "猫が好き");
            CHECK(loaded.transcript.unsure.size() == 1);
        }
    }

    // Never outside the stash
    delete_stashed(dir, "..");
    delete_stashed(dir, "../stash");
    CHECK(fs::exists(dir));

    delete_stashed(dir, second.id);
    CHECK(list_stash(dir).size() == 2);
    CHECK(!fs::exists(dir / second.id));
    bool threw = false;
    try {
        load_stashed(dir, second);
    } catch (const std::exception&){
        threw = true;
    }
    CHECK(threw);
    // Nor is a transcript saved for it, which would make its folder again
    threw = false;
    try {
        set_stash_transcript(dir, second.id, "late", "");
    } catch (const std::exception&){
        threw = true;
    }
    CHECK(threw);
    CHECK(!fs::exists(dir / second.id));

    clear_stash(dir);
    CHECK(list_stash(dir).empty());
    CHECK(fs::is_empty(dir));

    // Nothing recorded yet
    threw = false;
    try {
        stash_line(dir, StashCapture{.audio = {}, .rate = 48000, .selection_seconds = 8, .screenshot = nullptr,
                                     .screenshot_error = "", .app = "", .time = 0});
    } catch (const std::exception&){
        threw = true;
    }
    CHECK(threw);

    fs::remove_all(dir.parent_path());
    if (failures == 0) std::puts("All stash tests passed");
    return failures;
}
