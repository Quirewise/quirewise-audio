# quirewise-audio

Standalone Quirewise audio container and frame parser. CMake target:
`Quirewise::Audio`.

This library is a **packet reader (parser/demuxer)**, not a player, decoder or
transcoder.

## Capabilities

- Opens audio containers from memory and returns bounded audio packets with
  timestamps, durations, discard padding, decoder headers and block additions.
- Parses WAV/RIFX/RF64/BW64/Wave64, AIFF/AIFC, CAF, AU, DSF/DFF/DSS, VOC,
  FLAC, MP1/MP2/MP3, ADTS AAC, AMR, AC-3/E-AC-3, DTS, TTA, TAK, WavPack, APE,
  Musepack, WMA/ASF, M4A/MP4 (progressive and fragmented), Matroska/WebM audio,
  and Ogg (Opus/Vorbis/Speex/FLAC) subsets.
- PCM and other uncompressed containers produce uncompressed sample packets;
  compressed codecs produce compressed packets only.
- Encrypted music containers (NCM, QMC, KGM/KGMA/VPR, old KWM) are detected and
  decrypted to the underlying compressed audio payload; see
  `quirewise/encrypted_music.h`.

## Not included

- Audio decoding to PCM, resampling, channel mapping, sample conversion, audio
  encoding/muxing, and playback.
- Any external codec backend (FFmpeg/libav or similar). Unsupported routes
  return `QW_AUDIO_UNSUPPORTED`; the library never falls back to an external
  demuxer.
- Video parsing or decoding: MP4/Matroska support selects audio tracks only.
- The unified CLI exposes audio only through `--check` (container/packet
  parsing); it has no audio output route.

## Dependencies and tests

- `Quirewise::Compression` (PUBLIC), `Quirewise::Crypto` (PRIVATE); MIT licensed;
  no vendored implementation code.
- `ctest` covers AES known answers, KGM encrypted-music decryption and an audio
  fuzz smoke test; the external consumer template opens `tone.wav` through the
  public reader API.

## Build

```powershell
cmake -S . -B build
cmake --build build
ctest --test-dir build
```
