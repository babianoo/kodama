#pragma once

#include <functional>
#include <string>
#include <vector>

// FLTK
#include <FL/Fl_Double_Window.H>

#include "core/image.h"
#include "core/screen_frames.h"

// Only pointers and references to these are stored here, so the full
// headers are included in card_window.cpp instead.
class ClipboardInput;
class CropView;
class FrameStrip;
class SettingsStore;
class Fl_Box;
class Fl_Button;
class Fl_Widget;

// What a card starts with, from a capture
struct CardDraft {
    std::string word;
    std::string reading;
    std::string meaning;
    // The dictionary's own entry for the word, as the HTML the card gets
    // if meaning is left as it is: it has what plain text can't, like
    // example sentences. Empty when the dictionary gave none.
    std::string glossary_html;
    std::string sentence;
    // The part of sentence that was looked up, as it's written there
    std::string highlight;
    // The selection in the capture window
    std::vector<float> audio;
    int rate = 0;
    // The screenshots to pick the picture from, oldest first: those kept
    // every few seconds while the audio played, and the one taken when the
    // shortcut was pressed. Empty if none could be taken, and
    // screenshot_error then says why.
    std::vector<ScreenFrame> frames;
    std::string screenshot_error;
    // The one picked at first, -1 for none
    int frame = -1;
    // When the shortcut was pressed, which the frames' times are told
    // from, in seconds since 1970
    double captured_at = 0;
};

// The window "Add to Anki" opens: the card's text in fields that can be
// edited, formatted like the user's Yomitan cards when added (see
// format_card()), and the screenshot with a rectangle to crop it to, which starts
// where the last card's crop was. With more than one screenshot, a row of
// them under it picks which. The card is only added when Add is
// pressed, to the deck and note type chosen in Settings > Anki, each part
// in the field chosen there.
class CardWindow {
public:
    // settings has to outlive the window
    explicit CardWindow(SettingsStore& settings);

    // One card window per app: copying makes no sense
    CardWindow(const CardWindow&) = delete;
    CardWindow& operator=(const CardWindow&) = delete;

    // Shows draft in place of the card there was, and brings the window to
    // the front. on_added, if there's one, is called once the card is in
    // Anki.
    void open(CardDraft draft, std::function<void()> on_added = {});

private:
    SettingsStore& settings;

    // Owned by CardWindow as a member, so no new/delete anywhere. Closing
    // it (or Escape) hides it.
    Fl_Double_Window win{720, 686, "Add to Anki"};

    // Created with new in the constructor and added to win, which deletes
    // them when it dies
    ClipboardInput* word_input = nullptr;
    ClipboardInput* reading_input = nullptr;
    ClipboardInput* meaning_input = nullptr;
    ClipboardInput* sentence_input = nullptr;
    ClipboardInput* highlight_input = nullptr;
    // How long the audio is
    Fl_Box* audio_info = nullptr;
    CropView* crop_view = nullptr;
    // Under crop_view, shown only with more than one screenshot
    FrameStrip* frame_strip = nullptr;
    // Where the card goes, or what's missing in the settings
    Fl_Box* destination = nullptr;
    Fl_Button* add_btn = nullptr;

    // What frame_strip shows; it only borrows it
    CardDraft draft;
    std::function<void()> on_added;
    // The picked screenshot, decoded, which crop_view shows and only
    // borrows. Empty if there's none.
    Image screenshot;
    // Whether frame_strip is shown and the window grown for it
    bool strip_shown = false;

    // Static because FLTK callbacks are plain functions; the CardWindow
    // comes in through data.
    static void on_add(Fl_Widget* w, void* data);
    static void on_close(Fl_Widget* w, void* data);

    void add();
    // Hides the window, and forgets the card
    void close();
    // Shows draft's frame index in crop_view, cropped like the last one
    void show_frame(int index);
    // Shows or hides frame_strip, growing or shrinking the window for it
    void show_strip(bool show);
    // Puts the crop where the last card's was, if it fits this screenshot
    void restore_crop();
    // Remembers the crop for the next card
    void save_crop();
    // Shows where the card will go, as the settings are now
    void refresh_destination();
};
