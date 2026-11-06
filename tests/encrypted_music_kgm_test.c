/* KGM v1/v2 end-to-end decryption with an XZ key resource (DEP-009). */
#include <quirewise/encrypted_music.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const uint8_t kgm_magic[16] = {
    0x7c, 0xd5, 0x32, 0xeb, 0x86, 0x02, 0x7f, 0x4b,
    0xa8, 0xaf, 0xa6, 0x8e, 0x0f, 0xff, 0x99, 0x14
};

/* Copied verbatim from the KGM implementation; guards against silent drift. */
static const uint8_t kgm_mend[272] = {
    0xb8, 0xd5, 0x3d, 0xb2, 0xe9, 0xaf, 0x78, 0x8c, 0x83, 0x33, 0x71, 0x51,
    0x76, 0xa0, 0xcd, 0x37, 0x2f, 0x3e, 0x35, 0x8d, 0xa9, 0xbe, 0x98, 0xb7,
    0xe7, 0x8c, 0x22, 0xce, 0x5a, 0x61, 0xdf, 0x68, 0x69, 0x89, 0xfe, 0xa5,
    0xb6, 0xde, 0xa9, 0x77, 0xfc, 0xc8, 0xbd, 0xbd, 0xe5, 0x6d, 0x3e, 0x5a,
    0x36, 0xef, 0x69, 0x4e, 0xbe, 0xe1, 0xe9, 0x66, 0x1c, 0xf3, 0xd9, 0x02,
    0xb6, 0xf2, 0x12, 0x9b, 0x44, 0xd0, 0x6f, 0xb9, 0x35, 0x89, 0xb6, 0x46,
    0x6d, 0x73, 0x82, 0x06, 0x69, 0xc1, 0xed, 0xd7, 0x85, 0xc2, 0x30, 0xdf,
    0xa2, 0x62, 0xbe, 0x79, 0x2d, 0x62, 0x62, 0x3d, 0x0d, 0x7e, 0xbe, 0x48,
    0x89, 0x23, 0x02, 0xa0, 0xe4, 0xd5, 0x75, 0x51, 0x32, 0x02, 0x53, 0xfd,
    0x16, 0x3a, 0x21, 0x3b, 0x16, 0x0f, 0xc3, 0xb2, 0xbb, 0xb3, 0xe2, 0xba,
    0x3a, 0x3d, 0x13, 0xec, 0xf6, 0x01, 0x45, 0x84, 0xa5, 0x70, 0x0f, 0x93,
    0x49, 0x0c, 0x64, 0xcd, 0x31, 0xd5, 0xcc, 0x4c, 0x07, 0x01, 0x9e, 0x00,
    0x1a, 0x23, 0x90, 0xbf, 0x88, 0x1e, 0x3b, 0xab, 0xa6, 0x3e, 0xc4, 0x73,
    0x47, 0x10, 0x7e, 0x3b, 0x5e, 0xbc, 0xe3, 0x00, 0x84, 0xff, 0x09, 0xd4,
    0xe0, 0x89, 0x0f, 0x5b, 0x58, 0x70, 0x4f, 0xfb, 0x65, 0xd8, 0x5c, 0x53,
    0x1b, 0xd3, 0xc8, 0xc6, 0xbf, 0xef, 0x98, 0xb0, 0x50, 0x4f, 0x0f, 0xea,
    0xe5, 0x83, 0x58, 0x8c, 0x28, 0x2c, 0x84, 0x67, 0xcd, 0xd0, 0x9e, 0x47,
    0xdb, 0x27, 0x50, 0xca, 0xf4, 0x63, 0x63, 0xe8, 0x97, 0x7f, 0x1b, 0x4b,
    0x0c, 0xc2, 0xc1, 0x21, 0x4c, 0xcc, 0x58, 0xf5, 0x94, 0x52, 0xa3, 0xf3,
    0xd3, 0xe0, 0x68, 0xf4, 0x00, 0x23, 0xf3, 0x5e, 0x0a, 0x7b, 0x93, 0xdd,
    0xab, 0x12, 0xb2, 0x13, 0xe8, 0x84, 0xd7, 0xa7, 0x9f, 0x0f, 0x32, 0x4c,
    0x55, 0x1d, 0x04, 0x36, 0x52, 0xdc, 0x03, 0xf3, 0xf9, 0x4e, 0x42, 0xe9,
    0x3d, 0x61, 0xef, 0x7c, 0xb6, 0xb3, 0x93, 0x50
};

/* XZ stream that inflates to the four-byte public key {11 22 33 44}. */
static const uint8_t kugou_key_xz[60] = {
    0xfd, 0x37, 0x7a, 0x58, 0x5a, 0x00, 0x00, 0x04, 0xe6, 0xd6, 0xb4, 0x46,
    0x02, 0x00, 0x21, 0x01, 0x16, 0x00, 0x00, 0x00, 0x74, 0x2f, 0xe5, 0xa3,
    0x01, 0x00, 0x03, 0x11, 0x22, 0x33, 0x44, 0x00, 0x62, 0x09, 0x1c, 0x2d,
    0x7c, 0x10, 0x3b, 0x61, 0x00, 0x01, 0x1c, 0x04, 0x6f, 0x2c, 0x9c, 0xc1,
    0x1f, 0xb6, 0xf3, 0x7d, 0x01, 0x00, 0x00, 0x00, 0x00, 0x04, 0x59, 0x5a
};

#define KGM_HEADER_SIZE 1024u
#define PAYLOAD_SIZE 64u
#define KEY_PATH "kgm_key_test.xz"

static uint8_t scramble(uint8_t value)
{
    return (uint8_t)(value ^ ((value & 0x0fu) << 4));
}

static uint8_t unscramble(uint8_t value)
{
    return (uint8_t)((value & 0x0fu) |
                     (((value >> 4u) ^ (value & 0x0fu)) << 4u));
}

static void put_u32_le(uint8_t *target, uint32_t value)
{
    target[0] = (uint8_t)(value & 0xffu);
    target[1] = (uint8_t)((value >> 8u) & 0xffu);
    target[2] = (uint8_t)((value >> 16u) & 0xffu);
    target[3] = (uint8_t)((value >> 24u) & 0xffu);
}

static int write_file(const char *path, const uint8_t *data, size_t size)
{
    FILE *file = fopen(path, "wb");
    if (!file) return 0;
    if (fwrite(data, 1u, size, file) != size) {
        fclose(file);
        return 0;
    }
    fclose(file);
    return 1;
}

int main(void)
{
    static const uint8_t pub[4] = {0x11u, 0x22u, 0x33u, 0x44u};
    uint8_t plaintext[PAYLOAD_SIZE];
    uint8_t container[KGM_HEADER_SIZE + PAYLOAD_SIZE];
    uint8_t corrupted[sizeof(kugou_key_xz)];
    QwEncryptedMusicBuffer buffer = {0};
    QwEncryptedMusicOptions options = {0};
    QwEncryptedMusicStatus status;
    char error[128];
    size_t index;

    for (index = 0u; index < PAYLOAD_SIZE; ++index) {
        plaintext[index] = (uint8_t)(0x40u + index);
    }
    memcpy(plaintext, "fLaC", 4u);

    memset(container, 0, sizeof(container));
    memcpy(container, kgm_magic, sizeof(kgm_magic));
    put_u32_le(container + 0x10u, KGM_HEADER_SIZE);
    put_u32_le(container + 0x14u, 4u);
    for (index = 0u; index < 16u; ++index) {
        container[0x1cu + index] = (uint8_t)(index * 7u + 1u);
    }
    for (index = 0u; index < PAYLOAD_SIZE; ++index) {
        size_t block = index / 16u;
        uint8_t own = index % 17u == 16u
                          ? 0u
                          : container[0x1cu + (index % 17u)];
        uint8_t mask = scramble((uint8_t)(pub[block] ^
                                          kgm_mend[index % sizeof(kgm_mend)]));
        container[KGM_HEADER_SIZE + index] =
            (uint8_t)(unscramble((uint8_t)(plaintext[index] ^ mask)) ^ own);
    }

    if (!write_file(KEY_PATH, kugou_key_xz, sizeof(kugou_key_xz))) return 2;
    options.kgm_key_path = KEY_PATH;
    status = qw_encrypted_music_decrypt_buffer(QW_ENCRYPTED_MUSIC_KGM,
                                               container, sizeof(container),
                                               &options, &buffer, error,
                                               sizeof(error));
    if (status != QW_ENCRYPTED_MUSIC_OK || buffer.size != PAYLOAD_SIZE ||
        memcmp(buffer.data, plaintext, PAYLOAD_SIZE) != 0) {
        fprintf(stderr, "KGM XZ-key decryption failed: status=%d size=%zu err=%s\n",
                (int)status, buffer.size, error);
        qw_encrypted_music_buffer_free(&buffer);
        remove(KEY_PATH);
        return 3;
    }
    qw_encrypted_music_buffer_free(&buffer);

    memcpy(corrupted, kugou_key_xz, sizeof(corrupted));
    corrupted[20] ^= 0x01u; /* block header CRC no longer matches */
    if (!write_file(KEY_PATH, corrupted, sizeof(corrupted))) return 4;
    status = qw_encrypted_music_decrypt_buffer(QW_ENCRYPTED_MUSIC_KGM,
                                               container, sizeof(container),
                                               &options, &buffer, error,
                                               sizeof(error));
    qw_encrypted_music_buffer_free(&buffer);
    if (status != QW_ENCRYPTED_MUSIC_UNSUPPORTED) {
        fprintf(stderr, "corrupted KGM key resource returned %d\n", (int)status);
        remove(KEY_PATH);
        return 5;
    }
    if (!write_file(KEY_PATH, kugou_key_xz, 24u)) return 6;
    status = qw_encrypted_music_decrypt_buffer(QW_ENCRYPTED_MUSIC_KGM,
                                               container, sizeof(container),
                                               &options, &buffer, error,
                                               sizeof(error));
    qw_encrypted_music_buffer_free(&buffer);
    if (status != QW_ENCRYPTED_MUSIC_UNSUPPORTED) {
        fprintf(stderr, "truncated KGM key resource returned %d\n",
                (int)status);
        remove(KEY_PATH);
        return 7;
    }

    options.kgm_key_path = "missing-kgm-key.xz";
    status = qw_encrypted_music_decrypt_buffer(QW_ENCRYPTED_MUSIC_KGM,
                                               container, sizeof(container),
                                               &options, &buffer, error,
                                               sizeof(error));
    qw_encrypted_music_buffer_free(&buffer);
    if (status != QW_ENCRYPTED_MUSIC_UNSUPPORTED) {
        fprintf(stderr, "missing KGM key resource returned %d\n", (int)status);
        remove(KEY_PATH);
        return 8;
    }
    options.kgm_key_path = KEY_PATH;

    {
        uint8_t bad_offset[KGM_HEADER_SIZE + PAYLOAD_SIZE];
        memcpy(bad_offset, container, sizeof(bad_offset));
        put_u32_le(bad_offset + 0x10u, 0u);
        if (!write_file(KEY_PATH, kugou_key_xz, sizeof(kugou_key_xz))) return 9;
        status = qw_encrypted_music_decrypt_buffer(QW_ENCRYPTED_MUSIC_KGM,
                                                   bad_offset,
                                                   sizeof(bad_offset),
                                                   &options, &buffer, error,
                                                   sizeof(error));
        qw_encrypted_music_buffer_free(&buffer);
        if (status != QW_ENCRYPTED_MUSIC_FORMAT_ERROR) {
            fprintf(stderr, "invalid KGM audio offset returned %d\n",
                    (int)status);
            remove(KEY_PATH);
            return 10;
        }
    }
    status = qw_encrypted_music_decrypt_buffer(QW_ENCRYPTED_MUSIC_KGM,
                                               container, 512u, &options,
                                               &buffer, error, sizeof(error));
    qw_encrypted_music_buffer_free(&buffer);
    if (status != QW_ENCRYPTED_MUSIC_FORMAT_ERROR) {
        fprintf(stderr, "truncated KGM container returned %d\n", (int)status);
        remove(KEY_PATH);
        return 11;
    }

    remove(KEY_PATH);
    puts("KGM XZ key resource decrypted the known payload byte-for-byte");
    return 0;
}
