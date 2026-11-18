#pragma once

#include <array>
#include <functional>

#include "core/controller_combo.h"
#include "ui/global_hotkey.h"

// The controller shortcuts: a combination of buttons for each
// HotkeyAction, pressed on a game controller while any app has the focus,
// e.g. a fullscreen game. The game still gets the buttons; nothing is
// taken from it.
//
// On Windows it reads Xbox-style controllers through XInput, which only
// says what's held when asked, so it asks on a timeout: 60 times a second
// while one is connected and there's a combination to look for (or
// watch() is on), otherwise every two seconds, to notice one being
// connected. Up to four controllers, each on its own: a combination's
// buttons are held on the same one. Not on Linux yet.
//
// UI thread only, like the widgets.
class ControllerListener {
public:
    // on_press is called for each combination pressed, with what it's
    // for. Throws std::runtime_error if controllers can't be read, e.g.
    // there's no XInput, or on Linux.
    explicit ControllerListener(std::function<void(HotkeyAction)> on_press);
    // Stops asking
    ~ControllerListener();

    // The timeout holds a pointer to it
    ControllerListener(const ControllerListener&) = delete;
    ControllerListener& operator=(const ControllerListener&) = delete;

    // Makes buttons the combination that presses for action, in place of
    // the one before. 0 for none.
    void set_combo(HotkeyAction action, ControllerButtons buttons);

    // Whether a controller is connected, as of the last time it asked
    bool connected() const;

    // Calls on_change with the buttons held, on any controller, now and
    // whenever they or connected() change, until it's called with nullptr.
    // Meanwhile the combinations don't press, e.g. while Settings waits
    // for the buttons of a new one. One at a time: another replaces it.
    void watch(std::function<void(ControllerButtons held)> on_change);

    // Called whenever connected() changes, e.g. to show it. Empty for none.
    std::function<void()> on_connected;

private:
    static constexpr size_t PADS = 4;

    std::function<void(HotkeyAction)> on_press;
    std::array<ControllerButtons, HOTKEY_ACTION_COUNT> combos{};
    std::function<void(ControllerButtons)> watcher;
    // Each controller's, as of the last time it asked
    std::array<bool, PADS> pad_connected{};
    std::array<ControllerButtons, PADS> held{};
    // Fast ticks since the controllers that aren't connected were last
    // asked about, which is slow for them
    int ticks_since_scan = 0;
    // In on_tick(), which schedules the next one when it's done
    bool ticking = false;

    // XInput's DLL and its XInputGetState, so the app still starts
    // without them. Plain pointers so this header doesn't need windows.h.
    void* library = nullptr;
    void (*get_state)() = nullptr;

    // Whether to ask 60 times a second, rather than every two seconds
    bool fast() const;
    ControllerButtons held_on_any() const;
    // Asks now, then on the next timeout for fast() as it is then
    void reschedule();
    // Asks the controllers what's held, and presses what that completes
    void poll();
    // Reads controller pad into buttons. false if it isn't connected.
    bool read(size_t pad, ControllerButtons& buttons) const;

    // Static because FLTK timeouts are plain functions; the listener comes
    // in through data
    static void on_tick(void* data);
};
