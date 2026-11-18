#include "ui/controller_listener.h"

#include <algorithm>
#include <exception>
#include <format>
#include <iostream>
#include <stdexcept>
#include <utility>
#include <vector>

// FLTK
#include <FL/Fl.H>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
// Already defined by MinGW's C++ library, so std::min isn't broken
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <xinput.h>
#endif

// Seconds between asking, while one is connected and there's something to
// look for: a press shorter than this could be missed
static constexpr double FAST_TICK = 1.0 / 60;
// Otherwise. Also how often controllers that aren't connected are asked
// about while one is: XInput takes a while to say one isn't, up to
// milliseconds each.
static constexpr double SLOW_TICK = 2;
static constexpr int SCAN_TICKS = static_cast<int>(SLOW_TICK / FAST_TICK);

#ifdef _WIN32

// controller_button has XInput's bits, so wButtons is used as it is
static_assert(controller_button::UP == XINPUT_GAMEPAD_DPAD_UP && controller_button::DOWN == XINPUT_GAMEPAD_DPAD_DOWN
              && controller_button::LEFT == XINPUT_GAMEPAD_DPAD_LEFT
              && controller_button::RIGHT == XINPUT_GAMEPAD_DPAD_RIGHT
              && controller_button::START == XINPUT_GAMEPAD_START && controller_button::BACK == XINPUT_GAMEPAD_BACK
              && controller_button::LS == XINPUT_GAMEPAD_LEFT_THUMB
              && controller_button::RS == XINPUT_GAMEPAD_RIGHT_THUMB
              && controller_button::LB == XINPUT_GAMEPAD_LEFT_SHOULDER
              && controller_button::RB == XINPUT_GAMEPAD_RIGHT_SHOULDER && controller_button::A == XINPUT_GAMEPAD_A
              && controller_button::B == XINPUT_GAMEPAD_B && controller_button::X == XINPUT_GAMEPAD_X
              && controller_button::Y == XINPUT_GAMEPAD_Y);

using GetState = DWORD(WINAPI*)(DWORD, XINPUT_STATE*);

ControllerListener::ControllerListener(std::function<void(HotkeyAction)> on_press) : on_press(std::move(on_press)){
    static_assert(PADS == XUSER_MAX_COUNT);
    // 1.4 comes with Windows 8 and later; 9.1.0 with Vista and 7. Only
    // from System32, so a DLL of the same name next to a video can't be
    // loaded in its place.
    for (const wchar_t* name : {L"xinput1_4.dll", L"xinput9_1_0.dll", L"xinput1_3.dll"}){
        library = LoadLibraryExW(name, nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (library) break;
    }
    if (!library) throw std::runtime_error("Controllers can't be read: Windows has no XInput");
    get_state = reinterpret_cast<void (*)()>(GetProcAddress(static_cast<HMODULE>(library), "XInputGetState"));
    if (!get_state){
        FreeLibrary(static_cast<HMODULE>(library));
        throw std::runtime_error("Controllers can't be read: XInput has no XInputGetState");
    }
    Fl::add_timeout(0, on_tick, this);
}

ControllerListener::~ControllerListener(){
    Fl::remove_timeout(on_tick, this);
    FreeLibrary(static_cast<HMODULE>(library));
}

bool ControllerListener::read(size_t pad, ControllerButtons& buttons) const {
    XINPUT_STATE state{};
    if (reinterpret_cast<GetState>(get_state)(static_cast<DWORD>(pad), &state) != ERROR_SUCCESS) return false;
    const XINPUT_GAMEPAD& g = state.Gamepad;
    // Guide isn't in it: XInputGetState leaves it out, and the Game Bar or
    // Steam has it
    buttons = g.wButtons & controller_button::ALL;
    if (g.bLeftTrigger > XINPUT_GAMEPAD_TRIGGER_THRESHOLD) buttons |= controller_button::LT;
    if (g.bRightTrigger > XINPUT_GAMEPAD_TRIGGER_THRESHOLD) buttons |= controller_button::RT;
    return true;
}

#else

ControllerListener::ControllerListener(std::function<void(HotkeyAction)> on_press) : on_press(std::move(on_press)){
    throw std::runtime_error("Controllers aren't read on Linux yet");
}

ControllerListener::~ControllerListener(){
    Fl::remove_timeout(on_tick, this);
}

bool ControllerListener::read(size_t, ControllerButtons&) const {
    return false;
}

#endif

void ControllerListener::set_combo(HotkeyAction action, ControllerButtons buttons){
    const bool was_fast = fast();
    combos[static_cast<size_t>(action)] = buttons;
    // E.g. the first one: from now on a press has to be caught
    if (fast() != was_fast) reschedule();
}

bool ControllerListener::connected() const {
    return std::ranges::any_of(pad_connected, [](bool c){ return c; });
}

ControllerButtons ControllerListener::held_on_any() const {
    ControllerButtons out = 0;
    for (size_t pad = 0; pad < PADS; ++pad)
        if (pad_connected[pad]) out |= held[pad];
    return out;
}

void ControllerListener::watch(std::function<void(ControllerButtons)> on_change){
    watcher = std::move(on_change);
    if (!watcher) return;
    // What's known now, then what's held now as soon as it's asked
    std::function<void(ControllerButtons)> call = watcher;
    call(held_on_any());
    reschedule();
}

bool ControllerListener::fast() const {
    if (!connected()) return false;
    return watcher || std::ranges::any_of(combos, [](ControllerButtons c){ return c != 0; });
}

void ControllerListener::reschedule(){
    // on_tick() schedules the next one itself, as fast() is when it's done
    if (ticking) return;
    Fl::remove_timeout(on_tick, this);
    Fl::add_timeout(0, on_tick, this);
}

void ControllerListener::on_tick(void* data){
    ControllerListener* self = static_cast<ControllerListener*>(data);
    self->ticking = true;
    self->poll();
    self->ticking = false;
    // Not repeat_timeout(): reschedule() may have added one meanwhile, from
    // a callback, and there's to be only one
    Fl::remove_timeout(on_tick, self);
    Fl::add_timeout(self->fast() ? FAST_TICK : SLOW_TICK, on_tick, self);
}

void ControllerListener::poll(){
    const bool scan = !fast() || ++ticks_since_scan >= SCAN_TICKS;
    if (scan) ticks_since_scan = 0;

    const bool was_connected = connected();
    const ControllerButtons was_held = held_on_any();
    std::vector<HotkeyAction> pressed;
    for (size_t pad = 0; pad < PADS; ++pad){
        if (!pad_connected[pad] && !scan) continue;
        ControllerButtons now = 0;
        if (!read(pad, now)){
            pad_connected[pad] = false;
            held[pad] = 0;
            continue;
        }
        // Just connected: what it holds already wasn't pressed now
        const ControllerButtons before = pad_connected[pad] ? held[pad] : now;
        pad_connected[pad] = true;
        held[pad] = now;
        if (watcher) continue;
        for (size_t i : combos_pressed(combos, before, now))
            // Once, also when two controllers press it at the same time
            if (std::ranges::find(pressed, HOTKEY_ACTIONS[i]) == pressed.end()) pressed.push_back(HOTKEY_ACTIONS[i]);
    }

    // Copies: what they call may replace them, e.g. a box that has its
    // buttons stops watching
    if (watcher && (connected() != was_connected || held_on_any() != was_held)){
        std::function<void(ControllerButtons)> call = watcher;
        call(held_on_any());
    }
    if (connected() != was_connected && on_connected){
        std::function<void()> call = on_connected;
        call();
    }
    for (HotkeyAction action : pressed){
        // An exception can't unwind through FLTK's event loop
        try {
            on_press(action);
        } catch (const std::exception& e){
            std::cerr << std::format("Couldn't capture: {}\n", e.what());
        }
    }
}
