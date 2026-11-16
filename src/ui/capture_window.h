#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

// FLTK
#include <FL/Fl_Double_Window.H>

#include "core/image.h"
#include "core/subtitles.h"

// Only pointers and references to these are stored here, so the full
// headers are included in capture_window.cpp instead.
class CardWindow;
class ClipPlayer;
class LookupPanels;
class SettingsStore;
class SharedTranscriber;
class SubtitleTrack;
class SyncCounter;
class Waveform;
class Fl_Box;
class Fl_Button;
class Fl_Widget;

// The window a capture opens in: the waveform of the last seconds of
// what the computer played, with the end already selected. The selection
// can be changed by dragging, and played back with Space or the Play
// button. Enter (or the Transcribe button) turns it into text with
// whisper, shown under the waveform; Escape then cancels. Selecting part
// of the text with the mouse looks it up on Jisho, in a panel beside it;
// Translate (Ctrl+T) shows the whole line's DeepL translation there,
// which it starts with if the settings say to translate by itself.
// Add to Anki (Ctrl+N) then opens a card with the word, the line, the
// selected audio and the screenshot taken with the capture.
//
// The whisper model starts loading as soon as a capture opens, so it's
// mostly ready by the time the selection is.
//
// Closing it (or Escape) hides it, stops playing and transcribing, and
// forgets the audio and the screenshot.
//
// Subtitles (an .srt or .ass file) can be imported with the Subtitles
// button. They're then shown under the waveform, and kept for the next
// captures until removed; without them there's no room made for them.
// Each transcript is looked for in them, and when a line is like it
// enough, that line is shown in its place, since the subtitles have it
// right where whisper may have heard a word wrong, and the subtitles are
// synced to it. The sync can also be moved by hand, and is carried over to
// the next captures by the clock, which holds while the video isn't
// paused. Clicking a line shows that one.
//
// ScreenWindow is its twin for text on the screen.
class CaptureWindow {
public:
    // settings and transcriber have to outlive the window
    CaptureWindow(SettingsStore& settings, SharedTranscriber& transcriber);
    // Defined in capture_window.cpp, where ClipPlayer and CardWindow are
    // complete, which unique_ptr needs to delete them. Stops the threads
    // still running and waits for them.
    ~CaptureWindow();

    // One capture window per app: copying makes no sense
    CaptureWindow(const CaptureWindow&) = delete;
    CaptureWindow& operator=(const CaptureWindow&) = delete;

    // Shows audio, rate a second, in place of the capture there was, with
    // the last selection_seconds of it selected, and brings the window to
    // the front. A window that's already open is refreshed where it is.
    // Cancels a transcription of the capture there was.
    // screenshot is kept for the card; if it's empty, screenshot_error
    // says why. title is the window's. on_card_added, if there's one, is
    // called each time a card made from this capture is added to Anki,
    // e.g. to take a stashed line out of the stash. transcript, if it
    // isn't empty, is what the selection says already, e.g. transcribed
    // when it was stashed: it's shown at once, without pressing Enter.
    // captured_at is when the audio ends, in seconds since 1970 (UTC), to
    // carry the subtitles' sync over from capture to capture; 0 for now.
    void open(std::vector<float> audio, int rate, int selection_seconds, Image screenshot,
              std::string screenshot_error, std::string title = "Capture",
              std::function<void()> on_card_added = {}, std::string transcript = {},
              double captured_at = 0);

private:
    // A thread that loads the model or transcribes. Each one hands its
    // result back through Fl::awake(), even when stopped, so it can be
    // joined then.
    struct Job {
        unsigned id;
        std::jthread thread;
    };
    // What a job hands back to the UI thread
    struct JobResult {
        CaptureWindow* win;
        unsigned id;
        std::string text{};
        std::string error{};    // Non-empty if it failed
        bool cancelled = false;
    };

    SettingsStore& settings;
    SharedTranscriber& transcriber;

    // Owned by CaptureWindow as a member, so no new/delete anywhere
    Fl_Double_Window win{900, 560, "Capture"};

    // Created with new in the constructor and added to win, which deletes
    // them when it dies
    Waveform* waveform = nullptr;
    Fl_Button* play_btn = nullptr;
    // How long the selection is
    Fl_Box* selection_info = nullptr;
    Fl_Button* transcribe_btn = nullptr;
    Fl_Button* anki_btn = nullptr;
    Fl_Button* translate_btn = nullptr;
    Fl_Button* subtitles_btn = nullptr;
    // Under the waveform, shown only with subtitles: the subtitles, and a
    // row under them with what was found in them, a button back to
    // whisper's transcript, the sync, and one to remove them
    SubtitleTrack* subtitle_track = nullptr;
    Fl_Box* subtitle_info = nullptr;
    Fl_Button* whisper_btn = nullptr;
    SyncCounter* sync_counter = nullptr;
    Fl_Button* remove_subtitles_btn = nullptr;
    // The transcript and its lookups. Its widgets are win's.
    std::unique_ptr<LookupPanels> panels;

    // Imported, or empty. Kept from capture to capture.
    std::vector<SubtitleLine> subtitles;
    // The file's name, and the folder to start in when choosing the next
    std::string subtitles_name;
    std::string subtitles_dir;
    // The time in the video minus the time on the clock, from the last
    // sync, to sync the next capture by. None until there's been one.
    std::optional<double> video_minus_clock;
    // From open(), never 0
    double captured_at = 0;
    // Whisper's transcript of the selection (or the stashed one), and the
    // subtitles' line shown in its place, if any. whisper_btn goes back
    // and forth between them.
    std::string whisper_text;
    std::string subtitle_text;
    // Whether the subtitles' widgets are shown and the window grown for
    // them
    bool subtitle_area = false;

    // What waveform shows; it only borrows it
    std::vector<float> audio;
    int rate = 1;
    // Created on the first play, so a machine without audio output only
    // finds out then
    std::unique_ptr<ClipPlayer> player;
    Image screenshot;
    std::string screenshot_error;
    // From open()
    std::string title;
    std::function<void()> on_card_added;
    // Created the first time a card is added. Not a plain member: a window
    // created in this one's constructor would end up inside it.
    std::unique_ptr<CardWindow> card_win;

    // Running, or done and not joined yet. Usually one or two.
    std::vector<Job> jobs;
    unsigned next_job_id = 1;
    // The job whose transcript is awaited, or 0 if none
    unsigned transcribing = 0;

    // Static because FLTK callbacks are plain functions; the CaptureWindow
    // comes in through data.
    static void on_play(Fl_Widget* w, void* data);
    static void on_transcribe(Fl_Widget* w, void* data);
    static void on_add_to_anki(Fl_Widget* w, void* data);
    static void on_translate(Fl_Widget* w, void* data);
    // The window's close button and Escape
    static void on_close(Fl_Widget* w, void* data);
    // Many times a second while playing: moves the playhead, and notices
    // the end
    static void on_play_tick(void* data);
    // Runs on the UI thread, called by Fl::awake() from a job
    static void on_job_done(void* data);

    void play();
    void stop_playing();
    // Shows the selection's length, and whether there's one to act on
    void refresh_selection();
    // Starts loading the chosen model, if there's one ready to load
    void prepare_model();
    void transcribe();
    // Stops waiting for the transcript there is, if there is one
    void cancel_transcription();
    // Shows the Transcribe, Add to Anki and Translate buttons as they
    // should be now
    void refresh_transcribe_button();
    // Opens the card window with what's shown
    void add_to_anki();

    // Shows a transcript, or the subtitles' line most like it
    void show_transcript(const std::string& transcript);
    // Asks for a subtitle file and imports it
    void import_subtitles();
    void remove_subtitles();
    // Shows or hides the subtitles' widgets, growing or shrinking the
    // window for them
    void show_subtitle_area(bool show);
    // Sets the subtitles' sync, the time in the video at the capture's
    // end, and keeps it for the next captures
    void set_sync(double video_at_end);
    // Shows subtitle line index as the text, picked on the track
    void use_subtitle_line(size_t index);
};
