#pragma once

#include <functional>
#include <string>

// FLTK
#include <FL/Fl_Input.H>

#include "core/controller_combo.h"

class ControllerListener;

// A box that shows a controller shortcut, buttons held together like
// Back+A, or None, and takes a new one from the buttons pressed after it's
// clicked: hold them, then let go. Like ShortcutInput: from the keyboard,
// Tab to it and press Enter or Space first, and Escape gives up and keeps
// the one before. Delete or Backspace while it waits makes it None.
//
// The callback runs once a new one is taken; combo() has it then.
class ControllerInput : public Fl_Input {
public:
    // listener reads the buttons, and has to outlive the box. Null if
    // controllers can't be read; the box then only shows its combination.
    ControllerInput(int x, int y, int w, int h, ControllerListener* listener);
    // Stops listener calling it
    ~ControllerInput() override;

    int handle(int event) override;

    // 0 for none
    ControllerButtons combo() const { return current; }
    void combo(ControllerButtons combo);
    // Gives up waiting for buttons, as Escape does. For when it's hidden:
    // it keeps the focus then, so it doesn't hear about it.
    void stop_waiting(){ set_waiting(false); }

    // Why buttons can't be the combination, e.g. another shortcut has them
    // already, which the box then says. Empty if they can.
    std::function<std::string(ControllerButtons buttons)> problem;

private:
    ControllerListener* listener;
    ControllerButtons current = 0;
    bool waiting = false;
    // While waiting: every button pressed since the first one was, until
    // they're all let go
    ControllerButtons pressed = 0;
    // Held already when it started waiting, which don't count until
    // they're let go
    ControllerButtons ignored = 0;

    void set_waiting(bool waiting);
    // From listener, while waiting
    void on_held(ControllerButtons held);
    // Shows text, dimmed or not
    void show(const std::string& text, bool dim);
    void show_current();
};
