#include "ui/controller_input.h"

#include <format>
#include <utility>

// FLTK
#include <FL/Fl.H>

#include "ui/controller_listener.h"
#include "ui/theme.h"

ControllerInput::ControllerInput(int x, int y, int w, int h, ControllerListener* listener)
    : Fl_Input(x, y, w, h), listener(listener){
    // Typed into only through handle()
    readonly(1);
    show_current();
}

ControllerInput::~ControllerInput(){
    if (waiting && listener) listener->watch(nullptr);
}

void ControllerInput::combo(ControllerButtons combo){
    current = combo;
    if (!waiting) show_current();
}

void ControllerInput::show(const std::string& text, bool dim){
    value(text.c_str());
    textcolor(dim ? DIM : FL_FOREGROUND_COLOR);
    redraw();
}

void ControllerInput::show_current(){
    if (current) show(format_controller_combo(current), false);
    else show("None", true);
}

void ControllerInput::set_waiting(bool now_waiting){
    if (!listener) now_waiting = false;
    if (now_waiting == waiting) return;
    waiting = now_waiting;
    if (waiting){
        pressed = 0;
        // Narrowed to what's held now by the first on_held(), which watch()
        // makes right away
        ignored = controller_button::ALL;
        listener->watch([this](ControllerButtons held){ on_held(held); });
    } else {
        listener->watch(nullptr);
        show_current();
    }
}

void ControllerInput::on_held(ControllerButtons held){
    // Let go, they count again
    ignored &= held;
    held &= ~ignored;
    if (held){
        pressed |= held;
        show(format_controller_combo(pressed), true);
        return;
    }
    if (!pressed){
        // Nothing pressed yet, or a controller came or went
        show(listener->connected() ? "Press the buttons, or Esc" : "No controller connected", true);
        return;
    }

    // All let go: those are the combination
    const ControllerButtons combo = std::exchange(pressed, 0);
    if (problem)
        if (const std::string why = problem(combo); !why.empty()){
            show(std::format("{}: {}", format_controller_combo(combo), why), true);
            return;
        }
    current = combo;
    set_waiting(false);
    do_callback();
}

int ControllerInput::handle(int event){
    switch (event){
    case FL_PUSH:
        // A click starts waiting, also after Escape gave up
        if (Fl::focus() != this) take_focus();
        set_waiting(true);
        return 1;
    // Getting the focus doesn't start waiting, as with ShortcutInput: FLTK
    // gives it to a box by itself
    case FL_UNFOCUS:
        set_waiting(false);
        return Fl_Input::handle(event);
    case FL_KEYBOARD: {
        const int key = Fl::event_key();
        const bool modifiers = Fl::event_state(FL_SHIFT | FL_CTRL | FL_ALT | FL_META) != 0;
        if (modifiers) return 0;
        if (!waiting){
            // Without a mouse: Tab to the box, then Enter or Space
            if (key == FL_Enter || key == FL_KP_Enter || key == ' '){
                set_waiting(true);
                return 1;
            }
            return 0;
        }
        if (key == FL_Escape){
            set_waiting(false);
            return 1;
        }
        if (key == FL_Delete || key == FL_BackSpace){
            const bool changed = current != 0;
            current = 0;
            set_waiting(false);
            if (changed) do_callback();
            return 1;
        }
        // E.g. Tab to the next widget, which stops waiting
        return 0;
    }
    default:
        return Fl_Input::handle(event);
    }
}
