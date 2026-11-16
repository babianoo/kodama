#include "ui/capture_window.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <format>
#include <span>
#include <utility>

// FLTK
#include <FL/Fl.H>
#include <FL/fl_ask.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Counter.H>
#include <FL/Fl_Native_File_Chooser.H>

#include "core/clip_player.h"
#include "core/resample.h"
#include "core/settings.h"
#include "core/transcriber.h"
#include "core/whisper_models.h"
#include "ui/bring_to_front.h"
#include "ui/card_window.h"
#include "ui/lookup_panels.h"
#include "ui/settings_store.h"
#include "ui/shared_transcriber.h"
#include "ui/subtitle_track.h"
#include "ui/theme.h"
#include "ui/waveform.h"

// Space around and between things
static constexpr int MARGIN = 10;
static constexpr int WAVEFORM_H = 160;
static constexpr int BTN_W = 90, BTN_H = 28;
static constexpr int TRANSCRIBE_W = 130;
static constexpr int ANKI_W = 120;
static constexpr int TRANSLATE_W = 100;
static constexpr int SUBTITLES_W = 100;
// The subtitles' part, under the waveform: the track, then a row under it
static constexpr int SUB_GAP = 4;
static constexpr int TRACK_H = 34;
static constexpr int SUB_ROW_H = 24;
static constexpr int WHISPER_W = 130;
static constexpr int SYNC_W = 170;
static constexpr int REMOVE_W = 80;
// How much taller the window is with them
static constexpr int SUBTITLE_AREA_H = SUB_GAP + TRACK_H + SUB_GAP + SUB_ROW_H;
// Seconds a line found in the subtitles can be off the selection before
// they're synced to it anew
static constexpr double RESYNC_SLACK = 2.0;
// Seconds between playhead moves while playing
static constexpr double PLAY_TICK = 1.0 / 30;

// The clock, in seconds since 1970 (UTC)
static double clock_now(){
    return std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();
}

// A path from FLTK, which gives them in UTF-8 on every system
static std::filesystem::path utf8_path(const std::string& path){
    const auto* begin = reinterpret_cast<const char8_t*>(path.data());
    return std::filesystem::path(std::u8string(begin, begin + path.size()));
}

static std::string utf8_string(const std::filesystem::path& path){
    const std::u8string text = path.u8string();
    return std::string(text.begin(), text.end());
}

// The subtitles' sync: the time in the video at the capture's end, as
// "12:34.5". The arrows move it by a tenth of a second and by a second.
class SyncCounter : public Fl_Counter {
public:
    SyncCounter(int x, int y, int w, int h) : Fl_Counter(x, y, w, h){
        // Fl_Valuator's are 0 to 1
        bounds(-24 * 3600, 24 * 3600);
        step(0.1, 1.0);
    }

    // Shown as "not synced" until it is
    bool synced = false;

    int format(char* buffer) override {
        // FLTK's buffer has room for 128
        if (!synced) return std::snprintf(buffer, 128, "not synced");
        const double v = value();
        const double magnitude = std::fabs(v);
        const int minutes = static_cast<int>(magnitude / 60);
        return std::snprintf(buffer, 128, "%s%d:%04.1f", v < 0 ? "-" : "", minutes, magnitude - minutes * 60);
    }
};

CaptureWindow::CaptureWindow(SettingsStore& settings, SharedTranscriber& transcriber)
    : settings(settings), transcriber(transcriber){
    // win is the current group here, like in MainWindow
    const int inner_w = win.w() - 2 * MARGIN;
    waveform = new Waveform(MARGIN, MARGIN, inner_w, WAVEFORM_H);
    waveform->on_selection([this]{
        // What plays is the selection as it was
        stop_playing();
        refresh_selection();
    });

    // The subtitles' part, hidden until there are some. The rest is
    // where it is without it, and moved down when it's shown.
    const int track_y = MARGIN + WAVEFORM_H + SUB_GAP;
    subtitle_track = new SubtitleTrack(MARGIN, track_y, inner_w, TRACK_H);
    subtitle_track->on_sync([this]{ set_sync(*subtitle_track->sync()); });
    subtitle_track->on_pick([this](size_t index){ use_subtitle_line(index); });
    subtitle_track->tooltip("Drag the subtitles, or turn the mouse wheel over them, to sync them. "
                            "Click a line to use it as the text.");
    subtitle_track->hide();

    const int sub_row_y = track_y + TRACK_H + SUB_GAP;
    const int remove_x = MARGIN + inner_w - REMOVE_W;
    const int sync_x = remove_x - MARGIN - SYNC_W;
    const int whisper_x = sync_x - MARGIN - WHISPER_W;
    subtitle_info = new Fl_Box(MARGIN, sub_row_y, whisper_x - 2 * MARGIN, SUB_ROW_H);
    subtitle_info->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE | FL_ALIGN_CLIP);
    subtitle_info->labelcolor(DIM);
    subtitle_info->labelsize(13);
    subtitle_info->hide();

    whisper_btn = new Fl_Button(whisper_x, sub_row_y, WHISPER_W, SUB_ROW_H);
    whisper_btn->box(FL_ROUND_UP_BOX);
    whisper_btn->labelsize(13);
    whisper_btn->clear_visible_focus();
    whisper_btn->callback([](Fl_Widget*, void* data){
        CaptureWindow* self = static_cast<CaptureWindow*>(data);
        // Back and forth between whisper's transcript and the subtitles'
        const bool showing_subtitles = self->panels->text() == self->subtitle_text;
        self->panels->show_text(showing_subtitles ? self->whisper_text : self->subtitle_text);
        self->whisper_btn->label(showing_subtitles ? "Use Subtitles'" : "Use Whisper's");
        self->panels->auto_translate(self->settings.get());
    }, this);
    whisper_btn->hide();

    sync_counter = new SyncCounter(sync_x, sub_row_y, SYNC_W, SUB_ROW_H);
    sync_counter->clear_visible_focus();
    sync_counter->textsize(13);
    sync_counter->tooltip("The subtitles' sync: where the video was at the capture's end. "
                          "The arrows move them by 0.1 s and 1 s.");
    sync_counter->callback([](Fl_Widget*, void* data){
        CaptureWindow* self = static_cast<CaptureWindow*>(data);
        self->set_sync(self->sync_counter->value());
    }, this);
    sync_counter->deactivate();
    sync_counter->hide();

    remove_subtitles_btn = new Fl_Button(remove_x, sub_row_y, REMOVE_W, SUB_ROW_H, "Remove");
    remove_subtitles_btn->box(FL_ROUND_UP_BOX);
    remove_subtitles_btn->labelsize(13);
    remove_subtitles_btn->clear_visible_focus();
    remove_subtitles_btn->tooltip("Stop showing these subtitles");
    remove_subtitles_btn->callback([](Fl_Widget*, void* data){
        static_cast<CaptureWindow*>(data)->remove_subtitles();
    }, this);
    remove_subtitles_btn->hide();

    const int row_y = MARGIN + WAVEFORM_H + MARGIN;
    play_btn = new Fl_Button(MARGIN, row_y, BTN_W, BTN_H, "Play");
    play_btn->box(FL_ROUND_UP_BOX);
    play_btn->clear_visible_focus();
    play_btn->shortcut(' ');
    play_btn->tooltip("Play the selection (Space)");
    play_btn->callback(on_play, this);

    const int subtitles_x = MARGIN + BTN_W + MARGIN;
    subtitles_btn = new Fl_Button(subtitles_x, row_y, SUBTITLES_W, BTN_H, "Subtitles...");
    subtitles_btn->box(FL_ROUND_UP_BOX);
    subtitles_btn->clear_visible_focus();
    subtitles_btn->tooltip("Import the subtitles of what's playing, an .srt or .ass file, to check "
                           "transcripts against");
    subtitles_btn->callback([](Fl_Widget*, void* data){
        static_cast<CaptureWindow*>(data)->import_subtitles();
    }, this);

    const int transcribe_x = MARGIN + inner_w - TRANSCRIBE_W;
    const int anki_x = transcribe_x - MARGIN - ANKI_W;
    const int translate_x = anki_x - MARGIN - TRANSLATE_W;
    const int info_x = subtitles_x + SUBTITLES_W + MARGIN;
    selection_info = new Fl_Box(info_x, row_y, translate_x - MARGIN - info_x, BTN_H);
    selection_info->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    selection_info->labelcolor(DIM);

    translate_btn = new Fl_Button(translate_x, row_y, TRANSLATE_W, BTN_H, "Translate");
    translate_btn->box(FL_ROUND_UP_BOX);
    translate_btn->clear_visible_focus();
    translate_btn->tooltip("Translate this line with DeepL (Ctrl+T)");
    translate_btn->shortcut(FL_CTRL | 't');
    translate_btn->callback(on_translate, this);

    transcribe_btn = new Fl_Button(transcribe_x, row_y, TRANSCRIBE_W, BTN_H);
    transcribe_btn->box(FL_ROUND_UP_BOX);
    transcribe_btn->clear_visible_focus();
    transcribe_btn->shortcut(FL_Enter);
    transcribe_btn->tooltip("Turn the selection into text (Enter)");
    transcribe_btn->callback(on_transcribe, this);

    anki_btn = new Fl_Button(anki_x, row_y, ANKI_W, BTN_H, "Add to Anki");
    anki_btn->box(FL_ROUND_UP_BOX);
    anki_btn->clear_visible_focus();
    anki_btn->tooltip("Make a card of the word looked up and this line (Ctrl+N)");
    anki_btn->shortcut(FL_CTRL | 'n');
    anki_btn->callback(on_add_to_anki, this);

    // The rest of the window: the transcript, and the lookup panel
    const int panel_y = row_y + BTN_H + MARGIN;
    panels = std::make_unique<LookupPanels>(MARGIN, panel_y, inner_w, win.h() - panel_y - MARGIN);
    panels->on_change([this]{ refresh_transcribe_button(); });

    refresh_transcribe_button();

    // No resizable() on purpose: FLTK then keeps the window at a fixed
    // size, which also makes tiling window managers float it
    win.end();
    win.callback(on_close, this);
}

CaptureWindow::~CaptureWindow(){
    Fl::remove_timeout(on_play_tick, this);
    stop_bringing_to_front(win);
    // jobs then goes with the members, stopping and joining each thread.
    // Their results are never handled: Fl::run() has returned by now.
}

void CaptureWindow::open(std::vector<float> new_audio, int new_rate, int selection_seconds, Image new_screenshot,
                         std::string new_screenshot_error, std::string new_title,
                         std::function<void()> new_on_card_added, std::string transcript,
                         double new_captured_at){
    stop_playing();
    captured_at = new_captured_at > 0 ? new_captured_at : clock_now();
    title = std::move(new_title);
    // Borrowed by the window, so kept in a member
    win.label(title.c_str());
    on_card_added = std::move(new_on_card_added);
    // Its text would be of the capture there was
    cancel_transcription();
    screenshot = std::move(new_screenshot);
    screenshot_error = std::move(new_screenshot_error);

    audio = std::move(new_audio);
    rate = new_rate;
    waveform->audio(audio, rate);
    const size_t selected = std::min(audio.size(), static_cast<size_t>(selection_seconds) * static_cast<size_t>(rate));
    waveform->selection(audio.size() - selected, audio.size());
    refresh_selection();

    // The subtitles, kept from the capture before, synced by the clock
    subtitle_track->duration(static_cast<double>(audio.size()) / rate);
    subtitle_track->highlight(0, 0);
    whisper_btn->hide();
    whisper_text.clear();
    subtitle_text.clear();
    if (!subtitles.empty()){
        if (video_minus_clock) set_sync(captured_at + *video_minus_clock);
        else subtitle_track->sync(std::nullopt);
        subtitle_info->copy_label(std::format("{} · {} lines{}", subtitles_name, subtitles.size(),
                                              video_minus_clock ? "" : " · Transcribe a line to sync them").c_str());
    }

    if (transcript.empty())
        panels->show_note("Press Enter to turn the selection into text, or Space to hear it first.");
    else {
        // Enter transcribes it again, e.g. after changing the selection
        show_transcript(transcript);
    }
    refresh_transcribe_button();

    prepare_model();

    // A window that's already open is refreshed where it is, not hidden
    // and shown anew, so it keeps its place on the screen. show() then
    // only raises it. Wayland doesn't let an app do that: there it stays
    // behind what covers it.
    win.show();
    // On Windows that isn't enough, e.g. over a fullscreen game
    bring_to_front(win);
}

void CaptureWindow::refresh_selection(){
    const auto [begin, end] = waveform->selection();
    if (begin == end){
        selection_info->copy_label("Drag on the waveform to select a line.");
        play_btn->deactivate();
    } else {
        selection_info->copy_label(std::format("{:.1f} s selected", static_cast<double>(end - begin) / rate).c_str());
        play_btn->activate();
    }
    selection_info->redraw();
    refresh_transcribe_button();
}

void CaptureWindow::refresh_transcribe_button(){
    const auto [begin, end] = waveform->selection();
    transcribe_btn->label(transcribing ? "Transcribing..." : "Transcribe");
    if (transcribing || begin == end) transcribe_btn->deactivate();
    else transcribe_btn->activate();
    // A card needs something to show on its front
    if (panels->text().empty() || begin == end) anki_btn->deactivate();
    else anki_btn->activate();
    if (panels->text().empty()) translate_btn->deactivate();
    else translate_btn->activate();
}

void CaptureWindow::on_play(Fl_Widget*, void* data){
    CaptureWindow* self = static_cast<CaptureWindow*>(data);
    if (self->player && self->player->playing()) self->stop_playing();
    else self->play();
}

void CaptureWindow::play(){
    const auto [begin, end] = waveform->selection();
    if (begin == end) return;
    try {
        if (!player) player = std::make_unique<ClipPlayer>();
        player->play(std::span(audio).subspan(begin, end - begin), rate);
    } catch (const std::exception& e){
        fl_alert("%s", e.what());
        return;
    }
    play_btn->label("Stop");
    play_btn->tooltip("Stop playing (Space)");
    Fl::add_timeout(PLAY_TICK, on_play_tick, this);
}

void CaptureWindow::stop_playing(){
    Fl::remove_timeout(on_play_tick, this);
    if (player) player->stop();
    waveform->hide_playhead();
    play_btn->label("Play");
    play_btn->tooltip("Play the selection (Space)");
}

void CaptureWindow::on_play_tick(void* data){
    CaptureWindow* self = static_cast<CaptureWindow*>(data);
    if (!self->player->playing()){
        self->stop_playing();
        return;
    }
    self->waveform->playhead(self->waveform->selection().first + self->player->position());
    Fl::repeat_timeout(PLAY_TICK, on_play_tick, data);
}

void CaptureWindow::prepare_model(){
    // A job still running loads it anyway, or already has
    if (!jobs.empty()) return;
    const WhisperModel* model = find_model(settings.get().speech.whisper_model);
    // Said when Enter is pressed: maybe they only want to listen
    if (!model || !is_installed(*model)) return;

    const bool gpu = settings.get().speech.use_gpu;
    const unsigned id = next_job_id++;
    transcriber.hold();
    // model points into WHISPER_MODELS, which lives for the whole program
    jobs.push_back({id, std::jthread([this, id, model, gpu](std::stop_token stop){
        auto* result = new JobResult{.win = this, .id = id};
        try {
            transcriber.prepare(*model, gpu, stop);
        } catch (const TranscriptionCancelled&){
            result->cancelled = true;
        } catch (const std::exception& e){
            // Said if it's still so when transcribing, which tries again
            result->error = e.what();
        }
        Fl::awake(on_job_done, result);
    })});
}

void CaptureWindow::on_transcribe(Fl_Widget*, void* data){
    static_cast<CaptureWindow*>(data)->transcribe();
}

void CaptureWindow::transcribe(){
    const auto [begin, end] = waveform->selection();
    if (transcribing || begin == end) return;

    const WhisperModel* model = find_model(settings.get().speech.whisper_model);
    if (!model || !is_installed(*model)){
        // Modal: waits for OK before returning
        fl_alert("No whisper model is ready to turn speech into text.\n\n"
                 "Download one and choose it in Settings > Speech-to-Text.");
        return;
    }

    const unsigned id = next_job_id++;
    transcribing = id;
    panels->show_note("Transcribing...");
    whisper_btn->hide();
    refresh_transcribe_button();

    // A copy for the thread: a new capture replaces audio
    std::vector<float> clip(audio.begin() + static_cast<std::ptrdiff_t>(begin),
                            audio.begin() + static_cast<std::ptrdiff_t>(end));
    const bool gpu = settings.get().speech.use_gpu;
    transcriber.hold();
    // Waits its turn if the model is still loading
    jobs.push_back({id, std::jthread([this, id, model, gpu, clip = std::move(clip), clip_rate = rate](std::stop_token stop){
        auto* result = new JobResult{.win = this, .id = id};
        try {
            const std::vector<float> samples = resample(clip, clip_rate, Transcriber::SAMPLE_RATE);
            result->text = transcriber.transcribe(*model, gpu, samples, "ja", stop);
        } catch (const TranscriptionCancelled&){
            result->cancelled = true;
        } catch (const std::exception& e){
            result->error = e.what();
        }
        Fl::awake(on_job_done, result);
    })});
}

void CaptureWindow::cancel_transcription(){
    if (!transcribing) return;
    // Its result still comes, to join the thread, but isn't shown
    for (Job& job : jobs)
        if (job.id == transcribing) job.thread.request_stop();
    transcribing = 0;
    refresh_transcribe_button();
}

void CaptureWindow::on_job_done(void* data){
    // unique_ptr so result is deleted when this returns
    std::unique_ptr<JobResult> result(static_cast<JobResult*>(data));
    CaptureWindow* self = result->win;

    // Its last act was queueing this, so joining takes no time
    const auto job = std::ranges::find(self->jobs, result->id, &Job::id);
    if (job != self->jobs.end()){
        job->thread.join();
        self->jobs.erase(job);
    }
    self->transcriber.release();

    // Loading the model, or cancelled
    if (result->id != self->transcribing) return;
    self->transcribing = 0;

    if (!result->error.empty())
        self->panels->show_note(std::format("Couldn't turn the speech into text: {}", result->error));
    else if (result->text.empty()) self->panels->show_note("No speech found in the selection.");
    else self->show_transcript(result->text);
    self->refresh_transcribe_button();
}

void CaptureWindow::on_translate(Fl_Widget*, void* data){
    CaptureWindow* self = static_cast<CaptureWindow*>(data);
    self->panels->translate(self->settings.get());
}

void CaptureWindow::on_add_to_anki(Fl_Widget*, void* data){
    static_cast<CaptureWindow*>(data)->add_to_anki();
}

void CaptureWindow::add_to_anki(){
    const auto [begin, end] = waveform->selection();
    if (panels->text().empty() || begin == end) return;
    stop_playing();

    CardDraft draft;
    panels->fill(draft);
    draft.audio.assign(audio.begin() + static_cast<std::ptrdiff_t>(begin), audio.begin() + static_cast<std::ptrdiff_t>(end));
    draft.rate = rate;
    // A copy: more than one card can be made from a capture
    draft.screenshot = screenshot;
    draft.screenshot_error = screenshot_error;

    if (!card_win) card_win = std::make_unique<CardWindow>(settings);
    card_win->open(std::move(draft), on_card_added);
}

void CaptureWindow::on_close(Fl_Widget*, void* data){
    CaptureWindow* self = static_cast<CaptureWindow*>(data);
    // Escape while transcribing only cancels that
    if (self->transcribing && Fl::event() == FL_SHORTCUT && Fl::event_key() == FL_Escape){
        self->cancel_transcription();
        self->panels->show_note("Cancelled. Press Enter to try again.");
        return;
    }

    self->stop_playing();
    self->cancel_transcription();
    self->win.hide();
    // Not kept any longer than it's needed
    self->waveform->audio({}, self->rate);
    self->audio.clear();
    self->audio.shrink_to_fit();
    self->screenshot = {};
    self->panels->clear();
    self->whisper_text.clear();
    self->subtitle_text.clear();
    self->on_card_added = {};
}

void CaptureWindow::show_transcript(const std::string& transcript){
    whisper_text = transcript;
    subtitle_text.clear();
    whisper_btn->hide();
    subtitle_track->highlight(0, 0);
    const auto show_whisper = [&]{
        panels->show_text(transcript);
        panels->auto_translate(settings.get());
    };
    const auto [begin, end] = waveform->selection();
    if (subtitles.empty() || begin == end) return show_whisper();

    // Where the selection is in the capture, in seconds
    const double length = static_cast<double>(audio.size()) / rate;
    const double sel_from = static_cast<double>(begin) / rate, sel_to = static_cast<double>(end) / rate;
    const double sel_middle = (sel_from + sel_to) / 2;
    // And in the video, if the subtitles are synced, to tell apart lines
    // that say the same
    const std::optional<double> sync = subtitle_track->sync();
    std::optional<double> near;
    if (sync) near = *sync - length + sel_from;

    const std::optional<SubtitleMatch> match = match_subtitles(subtitles, transcript, near);
    if (!match){
        subtitle_info->copy_label(std::format("No line in {} is like the transcript", subtitles_name).c_str());
        subtitle_info->redraw();
        return show_whisper();
    }

    // Synced to it when they weren't, or were off: the line's middle at
    // the selection's. Left as they are when it's about where they say,
    // since they may have been synced more closely by hand.
    const double match_middle = (match->start + match->stop) / 2;
    if (!sync || std::fabs(*sync - length + sel_middle - match_middle) > (sel_to - sel_from) / 2 + RESYNC_SLACK)
        set_sync(match_middle - sel_middle + length);
    subtitle_track->highlight(match->begin, match->end);

    subtitle_text = match->text;
    panels->show_text(subtitle_text);
    panels->auto_translate(settings.get());
    if (normalized_for_match(subtitle_text) == normalized_for_match(transcript))
        subtitle_info->copy_label("The subtitles say the same as whisper heard");
    else {
        subtitle_info->copy_label(std::format("From the subtitles: {:.0f}% like what whisper heard",
                                              match->score * 100).c_str());
        whisper_btn->label("Use Whisper's");
        whisper_btn->show();
    }
    subtitle_info->redraw();
}

void CaptureWindow::use_subtitle_line(size_t index){
    if (index >= subtitles.size()) return;
    // Lines broken for the screen are one sentence, as when matched
    subtitle_text.clear();
    for (char c : subtitles[index].text) if (c != '\n') subtitle_text += c;
    subtitle_track->highlight(index, index + 1);
    panels->show_text(subtitle_text);
    panels->auto_translate(settings.get());
    subtitle_info->copy_label("Picked from the subtitles");
    subtitle_info->redraw();
    if (!whisper_text.empty() && whisper_text != subtitle_text){
        whisper_btn->label("Use Whisper's");
        whisper_btn->show();
    } else whisper_btn->hide();
    refresh_transcribe_button();
}

void CaptureWindow::import_subtitles(){
    Fl_Native_File_Chooser chooser;
    chooser.title("Import Subtitles");
    chooser.type(Fl_Native_File_Chooser::BROWSE_FILE);
    chooser.filter("Subtitles\t*.{srt,ass,ssa}\nAll Files\t*");
    if (!subtitles_dir.empty()) chooser.directory(subtitles_dir.c_str());
    // 1 if cancelled, -1 if it failed
    if (chooser.show() != 0 || !chooser.filename()) return;

    const std::filesystem::path path = utf8_path(chooser.filename());
    std::vector<SubtitleLine> loaded;
    try {
        loaded = load_subtitles(path);
    } catch (const std::exception& e){
        // Modal: waits for OK before returning
        fl_alert("Couldn't import the subtitles from %s:\n\n%s", utf8_string(path.filename()).c_str(), e.what());
        return;
    }
    subtitles = std::move(loaded);
    subtitles_name = utf8_string(path.filename());
    subtitles_dir = utf8_string(path.parent_path());

    // Other subtitles are of another episode: the sync there was is of
    // the one before
    video_minus_clock.reset();
    subtitle_track->lines(&subtitles);
    subtitle_track->sync(std::nullopt);
    sync_counter->synced = false;
    sync_counter->deactivate();
    sync_counter->redraw();
    subtitle_info->copy_label(std::format("{} · {} lines · Transcribe a line to sync them", subtitles_name,
                                          subtitles.size()).c_str());
    show_subtitle_area(true);

    // The transcript shown already is looked for in them
    if (!whisper_text.empty()) show_transcript(whisper_text);
}

void CaptureWindow::remove_subtitles(){
    // Back to what whisper heard, if a line of theirs is shown
    if (!whisper_text.empty() && panels->text() != whisper_text){
        panels->show_text(whisper_text);
        panels->auto_translate(settings.get());
    }
    subtitle_text.clear();
    subtitles.clear();
    subtitles_name.clear();
    video_minus_clock.reset();
    subtitle_track->lines(nullptr);
    subtitle_track->sync(std::nullopt);
    sync_counter->synced = false;
    show_subtitle_area(false);
    refresh_transcribe_button();
}

void CaptureWindow::show_subtitle_area(bool show){
    if (show == subtitle_area) return;
    subtitle_area = show;

    // Everything under the waveform moves down to make room, or back up
    const int dy = show ? SUBTITLE_AREA_H : -SUBTITLE_AREA_H;
    Fl_Widget* const own[] = {subtitle_track, subtitle_info, whisper_btn, sync_counter, remove_subtitles_btn};
    const int below = MARGIN + WAVEFORM_H;
    for (int i = 0; i < win.children(); ++i){
        Fl_Widget* child = win.child(i);
        if (std::ranges::find(own, child) != std::end(own)) continue;
        if (child->y() >= below) child->position(child->x(), child->y() + dy);
    }
    for (Fl_Widget* widget : own){
        if (show && widget != whisper_btn) widget->show();
        else if (!show) widget->hide();
    }

    // The window doesn't resize, so its size range is set first: a window
    // manager may hold it to the old one
    const int new_h = win.h() + dy;
    win.size_range(win.w(), new_h, win.w(), new_h);
    win.size(win.w(), new_h);
    win.redraw();
}

void CaptureWindow::set_sync(double video_at_end){
    subtitle_track->sync(video_at_end);
    sync_counter->synced = true;
    sync_counter->activate();
    sync_counter->value(video_at_end);
    sync_counter->redraw();
    // The next capture is this much further into the video as it is later
    // on the clock, unless the video was paused
    video_minus_clock = video_at_end - captured_at;
}
