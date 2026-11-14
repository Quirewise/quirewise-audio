# quirewise-audio

In-memory audio container reader: opens a container, reports its stream
parameters and returns bounded compressed packets. Not a decoder or a player.

`Quirewise::Audio` is an independent, MIT-licensed C11 library, installed as
the `QuirewiseAudio` package (target `Quirewise::Audio`).

## What it does

- Opens audio containers from caller-owned memory and reports format, codec,
  sample rate, channels, channel mask, sample count and metadata presence.
- Returns packets one at a time with payload bytes, size, timestamp, duration,
  discard padding and an optional small decoder header.
- Parses WAV (RIFF/RIFX/RF64/BW64), Wave64, AIFF/AIFC, CAF, AU, DSF, DFF, DSS,
  VOC, FLAC, MP2, MP3, ADTS AAC, AMR, AC-3/E-AC-3, DTS, TTA, TAK, WavPack, APE,
  Musepack, WMA/ASF, M4A/MP4 (progressive and fragmented), Matroska/WebM audio
  tracks, and Ogg streams carrying Opus, Vorbis, Speex or FLAC.
- Selects an audio track in M4A/MP4, Matroska and WebM: `requested_track` is
  one-based and `0` selects the default track.
- Decrypts encrypted music containers (NCM, QMC, KGM/KGMA/VPR, KWM) to the
  compressed audio payload they wrap; see `quirewise/encrypted_music.h`.

**It is not** a decoder, resampler, sample converter, encoder, muxer or player.
Compressed codecs produce compressed packets; nothing here turns them into PCM,
video tracks are ignored, and there is no external codec backend or demuxer
fallback; an unsupported profile returns `QW_AUDIO_UNSUPPORTED`.

## Requirements

- C11 compiler and CMake 3.21 or newer.
- `Quirewise::Runtime` and `Quirewise::Compression` are public dependencies;
  `Quirewise::Crypto` is linked privately for encrypted-music decryption.
- Install all three in the same prefix (`find_dependency` is used); CTest runs
  the test suite.

## Build, test and install

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
cmake --install build --prefix /path/to/prefix
```

Shared builds: `-DQUIREWISE_BUILD_SHARED=ON`. For a non-default prefix pass
`-DCMAKE_PREFIX_PATH=<prefix>` when configuring your project.

## Use it from CMake

```cmake
find_package(QuirewiseAudio CONFIG REQUIRED)
target_link_libraries(your_app PRIVATE Quirewise::Audio)
```

## Quick start

Read a WAV file into memory, print its parameters and count its packets. The
format enum is explicit; there is no auto-detection:

```c
#include <quirewise/audio.h>

#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
    FILE *file;
    long length;
    unsigned char *bytes;
    QwAudioReader *reader = NULL;
    QwAudioInfo info;
    QwAudioPacket packet;
    QwAudioStatus status;
    size_t packets = 0u;

    if (argc != 2) { fputs("usage: probe <file.wav>\n", stderr); return 1; }
    file = fopen(argv[1], "rb");
    if (!file) { perror("fopen"); return 1; }
    fseek(file, 0L, SEEK_END); length = ftell(file); rewind(file);
    bytes = length > 0L ? malloc((size_t)length) : NULL;
    if (!bytes || fread(bytes, 1u, (size_t)length, file) != (size_t)length) {
        free(bytes);
        fclose(file);
        return 1;
    }
    fclose(file); /* bytes stay valid until the reader is closed */

    status = qw_audio_reader_open(bytes, (size_t)length, QW_AUDIO_INPUT_WAV, 0u,
                                  &reader);
    if (status != QW_AUDIO_OK) {
        fprintf(stderr, "open failed: %d\n", (int)status);
        free(bytes);
        return 1;
    }
    if (qw_audio_reader_get_info(reader, &info) != QW_AUDIO_OK) {
        qw_audio_reader_close(reader);
        free(bytes);
        return 1;
    }
    printf("%s, %u Hz, %u channel(s), %llu samples\n",
           qw_audio_codec_name(info.codec), info.sample_rate, info.channels,
           (unsigned long long)info.sample_count);

    while ((status = qw_audio_reader_next_packet(reader, &packet)) ==
           QW_AUDIO_OK) {
        ++packets;
    }
    if (status != QW_AUDIO_END) fprintf(stderr, "read stopped: %d\n", (int)status);
    printf("%zu packet(s)\n", packets);

    qw_audio_reader_close(reader);
    free(bytes); /* closing the reader ends the borrow */
    return 0;
}
```

## API overview

| Header | Purpose |
|---|---|
| `quirewise/audio.h` | packet reader, format and codec enums, info and packet structs |
| `quirewise/encrypted_music.h` | encrypted-container probe, path detection, buffer decryption |

| Task | Functions |
|---|---|
| Open / close | `qw_audio_reader_open`, `qw_audio_reader_close` |
| Stream parameters | `qw_audio_reader_get_info` |
| Packets | `qw_audio_reader_next_packet` |
| Codec names | `qw_audio_codec_name` |
| Encrypted music | `qw_encrypted_music_probe_memory`, `qw_encrypted_music_format_from_path`, `qw_encrypted_music_decrypt_buffer`, `qw_encrypted_music_buffer_free`, `qw_encrypted_music_status_string`, `qw_encrypted_music_abi_version` |

## Behavior and limits

- Input bytes are **borrowed** until `qw_audio_reader_close`; packet payloads and
  block additions are borrowed until the next call or close. The caller owns and
  frees every buffer it passed in.
- `qw_audio_reader_next_packet` returns `QW_AUDIO_OK`, `QW_AUDIO_END` at the
  clean end, or another status that must be treated as a failure.
- `pts` and `duration` use the units supplied by the parser; for sample-oriented
  parsers they are sample positions, and MP4/M4A rejects tracks whose media
  timescale differs from the sample rate, so its `pts` is also in samples. When
  a parser cannot produce a timestamp it sets `no_pts` and `pts` is meaningless.
- `discard_padding_ns` is filled only where the container carries it (Matroska).
  `decoder_header` holds up to eight configuration bytes; `decoder_header_size`
  says how many are valid.
- PCM sources produce uncompressed sample packets unchanged; compressed codecs
  produce compressed packets only. Matroska block additions are capped at 16.
- Malformed or truncated containers return `QW_AUDIO_FORMAT_ERROR`, unsupported
  variants `QW_AUDIO_UNSUPPORTED`, and enforced caps `QW_AUDIO_RESOURCE_LIMIT`.
- Encrypted music decryption allocates its output (free with
  `qw_encrypted_music_buffer_free`). QMC `STag`/`MusicEx` and KGM v5 need an
  explicit EKey; KGM v1/v2 need `kgm_key_path`; NCM and KWM use embedded keys.

## Testing

`ctest` covers AES known-answer vectors, KGM decryption (valid, corrupted,
truncated and missing keys), a WAV PCM round trip, a fuzz smoke run, and the
external consumer template's `tone.wav` probe through the installed API.

## Documentation

See [docs/usage.md](docs/usage.md) for recipes, streaming, track selection,
encrypted-music workflows and failure handling.

## License

MIT. See [LICENSE](LICENSE).
