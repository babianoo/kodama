#pragma once

#include <cstddef>
#include <span>
#include <string>
#include <vector>

// A word whisper wasn't sure it heard right: the transcript's text from
// begin up to end, as byte offsets, and how likely whisper thought it was,
// from 0 to 1: the least likely of the tokens it's made of.
struct UnsureWord {
    size_t begin = 0, end = 0;
    float p = 0;

    bool operator==(const UnsureWord&) const = default;
};

// What whisper made of some speech: the text, trimmed, and the words in it
// it wasn't sure of, to check by ear. Those are in order and don't overlap.
struct Transcript {
    std::string text;
    std::vector<UnsureWord> unsure;
};

// One token whisper wrote, as whisper.cpp gives it: a piece of text,
// maybe only part of a UTF-8 character (a kanji is often two tokens), and
// its probability, 0 to 1
struct TranscriptToken {
    std::string text;
    float p = 1;
};

// Below this probability a word is unsure. Most of what whisper hears
// clearly is above 0.8; a word it guessed is often under 0.5.
inline constexpr float UNSURE_BELOW = 0.5f;

// The transcript the tokens make, one after another, with the words whose
// least likely token is below unsure_below. A word is the tokens up to a
// whole character, and in Latin script, the letters and digits that run
// on; so in Japanese it's usually one to a few characters. Spaces and
// punctuation at a word's edges aren't part of it, and a word that's only
// those isn't unsure.
Transcript make_transcript(std::span<const TranscriptToken> tokens, float unsure_below = UNSURE_BELOW);
