#ifndef QUIREWISE_AUDIO_BACKEND_H
#define QUIREWISE_AUDIO_BACKEND_H

#include "format_registry.h"
#include "../../core/audio_document.h"

#include <stdio.h>

struct QwSource;

extern const QwFormatDescriptor qw_format_mp3;
extern const QwFormatDescriptor qw_format_mp2;
extern const QwFormatDescriptor qw_format_flac;
extern const QwFormatDescriptor qw_format_m4a;
extern const QwFormatDescriptor qw_format_wav;
extern const QwFormatDescriptor qw_format_ogg;
extern const QwFormatDescriptor qw_format_aac;
extern const QwFormatDescriptor qw_format_ape;
extern const QwFormatDescriptor qw_format_wma;
extern const QwFormatDescriptor qw_format_aiff;
extern const QwFormatDescriptor qw_format_caf;
extern const QwFormatDescriptor qw_format_matroska;
extern const QwFormatDescriptor qw_format_ac3;
extern const QwFormatDescriptor qw_format_dts;
extern const QwFormatDescriptor qw_format_wavpack;
extern const QwFormatDescriptor qw_format_tta;
extern const QwFormatDescriptor qw_format_tak;
extern const QwFormatDescriptor qw_format_mpc;

int qw_audio_format_is(QwInputFormat format);
int qw_audio_input_format_is(QwInputFormat format);
enum QwAudioFormatCapabilities {
    QW_AUDIO_FORMAT_CAP_INPUT = 1u << 0,
    QW_AUDIO_FORMAT_CAP_OUTPUT = 1u << 1,
    QW_AUDIO_FORMAT_CAP_ENCRYPTED = 1u << 2
};
int qw_audio_format_capabilities_get(QwInputFormat format,
                                     unsigned *out_capabilities);
QwInputFormat qw_audio_detect_format(const void *data, size_t size);
int qw_audio_format_open_document(const struct QwSource *source,
                                  QwInputFormat input_format,
                                  const QwConvertOptions *options,
                                  QwAudioDocument *out_document,
                                  QwConvertReport *report);
int qw_encrypted_music_format_open_document(
    const struct QwSource *source,
    QwInputFormat input_format,
    const QwConvertOptions *options,
    QwAudioDocument *out_document,
    QwConvertReport *report);
int qw_encrypted_music_decrypt(QwInputFormat input_format,
                               const void *data,
                               size_t size,
                               const char *qmc_ekey,
                               const char *qmc_ekey_db_path,
                               const char *input_path,
                               uint8_t **out_data,
                               size_t *out_size,
                               QwConvertReport *report);
int qw_encrypted_music_decrypt_ex(QwInputFormat input_format,
                                  const void *data,
                                  size_t size,
                                  const char *qmc_ekey,
                                  const char *qmc_ekey_db_path,
                                  const char *input_path,
                                  const char *kgm_key_path,
                                  uint8_t **out_data,
                                  size_t *out_size,
                                  QwConvertReport *report);
QwInputFormat qw_encrypted_music_format_from_path_internal(const char *path);
QwInputFormat qw_encrypted_music_probe_format(const void *data, size_t size);

typedef struct QwAudioMetadata {
    const char *title;
    const char *artist;
    const char *album;
    const uint8_t *cover_data;
    size_t cover_size;
} QwAudioMetadata;

int qw_audio_transcode(const void *data,
                       size_t size,
                       QwInputFormat input_format,
                       QwOutputFormat output_format,
                       size_t audio_track,
                       uint8_t **out_data,
                       size_t *out_size,
                       QwConvertReport *report);
int qw_audio_transcode_with_metadata(const void *data,
                                     size_t size,
                                     QwInputFormat input_format,
                                     QwOutputFormat output_format,
                                     size_t audio_track,
                                     const QwAudioMetadata *metadata,
                                     uint8_t **out_data,
                                     size_t *out_size,
                                     QwConvertReport *report);
int qw_audio_transcode_with_metadata_stream(
    const void *data,
    size_t size,
    QwInputFormat input_format,
    QwOutputFormat output_format,
    size_t audio_track,
    const QwAudioMetadata *metadata,
    FILE *stream,
    size_t max_output_bytes,
    size_t *out_size,
    QwConvertReport *report);
int qw_audio_document_write(const QwAudioDocument *document,
                            QwOutputFormat output_format,
                            const QwConvertOptions *options,
                            uint8_t **out_data,
                            size_t *out_size,
                            QwConvertReport *report);
int qw_audio_document_write_stream(const QwAudioDocument *document,
                                   QwOutputFormat output_format,
                                   const QwConvertOptions *options,
                                   FILE *stream,
                                   size_t max_output_bytes,
                                   size_t *out_size,
                                   QwConvertReport *report);

int qw_encrypted_music_format_is(QwInputFormat format);
int qw_encrypted_music_transcode(const void *data,
                                 size_t size,
                                 QwInputFormat input_format,
                                 QwOutputFormat output_format,
                                 const char *qmc_ekey,
                                 const char *qmc_ekey_db_path,
                                 const char *input_path,
                                 size_t audio_track,
                                 uint8_t **out_data,
                                 size_t *out_size,
                                 QwConvertReport *report);

#endif
