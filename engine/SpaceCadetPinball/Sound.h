#pragma once
#include "maths.h"
#include "TPinballComponent.h"

struct ChannelInfo
{
	int TimeStamp;
	vector2 Position;
};

class Sound
{
public:
	static std::vector<ChannelInfo> Channels;

	static void Init(bool mixOpen, int channels, bool enableFlag, int volume);
	static void Enable(bool enableFlag);
	static void Activate();
	static void Deactivate();
	static void Close();
	static void PlaySound(Mix_Chunk* wavePtr, int time, TPinballComponent *soundSource, const char* info);
	static Mix_Chunk* LoadWaveFile(const std::string& lpName);
	static void FreeSound(Mix_Chunk* wave);
	static void SetChannels(int channels);
	static void SetVolume(int volume);
    // Compiled to no-ops unless PINBALL_AUDIO_DIAGNOSTICS is explicitly set.
    static bool BeginAudioDiagnostics();
    static void PollAudioDiagnostics();
private:
	static int num_channels;
	static bool enabled_flag;
	static int Volume;
	static bool MixOpen;
};
