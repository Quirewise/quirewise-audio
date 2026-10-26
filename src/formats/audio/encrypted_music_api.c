#include "quirewise/encrypted_music.h"
#include "audio_backend.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void encrypted_music_set_error(char *error, size_t capacity,
                                      const char *message)
{
    if (!error || !capacity) return;
    snprintf(error, capacity, "%s", message ? message : "encrypted music error");
}

static QwEncryptedMusicFormat encrypted_music_public_format(QwInputFormat format)
{
    switch (format) {
    case QW_FORMAT_NCM: return QW_ENCRYPTED_MUSIC_NCM;
    case QW_FORMAT_QMC: return QW_ENCRYPTED_MUSIC_QMC;
    case QW_FORMAT_KGM: return QW_ENCRYPTED_MUSIC_KGM;
    case QW_FORMAT_KWM: return QW_ENCRYPTED_MUSIC_KWM;
    default: return QW_ENCRYPTED_MUSIC_UNKNOWN;
    }
}

static QwInputFormat encrypted_music_internal_format(QwEncryptedMusicFormat format)
{
    switch (format) {
    case QW_ENCRYPTED_MUSIC_NCM: return QW_FORMAT_NCM;
    case QW_ENCRYPTED_MUSIC_QMC: return QW_FORMAT_QMC;
    case QW_ENCRYPTED_MUSIC_KGM: return QW_FORMAT_KGM;
    case QW_ENCRYPTED_MUSIC_KWM: return QW_FORMAT_KWM;
    default: return QW_FORMAT_UNKNOWN;
    }
}

static QwEncryptedMusicStatus encrypted_music_status(int status)
{
    switch (status) {
    case QW_STATUS_OK: return QW_ENCRYPTED_MUSIC_OK;
    case QW_STATUS_ERR_INVALID_ARGUMENT: return QW_ENCRYPTED_MUSIC_INVALID_ARGUMENT;
    case QW_STATUS_ERR_OUT_OF_MEMORY: return QW_ENCRYPTED_MUSIC_OUT_OF_MEMORY;
    case QW_STATUS_ERR_FORMAT:
    case QW_STATUS_ERR_EMPTY: return QW_ENCRYPTED_MUSIC_FORMAT_ERROR;
    case QW_STATUS_ERR_DECODE: return QW_ENCRYPTED_MUSIC_DECODE_ERROR;
    case QW_STATUS_ERR_UNSUPPORTED: return QW_ENCRYPTED_MUSIC_UNSUPPORTED;
    default: return QW_ENCRYPTED_MUSIC_INTERNAL_ERROR;
    }
}

uint32_t qw_encrypted_music_abi_version(void)
{
    return QUIREWISE_ENCRYPTED_MUSIC_ABI_VERSION;
}

QwEncryptedMusicFormat qw_encrypted_music_probe_memory(const void *data,
                                                       size_t size)
{
    return encrypted_music_public_format(
        qw_encrypted_music_probe_format(data, size));
}

QwEncryptedMusicFormat qw_encrypted_music_format_from_path(const char *path)
{
    return encrypted_music_public_format(qw_encrypted_music_format_from_path_internal(path));
}

QwEncryptedMusicStatus qw_encrypted_music_decrypt_buffer(
    QwEncryptedMusicFormat format,
    const void *data,
    size_t size,
    const QwEncryptedMusicOptions *options,
    QwEncryptedMusicBuffer *out_buffer,
    char *error,
    size_t error_capacity)
{
    QwConvertReport report = {0};
    QwInputFormat input_format;
    uint8_t *decoded = NULL;
    size_t decoded_size = 0u;
    int status;

    if (!out_buffer) {
        encrypted_music_set_error(error, error_capacity, "output buffer is required");
        return QW_ENCRYPTED_MUSIC_INVALID_ARGUMENT;
    }
    memset(out_buffer, 0, sizeof(*out_buffer));
    if (error && error_capacity) error[0] = '\0';
    input_format = encrypted_music_internal_format(format);
    if (input_format == QW_FORMAT_UNKNOWN || !data || !size) {
        encrypted_music_set_error(error, error_capacity,
                                  "encrypted format and nonempty input are required");
        return QW_ENCRYPTED_MUSIC_INVALID_ARGUMENT;
    }
    status = qw_encrypted_music_decrypt_ex(
        input_format, data, size,
        options ? options->encryption_key : NULL,
        NULL, NULL,
        options ? options->kgm_key_path : NULL,
        &decoded, &decoded_size, &report);
    if (status != QW_STATUS_OK) {
        encrypted_music_set_error(error, error_capacity, report.note);
        free(decoded);
        return encrypted_music_status(status);
    }
    out_buffer->data = decoded;
    out_buffer->size = decoded_size;
    return QW_ENCRYPTED_MUSIC_OK;
}

void qw_encrypted_music_buffer_free(QwEncryptedMusicBuffer *buffer)
{
    if (!buffer) return;
    free(buffer->data);
    memset(buffer, 0, sizeof(*buffer));
}

const char *qw_encrypted_music_status_string(QwEncryptedMusicStatus status)
{
    switch (status) {
    case QW_ENCRYPTED_MUSIC_OK: return "ok";
    case QW_ENCRYPTED_MUSIC_INVALID_ARGUMENT: return "invalid argument";
    case QW_ENCRYPTED_MUSIC_UNSUPPORTED: return "unsupported format or missing key";
    case QW_ENCRYPTED_MUSIC_FORMAT_ERROR: return "malformed encrypted container";
    case QW_ENCRYPTED_MUSIC_OUT_OF_MEMORY: return "out of memory";
    case QW_ENCRYPTED_MUSIC_DECODE_ERROR: return "decrypted payload is not recognized audio";
    case QW_ENCRYPTED_MUSIC_INTERNAL_ERROR: return "internal error";
    default: return "unknown encrypted music error";
    }
}
