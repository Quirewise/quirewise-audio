/*
 * WAV PCM smoke: the packet reader must report the stream parameters and
 * return the uncompressed sample bytes unchanged, then end cleanly.
 */

#include <quirewise/audio.h>

#include <stdio.h>
#include <string.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "FAIL: %s at line %d\n", #condition, __LINE__); \
        return 1; \
    } \
} while (0)

static void put_le16(unsigned char *p, unsigned value)
{
    p[0] = (unsigned char)value;
    p[1] = (unsigned char)(value >> 8);
}

static void put_le32(unsigned char *p, unsigned long value)
{
    p[0] = (unsigned char)value;
    p[1] = (unsigned char)(value >> 8);
    p[2] = (unsigned char)(value >> 16);
    p[3] = (unsigned char)(value >> 24);
}

int main(void)
{
    unsigned char wav[44 + 32];
    unsigned char samples[32];
    QwAudioReader *reader = NULL;
    QwAudioInfo info;
    QwAudioPacket packet;
    QwAudioStatus status;
    size_t index;

    for (index = 0u; index < sizeof(samples); ++index)
        samples[index] = (unsigned char)(index * 7u);

    memset(wav, 0, sizeof(wav));
    memcpy(wav, "RIFF", 4u);
    put_le32(wav + 4, (unsigned long)(sizeof(wav) - 8u));
    memcpy(wav + 8, "WAVE", 4u);
    memcpy(wav + 12, "fmt ", 4u);
    put_le32(wav + 16, 16u);
    put_le16(wav + 20, 1u);      /* PCM */
    put_le16(wav + 22, 1u);      /* mono */
    put_le32(wav + 24, 8000u);   /* sample rate */
    put_le32(wav + 28, 16000u);  /* byte rate */
    put_le16(wav + 32, 2u);      /* block align */
    put_le16(wav + 34, 16u);     /* bits per sample */
    memcpy(wav + 36, "data", 4u);
    put_le32(wav + 40, (unsigned long)sizeof(samples));
    memcpy(wav + 44, samples, sizeof(samples));

    status = qw_audio_reader_open(wav, sizeof(wav), QW_AUDIO_INPUT_WAV, 0u,
                                  &reader);
    CHECK(status == QW_AUDIO_OK);
    CHECK(reader != NULL);

    memset(&info, 0, sizeof(info));
    CHECK(qw_audio_reader_get_info(reader, &info) == QW_AUDIO_OK);
    CHECK(info.sample_rate == 8000u);
    CHECK(info.channels == 1u);
    CHECK(info.sample_count == 16u);
    CHECK(qw_audio_codec_name(info.codec) != NULL);

    memset(&packet, 0, sizeof(packet));
    status = qw_audio_reader_next_packet(reader, &packet);
    if (status != QW_AUDIO_OK) {
        fprintf(stderr, "next_packet status %d size %zu\n", (int)status,
                packet.size);
    }
    CHECK(status == QW_AUDIO_OK);
    CHECK(packet.data != NULL);
    CHECK(packet.size == sizeof(samples));
    CHECK(memcmp(packet.data, samples, sizeof(samples)) == 0);

    memset(&packet, 0, sizeof(packet));
    status = qw_audio_reader_next_packet(reader, &packet);
    CHECK(status == QW_AUDIO_END);

    qw_audio_reader_close(reader);

    /* A truncated stream is rejected with a status, not a crash. */
    reader = NULL;
    CHECK(qw_audio_reader_open(wav, 30u, QW_AUDIO_INPUT_WAV, 0u, &reader) !=
          QW_AUDIO_OK);
    return 0;
}
