#include "pch.h"
#include "midi.h"
#include "pb.h"
#ifdef PINBALL_AUDIO_DIAGNOSTICS
#include "Sound.h"
#endif
#if defined(__ANDROID__) || defined(__EMSCRIPTEN__) || defined(PINBALL_SOFTWARE_MIDI)
#define PINBALL_USE_SOFTWARE_MIDI
#include "software_midi.h"
#include <mutex>
namespace
{
SoftwareMidi synth;
std::mutex synthMutex;
int softwareTracks[4] = {-1, -1, -1, -1};
int mixerRate = 0, mixerChannels = 0, softwareVolume = MIX_MAX_VOLUME;
Uint16 mixerFormat = 0;

std::vector<uint8_t> ReadAudioFile(const std::string& path)
{
    std::vector<uint8_t> bytes;
    // The game assets are verified/extracted before startup. Use the same
    // filesystem resolution as DAT/WAV instead of Android's APK-relative lookup.
    auto* handle = fopenu(path.c_str(), "rb");
    if (!handle) return bytes;
    auto* file = SDL_RWFromFP(handle, SDL_TRUE);
    if (!file) { fclose(handle); return bytes; }
    auto size = SDL_RWsize(file);
    if (size > 0 && size <= 32 * 1024 * 1024)
    {
        bytes.resize(static_cast<size_t>(size));
        if (SDL_RWread(file, bytes.data(), 1, bytes.size()) != bytes.size()) bytes.clear();
    }
    SDL_RWclose(file);
    return bytes;
}
void SDLCALL SoftwareMusic(void*, Uint8* stream, int bytes)
{
    // SDL_mixer invokes this while holding its audio-device lock. Never call
    // Mix_HookMusic from this callback or while holding synthMutex elsewhere.
    std::lock_guard<std::mutex> guard(synthMutex);
    std::memset(stream, 0, bytes);
    const int bytesPerSample = SDL_AUDIO_BITSIZE(mixerFormat) / 8;
    if (bytesPerSample <= 0 || mixerChannels <= 0) return;
    int remaining = bytes / (bytesPerSample * mixerChannels);
    float pcm[256 * 2];
    const float gain = softwareVolume / float(MIX_MAX_VOLUME);
    while (remaining > 0)
    {
        int count = std::min(remaining, 256);
        synth.Render(pcm, count);
        // Keep transport moving while muted; preserve byte-exact device silence
        // (floating-point negative zero would otherwise retain a sign bit).
        if (softwareVolume == 0)
        { stream += count * bytesPerSample * mixerChannels; remaining -= count; continue; }
        for (int i = 0; i < count; ++i)
            for (int channel = 0; channel < mixerChannels; ++channel)
            {
                float sample = mixerChannels == 1 ? (pcm[i*2] + pcm[i*2+1]) * 0.5f : pcm[i*2+channel];
                sample = std::max(-1.0f, std::min(1.0f, sample * gain));
                if (mixerFormat == AUDIO_F32SYS)
                { std::memcpy(stream, &sample, sizeof sample); stream += sizeof sample; }
                else
                { Sint16 value = static_cast<Sint16>(sample * 32767.0f); std::memcpy(stream, &value, sizeof value); stream += sizeof value; }
            }
        remaining -= count;
    }
}
bool InitSoftware()
{
    if (!Mix_QuerySpec(&mixerRate, &mixerFormat, &mixerChannels) ||
        (mixerFormat != AUDIO_S16SYS && mixerFormat != AUDIO_F32SYS) ||
        (mixerChannels != 1 && mixerChannels != 2))
    { SDL_SetError("Unsupported software MIDI mixer format"); return false; }
    SDL_Log("Software MIDI: %d Hz, %d channels, format 0x%x", mixerRate, mixerChannels, mixerFormat);
    const char* paths[] = {"audio/TimGM6mb.sf2", "assets/audio/TimGM6mb.sf2", "/assets/audio/TimGM6mb.sf2"};
    for (auto path : paths)
    {
        auto bytes = ReadAudioFile(path);
        if (!bytes.empty() && synth.Initialize(bytes.data(), static_cast<int>(bytes.size()), mixerRate)) return true;
    }
    SDL_SetError("Cannot load packaged TimGM6mb.sf2");
    return false;
}
}
#endif

std::vector<Mix_Music*> midi::LoadedTracks{};
Mix_Music* midi::track1, * midi::track2, * midi::track3;
MidiTracks midi::active_track, midi::NextTrack;
int midi::Volume = MIX_MAX_VOLUME;
bool midi::IsPlaying = false, midi::MixOpen = false, midi::MusicEnabled = false;

constexpr uint32_t FOURCC(uint8_t a, uint8_t b, uint8_t c, uint8_t d)
{ return static_cast<uint32_t>((d << 24) | (c << 16) | (b << 8) | a); }
int ToVariableLen(uint32_t value, uint32_t& dst)
{
    auto count = 1; dst = value & 0x7F;
    while ((value >>= 7)) { dst <<= 8; dst |= ((value & 0x7F) | 0x80); count++; }
    return count;
}
void midi::music_play()
{
    MusicEnabled = true;
    if (!IsPlaying && NextTrack != MidiTracks::None) play_track(NextTrack, true);
}
void midi::music_stop()
{
    MusicEnabled = false;
    if (active_track != MidiTracks::None) NextTrack = active_track;
    StopPlayback();
}
void midi::StopPlayback()
{
#ifdef PINBALL_USE_SOFTWARE_MIDI
    // Hook detach waits for any in-flight mixer callback before state is reset.
    if (MixOpen) Mix_HookMusic(nullptr, nullptr);
    { std::lock_guard<std::mutex> guard(synthMutex); synth.Stop(); }
#else
    if (active_track != MidiTracks::None && MixOpen) Mix_HaltMusic();
#endif
    active_track = MidiTracks::None;
    IsPlaying = false;
}
int midi::music_init(bool mixOpen, int volume)
{
    MixOpen = mixOpen;
    SetVolume(volume);
    active_track = NextTrack = MidiTracks::None;
    IsPlaying = MusicEnabled = false;
    track1 = track2 = track3 = nullptr;
#ifdef PINBALL_USE_SOFTWARE_MIDI
    std::fill(softwareTracks, softwareTracks + 4, -1);
    if (!MixOpen || pb::quickFlag || !InitSoftware()) return 0;
    const char* names[] = {"PINBALL", "TABA1", "TABA2", "TABA3"};
    for (int track = 1; track <= (pb::FullTiltMode ? 3 : 1); ++track)
    {
        std::string name = pb::FullTiltMode ? std::string("SOUND") + PathSeparator + names[track] : names[0];
        for (int lower = 0; lower < 2 && softwareTracks[track] < 0; ++lower)
        {
            if (lower) std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return std::tolower(c); });
            auto bytes = ReadAudioFile(pb::make_path_name(name + (lower ? ".mid" : ".MID")));
            if (!bytes.empty()) softwareTracks[track] = synth.LoadTrack(bytes.data(), static_cast<int>(bytes.size()));
            if (softwareTracks[track] < 0)
            {
                auto* converted = MdsToMidi(pb::make_path_name(name + (lower ? ".mds" : ".MDS")));
                if (converted) { softwareTracks[track] = synth.LoadTrack(converted->data(), static_cast<int>(converted->size())); delete converted; }
            }
        }
        if (track == 1 && softwareTracks[1] < 0 && pb::FullTiltDemoMode)
        {
            auto bytes = ReadAudioFile(pb::make_path_name(std::string("SOUND") + PathSeparator + "DEMO.MID"));
            if (!bytes.empty()) softwareTracks[1] = synth.LoadTrack(bytes.data(), static_cast<int>(bytes.size()));
            if (softwareTracks[1] < 0)
            {
                auto* converted = MdsToMidi(pb::make_path_name(std::string("SOUND") + PathSeparator + "DEMO.MDS"));
                if (converted) { softwareTracks[1] = synth.LoadTrack(converted->data(), static_cast<int>(converted->size())); delete converted; }
            }
        }
    }
#ifdef PINBALL_AUDIO_DIAGNOSTICS
    SDL_Log("AUDIO_MUSIC loaded track1=%d track2=%d track3=%d", softwareTracks[1], softwareTracks[2], softwareTracks[3]);
#endif
    return softwareTracks[1] >= 0;
#else
    if (pb::FullTiltMode)
    {
        track1 = load_track("TABA1"); track2 = load_track("TABA2"); track3 = load_track("TABA3");
        if (!track1 && pb::FullTiltDemoMode) track1 = load_track("DEMO");
    }
    else track1 = load_track("PINBALL"); // PINBALL2.MID is bitmap font data.
    return track1 != nullptr;
#endif
}
void midi::music_shutdown()
{
    music_stop();
#ifdef PINBALL_USE_SOFTWARE_MIDI
    { std::lock_guard<std::mutex> guard(synthMutex); synth.Shutdown(); }
    std::fill(softwareTracks, softwareTracks + 4, -1);
#endif
    for (auto music : LoadedTracks) Mix_FreeMusic(music);
    LoadedTracks.clear(); track1 = track2 = track3 = nullptr;
    active_track = NextTrack = MidiTracks::None; MixOpen = false;
}
void midi::SetVolume(int volume)
{
    Volume = std::max(0, std::min(MIX_MAX_VOLUME, volume));
#ifdef PINBALL_USE_SOFTWARE_MIDI
    { std::lock_guard<std::mutex> guard(synthMutex); softwareVolume = Volume; }
#else
    if (MixOpen) Mix_VolumeMusic(Volume);
#endif
}
Mix_Music* midi::load_track(std::string fileName)
{
    if (!MixOpen || pb::quickFlag) return nullptr;
    if (pb::FullTiltMode) fileName = std::string("SOUND") + PathSeparator + fileName;
    auto audio = load_track_sub(fileName, true);
    if (!audio) audio = load_track_sub(fileName, false);
    if (audio) LoadedTracks.push_back(audio);
    return audio;
}
Mix_Music* midi::load_track_sub(std::string fileName, bool isMidi)
{
    Mix_Music* audio = nullptr;
    fileName += isMidi ? ".MID" : ".MDS";
    for (int i = 0; i < 2; i++)
    {
        if (i) std::transform(fileName.begin(), fileName.end(), fileName.begin(), [](unsigned char c) { return std::tolower(c); });
        if (isMidi)
        {
            auto path = pb::make_path_name(fileName);
            auto* rw = SDL_RWFromFile(path.c_str(), "rb");
            if (rw) audio = Mix_LoadMUS_RW(rw, 1);
        }
        else
        {
            auto* converted = MdsToMidi(pb::make_path_name(fileName));
            if (converted)
            {
                auto* rw = SDL_RWFromConstMem(converted->data(), static_cast<int>(converted->size()));
                if (rw) audio = Mix_LoadMUS_RW(rw, 1);
                delete converted;
            }
        }
        if (audio) break;
    }
    return audio;
}
bool midi::play_track(MidiTracks track, bool replay)
{
#ifdef PINBALL_USE_SOFTWARE_MIDI
    int index = static_cast<int>(track);
    if (index < 1 || index > 3 || softwareTracks[index] < 0) return false;
#else
    auto* music = TrackToMidi(track);
    if (!music) return false;
#endif
    if (!replay && IsPlaying && active_track == track) return false;
    StopPlayback();
    NextTrack = track;
    if (!MusicEnabled || !MixOpen) return false;
#ifdef PINBALL_USE_SOFTWARE_MIDI
    bool started;
    { std::lock_guard<std::mutex> guard(synthMutex); started = synth.Start(softwareTracks[index]); }
    if (!started) return false;
    Mix_HookMusic(SoftwareMusic, nullptr);
#else
    if (Mix_PlayMusic(music, -1)) return false;
#endif
    IsPlaying = true; active_track = track; NextTrack = MidiTracks::None;
    SetVolume(Volume);
#ifdef PINBALL_AUDIO_DIAGNOSTICS
    if (Sound::BeginAudioDiagnostics())
        SDL_Log("AUDIO_MUSIC started track=%d volume=%d", static_cast<int>(track), Volume);
#endif
    return true;
}
MidiTracks midi::get_active_track() { return IsPlaying ? active_track : NextTrack; }
Mix_Music* midi::TrackToMidi(MidiTracks track)
{
    switch (track)
    {
    case MidiTracks::Track1: return track1;
    case MidiTracks::Track2: return track2;
    case MidiTracks::Track3: return track3;
    default: return nullptr;
    }
}

/// <summary>
/// SDL_mixed does not support MIDS. To support FT music, a conversion to MIDI is required.
/// </summary>
/// <param name="file">Path to .MDS file</param>
/// <returns>Vector that contains MIDI file</returns>
std::vector<uint8_t>* midi::MdsToMidi(std::string file)
{
	auto fileHandle = fopenu(file.c_str(), "rb");
	if (!fileHandle)
		return nullptr;

	fseek(fileHandle, 0, SEEK_END);
	auto fileSize = static_cast<uint32_t>(ftell(fileHandle));
	auto buffer = new uint8_t[fileSize];
	auto fileBuf = reinterpret_cast<riff_header*>(buffer);
	fseek(fileHandle, 0, SEEK_SET);
	fread(fileBuf, 1, fileSize, fileHandle);
	fclose(fileHandle);

	int returnCode = 0;
	std::vector<uint8_t>* midiOut = nullptr;
	do
	{
		if (fileSize < 12)
		{
			returnCode = 3;
			break;
		}
		if (fileBuf->Riff != FOURCC('R', 'I', 'F', 'F') ||
			fileBuf->Mids != FOURCC('M', 'I', 'D', 'S') ||
			fileBuf->Fmt != FOURCC('f', 'm', 't', ' '))
		{
			returnCode = 3;
			break;
		}
		if (fileBuf->FileSize > fileSize - 8)
		{
			returnCode = 3;
			break;
		}
		if (fileSize - 12 < 8)
		{
			returnCode = 3;
			break;
		}
		if (fileBuf->FmtSize < 12 || fileBuf->FmtSize > fileSize - 12)
		{
			returnCode = 3;
			break;
		}

		auto streamIdUsed = fileBuf->dwFlags == 0;
		auto dataChunk = reinterpret_cast<riff_data*>(reinterpret_cast<char*>(&fileBuf->dwTimeFormat) + fileBuf->
			FmtSize);
		if (dataChunk->Data != FOURCC('d', 'a', 't', 'a'))
		{
			returnCode = 3;
			break;
		}
		if (dataChunk->DataSize < 4)
		{
			returnCode = 3;
			break;
		}

		auto srcPtr = dataChunk->Blocks;
		std::vector<midi_event> midiEvents{};
		for (auto blockIndex = dataChunk->BlocksPerChunk; blockIndex; blockIndex--)
		{
			auto eventSizeInt = streamIdUsed ? 3 : 2;
			auto eventCount = srcPtr->CbBuffer / (4 * eventSizeInt);

			auto currentTicks = srcPtr->TkStart;
			auto srcPtr2 = reinterpret_cast<uint32_t*>(srcPtr->AData);
			for (auto i = 0u; i < eventCount; i++)
			{
				currentTicks += srcPtr2[0];
				auto event = streamIdUsed ? srcPtr2[2] : srcPtr2[1];
				midiEvents.push_back({currentTicks, event});
				srcPtr2 += eventSizeInt;
			}

			srcPtr = reinterpret_cast<riff_block*>(&srcPtr->AData[srcPtr->CbBuffer]);
		}

		// MIDS events can be out of order in the file
		std::sort(midiEvents.begin(), midiEvents.end(), [](const midi_event& lhs, const midi_event& rhs)
		{
			return lhs.iTicks < rhs.iTicks;
		});

		// MThd chunk
		std::vector<uint8_t>& midiBytes = *new std::vector<uint8_t>();
		midiOut = &midiBytes;
		midi_header header(SwapByteOrderShort(static_cast<uint16_t>(fileBuf->dwTimeFormat)));
		auto headerData = reinterpret_cast<const uint8_t*>(&header);
		midiBytes.insert(midiBytes.end(), headerData, headerData + sizeof header);

		// MTrk chunk
		midi_track track(7);
		auto trackData = reinterpret_cast<const uint8_t*>(&track);
		midiBytes.insert(midiBytes.end(), trackData, trackData + sizeof track);
		auto lengthPos = midiBytes.size() - 4;

		auto prevTime = 0u;
		for (const auto& event : midiEvents)
		{
			assertm(event.iTicks >= prevTime, "MIDS events: negative delta-time");
			uint32_t delta = event.iTicks - prevTime;
			prevTime = event.iTicks;

			// Delta time is in variable quantity, Big Endian
			uint32_t deltaVarLen;
			auto count = ToVariableLen(delta, deltaVarLen);
			auto deltaData = reinterpret_cast<const uint8_t*>(&deltaVarLen);
			midiBytes.insert(midiBytes.end(), deltaData, deltaData + count);

			switch (event.iEvent >> 24)
			{
			case 0:
				{
					// Type 0 - MIDI short message. 3 bytes: xx p1 p2 00, where xx - message, p* - parameters
					// Some of the messages have only one parameter
					auto msgMask = (event.iEvent) & 0xF0;
					auto shortMsg = (msgMask == 0xC0 || msgMask == 0xD0);
					auto eventData = reinterpret_cast<const uint8_t*>(&event.iEvent);
					midiBytes.insert(midiBytes.end(), eventData, eventData + (shortMsg ? 2 : 3));
					break;
				}
			case 1:
				{
					// Type 1 - tempo change, 3 bytes: xx xx xx 01
					// Meta message, set tempo, 3 bytes payload
					const uint8_t metaSetTempo[] = {0xFF, 0x51, 0x03};
					midiBytes.insert(midiBytes.end(), metaSetTempo, metaSetTempo + 3);

					auto eventBE = SwapByteOrderInt(event.iEvent);
					auto eventData = reinterpret_cast<const uint8_t*>(&eventBE) + 1;
					midiBytes.insert(midiBytes.end(), eventData, eventData + 3);
					break;
				}
			default:
				assertm(0, "MIDS events: uknown event");
				break;
			}
		}

		// Meta message, end of track, 0 bytes payload
		const uint8_t metaEndTrack[] = {0x00, 0xFF, 0x2f, 0x00};
		midiBytes.insert(midiBytes.end(), metaEndTrack, metaEndTrack + 4);

		// Set final MTrk size
		auto lengthBE = SwapByteOrderInt(static_cast<uint32_t>(midiBytes.size()) - sizeof header - sizeof track);
		auto lengthData = reinterpret_cast<const uint8_t*>(&lengthBE);
		std::copy_n(lengthData, 4, midiBytes.begin() + lengthPos);
	}
	while (false);

	delete[] buffer;
	if (returnCode && midiOut)
	{
		delete midiOut;
		midiOut = nullptr;
	}

	return midiOut;
}
