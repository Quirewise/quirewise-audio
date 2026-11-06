#include <quirewise/audio.h>
#include <quirewise/encrypted_music.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define FUZZ_SEED UINT32_C(0x51f0aa66)
#define FUZZ_RUNS 200u

static uint32_t rng_state = FUZZ_SEED;

static const uint8_t wav_seed[] = {
    0x52, 0x49, 0x46, 0x46, 0x26, 0x00, 0x00, 0x00, 0x57, 0x41, 0x56, 0x45,
    0x66, 0x6d, 0x74, 0x20, 0x10, 0x00, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00,
    0x40, 0x1f, 0x00, 0x00, 0x40, 0x1f, 0x00, 0x00, 0x01, 0x00, 0x08, 0x00,
    0x64, 0x61, 0x74, 0x61, 0x01, 0x00, 0x00, 0x00, 0x80, 0x00
};
static const uint8_t kgm_magic[16] = {
    0x7c, 0xd5, 0x32, 0xeb, 0x86, 0x02, 0x7f, 0x4b,
    0xa8, 0xaf, 0xa6, 0x8e, 0x0f, 0xff, 0x99, 0x14
};

static uint32_t next_random(void)
{
    uint32_t value = rng_state;
    value ^= value << 13u;
    value ^= value >> 17u;
    value ^= value << 5u;
    rng_state = value;
    return value;
}

static size_t mutate(const uint8_t *seed, size_t seed_size, uint8_t *buffer,
                     size_t capacity)
{
    size_t length = seed_size > capacity ? capacity : seed_size;
    unsigned int flips = 1u + next_random() % 4u;
    size_t index;
    memcpy(buffer, seed, length);
    if (next_random() % 8u == 0u && length > 1u)
        length = 1u + next_random() % length;
    for (index = 0u; index < flips && length; ++index) {
        size_t position = next_random() % length;
        buffer[position] = (uint8_t)(buffer[position] ^
                                     (uint8_t)(1u << (next_random() % 8u)));
    }
    return length;
}

int main(void)
{
    uint8_t buffer[4096];
    size_t run;
    size_t ok = 0u;
    size_t rejected = 0u;
    {
        QwAudioReader *plain = NULL;
        if (qw_audio_reader_open(wav_seed, sizeof(wav_seed), QW_AUDIO_INPUT_WAV,
                                 0u, &plain) == QW_AUDIO_OK && plain) {
            ++ok;
            qw_audio_reader_close(plain);
        }
    }
    for (run = 0u; run < FUZZ_RUNS; ++run) {
        QwAudioReader *reader = NULL;
        size_t size = mutate(wav_seed, sizeof(wav_seed), buffer, sizeof(buffer));
        if (qw_audio_reader_open(buffer, size, QW_AUDIO_INPUT_WAV, 0u, &reader) ==
                QW_AUDIO_OK &&
            reader) {
            QwAudioInfo info = {0};
            QwAudioPacket packet;
            ++ok;
            (void)qw_audio_reader_get_info(reader, &info);
            (void)qw_audio_reader_next_packet(reader, &packet);
            qw_audio_reader_close(reader);
        } else {
            ++rejected;
        }
    }
    for (run = 0u; run < FUZZ_RUNS; ++run) {
        size_t size = 0u;
        QwEncryptedMusicBuffer output = {0};
        char error[64];
        size_t index;
        memcpy(buffer, kgm_magic, sizeof(kgm_magic));
        size = sizeof(kgm_magic) + (size_t)(next_random() % 512u);
        for (index = sizeof(kgm_magic); index < size; ++index)
            buffer[index] = (uint8_t)next_random();
        if (size > sizeof(kgm_magic)) {
            size_t position = sizeof(kgm_magic) + next_random() % (size - sizeof(kgm_magic));
            buffer[position] ^= (uint8_t)(1u << (next_random() % 8u));
        }
        if (qw_encrypted_music_probe_memory(buffer, size) ==
            QW_ENCRYPTED_MUSIC_UNKNOWN)
            ++ok;
        else
            ++rejected;
        if (qw_encrypted_music_decrypt_buffer(QW_ENCRYPTED_MUSIC_KGM, buffer,
                                              size, NULL, &output, error,
                                              sizeof(error)) ==
            QW_ENCRYPTED_MUSIC_OK) {
            qw_encrypted_music_buffer_free(&output);
        }
    }
    printf("Audio fuzz smoke: %zu ok, %zu rejected, seed=0x%08x runs=%u\n", ok,
           rejected, FUZZ_SEED, FUZZ_RUNS);
    return 0;
}
