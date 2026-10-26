#include "quirewise/audio.h"

#include "audio_custom_input.h"
#include "audio_matroska_additions_parser.h"
#include "quirewise/status.h"
#include "quirewise/convert.h"

#include <stdlib.h>
#include <string.h>

struct QwAudioReader {
    QwAudioCustomInput input;
    QwAudioInfo info;
    QwAudioBlockAddition block_additions[QW_AUDIO_MATROSKA_MAX_BLOCK_ADDITIONS];
};

static int audio_input_format_map(QwAudioInputFormat input_format,
                                  QwInputFormat *out_format)
{
    if (!out_format) return 0;
    switch (input_format) {
    case QW_AUDIO_INPUT_WAV: *out_format = QW_FORMAT_WAV; return 1;
    case QW_AUDIO_INPUT_W64: *out_format = QW_FORMAT_W64; return 1;
    case QW_AUDIO_INPUT_AIFF: *out_format = QW_FORMAT_AIFF; return 1;
    case QW_AUDIO_INPUT_CAF: *out_format = QW_FORMAT_CAF; return 1;
    case QW_AUDIO_INPUT_DSF: *out_format = QW_FORMAT_DSF; return 1;
    case QW_AUDIO_INPUT_DFF: *out_format = QW_FORMAT_DFF; return 1;
    case QW_AUDIO_INPUT_DSS: *out_format = QW_FORMAT_DSS; return 1;
    case QW_AUDIO_INPUT_M4A: *out_format = QW_FORMAT_M4A; return 1;
    case QW_AUDIO_INPUT_MATROSKA: *out_format = QW_FORMAT_MATROSKA; return 1;
    case QW_AUDIO_INPUT_WEBM: *out_format = QW_FORMAT_WEBM; return 1;
    case QW_AUDIO_INPUT_MPC: *out_format = QW_FORMAT_MPC; return 1;
    case QW_AUDIO_INPUT_WMA: *out_format = QW_FORMAT_WMA; return 1;
    case QW_AUDIO_INPUT_AU: *out_format = QW_FORMAT_AU; return 1;
    case QW_AUDIO_INPUT_AAC: *out_format = QW_FORMAT_AAC; return 1;
    case QW_AUDIO_INPUT_FLAC: *out_format = QW_FORMAT_FLAC; return 1;
    case QW_AUDIO_INPUT_MP2: *out_format = QW_FORMAT_MP2; return 1;
    case QW_AUDIO_INPUT_MP3: *out_format = QW_FORMAT_MP3; return 1;
    case QW_AUDIO_INPUT_AMR: *out_format = QW_FORMAT_AMR; return 1;
    case QW_AUDIO_INPUT_AC3: *out_format = QW_FORMAT_AC3; return 1;
    case QW_AUDIO_INPUT_DTS: *out_format = QW_FORMAT_DTS; return 1;
    case QW_AUDIO_INPUT_TTA: *out_format = QW_FORMAT_TTA; return 1;
    case QW_AUDIO_INPUT_TAK: *out_format = QW_FORMAT_TAK; return 1;
    case QW_AUDIO_INPUT_WAVPACK: *out_format = QW_FORMAT_WAVPACK; return 1;
    case QW_AUDIO_INPUT_VOC: *out_format = QW_FORMAT_VOC; return 1;
    case QW_AUDIO_INPUT_APE: *out_format = QW_FORMAT_APE; return 1;
    case QW_AUDIO_INPUT_OGG: *out_format = QW_FORMAT_OGG; return 1;
    default: return 0;
    }
}

static QwAudioCodec audio_matroska_codec_map(QwAudioMatroskaCodec codec)
{
    switch (codec) {
    case QW_AUDIO_MATROSKA_CODEC_AAC: return QW_AUDIO_CODEC_AAC;
    case QW_AUDIO_MATROSKA_CODEC_OPUS: return QW_AUDIO_CODEC_OPUS;
    case QW_AUDIO_MATROSKA_CODEC_VORBIS: return QW_AUDIO_CODEC_VORBIS;
    case QW_AUDIO_MATROSKA_CODEC_FLAC: return QW_AUDIO_CODEC_FLAC;
    case QW_AUDIO_MATROSKA_CODEC_ALAC: return QW_AUDIO_CODEC_ALAC;
    case QW_AUDIO_MATROSKA_CODEC_MP1: return QW_AUDIO_CODEC_MP1;
    case QW_AUDIO_MATROSKA_CODEC_MP2: return QW_AUDIO_CODEC_MP2;
    case QW_AUDIO_MATROSKA_CODEC_MP3: return QW_AUDIO_CODEC_MP3;
    case QW_AUDIO_MATROSKA_CODEC_AC3: return QW_AUDIO_CODEC_AC3;
    case QW_AUDIO_MATROSKA_CODEC_MLP: return QW_AUDIO_CODEC_MLP;
    case QW_AUDIO_MATROSKA_CODEC_TRUEHD: return QW_AUDIO_CODEC_TRUEHD;
    case QW_AUDIO_MATROSKA_CODEC_EAC3: return QW_AUDIO_CODEC_EAC3;
    case QW_AUDIO_MATROSKA_CODEC_DTS: return QW_AUDIO_CODEC_DTS;
    case QW_AUDIO_MATROSKA_CODEC_TTA: return QW_AUDIO_CODEC_TTA;
    case QW_AUDIO_MATROSKA_CODEC_PCM_U8: return QW_AUDIO_CODEC_PCM_U8;
    case QW_AUDIO_MATROSKA_CODEC_PCM_S8: return QW_AUDIO_CODEC_PCM_S8;
    case QW_AUDIO_MATROSKA_CODEC_PCM_S16LE: return QW_AUDIO_CODEC_PCM_S16LE;
    case QW_AUDIO_MATROSKA_CODEC_PCM_S16BE: return QW_AUDIO_CODEC_PCM_S16BE;
    case QW_AUDIO_MATROSKA_CODEC_PCM_S24LE: return QW_AUDIO_CODEC_PCM_S24LE;
    case QW_AUDIO_MATROSKA_CODEC_PCM_S24BE: return QW_AUDIO_CODEC_PCM_S24BE;
    case QW_AUDIO_MATROSKA_CODEC_PCM_S32LE: return QW_AUDIO_CODEC_PCM_S32LE;
    case QW_AUDIO_MATROSKA_CODEC_PCM_S32BE: return QW_AUDIO_CODEC_PCM_S32BE;
    case QW_AUDIO_MATROSKA_CODEC_PCM_F32LE: return QW_AUDIO_CODEC_PCM_F32LE;
    case QW_AUDIO_MATROSKA_CODEC_PCM_F32BE: return QW_AUDIO_CODEC_PCM_F32BE;
    case QW_AUDIO_MATROSKA_CODEC_PCM_F64LE: return QW_AUDIO_CODEC_PCM_F64LE;
    case QW_AUDIO_MATROSKA_CODEC_PCM_F64BE: return QW_AUDIO_CODEC_PCM_F64BE;
    case QW_AUDIO_MATROSKA_CODEC_PCM_ALAW: return QW_AUDIO_CODEC_PCM_ALAW;
    case QW_AUDIO_MATROSKA_CODEC_PCM_MULAW: return QW_AUDIO_CODEC_PCM_MULAW;
    case QW_AUDIO_MATROSKA_CODEC_ATRAC1: return QW_AUDIO_CODEC_ATRAC1;
    case QW_AUDIO_MATROSKA_CODEC_QDMC: return QW_AUDIO_CODEC_QDMC;
    case QW_AUDIO_MATROSKA_CODEC_QDM2: return QW_AUDIO_CODEC_QDM2;
    case QW_AUDIO_MATROSKA_CODEC_ADPCM_MS: return QW_AUDIO_CODEC_MS_ADPCM;
    case QW_AUDIO_MATROSKA_CODEC_ADPCM_IMA_WAV:
        return QW_AUDIO_CODEC_IMA_ADPCM;
    case QW_AUDIO_MATROSKA_CODEC_WAVPACK: return QW_AUDIO_CODEC_WAVPACK;
    default: return QW_AUDIO_CODEC_MATROSKA_AUDIO;
    }
}

static QwAudioCodec audio_codec_map(const QwAudioCustomInput *input)
{
    switch (input->kind) {
    case QW_AUDIO_CUSTOM_INPUT_PCM: return QW_AUDIO_CODEC_PCM;
    case QW_AUDIO_CUSTOM_INPUT_AU_ULAW:
    case QW_AUDIO_CUSTOM_INPUT_VOC_MULAW:
    case QW_AUDIO_CUSTOM_INPUT_WAVE_MULAW:
    case QW_AUDIO_CUSTOM_INPUT_AIFF_MULAW: return QW_AUDIO_CODEC_MULAW;
    case QW_AUDIO_CUSTOM_INPUT_AU_ALAW:
    case QW_AUDIO_CUSTOM_INPUT_VOC_ALAW:
    case QW_AUDIO_CUSTOM_INPUT_WAVE_ALAW:
    case QW_AUDIO_CUSTOM_INPUT_AIFF_ALAW: return QW_AUDIO_CODEC_ALAW;
    case QW_AUDIO_CUSTOM_INPUT_AU_ADPCM:
    case QW_AUDIO_CUSTOM_INPUT_VOC_ADPCM_SBPRO_4:
    case QW_AUDIO_CUSTOM_INPUT_VOC_ADPCM_SBPRO_3:
    case QW_AUDIO_CUSTOM_INPUT_VOC_ADPCM_SBPRO_2:
    case QW_AUDIO_CUSTOM_INPUT_VOC_ADPCM_CT: return QW_AUDIO_CODEC_ADPCM;
    case QW_AUDIO_CUSTOM_INPUT_AAC_ADTS:
    case QW_AUDIO_CUSTOM_INPUT_CAF_AAC: return QW_AUDIO_CODEC_AAC;
    case QW_AUDIO_CUSTOM_INPUT_FLAC: return QW_AUDIO_CODEC_FLAC;
    case QW_AUDIO_CUSTOM_INPUT_OGG_FLAC: return QW_AUDIO_CODEC_OGG_FLAC;
    case QW_AUDIO_CUSTOM_INPUT_MP2: return QW_AUDIO_CODEC_MP2;
    case QW_AUDIO_CUSTOM_INPUT_MP3:
    case QW_AUDIO_CUSTOM_INPUT_WAVE_MP3: return QW_AUDIO_CODEC_MP3;
    case QW_AUDIO_CUSTOM_INPUT_WAVE_MPEG1: return QW_AUDIO_CODEC_MPEG_AUDIO;
    case QW_AUDIO_CUSTOM_INPUT_AMR_NB:
    case QW_AUDIO_CUSTOM_INPUT_CAF_AMR: return QW_AUDIO_CODEC_AMR_NB;
    case QW_AUDIO_CUSTOM_INPUT_AMR_WB: return QW_AUDIO_CODEC_AMR_WB;
    case QW_AUDIO_CUSTOM_INPUT_AC3:
    case QW_AUDIO_CUSTOM_INPUT_CAF_AC3: return QW_AUDIO_CODEC_AC3;
    case QW_AUDIO_CUSTOM_INPUT_EAC3: return QW_AUDIO_CODEC_EAC3;
    case QW_AUDIO_CUSTOM_INPUT_DTS:
    case QW_AUDIO_CUSTOM_INPUT_WAVE_DTS: return QW_AUDIO_CODEC_DTS;
    case QW_AUDIO_CUSTOM_INPUT_TTA: return QW_AUDIO_CODEC_TTA;
    case QW_AUDIO_CUSTOM_INPUT_TAK: return QW_AUDIO_CODEC_TAK;
    case QW_AUDIO_CUSTOM_INPUT_WAVPACK: return QW_AUDIO_CODEC_WAVPACK;
    case QW_AUDIO_CUSTOM_INPUT_VOC_U8:
    case QW_AUDIO_CUSTOM_INPUT_VOC_S16LE: return QW_AUDIO_CODEC_PCM;
    case QW_AUDIO_CUSTOM_INPUT_APE: return QW_AUDIO_CODEC_APE;
    case QW_AUDIO_CUSTOM_INPUT_OGG_OPUS: return QW_AUDIO_CODEC_OPUS;
    case QW_AUDIO_CUSTOM_INPUT_OGG_SPEEX: return QW_AUDIO_CODEC_SPEEX;
    case QW_AUDIO_CUSTOM_INPUT_OGG_VORBIS: return QW_AUDIO_CODEC_VORBIS;
    case QW_AUDIO_CUSTOM_INPUT_WAVE_IMA: return QW_AUDIO_CODEC_IMA_ADPCM;
    case QW_AUDIO_CUSTOM_INPUT_WAVE_MS_ADPCM:
    case QW_AUDIO_CUSTOM_INPUT_WAVE_MS_ADPCM_CUSTOM_COEFFICIENTS:
        return QW_AUDIO_CODEC_MS_ADPCM;
    case QW_AUDIO_CUSTOM_INPUT_AIFF_IMA4:
    case QW_AUDIO_CUSTOM_INPUT_CAF_IMA4: return QW_AUDIO_CODEC_IMA4;
    case QW_AUDIO_CUSTOM_INPUT_AIFF_MACE:
    case QW_AUDIO_CUSTOM_INPUT_CAF_MACE: return QW_AUDIO_CODEC_MACE;
    case QW_AUDIO_CUSTOM_INPUT_DSF: return QW_AUDIO_CODEC_DSD;
    case QW_AUDIO_CUSTOM_INPUT_DFF:
        return input->dff.codec == QW_AUDIO_DFF_CODEC_DST
            ? QW_AUDIO_CODEC_DST : QW_AUDIO_CODEC_DSD;
    case QW_AUDIO_CUSTOM_INPUT_DSS: return QW_AUDIO_CODEC_DSS;
    case QW_AUDIO_CUSTOM_INPUT_MP4:
        return input->mp4.codec == QW_AUDIO_MP4_CODEC_ALAC
            ? QW_AUDIO_CODEC_ALAC : QW_AUDIO_CODEC_AAC;
    case QW_AUDIO_CUSTOM_INPUT_CAF_ALAC: return QW_AUDIO_CODEC_ALAC;
    case QW_AUDIO_CUSTOM_INPUT_MATROSKA:
        return audio_matroska_codec_map(input->matroska.codec);
    case QW_AUDIO_CUSTOM_INPUT_MPC: return QW_AUDIO_CODEC_MPC;
    case QW_AUDIO_CUSTOM_INPUT_ASF_WMA: return QW_AUDIO_CODEC_WMA;
    case QW_AUDIO_CUSTOM_INPUT_AIFF_CODEC: return QW_AUDIO_CODEC_AIFF_AUDIO;
    case QW_AUDIO_CUSTOM_INPUT_CAF_MPEG: return QW_AUDIO_CODEC_MPEG_AUDIO;
    case QW_AUDIO_CUSTOM_INPUT_CAF_GSM:
    case QW_AUDIO_CUSTOM_INPUT_WAVE_GSM_MS: return QW_AUDIO_CODEC_GSM;
    case QW_AUDIO_CUSTOM_INPUT_CAF_QDESIGN: return QW_AUDIO_CODEC_QDESIGN;
    case QW_AUDIO_CUSTOM_INPUT_CAF_QCELP: return QW_AUDIO_CODEC_QCELP;
    case QW_AUDIO_CUSTOM_INPUT_CAF_ILBC: return QW_AUDIO_CODEC_ILBC;
    case QW_AUDIO_CUSTOM_INPUT_CAF_G711:
        return input->caf_g711.codec == QW_AUDIO_CAF_G711_ALAW
            ? QW_AUDIO_CODEC_ALAW : QW_AUDIO_CODEC_MULAW;
    default: return QW_AUDIO_CODEC_UNKNOWN;
    }
}

static QwAudioStatus audio_status_map(int rc)
{
    switch (rc) {
    case QW_STATUS_OK: return QW_AUDIO_OK;
    case QW_STATUS_ERR_INVALID_ARGUMENT: return QW_AUDIO_INVALID_ARGUMENT;
    case QW_STATUS_ERR_OUT_OF_MEMORY: return QW_AUDIO_OUT_OF_MEMORY;
    case QW_STATUS_ERR_FORMAT: return QW_AUDIO_FORMAT_ERROR;
    case QW_STATUS_ERR_UNSUPPORTED: return QW_AUDIO_UNSUPPORTED;
    case QW_STATUS_ERR_RESOURCE_LIMIT: return QW_AUDIO_RESOURCE_LIMIT;
    default: return QW_AUDIO_INTERNAL_ERROR;
    }
}

QwAudioStatus qw_audio_reader_open(const uint8_t *data,
                                   size_t size,
                                   QwAudioInputFormat input_format,
                                   size_t requested_track,
                                   QwAudioReader **out_reader)
{
    QwAudioReader *reader;
    QwInputFormat internal_format;
    int rc;

    if (out_reader) *out_reader = NULL;
    if (!data || !size || !out_reader ||
        !audio_input_format_map(input_format, &internal_format)) {
        return QW_AUDIO_INVALID_ARGUMENT;
    }

    reader = (QwAudioReader *)calloc(1u, sizeof(*reader));
    if (!reader) return QW_AUDIO_OUT_OF_MEMORY;
    rc = qw_audio_custom_input_parse(data, size, internal_format,
                                     requested_track, &reader->input);
    if (rc != 1) {
        qw_audio_custom_input_dispose(&reader->input);
        free(reader);
        return audio_status_map(rc);
    }

    reader->info.input_format = input_format;
    reader->info.codec = audio_codec_map(&reader->input);
    reader->info.sample_rate = reader->input.sample_rate;
    reader->info.channels = reader->input.channels;
    reader->info.channel_mask = reader->input.channel_mask;
    reader->info.sample_count = reader->input.sample_count;
    reader->info.has_metadata = (uint8_t)(reader->input.has_metadata != 0);
    *out_reader = reader;
    return QW_AUDIO_OK;
}

QwAudioStatus qw_audio_reader_get_info(const QwAudioReader *reader,
                                       QwAudioInfo *out_info)
{
    if (!reader || !out_info) return QW_AUDIO_INVALID_ARGUMENT;
    *out_info = reader->info;
    return QW_AUDIO_OK;
}

QwAudioStatus qw_audio_reader_next_packet(QwAudioReader *reader,
                                          QwAudioPacket *out_packet)
{
    QwAudioCustomPacket packet;
    const QwAudioMatroskaBlockAdditions *additions;
    size_t i;
    int rc;

    if (!reader || !out_packet) return QW_AUDIO_INVALID_ARGUMENT;
    memset(out_packet, 0, sizeof(*out_packet));
    rc = qw_audio_custom_input_next_packet(&reader->input, &packet);
    if (rc <= 0) return rc == 0 ? QW_AUDIO_END : audio_status_map(rc);

    out_packet->data = packet.data;
    out_packet->size = packet.size;
    out_packet->pts = packet.pts;
    out_packet->duration = packet.duration;
    out_packet->discard_padding_ns = packet.discard_padding_ns;
    memcpy(out_packet->decoder_header, packet.decoder_header,
           sizeof(out_packet->decoder_header));
    out_packet->decoder_header_size = packet.decoder_header_size;
    out_packet->no_pts = packet.no_pts;

    additions = packet.block_additions;
    if (additions) {
        out_packet->block_addition_count = additions->count;
        if (out_packet->block_addition_count >
            QW_AUDIO_MATROSKA_MAX_BLOCK_ADDITIONS) {
            out_packet->block_addition_count =
                QW_AUDIO_MATROSKA_MAX_BLOCK_ADDITIONS;
        }
        for (i = 0u; i < out_packet->block_addition_count; ++i) {
            reader->block_additions[i].data = additions->items[i].data;
            reader->block_additions[i].size = additions->items[i].size;
            reader->block_additions[i].id = additions->items[i].id;
        }
        out_packet->block_additions = reader->block_additions;
    }
    return QW_AUDIO_OK;
}

void qw_audio_reader_close(QwAudioReader *reader)
{
    if (!reader) return;
    qw_audio_custom_input_dispose(&reader->input);
    free(reader);
}

const char *qw_audio_codec_name(QwAudioCodec codec)
{
    static const char *const names[] = {
        [QW_AUDIO_CODEC_UNKNOWN] = "unknown",
        [QW_AUDIO_CODEC_PCM] = "pcm",
        [QW_AUDIO_CODEC_ALAW] = "alaw",
        [QW_AUDIO_CODEC_MULAW] = "mulaw",
        [QW_AUDIO_CODEC_ADPCM] = "adpcm",
        [QW_AUDIO_CODEC_AAC] = "aac",
        [QW_AUDIO_CODEC_FLAC] = "flac",
        [QW_AUDIO_CODEC_MP1] = "mp1",
        [QW_AUDIO_CODEC_MP2] = "mp2",
        [QW_AUDIO_CODEC_MP3] = "mp3",
        [QW_AUDIO_CODEC_AMR_NB] = "amr-nb",
        [QW_AUDIO_CODEC_AMR_WB] = "amr-wb",
        [QW_AUDIO_CODEC_AC3] = "ac3",
        [QW_AUDIO_CODEC_MLP] = "mlp",
        [QW_AUDIO_CODEC_TRUEHD] = "truehd",
        [QW_AUDIO_CODEC_EAC3] = "eac3",
        [QW_AUDIO_CODEC_DTS] = "dts",
        [QW_AUDIO_CODEC_TTA] = "tta",
        [QW_AUDIO_CODEC_TAK] = "tak",
        [QW_AUDIO_CODEC_WAVPACK] = "wavpack",
        [QW_AUDIO_CODEC_APE] = "ape",
        [QW_AUDIO_CODEC_OPUS] = "opus",
        [QW_AUDIO_CODEC_OGG_FLAC] = "ogg-flac",
        [QW_AUDIO_CODEC_SPEEX] = "speex",
        [QW_AUDIO_CODEC_VORBIS] = "vorbis",
        [QW_AUDIO_CODEC_IMA_ADPCM] = "ima-adpcm",
        [QW_AUDIO_CODEC_MS_ADPCM] = "ms-adpcm",
        [QW_AUDIO_CODEC_PCM_U8] = "pcm-u8",
        [QW_AUDIO_CODEC_PCM_S8] = "pcm-s8",
        [QW_AUDIO_CODEC_PCM_S16LE] = "pcm-s16le",
        [QW_AUDIO_CODEC_PCM_S16BE] = "pcm-s16be",
        [QW_AUDIO_CODEC_PCM_S24LE] = "pcm-s24le",
        [QW_AUDIO_CODEC_PCM_S24BE] = "pcm-s24be",
        [QW_AUDIO_CODEC_PCM_S32LE] = "pcm-s32le",
        [QW_AUDIO_CODEC_PCM_S32BE] = "pcm-s32be",
        [QW_AUDIO_CODEC_PCM_F32LE] = "pcm-f32le",
        [QW_AUDIO_CODEC_PCM_F32BE] = "pcm-f32be",
        [QW_AUDIO_CODEC_PCM_F64LE] = "pcm-f64le",
        [QW_AUDIO_CODEC_PCM_F64BE] = "pcm-f64be",
        [QW_AUDIO_CODEC_PCM_ALAW] = "pcm-alaw",
        [QW_AUDIO_CODEC_PCM_MULAW] = "pcm-mulaw",
        [QW_AUDIO_CODEC_IMA4] = "ima4",
        [QW_AUDIO_CODEC_MACE] = "mace",
        [QW_AUDIO_CODEC_DSD] = "dsd",
        [QW_AUDIO_CODEC_DST] = "dst",
        [QW_AUDIO_CODEC_DSS] = "dss",
        [QW_AUDIO_CODEC_MP4_AUDIO] = "mp4-audio",
        [QW_AUDIO_CODEC_ALAC] = "alac",
        [QW_AUDIO_CODEC_MATROSKA_AUDIO] = "matroska-audio",
        [QW_AUDIO_CODEC_MPC] = "mpc",
        [QW_AUDIO_CODEC_WMA] = "wma",
        [QW_AUDIO_CODEC_MPEG_AUDIO] = "mpeg-audio",
        [QW_AUDIO_CODEC_GSM] = "gsm",
        [QW_AUDIO_CODEC_ATRAC1] = "atrac1",
        [QW_AUDIO_CODEC_AIFF_AUDIO] = "aiff-audio",
        [QW_AUDIO_CODEC_QDESIGN] = "qdesign",
        [QW_AUDIO_CODEC_QDMC] = "qdmc",
        [QW_AUDIO_CODEC_QDM2] = "qdm2",
        [QW_AUDIO_CODEC_QCELP] = "qcelp",
        [QW_AUDIO_CODEC_ILBC] = "ilbc"
    };
    if ((unsigned)codec >= sizeof(names) / sizeof(names[0])) return "unknown";
    return names[(unsigned)codec];
}
