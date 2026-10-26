#include "audio_custom_asf_input.h"

#include "audio_asf_parser.h"
#include "quirewise/status.h"

int qw_audio_custom_asf_input_parse(const uint8_t *data,
                                    size_t size,
                                    size_t requested_audio_track,
                                    QwAudioCustomInput *input)
{
    int rc;
    if (!data || !size || !input) return QW_STATUS_ERR_INVALID_ARGUMENT;
    rc = qw_audio_asf_parse(data, size, requested_audio_track, &input->asf);
    if (rc == QW_STATUS_ERR_UNSUPPORTED) return 0;
    if (rc != QW_STATUS_OK) return rc;
    input->kind = QW_AUDIO_CUSTOM_INPUT_ASF_WMA;
    input->sample_rate = input->asf.sample_rate;
    input->channels = input->asf.channels;
    input->sample_count = input->asf.sample_count;
    input->has_metadata = 1;
    return 1;
}

int qw_audio_custom_asf_input_next_packet(QwAudioCustomInput *input,
                                          QwAudioCustomPacket *packet)
{
    QwAudioAsfPacket asf_packet;
    int rc;
    if (!input || !packet) return QW_STATUS_ERR_INVALID_ARGUMENT;
    rc = qw_audio_asf_next_packet(&input->asf, &asf_packet);
    if (rc <= 0) return rc;
    packet->data = asf_packet.data;
    packet->size = asf_packet.size;
    packet->no_pts = 1u;
    return 1;
}

void qw_audio_custom_asf_input_dispose(QwAudioCustomInput *input)
{
    if (!input) return;
    qw_audio_asf_dispose(&input->asf);
}
