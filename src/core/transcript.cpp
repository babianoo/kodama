#include "core/transcript.h"

#include <algorithm>
#include <string_view>

// Bytes in a UTF-8 character that starts with lead. 1 for a byte that
// can't start one, so a broken byte is a character of its own.
static size_t sequence_length(unsigned char lead){
    if (lead >= 0xC0 && lead < 0xE0) return 2;
    if (lead >= 0xE0 && lead < 0xF0) return 3;
    if (lead >= 0xF0 && lead < 0xF8) return 4;
    return 1;
}

static bool is_continuation(char byte){
    return (static_cast<unsigned char>(byte) & 0xC0) == 0x80;
}

// Whether s ends with a whole character, not partway through one
static bool ends_whole(std::string_view s){
    // The last character's first byte is at most 3 back
    size_t lead = s.size();
    for (int back = 0; back < 4 && lead > 0; ++back){
        --lead;
        if (!is_continuation(s[lead])) return s.size() - lead >= sequence_length(static_cast<unsigned char>(s[lead]));
    }
    // Empty, or only continuation bytes: broken, and more won't mend it
    return true;
}

// The character starting at s[i], and in length how many bytes it takes.
// A broken one is its first byte, as it is.
static char32_t decode(std::string_view s, size_t i, size_t& length){
    const auto lead = static_cast<unsigned char>(s[i]);
    length = sequence_length(lead);
    if (length == 1 || i + length > s.size()){
        length = 1;
        return lead;
    }
    char32_t c = lead & (0x7F >> length);
    for (size_t k = 1; k < length; ++k){
        if (!is_continuation(s[i + k])){
            length = 1;
            return lead;
        }
        c = (c << 6) | (static_cast<unsigned char>(s[i + k]) & 0x3F);
    }
    return c;
}

// ASCII letters and digits, which run on from token to token into one
// word, e.g. " wor" and "ld"
static bool is_latin(char c){
    return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

// Spaces, punctuation and symbols, which aren't what's unsure about a word
static bool is_filler(char32_t c){
    if (c < 0x80) return !is_latin(static_cast<char>(c));
    return (c >= 0x00A0 && c <= 0x00BF)      // No-break space, « » ¿...
        || (c >= 0x2000 && c <= 0x206F)      // Spaces, dashes, quotes, … ‥ ※...
        || (c >= 0x2600 && c <= 0x27BF)      // Symbols, e.g. ♪
        || (c >= 0x3000 && c <= 0x3004)      // Ideographic space 、 。 〃 〄
        || (c >= 0x3008 && c <= 0x3020)      // Brackets 「」『』【】..., 〜
        || c == 0x30FB                       // ・
        || (c >= 0xFF01 && c <= 0xFF0F)      // Full-width ！ （ ）...
        || (c >= 0xFF1A && c <= 0xFF20)      // ： ？...
        || (c >= 0xFF3B && c <= 0xFF40)
        || (c >= 0xFF5B && c <= 0xFF65);     // ～, and half-width 。「」、・
}

Transcript make_transcript(std::span<const TranscriptToken> tokens, float unsure_below){
    // Words cover all of text, one after another
    struct Word {
        size_t begin, end;
        float p;
    };
    std::string text;
    std::vector<Word> words;
    // Where the tokens not yet in a word start: so far they end partway
    // through a character. And the least likely of them.
    size_t pending = 0;
    float pending_p = 1;
    for (const TranscriptToken& token : tokens){
        if (token.text.empty()) continue;
        text += token.text;
        pending_p = std::min(pending_p, token.p);
        if (!ends_whole(text)) continue;

        if (!words.empty() && is_latin(text[pending - 1]) && is_latin(text[pending])){
            words.back().end = text.size();
            words.back().p = std::min(words.back().p, pending_p);
        } else words.push_back({pending, text.size(), pending_p});
        pending = text.size();
        pending_p = 1;
    }
    // Cut off partway through a character
    if (pending < text.size()) words.push_back({pending, text.size(), pending_p});

    // Trimmed, since segments start with a space
    static constexpr const char* SPACE = " \t\r\n";
    const size_t first = text.find_first_not_of(SPACE);
    if (first == std::string::npos) return {};
    const size_t last = text.find_last_not_of(SPACE) + 1;

    Transcript transcript{.text = text.substr(first, last - first), .unsure = {}};
    for (const Word& word : words){
        if (!(word.p < unsure_below)) continue;
        // Without the spaces and punctuation at its edges. Those take in
        // any space, so it's all inside the trimmed text.
        const std::string_view within = std::string_view(text).substr(0, word.end);
        size_t begin = word.end, end = word.end;
        for (size_t i = word.begin, length = 1; i < word.end; i += length){
            if (is_filler(decode(within, i, length))) continue;
            if (begin == word.end) begin = i;
            end = i + length;
        }
        // Only punctuation, e.g. a 。 it wasn't sure where to put
        if (begin == word.end) continue;
        transcript.unsure.push_back({begin - first, end - first, word.p});
    }
    return transcript;
}
