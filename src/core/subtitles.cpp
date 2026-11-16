#include "core/subtitles.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <format>
#include <fstream>
#include <iterator>
#include <stdexcept>

// ---- Text ----

// The code point that starts at text[at], and how many bytes it takes.
// A byte that doesn't start valid UTF-8 counts as a code point of its own.
// Like lookup_text.cpp's.
static char32_t decode(std::string_view text, size_t at, size_t& length){
    const auto byte = [&](size_t i){ return static_cast<std::uint8_t>(text[at + i]); };
    const std::uint8_t first = byte(0);
    length = first < 0x80 ? 1 : (first >> 5) == 0x6 ? 2 : (first >> 4) == 0xE ? 3 : (first >> 3) == 0x1E ? 4 : 0;
    if (length == 0 || at + length > text.size()){
        length = 1;
        return first;
    }
    char32_t c = length == 1 ? first : first & (0x7F >> length);
    for (size_t i = 1; i < length; ++i){
        if ((byte(i) >> 6) != 0x2){
            length = 1;
            return first;
        }
        c = (c << 6) | (byte(i) & 0x3F);
    }
    return c;
}

static void append_utf8(std::string& out, char32_t c){
    if (c < 0x80) out += static_cast<char>(c);
    else if (c < 0x800){
        out += static_cast<char>(0xC0 | (c >> 6));
        out += static_cast<char>(0x80 | (c & 0x3F));
    } else if (c < 0x10000){
        out += static_cast<char>(0xE0 | (c >> 12));
        out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (c & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (c >> 18));
        out += static_cast<char>(0x80 | ((c >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (c & 0x3F));
    }
}

// UTF-16 after a BOM, little or big endian, as UTF-8
static std::string from_utf16(std::string_view bytes, bool little){
    std::string out;
    out.reserve(bytes.size());
    const auto unit = [&](size_t i) -> char32_t {
        const auto a = static_cast<std::uint8_t>(bytes[i]), b = static_cast<std::uint8_t>(bytes[i + 1]);
        return little ? (b << 8 | a) : (a << 8 | b);
    };
    for (size_t i = 0; i + 1 < bytes.size(); i += 2){
        char32_t c = unit(i);
        if (c >= 0xD800 && c < 0xDC00 && i + 3 < bytes.size()){
            const char32_t low = unit(i + 2);
            if (low >= 0xDC00 && low < 0xE000){
                c = 0x10000 + ((c - 0xD800) << 10) + (low - 0xDC00);
                i += 2;
            }
        }
        append_utf8(out, c);
    }
    return out;
}

// The file's text as UTF-8, without a BOM
static std::string as_utf8(std::string_view file){
    if (file.starts_with("\xEF\xBB\xBF")) return std::string(file.substr(3));
    if (file.starts_with("\xFF\xFE")) return from_utf16(file.substr(2), true);
    if (file.starts_with("\xFE\xFF")) return from_utf16(file.substr(2), false);
    return std::string(file);
}

// Its lines, without the "\r" of a "\r\n"
static std::vector<std::string_view> split_lines(std::string_view text){
    std::vector<std::string_view> lines;
    while (!text.empty()){
        const size_t nl = text.find('\n');
        std::string_view line = text.substr(0, nl);
        if (line.ends_with('\r')) line.remove_suffix(1);
        lines.push_back(line);
        if (nl == std::string_view::npos) break;
        text.remove_prefix(nl + 1);
    }
    return lines;
}

static std::string_view trim(std::string_view s){
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.remove_suffix(1);
    return s;
}

// A time like "01:02:03,456" (SRT), "1:02:03.45" (ASS) or "02:03.4", in
// seconds
static std::optional<double> parse_time(std::string_view s){
    s = trim(s);
    double seconds = 0;
    int parts = 0;
    while (!s.empty()){
        const size_t colon = s.find(':');
        std::string_view part = s.substr(0, colon);
        // The seconds can have a fraction, after a comma or a dot
        std::string text(part);
        std::ranges::replace(text, ',', '.');
        double value = 0;
        const auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
        if (ec != std::errc() || ptr != text.data() + text.size() || text.empty()) return std::nullopt;
        seconds = seconds * 60 + value;
        ++parts;
        if (colon == std::string_view::npos) break;
        s.remove_prefix(colon + 1);
    }
    if (parts < 2 || parts > 3) return std::nullopt;
    return seconds;
}

// Takes out <...> tags (SRT's <i>, <font ...>) and {...} blocks (ASS's
// override tags, which some SRT files have too). ASS's \N and \n become
// line breaks and \h a space. Sets drawing if a block turns on ASS's
// drawing mode, \p1 and up, whose "text" is shapes.
static std::string clean_text(std::string_view text, bool ass, bool& drawing){
    std::string out;
    drawing = false;
    for (size_t i = 0; i < text.size(); ++i){
        const char c = text[i];
        if (c == '{'){
            const size_t close = text.find('}', i);
            if (close == std::string_view::npos) break;
            const std::string_view block = text.substr(i, close - i);
            for (size_t p = block.find("\\p"); p != std::string_view::npos; p = block.find("\\p", p + 2))
                if (p + 2 < block.size() && block[p + 2] >= '1' && block[p + 2] <= '9') drawing = true;
            i = close;
        } else if (c == '<' && !ass){
            const size_t close = text.find('>', i);
            if (close == std::string_view::npos){
                out += c;
                continue;
            }
            i = close;
        } else if (c == '\\' && i + 1 < text.size() && (text[i + 1] == 'N' || text[i + 1] == 'n')){
            out += '\n';
            ++i;
        } else if (c == '\\' && i + 1 < text.size() && text[i + 1] == 'h'){
            out += ' ';
            ++i;
        } else out += c;
    }
    // No empty lines, and no spaces around them
    std::string joined;
    for (std::string_view line : split_lines(out)){
        line = trim(line);
        if (line.empty()) continue;
        if (!joined.empty()) joined += '\n';
        joined += line;
    }
    return joined;
}

// ---- SRT ----

// Each cue is a number, "start --> end", then its text up to an empty
// line. The number isn't needed: the line with "-->" is what starts one.
static std::vector<SubtitleLine> parse_srt(std::string_view text){
    std::vector<SubtitleLine> lines;
    const std::vector<std::string_view> rows = split_lines(text);
    for (size_t i = 0; i < rows.size(); ++i){
        const size_t arrow = rows[i].find("-->");
        if (arrow == std::string_view::npos) continue;
        // After the end there can be positions, e.g. "X1:..."
        std::string_view after = trim(rows[i].substr(arrow + 3));
        after = after.substr(0, after.find(' '));
        const auto start = parse_time(rows[i].substr(0, arrow));
        const auto end = parse_time(after);
        if (!start || !end) continue;

        std::string body;
        size_t j = i + 1;
        for (; j < rows.size() && !trim(rows[j]).empty(); ++j){
            // A cue with no empty line after it, before the next one
            if (rows[j].find("-->") != std::string_view::npos && j + 1 < rows.size()) break;
            if (!body.empty()) body += '\n';
            body += rows[j];
        }
        i = j - 1;
        bool drawing = false;
        std::string clean = clean_text(body, false, drawing);
        if (!clean.empty()) lines.push_back({*start, *end, std::move(clean)});
    }
    return lines;
}

// ---- ASS ----

// The lines under [Events]: a "Format:" line names the fields, and each
// "Dialogue:" line has them in that order, comma separated. Text is the
// last field, and can have commas of its own.
static std::vector<SubtitleLine> parse_ass(std::string_view text){
    std::vector<SubtitleLine> lines;
    bool in_events = false;
    // Where Start, End and Text are among the fields. The usual order,
    // until a Format line says otherwise.
    size_t start_field = 1, end_field = 2, text_field = 9, field_count = 10;
    for (std::string_view row : split_lines(text)){
        row = trim(row);
        if (row.starts_with('[')){
            in_events = row == "[Events]";
            continue;
        }
        if (!in_events) continue;

        if (row.starts_with("Format:")){
            std::vector<std::string_view> names;
            std::string_view rest = row.substr(7);
            while (true){
                const size_t comma = rest.find(',');
                names.push_back(trim(rest.substr(0, comma)));
                if (comma == std::string_view::npos) break;
                rest.remove_prefix(comma + 1);
            }
            field_count = names.size();
            for (size_t i = 0; i < names.size(); ++i){
                if (names[i] == "Start") start_field = i;
                else if (names[i] == "End") end_field = i;
                else if (names[i] == "Text") text_field = i;
            }
            continue;
        }
        if (!row.starts_with("Dialogue:")) continue;

        std::string_view rest = row.substr(9);
        std::vector<std::string_view> fields;
        // Text, the last field, takes the rest of the line, commas and all
        while (fields.size() + 1 < field_count){
            const size_t comma = rest.find(',');
            if (comma == std::string_view::npos) break;
            fields.push_back(rest.substr(0, comma));
            rest.remove_prefix(comma + 1);
        }
        fields.push_back(rest);
        if (fields.size() != field_count || text_field >= fields.size()) continue;
        const auto start = parse_time(fields[start_field]);
        const auto end = parse_time(fields[end_field]);
        if (!start || !end) continue;
        bool drawing = false;
        std::string clean = clean_text(fields[text_field], true, drawing);
        if (drawing || clean.empty()) continue;
        lines.push_back({*start, *end, std::move(clean)});
    }
    return lines;
}

// ---- Files ----

std::vector<SubtitleLine> parse_subtitles(std::string_view file){
    const std::string text = as_utf8(file);
    const bool ass = text.find("[Events]") != std::string::npos || text.find("[Script Info]") != std::string::npos;
    std::vector<SubtitleLine> lines = ass ? parse_ass(text) : parse_srt(text);
    if (lines.empty())
        throw std::runtime_error(ass ? "There are no dialogue lines in it." : "There are no subtitles in it. Is it an .srt or .ass file?");

    std::ranges::stable_sort(lines, {}, &SubtitleLine::start);
    // ASS files often draw a line more than once, e.g. its border and its
    // text in two layers
    const auto same = [](const SubtitleLine& a, const SubtitleLine& b){
        return a.start == b.start && a.end == b.end && a.text == b.text;
    };
    lines.erase(std::unique(lines.begin(), lines.end(), same), lines.end());
    return lines;
}

std::vector<SubtitleLine> load_subtitles(const std::filesystem::path& file){
    std::ifstream in(file, std::ios::binary);
    if (!in) throw std::runtime_error("Couldn't open the file.");
    const std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (in.bad()) throw std::runtime_error("Couldn't read the file.");
    return parse_subtitles(bytes);
}

// ---- Matching ----

// Spaces, punctuation and symbols, Japanese and Western: they're left out
// when comparing, since whisper and subtitles punctuate differently
static bool is_filler(char32_t c){
    if (c < 0x80) return !((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'));
    // 々, 〆 and 〇 are in the CJK punctuation block, but are parts of words
    if (c >= 0x3005 && c <= 0x3007) return false;
    return (c >= 0x3000 && c <= 0x303F)     // 、。「」『』〜 and the wide space
        || c == 0x30FB                      // ・
        || (c >= 0x2000 && c <= 0x206F)     // …‥ “” — and other spaces
        || (c >= 0x2190 && c <= 0x2BFF)     // Arrows, shapes, ♪ and other symbols
        || (c >= 0xFF01 && c <= 0xFF0F)     // ！＂＃ ... ／
        || (c >= 0xFF1A && c <= 0xFF20)     // ：；＜ ... ＠
        || (c >= 0xFF3B && c <= 0xFF40)     // ［＼］ ... ｀
        || (c >= 0xFF5B && c <= 0xFF65)     // ｛｜｝～ and half-width 。「」、・
        || c == 0x00A0;                     // No-break space
}

std::u32string normalized_for_match(std::string_view text){
    std::u32string out;
    for (size_t at = 0, length = 0; at < text.size(); at += length){
        char32_t c = decode(text, at, length);
        if (is_filler(c)) continue;
        // Full-width letters and digits
        if (c >= 0xFF10 && c <= 0xFF5A) c -= 0xFEE0;
        if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
        // Katakana as hiragana, ァ to ヶ
        if (c >= 0x30A1 && c <= 0x30F6) c -= 0x60;
        out += c;
    }
    return out;
}

// How many code points have to be changed, added or taken out to turn a
// into b
static size_t edit_distance(const std::u32string& a, const std::u32string& b){
    std::vector<size_t> row(b.size() + 1);
    for (size_t j = 0; j <= b.size(); ++j) row[j] = j;
    for (size_t i = 1; i <= a.size(); ++i){
        size_t diagonal = row[0];
        row[0] = i;
        for (size_t j = 1; j <= b.size(); ++j){
            const size_t above = row[j];
            row[j] = std::min({row[j] + 1, row[j - 1] + 1, diagonal + (a[i - 1] == b[j - 1] ? 0 : 1)});
            diagonal = above;
        }
    }
    return row[b.size()];
}

// Lines that score this close to the best one count as as good, for near
static constexpr double SAME_SCORE = 0.05;

std::optional<SubtitleMatch> match_subtitles(const std::vector<SubtitleLine>& lines, std::string_view transcript,
                                             std::optional<double> near, double min_score, size_t max_lines){
    const std::u32string wanted = normalized_for_match(transcript);
    if (wanted.empty() || lines.empty()) return std::nullopt;

    std::vector<std::u32string> normalized;
    normalized.reserve(lines.size());
    for (const SubtitleLine& line : lines) normalized.push_back(normalized_for_match(line.text));

    struct Candidate { size_t begin, end; double score; };
    std::vector<Candidate> candidates;
    double best = 0;
    for (size_t begin = 0; begin < lines.size(); ++begin){
        std::u32string joined;
        for (size_t end = begin + 1; end <= std::min(lines.size(), begin + max_lines); ++end){
            joined += normalized[end - 1];
            if (joined.empty()) continue;
            // Far longer or shorter can't score well: skips most of the work
            const size_t longer = std::max(joined.size(), wanted.size());
            const size_t shorter = std::min(joined.size(), wanted.size());
            if (static_cast<double>(shorter) / longer < min_score) {
                if (joined.size() > wanted.size()) break;
                continue;
            }
            const double score = 1.0 - static_cast<double>(edit_distance(joined, wanted)) / longer;
            if (score < min_score || score < best - SAME_SCORE) continue;
            best = std::max(best, score);
            candidates.push_back({begin, end, score});
        }
    }
    std::erase_if(candidates, [&](const Candidate& c){ return c.score < best - SAME_SCORE; });
    if (candidates.empty()) return std::nullopt;

    // The best scoring one, or with a time to go by, the closest of the
    // ones about as good. Fewer lines first when they're the same.
    const auto distance = [&](const Candidate& c){ return std::fabs(lines[c.begin].start - *near); };
    const Candidate* pick = &candidates.front();
    for (const Candidate& c : candidates){
        if (near){
            if (distance(c) < distance(*pick) - 0.5 || (std::fabs(distance(c) - distance(*pick)) <= 0.5 && c.score > pick->score))
                pick = &c;
        } else if (c.score > pick->score) pick = &c;
    }

    SubtitleMatch match{.begin = pick->begin, .end = pick->end, .score = pick->score,
                        .text = "", .start = lines[pick->begin].start, .stop = lines[pick->end - 1].end};
    for (size_t i = pick->begin; i < pick->end; ++i){
        // Lines broken for the screen are one sentence
        for (char c : lines[i].text) if (c != '\n') match.text += c;
    }
    return match;
}
