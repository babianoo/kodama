#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// Buttons held together on a game controller, for the shortcuts that work
// from one. In the settings it's written with the buttons' names, e.g.
// "Back+LB", the way format_controller_combo writes it; "" for none.
//
// One bit per button, so a combination is the bits of its buttons together.
// They're the bits XInput (Windows) gives an Xbox-style controller's
// buttons, so its state needs no converting. The triggers are analog
// there; they count as pressed past a threshold, and get bits it doesn't
// use.
using ControllerButtons = std::uint32_t;

namespace controller_button {
inline constexpr ControllerButtons UP = 0x0001;
inline constexpr ControllerButtons DOWN = 0x0002;
inline constexpr ControllerButtons LEFT = 0x0004;
inline constexpr ControllerButtons RIGHT = 0x0008;
inline constexpr ControllerButtons START = 0x0010;
inline constexpr ControllerButtons BACK = 0x0020;
// The sticks, pressed in
inline constexpr ControllerButtons LS = 0x0040;
inline constexpr ControllerButtons RS = 0x0080;
inline constexpr ControllerButtons LB = 0x0100;
inline constexpr ControllerButtons RB = 0x0200;
inline constexpr ControllerButtons A = 0x1000;
inline constexpr ControllerButtons B = 0x2000;
inline constexpr ControllerButtons X = 0x4000;
inline constexpr ControllerButtons Y = 0x8000;
inline constexpr ControllerButtons LT = 0x10000;
inline constexpr ControllerButtons RT = 0x20000;
// Every one above
inline constexpr ControllerButtons ALL = 0x3F3FF;
}

// buttons written the way parse_controller_combo reads them, in the same
// order whichever was pressed first: Back and Start, the shoulders and
// triggers, the sticks, the D-pad, then the face buttons, e.g.
// "Back+LB+A". "" for none.
std::string format_controller_combo(ControllerButtons buttons);

// Reads text like "Back+LB+A", whatever the case and order, with any spaces
// around the names. "" is none (0). Empty if a name isn't a button's or is
// there twice.
std::optional<ControllerButtons> parse_controller_combo(std::string_view text);

// Which of combos the buttons just pressed complete: those held now
// (in now) that weren't all held before, i.e. one of their buttons is new.
// If several are, only the ones with the most buttons, so Back+A doesn't
// also do what A alone does. Indexes into combos; a combo of 0 is none
// and never pressed.
std::vector<size_t> combos_pressed(std::span<const ControllerButtons> combos, ControllerButtons before,
                                   ControllerButtons now);
