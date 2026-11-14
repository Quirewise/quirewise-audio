# quirewise-audio usage guide

This guide covers the task-level workflows of `Quirewise::Audio`. The public API
is declared in `quirewise/audio.h` and `quirewise/encrypted_music.h`.

## 1. Build and integrate

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
cmake --install build --prefix /path/to/prefix
```

Consumer CMake:

```cmake
find_package(QuirewiseAudio CONFIG REQUIRED)
target_link_libraries(your_app PRIVATE Quirewise::Audio)
```

`QuirewiseAudio` depends on `QuirewiseRuntime`, `QuirewiseCompression` and
`QuirewiseCrypto`; install all three into the same prefix first. There is no
runtime initialisation.

## 2. Core model and ownership

One reader handles one stream:

1. `qw_audio_reader_open` validates the container header and allocates the
   reader; `qw_audio_reader_get_info` then copies the stream parameters.
2. `qw_audio_reader_next_packet` advances the stream and returns one payload.
3. `qw_audio_reader_close` frees the reader.

Ownership rules:

| Object | Owner | Valid until |
|---|---|---|
| `data` / `size` passed to open | caller | `qw_audio_reader_close` (bytes are borrowed) |
| `packet.data`, `packet.block_additions[]` | reader | the next `qw_audio_reader_next_packet` call or close |
| `QwAudioReader` | caller | released by `qw_audio_reader_close` |
| `QwEncryptedMusicBuffer.data` | caller | released by `qw_encrypted_music_buffer_free` |

The reader borrows the input rather than taking ownership, so the buffer must
stay alive and unchanged while it is open: load the file, open the reader,
consume packets, close the reader, then free the bytes.

`QwAudioInfo` reports the *stream*, not the file: `input_format` echoes the enum
you passed, `codec` is the mapped codec, and `sample_count` is sample frames
when the container declares it (`0` when unknown).

## 3. Recipe: probe a stream

```c
#include <quirewise/audio.h>
#include <stdio.h>

static void probe(const unsigned char *data, size_t size,
                  QwAudioInputFormat format)
{
    QwAudioReader *reader = NULL;
    QwAudioInfo info;

    if (qw_audio_reader_open(data, size, format, 0u, &reader) != QW_AUDIO_OK) {
        fputs("not a readable stream\n", stderr);
        return;
    }
    if (qw_audio_reader_get_info(reader, &info) == QW_AUDIO_OK) {
        printf("codec=%s rate=%u channels=%u mask=0x%08x frames=%llu\n",
               qw_audio_codec_name(info.codec), info.sample_rate,
               info.channels, info.channel_mask,
               (unsigned long long)info.sample_count);
    }
    qw_audio_reader_close(reader);
}
```

`channel_mask` is the container's speaker mask (`0` if it declares none).
`has_metadata` is `1` when the container carried tags or attachments.

## 4. Recipe: stream packets

```c
QwAudioPacket packet;
QwAudioStatus status;

for (;;) {
    status = qw_audio_reader_next_packet(reader, &packet);
    if (status == QW_AUDIO_END) break;
    if (status != QW_AUDIO_OK) {
        fprintf(stderr, "packet error %d\n", (int)status);
        break;
    }
    /* Consume packet.data / packet.size before the next call. */
    if (!packet.no_pts)
        printf("pts=%llu duration=%u size=%zu\n",
               (unsigned long long)packet.pts, packet.duration, packet.size);
    if (packet.decoder_header_size) {
        size_t i;
        for (i = 0u; i < packet.decoder_header_size; ++i)
            printf(" %02x", packet.decoder_header[i]);
    }
    if (packet.discard_padding_ns)
        printf("trim %lld ns\n", (long long)packet.discard_padding_ns);
}
```

Points to keep in mind:

- `QW_AUDIO_END` is a normal termination, not an error.
- Copy anything you need to keep: `packet.data` is only valid until the next
  call, and `packet.block_additions` points into reader-owned storage.
- `decoder_header` is filled by a few parsers only: currently APE (samples and
  skip, 8 bytes) and Musepack (4 bytes). Check `decoder_header_size` first.
- `duration` uses the same units as `pts`, so a decoder can advance its clock
  without decoding.

## 5. Recipe: select a track

M4A/MP4, Matroska and WebM files can carry several audio tracks.
`requested_track` is one-based; `0` means "default track". Formats without
multiple audio tracks ignore the value.

```c
QwAudioReader *reader = NULL;
QwAudioStatus status = qw_audio_reader_open(data, size, QW_AUDIO_INPUT_M4A,
                                            2u, &reader);
if (status == QW_AUDIO_UNSUPPORTED) {
    /* The file may have no second track, or the profile is unsupported. */
    status = qw_audio_reader_open(data, size, QW_AUDIO_INPUT_M4A, 0u, &reader);
}
if (status != QW_AUDIO_OK) return 1;
```

An out-of-range request fails instead of silently selecting another track
(`UNSUPPORTED` for MP4, `INVALID_ARGUMENT` for Matroska).

## 6. Recipe: encrypted music containers

Encrypted music files wrap a normal audio stream. Detect the wrapper, decrypt
it, then feed the decoded payload back into the reader.

```c
#include <quirewise/audio.h>
#include <quirewise/encrypted_music.h>

/* data/size/path/ekey/kgm_key_path come from the application. */
QwEncryptedMusicFormat wrapper = qw_encrypted_music_probe_memory(data, size);
QwEncryptedMusicOptions options = {0};
QwEncryptedMusicBuffer payload = {0};
QwEncryptedMusicStatus status;
QwAudioReader *reader = NULL;
char error[128];

if (wrapper == QW_ENCRYPTED_MUSIC_UNKNOWN)
    wrapper = qw_encrypted_music_format_from_path(path); /* KWM needs the path */
if (wrapper == QW_ENCRYPTED_MUSIC_UNKNOWN) return 1;
options.encryption_key = ekey;        /* QMC STag/MusicEx and KGM v5 */
options.kgm_key_path = kgm_key_path;  /* KGM v1/v2 compressed key file */
status = qw_encrypted_music_decrypt_buffer(wrapper, data, size, &options,
                                           &payload, error, sizeof error);
if (status != QW_ENCRYPTED_MUSIC_OK) {
    fprintf(stderr, "decrypt failed: %s\n",
            qw_encrypted_music_status_string(status));
    return 1;
}

/* The payload is a plain container; its type still has to be known. */
if (qw_audio_reader_open(payload.data, payload.size, QW_AUDIO_INPUT_FLAC, 0u,
                         &reader) == QW_AUDIO_OK) {
    /* ... consume packets ... */
    qw_audio_reader_close(reader);
}
qw_encrypted_music_buffer_free(&payload);
```

Notes:

- `qw_encrypted_music_probe_memory` recognizes NCM, QMC and KGM from their
  bytes. KWM has no reliable content signature, so `..._format_from_path` is the
  only way to classify it; path detection also understands the many QMC and
  KGM/KGMA/VPR suffixes.
- `options` may be `NULL` when no key material is needed (NCM, KWM, and QMC
  files whose footer contains an embedded key).
- QMC `STag`/`MusicEx` and KGM v5 need `encryption_key`; KGM v1/v2 read a
  compressed Kugou key resource from `kgm_key_path`.
- `error` is only written on failure and may be truncated to its capacity;
  `qw_encrypted_music_status_string` gives the stable short description.
- The payload is verified to be a recognized audio stream before it is
  returned; a wrong key yields `QW_ENCRYPTED_MUSIC_DECODE_ERROR`.

## 7. Status handling

| Status | Value | Meaning |
|---|---|---|
| `QW_AUDIO_OK` | 0 | packet returned / call succeeded |
| `QW_AUDIO_INVALID_ARGUMENT` | 1 | null pointer, empty input, bad enum |
| `QW_AUDIO_UNSUPPORTED` | 2 | valid but unsupported profile or track |
| `QW_AUDIO_FORMAT_ERROR` | 3 | malformed or truncated container |
| `QW_AUDIO_OUT_OF_MEMORY` | 4 | allocation failed |
| `QW_AUDIO_RESOURCE_LIMIT` | 5 | an internal cap was exceeded |
| `QW_AUDIO_INTERNAL_ERROR` | 6 | unexpected internal failure |
| `QW_AUDIO_END` | 7 | clean end of stream |

Encrypted-music calls use a separate enum: `QW_ENCRYPTED_MUSIC_OK` is `0` and
failures are negative (`-1` through `-6`, with `..._DECODE_ERROR` `-5` meaning
the payload did not decrypt to audio). Do not mix the two enums.

Treat only `QW_AUDIO_END` as success after the loop; treat `UNSUPPORTED` as
"this file needs a different tool", not as a retryable error.

## 8. Memory, lifetime and threading

- No global mutable state: the static tables are `const` and
  `qw_audio_codec_name` returns string literals. Distinct readers are
  independent and may run concurrently, but one reader must not be used from
  two threads at once.
- Some containers allocate internally (Matroska packet buffers, DSF packet
  data); `qw_audio_reader_close` releases them even after a failed call.
- `qw_encrypted_music_buffer_free` tolerates `NULL` and a zeroed struct.

## 9. Limits and format notes

- Input must be complete in memory; there is no streaming or seek API. Empty
  input and `NULL` data return `QW_AUDIO_INVALID_ARGUMENT`; truncated
  containers usually return `QW_AUDIO_FORMAT_ERROR`.
- Payloads are bounded by the format's block or frame size; oversized tables
  and headers are rejected with `QW_AUDIO_FORMAT_ERROR` or
  `QW_AUDIO_RESOURCE_LIMIT` instead of unbounded allocation.
- Matroska `BlockAdditional` payloads are exposed up to 16 entries per packet;
  the count is clamped, so `block_addition_count` never exceeds 16.
- Out-of-scope profiles return `QW_AUDIO_UNSUPPORTED`: uncommon MP4 edit
  lists, unsupported Matroska codecs, encrypted Matroska, video-only files.
- PCM and other uncompressed containers return raw sample bytes; no sample
  format conversion, byte swapping or channel reordering is performed.

## 10. Related packages

- `Quirewise::Runtime` supplies the shared `QwStatus`/`QwFormatId` vocabulary;
  `Quirewise::Crypto` provides the AES and TEA primitives the decryptors use.
- `Quirewise::Compression` decodes the XZ-compressed Kugou key resource.
- `Quirewise::Document` leaves audio inputs to this package: its `qw_check_file`
  does not verify audio itself and expects the application to scan packets here.
- `Quirewise::Pipeline` and the application layer choose the `QwAudioInputFormat`
  from the file name or a probe before calling the reader.

## 11. License

MIT. See `LICENSE`.
