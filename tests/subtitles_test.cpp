// Tests for parse_subtitles() and match_subtitles(). No framework, like
// sample_ring_test.cpp.

#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <string>

#include "core/subtitles.h"

static int failures = 0;

#define CHECK(condition) do { \
    if (!(condition)){ \
        std::fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #condition); \
        ++failures; \
    } \
} while (0)

static bool near(double a, double b){ return std::fabs(a - b) < 1e-6; }

static void test_srt(){
    const auto lines = parse_subtitles(
        "\xEF\xBB\xBF" "1\r\n"
        "00:00:01,000 --> 00:00:03,500\r\n"
        "<i>今日は</i>いい天気ですね\r\n"
        "\r\n"
        "2\r\n"
        "00:01:02,250 --> 00:01:04,000 X1:10 X2:20\r\n"
        "そうですね\r\n"
        "散歩しましょう\r\n"
        "\r\n"
        "3\r\n"
        "01:00:00.5 --> 01:00:01.5\r\n"
        "{\\an8}上の字幕\r\n");
    CHECK(lines.size() == 3);
    if (lines.size() != 3) return;
    CHECK(near(lines[0].start, 1.0) && near(lines[0].end, 3.5));
    CHECK(lines[0].text == "今日はいい天気ですね");
    CHECK(near(lines[1].start, 62.25) && near(lines[1].end, 64.0));
    CHECK(lines[1].text == "そうですね\n散歩しましょう");
    CHECK(near(lines[2].start, 3600.5));
    CHECK(lines[2].text == "上の字幕");
}

static void test_ass(){
    const auto lines = parse_subtitles(
        "[Script Info]\n"
        "Title: Test\n"
        "\n"
        "[V4+ Styles]\n"
        "Format: Name, Fontname\n"
        "Style: Default,Arial\n"
        "\n"
        "[Events]\n"
        "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
        "Dialogue: 0,0:00:05.00,0:00:07.50,Default,,0,0,0,,{\\i1}待って{\\i0}、まだ\\N終わってない\n"
        "Comment: 0,0:00:06.00,0:00:07.00,Default,,0,0,0,,これは出ない\n"
        "Dialogue: 0,0:00:02.00,0:00:04.00,Default,,0,0,0,,先に,カンマ\n"
        "Dialogue: 1,0:00:02.00,0:00:04.00,Default,,0,0,0,,先に,カンマ\n"
        "Dialogue: 0,0:00:08.00,0:00:09.00,Sign,,0,0,0,,{\\p1}m 0 0 l 100 0 100 100{\\p0}\n");
    CHECK(lines.size() == 2);
    if (lines.size() != 2) return;
    // Sorted by start, and the second layer of the same line dropped
    CHECK(near(lines[0].start, 2.0) && near(lines[0].end, 4.0));
    CHECK(lines[0].text == "先に,カンマ");
    CHECK(near(lines[1].start, 5.0) && near(lines[1].end, 7.5));
    CHECK(lines[1].text == "待って、まだ\n終わってない");
}

static void test_utf16(){
    // "1\n00:00:01,000 --> 00:00:02,000\nあ\n" in UTF-16 LE, with its BOM
    const std::string utf8 = "1\n00:00:01,000 --> 00:00:02,000\n";
    std::string utf16 = "\xFF\xFE";
    for (char c : utf8){
        utf16 += c;
        utf16 += '\0';
    }
    utf16 += "\x42\x30";    // あ, U+3042
    const auto lines = parse_subtitles(utf16);
    CHECK(lines.size() == 1);
    if (!lines.empty()) CHECK(lines[0].text == "あ");
}

static void test_empty(){
    bool threw = false;
    try {
        parse_subtitles("not subtitles at all");
    } catch (const std::runtime_error&){
        threw = true;
    }
    CHECK(threw);
}

static void test_normalize(){
    CHECK(normalized_for_match("「カタカナ」、ＡＢＣ　１２ abc!") == U"かたかなabc12abc");
    CHECK(normalized_for_match("ラーメン♪") == U"らーめん");
}

static void test_match(){
    const std::vector<SubtitleLine> lines = {
        {10, 12, "はい"},
        {20, 23, "お前の言うことは分かってる"},
        {24, 26, "でも、行かなきゃ"},
        {30, 32, "明日また会おう"},
        {100, 101, "はい"},
    };
    // A different kanji, punctuation and katakana: still that line
    auto m = match_subtitles(lines, "お前の言う事はわかってる。");
    CHECK(m && m->begin == 1 && m->end == 2);
    if (m) CHECK(m->text == "お前の言うことは分かってる");

    // Spanning two lines
    m = match_subtitles(lines, "お前の言うことは分かってるでも行かなきゃ");
    CHECK(m && m->begin == 1 && m->end == 3);
    if (m){
        CHECK(near(m->start, 20) && near(m->stop, 26));
        CHECK(m->text == "お前の言うことは分かってるでも、行かなきゃ");
    }

    // The same line twice: the one nearer the time given
    m = match_subtitles(lines, "はい", 95.0);
    CHECK(m && m->begin == 4);
    m = match_subtitles(lines, "はい", 11.0);
    CHECK(m && m->begin == 0);

    // Nothing like it
    CHECK(!match_subtitles(lines, "全然違う文章を書いてみた"));
    CHECK(!match_subtitles(lines, "。"));
    CHECK(!match_subtitles({}, "はい"));
}

int main(){
    test_srt();
    test_ass();
    test_utf16();
    test_empty();
    test_normalize();
    test_match();
    if (failures == 0) std::puts("All subtitles tests passed");
    return failures;
}
