#include "audio_custom_input_packets.h"
#include "audio_custom_wave_ms_input.h"
#include "audio_custom_au_input.h"
#include "audio_custom_aiff_ima4_input.h"
#include "audio_custom_aiff_codec_input.h"
#include "audio_custom_aiff_mace_input.h"
#include "audio_custom_mpc_input.h"
#include "audio_custom_asf_input.h"

#include "audio_custom_caf_input.h"
#include "audio_custom_dff_input.h"
#include "audio_custom_dsf_input.h"
#include "audio_custom_dss_input.h"
#include "audio_custom_mp4_input.h"
#include "audio_custom_ogg_input.h"

#include "quirewise/status.h"

#include <string.h>

int qw_audio_custom_input_next_g711_data_packet(const uint8_t *data,
                                                size_t data_size,
                                                uint64_t sample_count,
                                                uint16_t channels,
                                                uint64_t *sample_position,
                                                QwAudioCustomPacket *packet)
{
    uint64_t remaining_samples;
    uint32_t packet_samples;
    size_t byte_offset;
    size_t byte_count;

    if (!data || !channels || !sample_position || !packet)
        return QW_STATUS_ERR_INVALID_ARGUMENT;
    if (*sample_position >= sample_count) return 0;
    if (*sample_position > (uint64_t)(SIZE_MAX / channels))
        return QW_STATUS_ERR_FORMAT;
    remaining_samples = sample_count - *sample_position;
    packet_samples = remaining_samples > 4096u
        ? 4096u : (uint32_t)remaining_samples;
    byte_offset = (size_t)*sample_position * channels;
    byte_count = (size_t)packet_samples * channels;
    if (byte_offset > data_size || byte_count > data_size - byte_offset) {
        return QW_STATUS_ERR_FORMAT;
    }
    packet->data = data + byte_offset;
    packet->size = byte_count;
    packet->pts = *sample_position;
    packet->duration = packet_samples;
    *sample_position += packet_samples;
    return 1;
}

static int audio_custom_next_adts_packet(QwAudioCustomInput *input,
                                         QwAudioCustomPacket *packet)
{
    QwAudioAdtsFrame frame;
    const QwAudioAdtsRawDataBlock *raw_data_block;
    int rc;
    if (input->packet_position >= input->adts.frame_data_end) return 0;
    rc = qw_audio_adts_next_frame(&input->adts, input->packet_position, &frame);
    if (rc <= 0) return rc < 0 ? rc : QW_STATUS_ERR_FORMAT;
    if (input->adts_block_index >= frame.raw_data_block_count)
        return QW_STATUS_ERR_FORMAT;
    if (input->packet_sample_position > UINT64_MAX - 1024u)
        return QW_STATUS_ERR_FORMAT;
    raw_data_block = &frame.raw_data_blocks[input->adts_block_index];
    packet->data = raw_data_block->data;
    packet->size = raw_data_block->size;
    packet->pts = input->packet_sample_position;
    packet->duration = 1024u;
    input->packet_sample_position += 1024u;
    ++input->adts_block_index;
    if (input->adts_block_index == frame.raw_data_block_count) {
        input->adts_block_index = 0u;
        input->packet_position = frame.next_position;
    }
    return 1;
}

static int audio_custom_next_flac_packet(QwAudioCustomInput *input,
                                         QwAudioCustomPacket *packet)
{
    QwAudioFlacFrame frame;
    int rc;
    if (input->packet_position >= input->flac.frame_data_end) return 0;
    rc = qw_audio_flac_next_frame(&input->flac, input->packet_position, &frame);
    if (rc <= 0) return rc < 0 ? rc : QW_STATUS_ERR_FORMAT;
    packet->data = frame.data;
    packet->size = frame.size;
    packet->pts = input->packet_sample_position;
    packet->duration = frame.sample_count;
    input->packet_position = frame.next_position;
    input->packet_sample_position += frame.sample_count;
    return 1;
}

static int audio_custom_next_mpeg_packet(QwAudioCustomInput *input,
                                         QwAudioCustomPacket *packet)
{
    const QwAudioMpegSource *source;
    QwAudioMpegFrame frame;
    int rc;
    if (input->kind == QW_AUDIO_CUSTOM_INPUT_WAVE_MP3) {
        source = &input->wave_mpeg.mpeg;
    } else if (input->kind == QW_AUDIO_CUSTOM_INPUT_WAVE_MPEG1) {
        source = &input->wave_mpeg1.mpeg;
    } else {
        source = &input->mpeg;
    }
    if (input->packet_position >= source->frame_data_end) return 0;
    rc = qw_audio_mpeg_next_frame(source, input->packet_position, &frame);
    if (rc <= 0) return rc < 0 ? rc : QW_STATUS_ERR_FORMAT;
    packet->data = frame.data;
    packet->size = frame.size;
    packet->pts = input->packet_sample_position;
    packet->duration = frame.sample_count;
    input->packet_position = frame.next_position;
    input->packet_sample_position += frame.sample_count;
    return 1;
}

static int audio_custom_next_amr_packet(QwAudioCustomInput *input,
                                        QwAudioCustomPacket *packet)
{
    QwAudioAmrFrame frame;
    int rc;
    if (input->packet_position >= input->amr.frame_data_end) return 0;
    rc = qw_audio_amr_next_frame(&input->amr, input->packet_position, &frame);
    if (rc <= 0) return rc < 0 ? rc : QW_STATUS_ERR_FORMAT;
    packet->data = frame.data;
    packet->size = frame.size;
    packet->pts = input->packet_sample_position;
    packet->duration = frame.sample_count;
    input->packet_position = frame.next_position;
    input->packet_sample_position += frame.sample_count;
    return 1;
}

static int audio_custom_next_dolby_digital_packet(
    QwAudioCustomInput *input,
    QwAudioCustomPacket *packet)
{
    QwAudioDolbyDigitalFrame frame;
    int rc;
    while (input->packet_position < input->dolby_digital.frame_data_end) {
        rc = qw_audio_dolby_digital_next_frame(
            &input->dolby_digital, input->packet_position, &frame);
        if (rc <= 0) return rc < 0 ? rc : QW_STATUS_ERR_FORMAT;
        if (frame.next_position <= input->packet_position)
            return QW_STATUS_ERR_FORMAT;
        input->packet_position = frame.next_position;
        if (!frame.sample_count) continue;
        if (input->packet_sample_position >
            UINT64_MAX - frame.sample_count) {
            return QW_STATUS_ERR_FORMAT;
        }
        packet->data = frame.data;
        packet->size = frame.size;
        packet->pts = input->packet_sample_position;
        packet->duration = frame.sample_count;
        input->packet_sample_position += frame.sample_count;
        return 1;
    }
    return 0;
}

static int audio_custom_next_dts_packet(QwAudioCustomInput *input,
                                        QwAudioCustomPacket *packet)
{
    QwAudioDtsFrame frame;
    int rc;
    if (input->packet_position >= input->dts.frame_data_end) return 0;
    rc = qw_audio_dts_next_frame(&input->dts, input->packet_position, &frame);
    if (rc <= 0) return rc < 0 ? rc : QW_STATUS_ERR_FORMAT;
    packet->data = frame.data;
    packet->size = frame.size;
    packet->pts = input->packet_sample_position;
    packet->duration = frame.sample_count;
    input->packet_position = frame.next_position;
    input->packet_sample_position += frame.sample_count;
    return 1;
}

static int audio_custom_next_tta_packet(QwAudioCustomInput *input,
                                        QwAudioCustomPacket *packet)
{
    QwAudioTtaFrame frame;
    int rc;
    rc = qw_audio_tta_next_frame(&input->tta, input->packet_index,
                                 input->packet_position, &frame);
    if (rc <= 0) return rc < 0 ? rc : 0;
    packet->data = frame.data;
    packet->size = frame.size;
    packet->pts = input->packet_sample_position;
    packet->duration = frame.sample_count;
    input->packet_position = frame.next_position;
    input->packet_sample_position += frame.sample_count;
    ++input->packet_index;
    return 1;
}

static int audio_custom_next_wavpack_packet(QwAudioCustomInput *input,
                                            QwAudioCustomPacket *packet)
{
    QwAudioWavpackFrame frame;
    int rc;
    if (input->packet_position >= input->wavpack.frame_data_end) return 0;
    rc = qw_audio_wavpack_next_frame(&input->wavpack,
                                    input->packet_position, &frame);
    if (rc <= 0) return rc < 0 ? rc : QW_STATUS_ERR_FORMAT;
    packet->data = frame.data;
    packet->size = frame.size;
    packet->pts = input->packet_sample_position;
    packet->duration = frame.sample_count;
    input->packet_position = frame.next_position;
    input->packet_sample_position += frame.sample_count;
    return 1;
}

static int audio_custom_emit_voc_silence_packet(QwAudioCustomInput *input,
                                                uint32_t sample_count,
                                                QwAudioCustomPacket *packet)
{
    size_t byte_count;
    if (!input || !packet || !sample_count || !input->channels ||
        sample_count > QW_AUDIO_VOC_SILENCE_PACKET_SAMPLES ||
        (size_t)sample_count > SIZE_MAX / input->channels ||
        input->packet_sample_position > UINT64_MAX - sample_count) {
        return QW_STATUS_ERR_FORMAT;
    }
    byte_count = (size_t)sample_count * input->channels;
    if (byte_count > sizeof(input->voc_silence_buffer))
        return QW_STATUS_ERR_FORMAT;
    memset(input->voc_silence_buffer, 0x80, byte_count);
    packet->data = input->voc_silence_buffer;
    packet->size = byte_count;
    packet->pts = input->packet_sample_position;
    packet->duration = sample_count;
    input->packet_sample_position += sample_count;
    return 1;
}

static int audio_custom_next_voc_packet(QwAudioCustomInput *input,
                                        QwAudioCustomPacket *packet)
{
    QwAudioVocFrame frame;
    uint32_t silence_samples;
    int rc;
    if (input->voc_silence_remaining) {
        silence_samples = input->voc_silence_remaining >
                QW_AUDIO_VOC_SILENCE_PACKET_SAMPLES
            ? QW_AUDIO_VOC_SILENCE_PACKET_SAMPLES
            : input->voc_silence_remaining;
        rc = audio_custom_emit_voc_silence_packet(input, silence_samples,
                                                  packet);
        if (rc > 0) input->voc_silence_remaining -= silence_samples;
        return rc;
    }
    rc = qw_audio_voc_next_frame(&input->voc, &input->voc_cursor, &frame);
    if (rc <= 0) return rc < 0 ? rc : 0;
    if (frame.is_silence) {
        input->voc_silence_remaining = frame.silence_remaining;
        silence_samples = frame.sample_count;
        return audio_custom_emit_voc_silence_packet(input, silence_samples,
                                                    packet);
    }
    if (!frame.data || !frame.size) return QW_STATUS_ERR_FORMAT;
    packet->data = frame.data;
    packet->size = frame.size;
    packet->pts = input->packet_sample_position;
    packet->duration = frame.sample_count;
    input->packet_sample_position += frame.sample_count;
    return 1;
}

static int audio_custom_next_ape_packet(QwAudioCustomInput *input,
                                        QwAudioCustomPacket *packet)
{
    QwAudioApeFrame frame;
    int rc;
    rc = qw_audio_ape_next_frame(&input->ape, input->packet_index, &frame);
    if (rc <= 0) return rc < 0 ? rc : 0;
    packet->data = frame.data;
    packet->size = frame.size;
    packet->pts = input->packet_sample_position;
    packet->duration = frame.sample_count;
    packet->decoder_header[0] = (uint8_t)(frame.sample_count & 0xffu);
    packet->decoder_header[1] = (uint8_t)(frame.sample_count >> 8u);
    packet->decoder_header[2] = (uint8_t)(frame.sample_count >> 16u);
    packet->decoder_header[3] = (uint8_t)(frame.sample_count >> 24u);
    packet->decoder_header[4] = (uint8_t)(frame.skip & 0xffu);
    packet->decoder_header[5] = (uint8_t)(frame.skip >> 8u);
    packet->decoder_header[6] = (uint8_t)(frame.skip >> 16u);
    packet->decoder_header[7] = (uint8_t)(frame.skip >> 24u);
    packet->decoder_header_size = 8u;
    input->packet_sample_position += frame.sample_count;
    ++input->packet_index;
    return 1;
}

static int audio_custom_next_wave_ima_packet(QwAudioCustomInput *input,
                                              QwAudioCustomPacket *packet)
{
    QwAudioWaveImaFrame frame;
    int rc = qw_audio_wave_ima_next_frame(&input->wave_ima,
                                          input->packet_index, &frame);
    if (rc <= 0) return rc < 0 ? rc : 0;
    if ((uint64_t)frame.sample_count >
        UINT64_MAX - input->packet_sample_position) {
        return QW_STATUS_ERR_FORMAT;
    }
    packet->data = frame.data;
    packet->size = frame.size;
    packet->pts = input->packet_sample_position;
    packet->duration = frame.sample_count;
    input->packet_index = frame.next_frame_index;
    input->packet_sample_position += frame.sample_count;
    return 1;
}

static int audio_custom_next_wave_gsm_packet(QwAudioCustomInput *input,
                                             QwAudioCustomPacket *packet)
{
    QwAudioWaveGsmFrame frame;
    int rc;

    rc = qw_audio_wave_gsm_next_frame(&input->wave_gsm_ms,
                                      input->packet_index, &frame);
    if (rc <= 0) return rc < 0 ? rc : 0;
    if (!frame.sample_count ||
        input->packet_sample_position > UINT64_MAX - frame.sample_count) {
        return QW_STATUS_ERR_FORMAT;
    }
    packet->data = frame.data;
    packet->size = frame.size;
    packet->pts = input->packet_sample_position;
    packet->duration = frame.sample_count;
    input->packet_sample_position += frame.sample_count;
    input->packet_index = frame.next_packet_index;
    return 1;
}

static int audio_custom_next_wave_ms_packet(QwAudioCustomInput *input,
                                             QwAudioCustomPacket *packet)
{
    QwAudioWaveMsFrame frame;
    int rc = qw_audio_wave_ms_next_frame(&input->wave_ms,
                                         input->packet_index, &frame);
    if (rc <= 0) return rc < 0 ? rc : 0;
    if ((uint64_t)frame.sample_count >
        UINT64_MAX - input->packet_sample_position) {
        return QW_STATUS_ERR_FORMAT;
    }
    packet->data = frame.data;
    packet->size = frame.size;
    packet->pts = input->packet_sample_position;
    packet->duration = frame.sample_count;
    input->packet_index = frame.next_frame_index;
    input->packet_sample_position += frame.sample_count;
    return 1;
}

static int audio_custom_next_wave_g711_packet(QwAudioCustomInput *input,
                                               QwAudioCustomPacket *packet)
{
    return qw_audio_custom_input_next_g711_data_packet(
        input->wave_g711.data, input->wave_g711.size,
        input->wave_g711.sample_count, input->wave_g711.channels,
        &input->packet_sample_position, packet);
}

static int audio_custom_next_aiff_g711_packet(QwAudioCustomInput *input,
                                               QwAudioCustomPacket *packet)
{
    return qw_audio_custom_input_next_g711_data_packet(
        input->aiff_g711.data, input->aiff_g711.size,
        input->aiff_g711.sample_count, input->aiff_g711.channels,
        &input->packet_sample_position, packet);
}

int qw_audio_custom_input_next_packet(QwAudioCustomInput *input,
                                      QwAudioCustomPacket *packet)
{
    if (!input || !packet) return QW_STATUS_ERR_INVALID_ARGUMENT;
    memset(packet, 0, sizeof(*packet));
    switch (input->kind) {
    case QW_AUDIO_CUSTOM_INPUT_ASF_WMA:
        return qw_audio_custom_asf_input_next_packet(input, packet);
    case QW_AUDIO_CUSTOM_INPUT_MPC:
        return qw_audio_custom_mpc_input_next_packet(input, packet);
    case QW_AUDIO_CUSTOM_INPUT_MATROSKA:
        return qw_audio_custom_matroska_input_next_packet(input, packet);
    case QW_AUDIO_CUSTOM_INPUT_MP4:
        return qw_audio_custom_mp4_input_next_packet(input, packet);
    case QW_AUDIO_CUSTOM_INPUT_DSS:
        return qw_audio_custom_dss_input_next_packet(input, packet);
    case QW_AUDIO_CUSTOM_INPUT_DFF:
        return qw_audio_custom_dff_input_next_packet(input, packet);
    case QW_AUDIO_CUSTOM_INPUT_DSF:
        return qw_audio_custom_dsf_input_next_packet(input, packet);
    case QW_AUDIO_CUSTOM_INPUT_AU_ULAW:
    case QW_AUDIO_CUSTOM_INPUT_AU_ALAW:
    case QW_AUDIO_CUSTOM_INPUT_AU_ADPCM:
        return qw_audio_custom_au_input_next_packet(input, packet);
    case QW_AUDIO_CUSTOM_INPUT_AAC_ADTS:
        return audio_custom_next_adts_packet(input, packet);
    case QW_AUDIO_CUSTOM_INPUT_FLAC:
        return audio_custom_next_flac_packet(input, packet);
    case QW_AUDIO_CUSTOM_INPUT_MP2:
    case QW_AUDIO_CUSTOM_INPUT_MP3:
    case QW_AUDIO_CUSTOM_INPUT_WAVE_MP3:
    case QW_AUDIO_CUSTOM_INPUT_WAVE_MPEG1:
        return audio_custom_next_mpeg_packet(input, packet);
    case QW_AUDIO_CUSTOM_INPUT_AMR_NB:
    case QW_AUDIO_CUSTOM_INPUT_AMR_WB:
        return audio_custom_next_amr_packet(input, packet);
    case QW_AUDIO_CUSTOM_INPUT_AC3:
    case QW_AUDIO_CUSTOM_INPUT_EAC3:
        return audio_custom_next_dolby_digital_packet(input, packet);
    case QW_AUDIO_CUSTOM_INPUT_DTS:
    case QW_AUDIO_CUSTOM_INPUT_WAVE_DTS:
        return audio_custom_next_dts_packet(input, packet);
    case QW_AUDIO_CUSTOM_INPUT_TTA:
        return audio_custom_next_tta_packet(input, packet);
    case QW_AUDIO_CUSTOM_INPUT_TAK:
        return qw_audio_custom_tak_input_next_packet(input, packet);
    case QW_AUDIO_CUSTOM_INPUT_WAVPACK:
        return audio_custom_next_wavpack_packet(input, packet);
    case QW_AUDIO_CUSTOM_INPUT_VOC_U8:
    case QW_AUDIO_CUSTOM_INPUT_VOC_S16LE:
    case QW_AUDIO_CUSTOM_INPUT_VOC_ADPCM_SBPRO_4:
    case QW_AUDIO_CUSTOM_INPUT_VOC_ADPCM_SBPRO_3:
    case QW_AUDIO_CUSTOM_INPUT_VOC_ADPCM_SBPRO_2:
    case QW_AUDIO_CUSTOM_INPUT_VOC_ADPCM_CT:
    case QW_AUDIO_CUSTOM_INPUT_VOC_ALAW:
    case QW_AUDIO_CUSTOM_INPUT_VOC_MULAW:
        return audio_custom_next_voc_packet(input, packet);
    case QW_AUDIO_CUSTOM_INPUT_APE:
        return audio_custom_next_ape_packet(input, packet);
    case QW_AUDIO_CUSTOM_INPUT_OGG_OPUS:
    case QW_AUDIO_CUSTOM_INPUT_OGG_FLAC:
    case QW_AUDIO_CUSTOM_INPUT_OGG_SPEEX:
    case QW_AUDIO_CUSTOM_INPUT_OGG_VORBIS:
        return qw_audio_custom_ogg_input_next_packet(input, packet);
    case QW_AUDIO_CUSTOM_INPUT_WAVE_IMA:
        return audio_custom_next_wave_ima_packet(input, packet);
    case QW_AUDIO_CUSTOM_INPUT_WAVE_MS_ADPCM:
        return audio_custom_next_wave_ms_packet(input, packet);
    case QW_AUDIO_CUSTOM_INPUT_WAVE_MS_ADPCM_CUSTOM_COEFFICIENTS:
        return qw_audio_custom_wave_ms_input_next_packet(input, packet);
    case QW_AUDIO_CUSTOM_INPUT_WAVE_GSM_MS:
        return audio_custom_next_wave_gsm_packet(input, packet);
    case QW_AUDIO_CUSTOM_INPUT_WAVE_ALAW:
    case QW_AUDIO_CUSTOM_INPUT_WAVE_MULAW:
        return audio_custom_next_wave_g711_packet(input, packet);
    case QW_AUDIO_CUSTOM_INPUT_AIFF_ALAW:
    case QW_AUDIO_CUSTOM_INPUT_AIFF_MULAW:
        return audio_custom_next_aiff_g711_packet(input, packet);
    case QW_AUDIO_CUSTOM_INPUT_AIFF_IMA4:
        return qw_audio_custom_aiff_ima4_input_next_packet(input, packet);
    case QW_AUDIO_CUSTOM_INPUT_AIFF_MACE:
        return qw_audio_custom_aiff_mace_input_next_packet(input, packet);
    case QW_AUDIO_CUSTOM_INPUT_AIFF_CODEC:
        return qw_audio_custom_aiff_codec_input_next_packet(input, packet);
    case QW_AUDIO_CUSTOM_INPUT_CAF_IMA4:
    case QW_AUDIO_CUSTOM_INPUT_CAF_G711:
    case QW_AUDIO_CUSTOM_INPUT_CAF_AAC:
    case QW_AUDIO_CUSTOM_INPUT_CAF_ALAC:
    case QW_AUDIO_CUSTOM_INPUT_CAF_MPEG:
    case QW_AUDIO_CUSTOM_INPUT_CAF_MACE:
    case QW_AUDIO_CUSTOM_INPUT_CAF_GSM:
    case QW_AUDIO_CUSTOM_INPUT_CAF_AMR:
    case QW_AUDIO_CUSTOM_INPUT_CAF_ILBC:
    case QW_AUDIO_CUSTOM_INPUT_CAF_AC3:
    case QW_AUDIO_CUSTOM_INPUT_CAF_QDESIGN:
    case QW_AUDIO_CUSTOM_INPUT_CAF_QCELP:
        return qw_audio_custom_caf_input_next_packet(input, packet);
    default:
        return QW_STATUS_ERR_UNSUPPORTED;
    }
}
