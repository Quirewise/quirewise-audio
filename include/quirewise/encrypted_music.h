#ifndef QUIREWISE_ENCRYPTED_MUSIC_H
#define QUIREWISE_ENCRYPTED_MUSIC_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define QUIREWISE_ENCRYPTED_MUSIC_ABI_VERSION 1u

typedef enum QwEncryptedMusicFormat {
    QW_ENCRYPTED_MUSIC_UNKNOWN = 0,
    QW_ENCRYPTED_MUSIC_NCM = 1,
    QW_ENCRYPTED_MUSIC_QMC = 2,
    QW_ENCRYPTED_MUSIC_KGM = 3,
    QW_ENCRYPTED_MUSIC_KWM = 4
} QwEncryptedMusicFormat;

typedef enum QwEncryptedMusicStatus {
    QW_ENCRYPTED_MUSIC_OK = 0,
    QW_ENCRYPTED_MUSIC_INVALID_ARGUMENT = -1,
    QW_ENCRYPTED_MUSIC_UNSUPPORTED = -2,
    QW_ENCRYPTED_MUSIC_FORMAT_ERROR = -3,
    QW_ENCRYPTED_MUSIC_OUT_OF_MEMORY = -4,
    QW_ENCRYPTED_MUSIC_DECODE_ERROR = -5,
    QW_ENCRYPTED_MUSIC_INTERNAL_ERROR = -6
} QwEncryptedMusicStatus;

typedef struct QwEncryptedMusicOptions {
    /* Optional QMC/KGM v5 EKey. */
    const char *encryption_key;
    /* Optional compressed Kugou key file for KGM v1/v2. */
    const char *kgm_key_path;
} QwEncryptedMusicOptions;

typedef struct QwEncryptedMusicBuffer {
    uint8_t *data;
    size_t size;
} QwEncryptedMusicBuffer;

uint32_t qw_encrypted_music_abi_version(void);

/* Detect NCM, QMC, and KGM from bytes. KWM is path-detected because its header
 * does not provide a reliable content signature. */
QwEncryptedMusicFormat qw_encrypted_music_probe_memory(const void *data,
                                                       size_t size);
QwEncryptedMusicFormat qw_encrypted_music_format_from_path(const char *path);

/* Decrypts the container and returns the encoded audio payload. The output is
 * owned by the caller and released with qw_encrypted_music_buffer_free. */
QwEncryptedMusicStatus qw_encrypted_music_decrypt_buffer(
    QwEncryptedMusicFormat format,
    const void *data,
    size_t size,
    const QwEncryptedMusicOptions *options,
    QwEncryptedMusicBuffer *out_buffer,
    char *error,
    size_t error_capacity);
void qw_encrypted_music_buffer_free(QwEncryptedMusicBuffer *buffer);
const char *qw_encrypted_music_status_string(QwEncryptedMusicStatus status);

#ifdef __cplusplus
}
#endif

#endif
