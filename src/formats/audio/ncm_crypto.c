#include "ncm_crypto.h"

#include <quirewise/crypto.h>

int qw_audio_ncm_decrypt_aes_ecb(uint8_t *data, size_t size,
                                 const uint8_t key[16])
{
    return qw_crypto_aes128_ecb_decrypt(key, data, size) == QW_CRYPTO_OK;
}
