#include "audio_custom_caf_input.h"

#include "audio_custom_input_packets.h"
#include "quirewise/status.h"

static int audio_custom_caf_set_ima4(const uint8_t *data,
                                     size_t size,
                                     QwAudioCustomInput *input)
{
    int rc = qw_audio_caf_ima4_parse(data, size, &input->caf_ima4, NULL);
    if (rc == QW_STATUS_ERR_UNSUPPORTED) return 0;
    if (rc != QW_STATUS_OK) return rc;
    input->kind = QW_AUDIO_CUSTOM_INPUT_CAF_IMA4;
    input->sample_rate = input->caf_ima4.sample_rate;
    input->channels = input->caf_ima4.channels;
    input->sample_count = input->caf_ima4.sample_count;
    input->has_metadata = input->caf_ima4.has_metadata;
    return 1;
}

static int audio_custom_caf_set_g711(const uint8_t *data,
                                     size_t size,
                                     QwAudioCustomInput *input)
{
    int rc = qw_audio_caf_g711_parse(data, size, &input->caf_g711, NULL);
    if (rc == QW_STATUS_ERR_UNSUPPORTED) return 0;
    if (rc != QW_STATUS_OK) return rc;
    input->kind = QW_AUDIO_CUSTOM_INPUT_CAF_G711;
    input->sample_rate = input->caf_g711.sample_rate;
    input->channels = input->caf_g711.channels;
    input->sample_count = input->caf_g711.sample_count;
    input->has_metadata = input->caf_g711.has_metadata;
    return 1;
}

static int audio_custom_caf_set_aac(const uint8_t *data,
                                    size_t size,
                                    QwAudioCustomInput *input)
{
    int rc = qw_audio_caf_aac_parse(data, size, &input->caf_aac, NULL);
    if (rc == QW_STATUS_ERR_UNSUPPORTED) return 0;
    if (rc != QW_STATUS_OK) return rc;
    input->kind = QW_AUDIO_CUSTOM_INPUT_CAF_AAC;
    input->sample_rate = input->caf_aac.sample_rate;
    input->channels = input->caf_aac.channels;
    input->sample_count = input->caf_aac.sample_count;
    input->has_metadata = input->caf_aac.has_metadata;
    return 1;
}

static int audio_custom_caf_set_amr(const uint8_t *data,
                                    size_t size,
                                    QwAudioCustomInput *input)
{
    int rc = qw_audio_caf_amr_parse(data, size, &input->caf_amr, NULL);
    if (rc == QW_STATUS_ERR_UNSUPPORTED) return 0;
    if (rc != QW_STATUS_OK) return rc;
    input->kind = QW_AUDIO_CUSTOM_INPUT_CAF_AMR;
    input->sample_rate = 8000u;
    input->channels = 1u;
    input->sample_count = input->caf_amr.sample_count;
    input->has_metadata = input->caf_amr.has_metadata;
    return 1;
}

static int audio_custom_caf_set_ac3(const uint8_t *data,
                                    size_t size,
                                    QwAudioCustomInput *input)
{
    int rc = qw_audio_caf_ac3_parse(data, size, &input->caf_ac3, NULL);
    if (rc == QW_STATUS_ERR_UNSUPPORTED) return 0;
    if (rc != QW_STATUS_OK) return rc;
    input->kind = QW_AUDIO_CUSTOM_INPUT_CAF_AC3;
    input->sample_rate = input->caf_ac3.sample_rate;
    input->channels = input->caf_ac3.channels;
    input->sample_count = input->caf_ac3.sample_count;
    input->has_metadata = input->caf_ac3.has_metadata;
    return 1;
}

static int audio_custom_caf_set_qdesign(const uint8_t *data,
                                        size_t size,
                                        QwAudioCustomInput *input)
{
    int rc = qw_audio_caf_qdesign_parse(data, size,
                                        &input->caf_qdesign, NULL);
    if (rc == QW_STATUS_ERR_UNSUPPORTED) return 0;
    if (rc != QW_STATUS_OK) return rc;
    input->kind = QW_AUDIO_CUSTOM_INPUT_CAF_QDESIGN;
    input->sample_rate = input->caf_qdesign.sample_rate;
    input->channels = input->caf_qdesign.channels;
    input->sample_count = input->caf_qdesign.sample_count;
    input->has_metadata = input->caf_qdesign.has_metadata;
    return 1;
}

static int audio_custom_caf_set_qcelp(const uint8_t *data,
                                      size_t size,
                                      QwAudioCustomInput *input)
{
    int rc = qw_audio_caf_qcelp_parse(data, size,
                                      &input->caf_qcelp, NULL);
    if (rc == QW_STATUS_ERR_UNSUPPORTED) return 0;
    if (rc != QW_STATUS_OK) return rc;
    input->kind = QW_AUDIO_CUSTOM_INPUT_CAF_QCELP;
    input->sample_rate = 8000u;
    input->channels = 1u;
    input->sample_count = input->caf_qcelp.sample_count;
    input->has_metadata = input->caf_qcelp.has_metadata;
    return 1;
}

static int audio_custom_caf_set_mpeg(const uint8_t *data,
                                     size_t size,
                                     QwAudioCustomInput *input)
{
    int rc = qw_audio_caf_mpeg_parse(data, size, &input->caf_mpeg, NULL);
    if (rc == QW_STATUS_ERR_UNSUPPORTED) return 0;
    if (rc != QW_STATUS_OK) return rc;
    input->kind = QW_AUDIO_CUSTOM_INPUT_CAF_MPEG;
    input->sample_rate = input->caf_mpeg.sample_rate;
    input->channels = input->caf_mpeg.channels;
    input->sample_count = input->caf_mpeg.sample_count;
    input->has_metadata = input->caf_mpeg.has_metadata;
    return 1;
}

static int audio_custom_caf_set_mace(const uint8_t *data,
                                     size_t size,
                                     QwAudioCustomInput *input)
{
    int rc = qw_audio_caf_mace_parse(data, size, &input->caf_mace, NULL);
    if (rc == QW_STATUS_ERR_UNSUPPORTED) return 0;
    if (rc != QW_STATUS_OK) return rc;
    input->kind = QW_AUDIO_CUSTOM_INPUT_CAF_MACE;
    input->sample_rate = input->caf_mace.sample_rate;
    input->channels = input->caf_mace.channels;
    input->sample_count = input->caf_mace.sample_count;
    input->has_metadata = input->caf_mace.has_metadata;
    return 1;
}

static int audio_custom_caf_set_gsm(const uint8_t *data,
                                    size_t size,
                                    QwAudioCustomInput *input)
{
    int rc = qw_audio_caf_gsm_parse(data, size, &input->caf_gsm, NULL);
    if (rc == QW_STATUS_ERR_UNSUPPORTED) return 0;
    if (rc != QW_STATUS_OK) return rc;
    input->kind = QW_AUDIO_CUSTOM_INPUT_CAF_GSM;
    input->sample_rate = input->caf_gsm.sample_rate;
    input->channels = input->caf_gsm.channels;
    input->sample_count = input->caf_gsm.sample_count;
    input->has_metadata = input->caf_gsm.has_metadata;
    return 1;
}

static int audio_custom_caf_set_ilbc(const uint8_t *data,
                                     size_t size,
                                     QwAudioCustomInput *input)
{
    int rc = qw_audio_caf_ilbc_parse(data, size, &input->caf_ilbc, NULL);
    if (rc == QW_STATUS_ERR_UNSUPPORTED) return 0;
    if (rc != QW_STATUS_OK) return rc;
    input->kind = QW_AUDIO_CUSTOM_INPUT_CAF_ILBC;
    input->sample_rate = input->caf_ilbc.sample_rate;
    input->channels = input->caf_ilbc.channels;
    input->sample_count = input->caf_ilbc.sample_count;
    input->has_metadata = input->caf_ilbc.has_metadata;
    return 1;
}

static int audio_custom_caf_set_alac(const uint8_t *data,
                                     size_t size,
                                     QwAudioCustomInput *input)
{
    int rc = qw_audio_caf_alac_parse(data, size, &input->caf_alac, NULL);
    if (rc == QW_STATUS_ERR_UNSUPPORTED) return 0;
    if (rc != QW_STATUS_OK) return rc;
    input->kind = QW_AUDIO_CUSTOM_INPUT_CAF_ALAC;
    input->sample_rate = input->caf_alac.sample_rate;
    input->channels = input->caf_alac.channels;
    input->sample_count = input->caf_alac.sample_count;
    input->has_metadata = input->caf_alac.has_metadata;
    return 1;
}

int qw_audio_custom_caf_input_parse(const uint8_t *data,
                                    size_t size,
                                    QwAudioCustomInput *input)
{
    int rc;
    if (!data || !size || !input) return QW_STATUS_ERR_INVALID_ARGUMENT;

    rc = audio_custom_caf_set_ima4(data, size, input);
    if (rc != 0) return rc;
    rc = audio_custom_caf_set_g711(data, size, input);
    if (rc != 0) return rc;
    rc = audio_custom_caf_set_aac(data, size, input);
    if (rc != 0) return rc;
    rc = audio_custom_caf_set_amr(data, size, input);
    if (rc != 0) return rc;
    rc = audio_custom_caf_set_ac3(data, size, input);
    if (rc != 0) return rc;
    rc = audio_custom_caf_set_qdesign(data, size, input);
    if (rc != 0) return rc;
    rc = audio_custom_caf_set_qcelp(data, size, input);
    if (rc != 0) return rc;
    rc = audio_custom_caf_set_mpeg(data, size, input);
    if (rc != 0) return rc;
    rc = audio_custom_caf_set_mace(data, size, input);
    if (rc != 0) return rc;
    rc = audio_custom_caf_set_gsm(data, size, input);
    if (rc != 0) return rc;
    rc = audio_custom_caf_set_ilbc(data, size, input);
    if (rc != 0) return rc;
    return audio_custom_caf_set_alac(data, size, input);
}

static int audio_custom_caf_next_ima4_packet(QwAudioCustomInput *input,
                                              QwAudioCustomPacket *packet)
{
    QwAudioCafIma4Frame frame;
    int rc = qw_audio_caf_ima4_next_frame(&input->caf_ima4,
                                          input->packet_index, &frame);
    if (rc <= 0) return rc < 0 ? rc : 0;
    packet->data = frame.data;
    packet->size = frame.size;
    packet->pts = input->packet_sample_position;
    packet->duration = frame.sample_count;
    input->packet_index = frame.next_packet_index;
    input->packet_sample_position += frame.sample_count;
    return 1;
}

static int audio_custom_caf_next_aac_packet(QwAudioCustomInput *input,
                                             QwAudioCustomPacket *packet)
{
    QwAudioCafAacFrame frame;
    int rc = qw_audio_caf_aac_next_frame(
        &input->caf_aac, input->packet_index, input->packet_position,
        input->packet_size_table_position, &frame);
    if (rc <= 0) return rc < 0 ? rc : 0;
    if ((uint64_t)frame.sample_count >
        UINT64_MAX - input->packet_sample_position) {
        return QW_STATUS_ERR_FORMAT;
    }
    packet->data = frame.data;
    packet->size = frame.size;
    packet->pts = input->packet_sample_position;
    packet->duration = frame.sample_count;
    ++input->packet_index;
    input->packet_position = frame.next_data_offset;
    input->packet_size_table_position = frame.next_packet_size_offset;
    input->packet_sample_position += frame.sample_count;
    return 1;
}

static int audio_custom_caf_next_amr_packet(QwAudioCustomInput *input,
                                             QwAudioCustomPacket *packet)
{
    QwAudioCafAmrFrame frame;
    int rc = qw_audio_caf_amr_next_frame(
        &input->caf_amr, input->packet_index, input->packet_position,
        input->packet_size_table_position, &frame);
    if (rc <= 0) return rc < 0 ? rc : 0;
    if ((uint64_t)frame.sample_count >
        UINT64_MAX - input->packet_sample_position) {
        return QW_STATUS_ERR_FORMAT;
    }
    packet->data = frame.data;
    packet->size = frame.size;
    packet->pts = input->packet_sample_position;
    packet->duration = frame.sample_count;
    ++input->packet_index;
    input->packet_position = frame.next_data_offset;
    input->packet_size_table_position = frame.next_packet_size_offset;
    input->packet_sample_position += frame.sample_count;
    return 1;
}

static int audio_custom_caf_next_ac3_packet(QwAudioCustomInput *input,
                                             QwAudioCustomPacket *packet)
{
    QwAudioCafAc3Frame frame;
    int rc = qw_audio_caf_ac3_next_frame(
        &input->caf_ac3, input->packet_index, input->packet_position,
        input->packet_size_table_position, &frame);
    if (rc <= 0) return rc < 0 ? rc : 0;
    if ((uint64_t)frame.sample_count >
        UINT64_MAX - input->packet_sample_position) {
        return QW_STATUS_ERR_FORMAT;
    }
    packet->data = frame.data;
    packet->size = frame.size;
    packet->pts = input->packet_sample_position;
    packet->duration = frame.sample_count;
    ++input->packet_index;
    input->packet_position = frame.next_data_offset;
    input->packet_size_table_position = frame.next_packet_size_offset;
    input->packet_sample_position += frame.sample_count;
    return 1;
}

static int audio_custom_caf_next_qdesign_packet(
    QwAudioCustomInput *input,
    QwAudioCustomPacket *packet)
{
    QwAudioCafQdesignFrame frame;
    int rc = qw_audio_caf_qdesign_next_frame(
        &input->caf_qdesign, input->packet_index, &frame);
    if (rc <= 0) return rc < 0 ? rc : 0;
    if ((uint64_t)frame.sample_count >
        UINT64_MAX - input->packet_sample_position) {
        return QW_STATUS_ERR_FORMAT;
    }
    packet->data = frame.data;
    packet->size = frame.size;
    packet->pts = input->packet_sample_position;
    packet->duration = frame.sample_count;
    input->packet_index = frame.next_packet_index;
    input->packet_sample_position += frame.sample_count;
    return 1;
}

static int audio_custom_caf_next_qcelp_packet(
    QwAudioCustomInput *input,
    QwAudioCustomPacket *packet)
{
    QwAudioCafQcelpFrame frame;
    int rc = qw_audio_caf_qcelp_next_frame(
        &input->caf_qcelp, input->packet_index, input->packet_position,
        input->packet_size_table_position, &frame);
    if (rc <= 0) return rc < 0 ? rc : 0;
    if ((uint64_t)frame.sample_count >
        UINT64_MAX - input->packet_sample_position) {
        return QW_STATUS_ERR_FORMAT;
    }
    packet->data = frame.data;
    packet->size = frame.size;
    packet->pts = input->packet_sample_position;
    packet->duration = frame.sample_count;
    ++input->packet_index;
    input->packet_position = frame.next_data_offset;
    input->packet_size_table_position = frame.next_packet_size_offset;
    input->packet_sample_position += frame.sample_count;
    return 1;
}

static int audio_custom_caf_next_mpeg_packet(QwAudioCustomInput *input,
                                              QwAudioCustomPacket *packet)
{
    QwAudioCafMpegFrame frame;
    int rc = qw_audio_caf_mpeg_next_frame(
        &input->caf_mpeg, input->packet_index, input->packet_position,
        input->packet_size_table_position, &frame);
    if (rc <= 0) return rc < 0 ? rc : 0;
    if ((uint64_t)frame.sample_count >
        UINT64_MAX - input->packet_sample_position) {
        return QW_STATUS_ERR_FORMAT;
    }
    packet->data = frame.data;
    packet->size = frame.size;
    packet->pts = input->packet_sample_position;
    packet->duration = frame.sample_count;
    ++input->packet_index;
    input->packet_position = frame.next_data_offset;
    input->packet_size_table_position = frame.next_packet_size_offset;
    input->packet_sample_position += frame.sample_count;
    return 1;
}

static int audio_custom_caf_next_mace_packet(QwAudioCustomInput *input,
                                              QwAudioCustomPacket *packet)
{
    QwAudioCafMaceFrame frame;
    int rc = qw_audio_caf_mace_next_frame(
        &input->caf_mace, input->packet_index, &frame);
    if (rc <= 0) return rc < 0 ? rc : 0;
    if ((uint64_t)frame.sample_count >
        UINT64_MAX - input->packet_sample_position) {
        return QW_STATUS_ERR_FORMAT;
    }
    packet->data = frame.data;
    packet->size = frame.size;
    packet->pts = input->packet_sample_position;
    packet->duration = frame.sample_count;
    input->packet_index = frame.next_packet_index;
    input->packet_sample_position += frame.sample_count;
    return 1;
}

static int audio_custom_caf_next_gsm_packet(QwAudioCustomInput *input,
                                             QwAudioCustomPacket *packet)
{
    QwAudioCafGsmFrame frame;
    int rc = qw_audio_caf_gsm_next_frame(
        &input->caf_gsm, input->packet_index, &frame);
    if (rc <= 0) return rc < 0 ? rc : 0;
    if ((uint64_t)frame.sample_count >
        UINT64_MAX - input->packet_sample_position) {
        return QW_STATUS_ERR_FORMAT;
    }
    packet->data = frame.data;
    packet->size = frame.size;
    packet->pts = input->packet_sample_position;
    packet->duration = frame.sample_count;
    input->packet_index = frame.next_packet_index;
    input->packet_sample_position += frame.sample_count;
    return 1;
}

static int audio_custom_caf_next_ilbc_packet(QwAudioCustomInput *input,
                                              QwAudioCustomPacket *packet)
{
    QwAudioCafIlbcFrame frame;
    int rc = qw_audio_caf_ilbc_next_frame(
        &input->caf_ilbc, input->packet_index, input->packet_position,
        input->packet_size_table_position, &frame);
    if (rc <= 0) return rc < 0 ? rc : 0;
    if ((uint64_t)frame.sample_count >
        UINT64_MAX - input->packet_sample_position) {
        return QW_STATUS_ERR_FORMAT;
    }
    packet->data = frame.data;
    packet->size = frame.size;
    packet->pts = input->packet_sample_position;
    packet->duration = frame.sample_count;
    ++input->packet_index;
    input->packet_position = frame.next_data_offset;
    input->packet_size_table_position = frame.next_packet_size_offset;
    input->packet_sample_position += frame.sample_count;
    return 1;
}

static int audio_custom_caf_next_alac_packet(QwAudioCustomInput *input,
                                              QwAudioCustomPacket *packet)
{
    QwAudioCafAlacFrame frame;
    int rc = qw_audio_caf_alac_next_frame(
        &input->caf_alac, input->packet_index, input->packet_position,
        input->packet_size_table_position, &frame);
    if (rc <= 0) return rc < 0 ? rc : 0;
    if ((uint64_t)frame.sample_count >
        UINT64_MAX - input->packet_sample_position) {
        return QW_STATUS_ERR_FORMAT;
    }
    packet->data = frame.data;
    packet->size = frame.size;
    packet->pts = input->packet_sample_position;
    packet->duration = frame.sample_count;
    ++input->packet_index;
    input->packet_position = frame.next_data_offset;
    input->packet_size_table_position = frame.next_packet_size_offset;
    input->packet_sample_position += frame.sample_count;
    return 1;
}

static int audio_custom_caf_next_g711_packet(QwAudioCustomInput *input,
                                              QwAudioCustomPacket *packet)
{
    return qw_audio_custom_input_next_g711_data_packet(
        input->caf_g711.data, input->caf_g711.size,
        input->caf_g711.encoded_frame_count, input->caf_g711.channels,
        &input->packet_sample_position, packet);
}

int qw_audio_custom_caf_input_next_packet(QwAudioCustomInput *input,
                                          QwAudioCustomPacket *packet)
{
    if (!input || !packet) return QW_STATUS_ERR_INVALID_ARGUMENT;
    switch (input->kind) {
    case QW_AUDIO_CUSTOM_INPUT_CAF_IMA4:
        return audio_custom_caf_next_ima4_packet(input, packet);
    case QW_AUDIO_CUSTOM_INPUT_CAF_G711:
        return audio_custom_caf_next_g711_packet(input, packet);
    case QW_AUDIO_CUSTOM_INPUT_CAF_AAC:
        return audio_custom_caf_next_aac_packet(input, packet);
    case QW_AUDIO_CUSTOM_INPUT_CAF_AMR:
        return audio_custom_caf_next_amr_packet(input, packet);
    case QW_AUDIO_CUSTOM_INPUT_CAF_AC3:
        return audio_custom_caf_next_ac3_packet(input, packet);
    case QW_AUDIO_CUSTOM_INPUT_CAF_QDESIGN:
        return audio_custom_caf_next_qdesign_packet(input, packet);
    case QW_AUDIO_CUSTOM_INPUT_CAF_QCELP:
        return audio_custom_caf_next_qcelp_packet(input, packet);
    case QW_AUDIO_CUSTOM_INPUT_CAF_MPEG:
        return audio_custom_caf_next_mpeg_packet(input, packet);
    case QW_AUDIO_CUSTOM_INPUT_CAF_MACE:
        return audio_custom_caf_next_mace_packet(input, packet);
    case QW_AUDIO_CUSTOM_INPUT_CAF_GSM:
        return audio_custom_caf_next_gsm_packet(input, packet);
    case QW_AUDIO_CUSTOM_INPUT_CAF_ILBC:
        return audio_custom_caf_next_ilbc_packet(input, packet);
    case QW_AUDIO_CUSTOM_INPUT_CAF_ALAC:
        return audio_custom_caf_next_alac_packet(input, packet);
    default:
        return QW_STATUS_ERR_UNSUPPORTED;
    }
}
