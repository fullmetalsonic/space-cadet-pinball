#include "pch.h"
#include "options.h"
#include "Sound.h"
#include "maths.h"

#ifdef PINBALL_AUDIO_DIAGNOSTICS
#include <atomic>
namespace
{
std::atomic<bool> diagnosticCapture{false};
std::atomic<Uint32> diagnosticFrames{0}, diagnosticBlocks{0}, diagnosticNonzeroBlocks{0};
std::atomic<Uint32> diagnosticPeak{0}, diagnosticInvalidSamples{0};
Uint16 diagnosticFormat = 0;
int diagnosticRate = 0, diagnosticChannels = 0;
Uint32 diagnosticStarted = 0, diagnosticLastReport = 0;
unsigned diagnosticLoaded = 0, diagnosticLoadFailed = 0, diagnosticAttempts = 0;
unsigned diagnosticNullWave = 0, diagnosticDisabled = 0, diagnosticPlayFailed = 0;
bool diagnosticInstalled = false, diagnosticFinished = false;

void SDLCALL ObserveMixedPcm(void*, Uint8* stream, int bytes)
{
    // Observation only: never modify samples, log, query the mixer, acquire a
    // mutex or allocate on the audio thread. Atomics are lock-free on arm64.
    if (!diagnosticCapture.load(std::memory_order_relaxed)) return;
    const int sampleBytes = SDL_AUDIO_BITSIZE(diagnosticFormat) / 8;
    if (!sampleBytes || diagnosticChannels <= 0) return;
    const int samples = bytes / sampleBytes;
    Uint32 localPeak = 0, invalid = 0;
    bool nonzero = false;
    for (int i = 0; i < samples; ++i)
    {
        float sample;
        if (diagnosticFormat == AUDIO_F32SYS) std::memcpy(&sample, stream + i * sampleBytes, sizeof sample);
        else { Sint16 value; std::memcpy(&value, stream + i * sampleBytes, sizeof value); sample = value / 32768.0f; }
        if (!std::isfinite(sample)) { ++invalid; continue; }
        const float magnitude = std::fabs(sample);
        nonzero |= magnitude > 0.00001f;
        localPeak = std::max(localPeak, static_cast<Uint32>(std::min(1.0f, magnitude) * 32768));
    }
    diagnosticBlocks.fetch_add(1, std::memory_order_relaxed);
    if (nonzero) diagnosticNonzeroBlocks.fetch_add(1, std::memory_order_relaxed);
    diagnosticInvalidSamples.fetch_add(invalid, std::memory_order_relaxed);
    auto previous = diagnosticPeak.load(std::memory_order_relaxed);
    while (previous < localPeak && !diagnosticPeak.compare_exchange_weak(previous, localPeak, std::memory_order_relaxed)) {}
    auto frames = diagnosticFrames.fetch_add(samples / diagnosticChannels, std::memory_order_relaxed) + samples / diagnosticChannels;
    // Independent of main-loop progress, bound observation to 15 audio seconds.
    if (frames >= static_cast<Uint32>(diagnosticRate) * 15) diagnosticCapture.store(false, std::memory_order_relaxed);
}
void DetachAudioDiagnostics()
{
    diagnosticCapture.store(false, std::memory_order_relaxed);
    if (diagnosticInstalled) Mix_SetPostMix(nullptr, nullptr); // waits for callback
    diagnosticInstalled = false;
}
}
#endif

int Sound::num_channels;
bool Sound::enabled_flag = false;
std::vector<ChannelInfo> Sound::Channels{};
int Sound::Volume = MIX_MAX_VOLUME;
bool Sound::MixOpen = false;

void Sound::Init(bool mixOpen, int channels, bool enableFlag, int volume)
{
	MixOpen = mixOpen;
	Volume = volume;
	SetChannels(channels);
	Enable(enableFlag);
#ifdef PINBALL_AUDIO_DIAGNOSTICS
    DetachAudioDiagnostics();
    diagnosticStarted = diagnosticLastReport = 0; diagnosticFinished = false;
    diagnosticLoaded = diagnosticLoadFailed = diagnosticAttempts = 0;
    diagnosticNullWave = diagnosticDisabled = diagnosticPlayFailed = 0;
    diagnosticFrames = diagnosticBlocks = diagnosticNonzeroBlocks = diagnosticPeak = diagnosticInvalidSamples = 0;
    if (MixOpen && Mix_QuerySpec(&diagnosticRate, &diagnosticFormat, &diagnosticChannels) &&
        (diagnosticFormat == AUDIO_S16SYS || diagnosticFormat == AUDIO_F32SYS) && diagnosticChannels > 0)
    {
        Mix_SetPostMix(ObserveMixedPcm, nullptr); diagnosticInstalled = true;
        SDL_Log("AUDIO_DIAGNOSTIC ready rate=%d channels=%d format=0x%x sounds=%d volume=%d", diagnosticRate, diagnosticChannels, diagnosticFormat, enabled_flag, Volume);
    }
#endif
}

void Sound::Enable(bool enableFlag)
{
	enabled_flag = enableFlag;
	if (MixOpen && !enableFlag)
		Mix_HaltChannel(-1);
}

void Sound::Activate()
{
	if (MixOpen)
		Mix_Resume(-1);
}

void Sound::Deactivate()
{
	if (MixOpen)
		Mix_Pause(-1);
}

void Sound::Close()
{
#ifdef PINBALL_AUDIO_DIAGNOSTICS
    DetachAudioDiagnostics();
    if (diagnosticStarted && !diagnosticFinished) { diagnosticLastReport = 0; PollAudioDiagnostics(); }
#endif
	Enable(false);
	Channels.clear();
}

void Sound::PlaySound(Mix_Chunk* wavePtr, int time, TPinballComponent* soundSource, const char* info)
{
#ifdef PINBALL_AUDIO_DIAGNOSTICS
    const bool diagnosticActive = BeginAudioDiagnostics();
    if (diagnosticActive)
    {
        ++diagnosticAttempts;
        if (!wavePtr) ++diagnosticNullWave;
        if (!MixOpen || !enabled_flag) ++diagnosticDisabled;
    }
#endif
	if (MixOpen && wavePtr && enabled_flag)
	{
		if (Mix_Playing(-1) == num_channels)
		{
			auto cmp = [](const ChannelInfo& a, const ChannelInfo& b)
			{
				return a.TimeStamp < b.TimeStamp;
			};
			auto min = std::min_element(Channels.begin(), Channels.end(), cmp);
			auto oldestChannel = static_cast<int>(std::distance(Channels.begin(), min));
			Mix_HaltChannel(oldestChannel);
		}

		auto channel = Mix_PlayChannel(-1, wavePtr, 0);
#ifdef PINBALL_AUDIO_DIAGNOSTICS
        if (diagnosticActive && channel < 0) ++diagnosticPlayFailed;
        if (diagnosticActive && (diagnosticAttempts <= 4 || (channel < 0 && diagnosticPlayFailed <= 4)))
            SDL_Log("AUDIO_EFFECT play channel=%d paused=%d volume=%d chunk_bytes=%u error=%s", channel,
                channel >= 0 ? Mix_Paused(channel) : -1, channel >= 0 ? Mix_Volume(channel, -1) : -1,
                wavePtr->alen, channel < 0 ? Mix_GetError() : "none");
#endif
		if (channel != -1)
		{
			Channels[channel].TimeStamp = time;
			if (options::Options.SoundStereo)
			{
				// Positional audio uses collision grid 2D coordinates normalized to [0, 1]
				// Point (0, 0) is bottom left table corner; point (1, 1) is top right table corner.
				// Z is defined as: 0 at table level, positive axis goes up from table surface.

				// Get the source sound position.
				// Sound without position are assumed to be at the center top of the table.
				vector3 soundPos{};
				if (soundSource)
				{
					auto soundPos2D = soundSource->get_coordinates();
					soundPos = {soundPos2D.X, soundPos2D.Y, 0.0f};
				}
				else
				{
					soundPos = {0.5f, 1.0f, 0.0f};
				}
				Channels[channel].Position = soundPos;

				// Listener is positioned at the bottom center of the table,
				// at 0.5 height, so roughly a table half - length.
				vector3 playerPos = {0.5f, 0.0f, 0.5f};
				auto soundDir = maths::vector_sub(soundPos, playerPos);

				// Find sound angle from positive Y axis in clockwise direction with atan2
				// Remap atan2 output from (-Pi, Pi] to [0, 2 * Pi)
				auto angle = fmodf(atan2(soundDir.X, soundDir.Y) + Pi * 2, Pi * 2);
				auto angleDeg = angle * 180.0f / Pi;
				auto angleSdl = static_cast<Sint16>(angleDeg);

				// Distance from listener to the sound position is roughly in the [0, ~1.22] range.
				// Remap to [0, 122] by multiplying by 100 and cast to an integer.
				auto distance = static_cast<Uint8>(100.0f * maths::magnitude(soundDir));

				// Mix_SetPosition expects an angle in (Sint16)degrees, where
				// angle 0 is due north, and rotates clockwise as the value increases.
				// Mix_SetPosition expects a (Uint8)distance from 0 (near) to 255 (far).
				Mix_SetPosition(channel, angleSdl, distance);

				// Output position of each sound emitted so we can verify
				// the sanity of the implementation.
				/*printf("X: %3.3f Y: %3.3f Angle: %3.3f Distance: %3d, Object: %s\n",
				       soundPos.X,
				       soundPos.Y,
				       angleDeg,
				       distance,
				       info
				);*/
			}
		}
	}
}

Mix_Chunk* Sound::LoadWaveFile(const std::string& lpName)
{
	if (!MixOpen)
		return nullptr;

	auto wavFile = fopenu(lpName.c_str(), "rb");
    if (!wavFile)
    {
#ifdef PINBALL_AUDIO_DIAGNOSTICS
        if (++diagnosticLoadFailed <= 4) SDL_Log("AUDIO_EFFECT load failed stage=fopen file=%s", lpName.substr(lpName.find_last_of("/\\") + 1).c_str());
#endif
        return nullptr;
    }
    // Keep the successfully opened filesystem handle. Android SDL_RWFromFile
    // resolves relative names against its internal-storage root/APK assets,
    // whereas the game uses its verified extracted-content working directory.
    auto* source = SDL_RWFromFP(wavFile, SDL_TRUE);
    if (!source)
    {
        fclose(wavFile); // ownership transfers only after RWops allocation
#ifdef PINBALL_AUDIO_DIAGNOSTICS
        if (++diagnosticLoadFailed <= 4) SDL_Log("AUDIO_EFFECT load failed stage=rwops error=%s", SDL_GetError());
#endif
        return nullptr;
    }
    auto* wave = Mix_LoadWAV_RW(source, 1); // closes RWops and its FILE on success or failure
#ifdef PINBALL_AUDIO_DIAGNOSTICS
    if (wave) ++diagnosticLoaded;
    else if (++diagnosticLoadFailed <= 4) SDL_Log("AUDIO_EFFECT load failed stage=mixer error=%s", Mix_GetError());
#endif
    return wave;
}

void Sound::FreeSound(Mix_Chunk* wave)
{
	if (MixOpen && wave)
		Mix_FreeChunk(wave);
}

void Sound::SetChannels(int channels)
{
	if (channels <= 0)
		channels = 8;

	num_channels = channels;
	Channels.resize(num_channels);
	if (MixOpen)
		Mix_AllocateChannels(num_channels);
	SetVolume(Volume);
}

void Sound::SetVolume(int volume)
{
	Volume = volume;
	if (MixOpen)
		Mix_Volume(-1, volume);
}

bool Sound::BeginAudioDiagnostics()
{
#ifdef PINBALL_AUDIO_DIAGNOSTICS
    if (!diagnosticInstalled || diagnosticFinished) return false;
    if (diagnosticStarted) return diagnosticCapture.load(std::memory_order_relaxed);
    diagnosticStarted = SDL_GetTicks();
    if (!diagnosticStarted) diagnosticStarted = 1;
    diagnosticLastReport = diagnosticStarted;
    diagnosticCapture.store(true, std::memory_order_relaxed);
    SDL_Log("AUDIO_DIAGNOSTIC begin bounded_ms=15000");
    return true;
#else
    return false;
#endif
}
void Sound::PollAudioDiagnostics()
{
#ifdef PINBALL_AUDIO_DIAGNOSTICS
    if (!diagnosticStarted || diagnosticFinished) return;
    auto now = SDL_GetTicks();
    bool done = now - diagnosticStarted >= 15000 || !diagnosticCapture.load(std::memory_order_relaxed);
    if (!done && now - diagnosticLastReport < 1000) return;
    if (done) { DetachAudioDiagnostics(); diagnosticFinished = true; }
    diagnosticLastReport = now;
    SDL_Log("AUDIO_PCM elapsed_ms=%u blocks=%u nonzero=%u peak=%.6f invalid=%u loaded=%u load_errors=%u attempts=%u null_wave=%u disabled=%u play_errors=%u channels_playing=%d channels_paused=%d volume=%d final=%d",
        now - diagnosticStarted, diagnosticBlocks.load(), diagnosticNonzeroBlocks.load(), diagnosticPeak.load()/32768.0,
        diagnosticInvalidSamples.load(), diagnosticLoaded, diagnosticLoadFailed, diagnosticAttempts, diagnosticNullWave,
        diagnosticDisabled, diagnosticPlayFailed, Mix_Playing(-1), Mix_Paused(-1), Volume, done);
#endif
}
