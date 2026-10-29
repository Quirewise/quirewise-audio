#ifndef QUIREWISE_AUDIO_NCM_CRYPTO_H
#define QUIREWISE_AUDIO_NCM_CRYPTO_H

#include <stddef.h>
#include <stdint.h>

int qw_audio_ncm_decrypt_aes_ecb(uint8_t *data, size_t size,
                                 const uint8_t key[16]);

#endif
