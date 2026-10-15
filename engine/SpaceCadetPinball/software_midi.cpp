#include "software_midi.h"
#include <algorithm>
#include <cstring>
#include <limits>
#define TSF_IMPLEMENTATION
#include "third_party/TinySoundFont/tsf.h"
#define TML_IMPLEMENTATION
#include "third_party/TinySoundFont/tml.h"

SoftwareMidi::~SoftwareMidi() { Shutdown(); }

bool SoftwareMidi::Initialize(const void* sf, int bytes, int rate)
{
    Shutdown();
    if (!sf || bytes <= 0 || rate < 8000 || rate > 192000) return false;
    synth_ = tsf_load_memory(sf, bytes);
    if (!synth_) return false;
    rate_ = rate;
    tsf_set_output(synth_, TSF_STEREO_INTERLEAVED, rate_, -6.0f);
    if (!tsf_set_max_voices(synth_, 256)) { Shutdown(); return false; }
    // Allocate all sixteen channels before entering the audio thread.
    for (int channel = 0; channel < 16; ++channel)
        if (!tsf_channel_set_presetnumber(synth_, channel, 0, channel == 9))
        { Shutdown(); return false; }
    ResetChannels();
    return true;
}

int SoftwareMidi::LoadTrack(const void* bytes, int size)
{
    if (!synth_ || !bytes || size < 14 || std::memcmp(bytes, "MThd", 4)) return -1;
    auto* track = tml_load_memory(bytes, size);
    if (!track) return -1;
    int notes = 0;
    for (auto* m = track; m; m = m->next)
        if (m->type == TML_NOTE_ON && m->velocity) ++notes;
    if (!notes) { tml_free(track); return -1; }
    tracks_.push_back(track);
    return static_cast<int>(tracks_.size()) - 1;
}

bool SoftwareMidi::Start(int track, bool loop)
{
    Stop();
    if (!synth_ || track < 0 || track >= static_cast<int>(tracks_.size())) return false;
    current_ = track; cursor_ = tracks_[track];
    endFrame_ = std::max<uint64_t>(1, (uint64_t(TrackDurationMs(track)) * rate_ + 999) / 1000);
    loop_ = loop; playing_ = true; frame_ = loops_ = 0; tail_ = false;
    return true;
}

void SoftwareMidi::ResetChannels()
{
    if (!synth_) return;
    // A loop or a stopped track must leave no releasing/sustained voices. The
    // pinned TSF exposes its voice storage in the implementation translation unit;
    // clearing it avoids tsf_reset(), which frees channels and allocates in the callback.
    for (int i = 0; i < synth_->voiceNum; ++i) synth_->voices[i].playingPreset = -1;
    for (int ch = 0; ch < 16; ++ch)
    {
        tsf_channel_midi_control(synth_, ch, 121, 0);
        tsf_channel_set_sustain(synth_, ch, 0);
        tsf_channel_set_pitchwheel(synth_, ch, 8192);
        tsf_channel_set_presetnumber(synth_, ch, 0, ch == 9);
    }
}

void SoftwareMidi::Stop()
{
    playing_ = false; cursor_ = nullptr; current_ = -1;
    frame_ = endFrame_ = loops_ = 0; tail_ = false;
    ResetChannels();
}
void SoftwareMidi::Shutdown()
{
    Stop();
    for (auto* track : tracks_) tml_free(track);
    tracks_.clear();
    if (synth_) tsf_close(synth_);
    synth_ = nullptr; rate_ = 0;
}
int SoftwareMidi::ActiveVoices() const { return synth_ ? tsf_active_voice_count(synth_) : 0; }
bool SoftwareMidi::HasPreset(int bank, int program) const
{ return synth_ && tsf_get_presetindex(synth_, bank, program) >= 0; }
bool SoftwareMidi::HasKey(int bank, int program, int key) const
{
    if (!synth_) return false;
    int index = tsf_get_presetindex(synth_, bank, program);
    if (index < 0) return false;
    const auto& preset = synth_->presets[index];
    for (int i = 0; i < preset.regionNum; ++i)
        if (key >= preset.regions[i].lokey && key <= preset.regions[i].hikey) return true;
    return false;
}
unsigned int SoftwareMidi::TrackDurationMs(int track) const
{
    if (track < 0 || track >= static_cast<int>(tracks_.size())) return 0;
    unsigned int duration = 0;
    tml_get_info(tracks_[track], nullptr, nullptr, nullptr, nullptr, &duration);
    return duration;
}
void SoftwareMidi::Dispatch(const tml_message* m)
{
    switch (m->type)
    {
    case TML_PROGRAM_CHANGE: tsf_channel_set_presetnumber(synth_, m->channel, m->program, m->channel == 9); break;
    case TML_NOTE_ON: tsf_channel_note_on(synth_, m->channel, m->key, m->velocity / 127.0f); break;
    case TML_NOTE_OFF: tsf_channel_note_off(synth_, m->channel, m->key); break;
    case TML_PITCH_BEND: tsf_channel_set_pitchwheel(synth_, m->channel, m->pitch_bend); break;
    case TML_CONTROL_CHANGE: tsf_channel_midi_control(synth_, m->channel, m->control, m->control_value); break;
    default: break;
    }
}
void SoftwareMidi::Render(float* stereo, int frames)
{
    if (!stereo || frames <= 0) return;
    std::fill(stereo, stereo + frames * 2, 0.0f);
    if (!playing_ || !synth_) return;
    while (frames > 0 && playing_)
    {
        while (cursor_ && (uint64_t(cursor_->time) * rate_ + 999) / 1000 <= frame_)
        { Dispatch(cursor_); cursor_ = cursor_->next; }
        if (!cursor_ && frame_ >= endFrame_)
        {
            if (loop_)
            {
                ResetChannels(); cursor_ = tracks_[current_]; frame_ = 0; ++loops_;
                continue;
            }
            if (!tail_)
            {
                for (int ch = 0; ch < 16; ++ch)
                { tsf_channel_set_sustain(synth_, ch, 0); tsf_channel_note_off_all(synth_, ch); }
                tail_ = true;
            }
            if (!ActiveVoices() || frame_ >= endFrame_ + uint64_t(rate_) * 3)
            { Stop(); break; }
        }
        uint64_t next = cursor_ ? (uint64_t(cursor_->time) * rate_ + 999) / 1000 : endFrame_;
        int count = std::min(frames, 64);
        if (next > frame_) count = std::min<uint64_t>(count, next - frame_);
        tsf_render_float(synth_, stereo, count, 0);
        frame_ += count; frames -= count; stereo += count * 2;
    }
}
