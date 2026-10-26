#ifndef QUIREWISE_AUDIO_CUSTOM_INPUT_H
#define QUIREWISE_AUDIO_CUSTOM_INPUT_H

#include "audio_ape_parser.h"
#include "audio_aiff_ima4_parser.h"
#include "audio_aiff_codec_parser.h"
#include "audio_aiff_mace_parser.h"
#include "audio_amr_parser.h"
#include "audio_dolby_digital_parser.h"
#include "audio_aiff_g711_parser.h"
#include "audio_adts_parser.h"
#include "audio_au_parser.h"
#include "audio_caf_aac_parser.h"
#include "audio_caf_ac3_parser.h"
#include "audio_caf_amr_parser.h"
#include "audio_caf_alac_parser.h"
#include "audio_caf_mace_parser.h"
#include "audio_caf_mpeg_parser.h"
#include "audio_caf_qdesign_parser.h"
#include "audio_caf_qcelp_parser.h"
#include "audio_caf_g711_parser.h"
#include "audio_caf_gsm_parser.h"
#include "audio_caf_ilbc_parser.h"
#include "audio_caf_ima4_parser.h"
#include "audio_dts_parser.h"
#include "audio_dff_parser.h"
#include "audio_dss_parser.h"
#include "audio_dsf_parser.h"
#include "audio_mp4_parser.h"
#include "audio_flac_parser.h"
#include "audio_mpeg_parser.h"
#include "audio_ogg_opus_parser.h"
#include "audio_ogg_flac_parser.h"
#include "audio_ogg_speex_parser.h"
#include "audio_ogg_vorbis_parser.h"
#include "audio_pcm_parser.h"
#include "audio_tta_parser.h"
#include "audio_tak_parser.h"
#include "audio_wavpack_parser.h"
#include "audio_voc_parser.h"
#include "audio_wave_ima_parser.h"
#include "audio_wave_g711_parser.h"
#include "audio_wave_ms_parser.h"
#include "audio_matroska_parser.h"
#include "audio_mpc_parser.h"
#include "audio_asf_parser.h"
#include "audio_wave_mpeg_parser.h"
#include "audio_wave_mpeg1_parser.h"
#include "audio_wave_gsm_parser.h"
#include "audio_wave_dts_parser.h"

typedef enum QwAudioCustomInputKind {
    QW_AUDIO_CUSTOM_INPUT_NONE = 0,
    QW_AUDIO_CUSTOM_INPUT_PCM,
    QW_AUDIO_CUSTOM_INPUT_AU_ULAW,
    QW_AUDIO_CUSTOM_INPUT_AU_ALAW,
    QW_AUDIO_CUSTOM_INPUT_AU_ADPCM,
    QW_AUDIO_CUSTOM_INPUT_AAC_ADTS,
    QW_AUDIO_CUSTOM_INPUT_FLAC,
    QW_AUDIO_CUSTOM_INPUT_MP2,
    QW_AUDIO_CUSTOM_INPUT_MP3,
    QW_AUDIO_CUSTOM_INPUT_AMR_NB,
    QW_AUDIO_CUSTOM_INPUT_AMR_WB,
    QW_AUDIO_CUSTOM_INPUT_AC3,
    QW_AUDIO_CUSTOM_INPUT_EAC3,
    QW_AUDIO_CUSTOM_INPUT_DTS,
    QW_AUDIO_CUSTOM_INPUT_TTA,
    QW_AUDIO_CUSTOM_INPUT_TAK,
    QW_AUDIO_CUSTOM_INPUT_WAVPACK,
    QW_AUDIO_CUSTOM_INPUT_VOC_U8,
    QW_AUDIO_CUSTOM_INPUT_VOC_S16LE,
    QW_AUDIO_CUSTOM_INPUT_VOC_ADPCM_SBPRO_4,
    QW_AUDIO_CUSTOM_INPUT_VOC_ADPCM_SBPRO_3,
    QW_AUDIO_CUSTOM_INPUT_VOC_ADPCM_SBPRO_2,
    QW_AUDIO_CUSTOM_INPUT_VOC_ADPCM_CT,
    QW_AUDIO_CUSTOM_INPUT_VOC_ALAW,
    QW_AUDIO_CUSTOM_INPUT_VOC_MULAW,
    QW_AUDIO_CUSTOM_INPUT_APE,
    QW_AUDIO_CUSTOM_INPUT_OGG_OPUS,
    QW_AUDIO_CUSTOM_INPUT_OGG_FLAC,
    QW_AUDIO_CUSTOM_INPUT_OGG_SPEEX,
    QW_AUDIO_CUSTOM_INPUT_OGG_VORBIS,
    QW_AUDIO_CUSTOM_INPUT_WAVE_IMA,
    QW_AUDIO_CUSTOM_INPUT_WAVE_MS_ADPCM,
    QW_AUDIO_CUSTOM_INPUT_WAVE_MS_ADPCM_CUSTOM_COEFFICIENTS,
    QW_AUDIO_CUSTOM_INPUT_WAVE_ALAW,
    QW_AUDIO_CUSTOM_INPUT_WAVE_MULAW,
    QW_AUDIO_CUSTOM_INPUT_AIFF_ALAW,
    QW_AUDIO_CUSTOM_INPUT_AIFF_MULAW,
    QW_AUDIO_CUSTOM_INPUT_AIFF_IMA4,
    QW_AUDIO_CUSTOM_INPUT_AIFF_MACE,
    QW_AUDIO_CUSTOM_INPUT_CAF_IMA4,
    QW_AUDIO_CUSTOM_INPUT_CAF_G711,
    QW_AUDIO_CUSTOM_INPUT_CAF_AAC,
    QW_AUDIO_CUSTOM_INPUT_CAF_ALAC,
    QW_AUDIO_CUSTOM_INPUT_DSF,
    QW_AUDIO_CUSTOM_INPUT_DFF,
    QW_AUDIO_CUSTOM_INPUT_DSS,
    QW_AUDIO_CUSTOM_INPUT_MP4,
    QW_AUDIO_CUSTOM_INPUT_MATROSKA,
    QW_AUDIO_CUSTOM_INPUT_MPC,
    QW_AUDIO_CUSTOM_INPUT_ASF_WMA,
    QW_AUDIO_CUSTOM_INPUT_WAVE_MP3,
    QW_AUDIO_CUSTOM_INPUT_WAVE_MPEG1,
    QW_AUDIO_CUSTOM_INPUT_AIFF_CODEC,
    QW_AUDIO_CUSTOM_INPUT_CAF_MPEG,
    QW_AUDIO_CUSTOM_INPUT_CAF_MACE,
    QW_AUDIO_CUSTOM_INPUT_CAF_GSM,
    QW_AUDIO_CUSTOM_INPUT_CAF_AMR,
    QW_AUDIO_CUSTOM_INPUT_CAF_AC3,
    QW_AUDIO_CUSTOM_INPUT_CAF_QDESIGN,
    QW_AUDIO_CUSTOM_INPUT_CAF_QCELP,
    QW_AUDIO_CUSTOM_INPUT_CAF_ILBC,
    QW_AUDIO_CUSTOM_INPUT_WAVE_GSM_MS,
    QW_AUDIO_CUSTOM_INPUT_WAVE_DTS
} QwAudioCustomInputKind;

typedef struct QwAudioCustomInput {
    QwAudioCustomInputKind kind;
    uint32_t sample_rate;
    uint16_t channels;
    uint32_t channel_mask;
    uint64_t sample_count;
    int has_metadata;
    size_t packet_position;
    size_t adts_block_index;
    size_t packet_size_table_position;
    uint64_t packet_sample_position;
    uint32_t voc_silence_remaining;
    uint8_t voc_silence_buffer[QW_AUDIO_VOC_SILENCE_PACKET_SAMPLES];
    QwAudioPcmSource pcm;
    QwAudioAuSource au;
    QwAudioCafAacSource caf_aac;
    QwAudioCafAc3Source caf_ac3;
    QwAudioCafAmrSource caf_amr;
    QwAudioCafAlacSource caf_alac;
    QwAudioCafMaceSource caf_mace;
    QwAudioCafGsmSource caf_gsm;
    QwAudioCafIlbcSource caf_ilbc;
    QwAudioCafQdesignSource caf_qdesign;
    QwAudioCafQcelpSource caf_qcelp;
    QwAudioCafMpegSource caf_mpeg;
    QwAudioCafG711Source caf_g711;
    QwAudioCafIma4Source caf_ima4;
    QwAudioDsfSource dsf;
    QwAudioDffSource dff;
    QwAudioDssSource dss;
    QwAudioMp4Source mp4;
    QwAudioMatroskaSource matroska;
    QwAudioMpcSource mpc;
    QwAudioAsfSource asf;
    QwAudioAdtsSource adts;
    QwAudioFlacSource flac;
    QwAudioMpegSource mpeg;
    QwAudioAmrSource amr;
    QwAudioDolbyDigitalSource dolby_digital;
    QwAudioDtsSource dts;
    QwAudioWaveDtsSource wave_dts;
    QwAudioTtaSource tta;
    QwAudioTakSource tak;
    QwAudioWavpackSource wavpack;
    QwAudioVocSource voc;
    QwAudioVocCursor voc_cursor;
    QwAudioApeSource ape;
    QwAudioOggOpusSource ogg_opus;
    QwAudioOggFlacSource ogg_flac;
    QwAudioOggSpeexSource ogg_speex;
    QwAudioOggVorbisSource ogg_vorbis;
    QwAudioWaveImaSource wave_ima;
    QwAudioWaveMsSource wave_ms;
    QwAudioWaveG711Source wave_g711;
    QwAudioWaveMpegSource wave_mpeg;
    QwAudioWaveMpeg1Source wave_mpeg1;
    QwAudioWaveGsmSource wave_gsm_ms;
    QwAudioAiffG711Source aiff_g711;
    QwAudioAiffIma4Source aiff_ima4;
    QwAudioAiffMaceSource aiff_mace;
    QwAudioAiffCodecSource aiff_codec;
    size_t packet_index;
    uint8_t *ogg_selected_data;
    size_t ogg_selected_size;
    QwAudioOggPacketBuffer ogg_packet_buffer;
    uint8_t *dsf_packet_data;
    size_t dsf_packet_capacity;
    uint8_t *matroska_packet_data;
    size_t matroska_packet_capacity;
    uint8_t *wave_ms_pcm_buffer;
    size_t wave_ms_pcm_capacity;
    size_t dss_payload_position;
    uint8_t dss_swap;
    uint8_t dss_previous_byte;
    uint8_t dss_packet_data[QW_AUDIO_DSS_SP_PACKET_SIZE + 1u];
    QwAudioMp4Cursor mp4_cursor;
} QwAudioCustomInput;

typedef struct QwAudioCustomPacket {
    const uint8_t *data;
    size_t size;
    uint64_t pts;
    uint32_t duration;
    /* Signed Matroska DiscardPadding value, in nanoseconds. */
    int64_t discard_padding_ns;
    /* Raw Matroska BlockAdditional payloads associated with this packet. */
    const QwAudioMatroskaBlockAdditions *block_additions;
    uint8_t decoder_header[8];
    uint8_t decoder_header_size;
    uint8_t no_pts;
} QwAudioCustomPacket;

/* requested_audio_track is one-based; zero selects the default audio track.
 * Returns 1 for a parsed input, QW_STATUS_ERR_UNSUPPORTED for unsupported formats or
 * variants, or another PAGE error. No generic demux fallback is implied.
 */
int qw_audio_custom_input_parse(const uint8_t *data,
                                size_t size,
                                QwInputFormat input_format,
                                size_t requested_audio_track,
                                QwAudioCustomInput *input);

/* Releases buffers allocated while iterating custom packets. */
void qw_audio_custom_input_dispose(QwAudioCustomInput *input);

/* Packet bytes remain borrowed until the next packet call or dispose. */
/* Returns 1 with a packet, 0 at end of input, or a PAGE error. */
int qw_audio_custom_input_next_packet(QwAudioCustomInput *input,
                                      QwAudioCustomPacket *packet);

int qw_audio_custom_matroska_input_parse(const uint8_t *data,
                                         size_t size,
                                         QwInputFormat input_format,
                                         size_t requested_audio_track,
                                         QwAudioCustomInput *input);
int qw_audio_custom_matroska_input_next_packet(QwAudioCustomInput *input,
                                               QwAudioCustomPacket *packet);
void qw_audio_custom_matroska_input_dispose(QwAudioCustomInput *input);
int qw_audio_custom_tak_input_parse(const uint8_t *data,
                                    size_t size,
                                    QwAudioCustomInput *input);
int qw_audio_custom_tak_input_next_packet(QwAudioCustomInput *input,
                                          QwAudioCustomPacket *packet);
#endif
