// Tests for reading and writing the controller shortcuts, and for which of
// them a press sets off. No framework: each failed check prints where it
// was, and the exit code is how many failed.
// Run with: ctest --test-dir build

#include <array>
#include <cstdio>
#include <optional>
#include <string>
#include <vector>

#include "core/controller_combo.h"

namespace cb = controller_button;

static int failures = 0;

#define CHECK(condition) do { \
    if (!(condition)){ \
        std::fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #condition); \
        ++failures; \
    } \
} while (0)

// text read and written again, or "-" if it can't be read
static std::string round_trip(const char* text){
    const std::optional<ControllerButtons> buttons = parse_controller_combo(text);
    return buttons ? format_controller_combo(*buttons) : "-";
}

static void test_reads(){
    CHECK(parse_controller_combo("Back+A") == (cb::BACK | cb::A));
    CHECK(parse_controller_combo("LT") == cb::LT);
    // None
    CHECK(parse_controller_combo("") == 0u);
    CHECK(parse_controller_combo("  ") == 0u);
    CHECK(format_controller_combo(0).empty());
}

static void test_case_spaces_and_order(){
    CHECK(round_trip("a + back") == "Back+A");
    CHECK(round_trip(" y+rb+lb ") == "LB+RB+Y");
    CHECK(round_trip("Right+Start+rs") == "Start+RS+Right");
    CHECK(format_controller_combo(cb::ALL) == "Back+Start+LB+RB+LT+RT+LS+RS+Up+Down+Left+Right+A+B+X+Y");
    CHECK(parse_controller_combo(format_controller_combo(cb::ALL)) == cb::ALL);
}

static void test_rejects(){
    CHECK(round_trip("A+A") == "-");
    CHECK(round_trip("Back+") == "-");
    CHECK(round_trip("+A") == "-");
    CHECK(round_trip("Guide") == "-");
    CHECK(round_trip("Back A") == "-");
    CHECK(round_trip("Ctrl+A") == "-");
}

static void test_pressed(){
    // Capture, read screen, stash
    const std::array<ControllerButtons, 3> combos = {cb::BACK | cb::A, 0, cb::BACK | cb::B};
    CHECK(combos_pressed(combos, 0, cb::BACK).empty());
    CHECK(combos_pressed(combos, cb::BACK, cb::BACK | cb::A) == std::vector<size_t>{0});
    // Both at once
    CHECK(combos_pressed(combos, 0, cb::BACK | cb::A) == std::vector<size_t>{0});
    // Held: not again, also when another button is pressed with it
    CHECK(combos_pressed(combos, cb::BACK | cb::A, cb::BACK | cb::A).empty());
    CHECK(combos_pressed(combos, cb::BACK | cb::A, cb::BACK | cb::A | cb::X).empty());
    // Another held, like the trigger in a racing game, doesn't stop it
    CHECK(combos_pressed(combos, cb::RT | cb::BACK, cb::RT | cb::BACK | cb::B) == std::vector<size_t>{2});
    // Released and pressed again
    CHECK(combos_pressed(combos, cb::BACK, cb::BACK | cb::B) == std::vector<size_t>{2});
    // None is never pressed
    CHECK(combos_pressed(combos, 0, cb::ALL).size() == 2);
}

static void test_most_buttons_win(){
    const std::array<ControllerButtons, 3> combos = {cb::A, cb::BACK | cb::A, cb::LB | cb::RB | cb::A};
    CHECK(combos_pressed(combos, 0, cb::A) == std::vector<size_t>{0});
    CHECK(combos_pressed(combos, cb::BACK, cb::BACK | cb::A) == std::vector<size_t>{1});
    CHECK(combos_pressed(combos, cb::LB | cb::RB, cb::LB | cb::RB | cb::A) == std::vector<size_t>{2});
    // A held, then Back: only the one Back completes
    CHECK(combos_pressed(combos, cb::A, cb::A | cb::BACK) == std::vector<size_t>{1});
    // As many buttons each: both
    const std::array<ControllerButtons, 2> two = {cb::BACK | cb::A, cb::LB | cb::B};
    CHECK(combos_pressed(two, 0, cb::BACK | cb::A | cb::LB | cb::B) == (std::vector<size_t>{0, 1}));
}

int main(){
    test_reads();
    test_case_spaces_and_order();
    test_rejects();
    test_pressed();
    test_most_buttons_win();
    if (failures == 0) std::puts("All controller combo tests passed");
    return failures;
}
