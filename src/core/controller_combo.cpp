#include "core/controller_combo.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cctype>

namespace {

struct ButtonName {
    ControllerButtons button;
    const char* name;
};

// In the order format_controller_combo writes them: what's usually held
// first, then what's pressed with it
constexpr std::array<ButtonName, 16> BUTTON_NAMES = {{
    {controller_button::BACK, "Back"},
    {controller_button::START, "Start"},
    {controller_button::LB, "LB"},
    {controller_button::RB, "RB"},
    {controller_button::LT, "LT"},
    {controller_button::RT, "RT"},
    {controller_button::LS, "LS"},
    {controller_button::RS, "RS"},
    {controller_button::UP, "Up"},
    {controller_button::DOWN, "Down"},
    {controller_button::LEFT, "Left"},
    {controller_button::RIGHT, "Right"},
    {controller_button::A, "A"},
    {controller_button::B, "B"},
    {controller_button::X, "X"},
    {controller_button::Y, "Y"},
}};

// Whether a and b are the same, ignoring the case of ASCII letters
bool same(std::string_view a, std::string_view b){
    return std::ranges::equal(a, b, [](char x, char y){
        return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
    });
}

// text without the spaces around it
std::string_view trim(std::string_view text){
    const size_t start = text.find_first_not_of(' ');
    if (start == std::string_view::npos) return {};
    return text.substr(start, text.find_last_not_of(' ') - start + 1);
}

}

std::string format_controller_combo(ControllerButtons buttons){
    std::string text;
    for (const ButtonName& b : BUTTON_NAMES){
        if (!(buttons & b.button)) continue;
        if (!text.empty()) text += '+';
        text += b.name;
    }
    return text;
}

std::optional<ControllerButtons> parse_controller_combo(std::string_view text){
    if (trim(text).empty()) return 0;
    ControllerButtons buttons = 0;
    for (size_t start = 0;;){
        const size_t plus = text.find('+', start);
        const std::string_view part = trim(text.substr(start, plus == std::string_view::npos ? std::string_view::npos
                                                                                             : plus - start));
        const auto found = std::ranges::find_if(BUTTON_NAMES, [part](const ButtonName& b){ return same(part, b.name); });
        if (found == BUTTON_NAMES.end() || (buttons & found->button)) return std::nullopt;
        buttons |= found->button;
        if (plus == std::string_view::npos) break;
        start = plus + 1;
    }
    return buttons;
}

std::vector<size_t> combos_pressed(std::span<const ControllerButtons> combos, ControllerButtons before,
                                   ControllerButtons now){
    const ControllerButtons pressed = now & ~before;
    std::vector<size_t> out;
    if (!pressed) return out;
    int most = 0;
    for (size_t i = 0; i < combos.size(); ++i){
        const ControllerButtons combo = combos[i];
        if (!combo || (combo & now) != combo || !(combo & pressed)) continue;
        const int count = std::popcount(combo);
        if (count < most) continue;
        if (count > most) out.clear();
        most = count;
        out.push_back(i);
    }
    return out;
}
