#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>
struct tsf;
struct tml_message;

// This renderer has no device ownership. Its caller serializes every operation
// with the SDL mixer callback; parsing/loading always happens before hooking.
class SoftwareMidi
{
public:
    SoftwareMidi() = default;
    ~SoftwareMidi();
    SoftwareMidi(const SoftwareMidi&) = delete;
    SoftwareMidi& operator=(const SoftwareMidi&) = delete;
    bool Initialize(const void* soundfont, int bytes, int sampleRate);
    int LoadTrack(const void* midi, int bytes);
    bool Start(int track, bool loop = true);
    void Stop();
    void Shutdown();
    void Render(float* stereo, int frames);
    bool IsPlaying() const { return playing_; }
    uint64_t LoopCount() const { return loops_; }
    int ActiveVoices() const;
    unsigned int TrackDurationMs(int track) const;
    bool HasPreset(int bank, int program) const;
    bool HasKey(int bank, int program, int key) const;
private:
    void ResetChannels();
    void Dispatch(const tml_message* message);
    tsf* synth_ = nullptr;
    std::vector<tml_message*> tracks_;
    tml_message* cursor_ = nullptr;
    int current_ = -1, rate_ = 0;
    uint64_t frame_ = 0, loops_ = 0, endFrame_ = 0;
    bool playing_ = false, loop_ = false, tail_ = false;
};
