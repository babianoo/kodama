// Tests for make_transcript(). No framework, like sample_ring_test.cpp.

#include <cstdio>
#include <string>
#include <vector>

#include "core/transcript.h"

static int failures = 0;

#define CHECK(condition) do { \
    if (!(condition)){ \
        std::fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #condition); \
        ++failures; \
    } \
} while (0)

// The unsure words' text, to check them by
static std::vector<std::string> unsure_text(const Transcript& transcript){
    std::vector<std::string> words;
    for (const UnsureWord& word : transcript.unsure)
        words.push_back(transcript.text.substr(word.begin, word.end - word.begin));
    return words;
}

static void test_japanese(){
    // 猫 is E7 8C AB, written in two tokens: the word is both, as likely
    // as the less likely
    const std::vector<TranscriptToken> tokens{
        {" 今日", 0.95f}, {"は", 0.9f}, {"\xE7\x8C", 0.3f}, {"\xAB", 0.99f}, {"が", 0.4f},
        {"好き", 0.97f}, {"だね。", 0.2f}, {"」", 0.1f},
    };
    const Transcript transcript = make_transcript(tokens);
    CHECK(transcript.text == "今日は猫が好きだね。」");
    // Without the 。, and the 」 alone isn't a word
    CHECK((unsure_text(transcript) == std::vector<std::string>{"猫", "が", "だね"}));
    CHECK(transcript.unsure.size() == 3);
    if (transcript.unsure.size() == 3){
        CHECK((transcript.unsure[0] == UnsureWord{9, 12, 0.3f}));
        CHECK(transcript.unsure[1].p == 0.4f);
        CHECK(transcript.unsure[2].p == 0.2f);
    }
}

static void test_latin(){
    // Pieces of a word run on into it; its comma and spaces aren't part
    const std::vector<TranscriptToken> tokens{
        {" Hello", 0.9f}, {" wor", 0.3f}, {"ld", 0.9f}, {",", 0.1f}, {" there", 0.6f}, {" ok ", 0.2f},
    };
    const Transcript transcript = make_transcript(tokens);
    CHECK(transcript.text == "Hello world, there ok");
    CHECK((unsure_text(transcript) == std::vector<std::string>{"world", "ok"}));
}

static void test_threshold(){
    const std::vector<TranscriptToken> tokens{{"a", 0.5f}, {" b", 0.49f}};
    // At the threshold is sure
    CHECK((unsure_text(make_transcript(tokens)) == std::vector<std::string>{"b"}));
    CHECK(make_transcript(tokens, 0.1f).unsure.empty());
    CHECK(make_transcript(tokens, 0.6f).unsure.size() == 2);
}

static void test_empty(){
    CHECK(make_transcript({}).text.empty());
    const std::vector<TranscriptToken> spaces{{" ", 0.1f}, {"\n", 0.1f}};
    const Transcript blank = make_transcript(spaces);
    CHECK(blank.text.empty() && blank.unsure.empty());
}

static void test_cut_off(){
    // Ends partway through a character: still a word, as it is
    const std::vector<TranscriptToken> tokens{{"猫", 0.9f}, {"\xE7\x8C", 0.2f}};
    const Transcript transcript = make_transcript(tokens);
    CHECK(transcript.text == "猫\xE7\x8C");
    CHECK(transcript.unsure.size() == 1);
    if (transcript.unsure.size() == 1) CHECK(transcript.unsure[0].begin == 3 && transcript.unsure[0].end == 5);
}

int main(){
    test_japanese();
    test_latin();
    test_threshold();
    test_empty();
    test_cut_off();
    if (failures == 0) std::puts("All transcript tests passed");
    return failures == 0 ? 0 : 1;
}
