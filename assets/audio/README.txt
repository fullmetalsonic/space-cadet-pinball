# Offline MIDI assets

TimGM6mb.sf2 is the complete unmodified asset from Debian timgm6mb-soundfont
1.3-5. Copyright 2004 Tim Brechbill and 2010 David Bolton, GPL version 2 only.
`copyright`, `GPL-2.txt` and `SOURCE.json` retain its attribution, complete license,
upstream source and verified hashes. Its preferred source is the SoundFont asset
itself; no proprietary Roland SoundFont is used.

The synthesis implementation is TinySoundFont (MIT) with TinyMidiLoader (Zlib),
pinned under engine/SpaceCadetPinball/third_party/TinySoundFont with source and
licenses included. Original XP game data and MIDI have separate provenance and
rights; this asset license does not grant rights to distribute the original game.

Android and web load `audio/TimGM6mb.sf2` via SDL_RWFromFile before installing the
mixer hook. Desktop keeps its existing SDL_mixer music decoder unless built with
PINBALL_SOFTWARE_MIDI for the standalone integration test. No network, timidity
configuration, instrument-download API, or external MIDI player is required.
