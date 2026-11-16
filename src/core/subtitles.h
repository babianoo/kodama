#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// A line of a subtitle file: when it's on screen, in seconds from the
// start of the video, and what it says. Lines shown at once on screen are
// joined with '\n'; formatting (ASS's {\...} tags, SRT's <i> and such) is
// taken out.
struct SubtitleLine {
    double start = 0;
    double end = 0;
    std::string text;
};

// The lines of an .srt or .ass (or .ssa) file's text, sorted by start.
// Which one it is is told from the text, not the file's name. UTF-8, with
// or without a BOM; UTF-16 with a BOM is converted. Throws if there are no
// lines in it.
std::vector<SubtitleLine> parse_subtitles(std::string_view file);

// The same for a file. Throws if it can't be read.
std::vector<SubtitleLine> load_subtitles(const std::filesystem::path& file);

// The line, or the few lines in a row, of a subtitle file that say most
// nearly what a transcript does
struct SubtitleMatch {
    // Index of the first line and one past the last
    size_t begin = 0, end = 0;
    // From 0 to 1: how alike they are, punctuation, spaces and katakana
    // against hiragana aside. 1 is the same.
    double score = 0;
    // Their text, joined with no break, and when they start and end
    std::string text;
    double start = 0, stop = 0;
};

// The lines that are most like transcript, if any are alike enough (score
// at least min_score). Up to max_lines lines in a row are tried, since a
// transcript can span a few. A transcript can't tell apart lines that say
// the same, e.g. a short "はい": near, a time in the video, then picks the
// one closest to it among the ones that score about the same.
std::optional<SubtitleMatch> match_subtitles(const std::vector<SubtitleLine>& lines, std::string_view transcript,
                                             std::optional<double> near = {}, double min_score = 0.5,
                                             size_t max_lines = 3);

// What's compared: transcript with the spaces and punctuation taken out,
// katakana as hiragana and full-width letters and digits as ASCII ones,
// lower case. As code points. For the tests.
std::u32string normalized_for_match(std::string_view text);
