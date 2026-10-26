#include "audio_custom_mp4_input.h"

#include "quirewise/status.h"

int qw_audio_custom_mp4_input_parse(const uint8_t *data,
                                    size_t size,
                                    size_t requested_audio_track,
                                    QwAudioCustomInput *input)
{
    int rc;
    if (!data || !size || !input) return QW_STATUS_ERR_INVALID_ARGUMENT;
    rc = qw_audio_mp4_parse(data, size, requested_audio_track,
                            &input->mp4, NULL);
    if (rc == QW_STATUS_ERR_UNSUPPORTED) return 0;
    if (rc != QW_STATUS_OK) return rc;
    input->kind = QW_AUDIO_CUSTOM_INPUT_MP4;
    input->sample_rate = input->mp4.sample_rate;
    input->channels = input->mp4.channels;
    input->sample_count = input->mp4.presentation_sample_count;
    input->has_metadata = input->mp4.has_metadata;
    return 1;
}

int qw_audio_custom_mp4_input_next_packet(QwAudioCustomInput *input,
                                          QwAudioCustomPacket *packet)
{
    QwAudioMp4Sample sample;
    int rc;
    if (!input || !packet || input->kind != QW_AUDIO_CUSTOM_INPUT_MP4) {
        return QW_STATUS_ERR_INVALID_ARGUMENT;
    }
    rc = qw_audio_mp4_next_sample(&input->mp4, &input->mp4_cursor, &sample);
    if (rc <= 0) return rc;
    if (sample.pts > UINT64_MAX - sample.duration ||
        input->mp4_cursor.next_pts > input->mp4.decoded_sample_count) {
        return QW_STATUS_ERR_FORMAT;
    }
    packet->data = sample.data;
    packet->size = sample.size;
    packet->pts = sample.pts;
    packet->duration = sample.duration;
    packet->no_pts = sample.no_pts;
    return 1;
}
