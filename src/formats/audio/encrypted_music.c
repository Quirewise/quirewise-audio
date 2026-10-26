#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include "audio_backend.h"
#include "../../core/byte_utils.h"
#include "../../core/platform_unicode.h"
#if !defined(QUIREWISE_ENCRYPTED_MUSIC_PARSER_ONLY)
#include "../../core/source.h"
#include "../../backends/tool_bridge.h"
#endif

#include "aes.h"
#include "kgm_database.h"

#include <lzma.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <windows.h>
#else
#include <unistd.h>
#endif

enum {
    NCM_HEADER_SIZE = 10,
    KGM_HEADER_SIZE = 1024,
    KWM_HEADER_SIZE = 1024,
    NCM_MAX_METADATA_SIZE = 4 * 1024 * 1024
};

typedef struct QwNcmMetadata {
    char *title;
    char *artist;
    char *album;
    const uint8_t *cover_data;
    size_t cover_size;
} QwNcmMetadata;

typedef struct QwNcmJsonParser {
    const uint8_t *data;
    size_t size;
    size_t position;
} QwNcmJsonParser;

static int base64_decode(const uint8_t *text, size_t size,
                         uint8_t **out, size_t *out_size);

#if !defined(QUIREWISE_ENCRYPTED_MUSIC_PARSER_ONLY)
static void ncm_metadata_clear(QwNcmMetadata *metadata)
{
    if (!metadata) return;
    free(metadata->title);
    free(metadata->artist);
    free(metadata->album);
    memset(metadata, 0, sizeof(*metadata));
}
#endif

static const uint8_t ncm_magic[8] = {'C','T','E','N','F','D','A','M'};
static const uint8_t ncm_core_key[16] = {
    'h','z','H','R','A','m','s','o','5','k','I','n','b','a','x','W'
};
static const uint8_t kgm_magic[16] = {
    0x7c,0xd5,0x32,0xeb,0x86,0x02,0x7f,0x4b,
    0xa8,0xaf,0xa6,0x8e,0x0f,0xff,0x99,0x14
};
static const uint8_t vpr_magic[16] = {
    0x05,0x28,0xbc,0x96,0xe9,0xe4,0x5a,0x43,
    0x91,0xaa,0xbd,0xd0,0x7a,0xf5,0x36,0x31
};
static const uint8_t qmc_v1_key[128] = {
    0xc3,0x4a,0xd6,0xca,0x90,0x67,0xf7,0x52,0xd8,0xa1,0x66,0x62,0x9f,0x5b,0x09,0x00,
    0xc3,0x5e,0x95,0x23,0x9f,0x13,0x11,0x7e,0xd8,0x92,0x3f,0xbc,0x90,0xbb,0x74,0x0e,
    0xc3,0x47,0x74,0x3d,0x90,0xaa,0x3f,0x51,0xd8,0xf4,0x11,0x84,0x9f,0xde,0x95,0x1d,
    0xc3,0xc6,0x09,0xd5,0x9f,0xfa,0x66,0xf9,0xd8,0xf0,0xf7,0xa0,0x90,0xa1,0xd6,0xf3,
    0xc3,0xf3,0xd6,0xa1,0x90,0xa0,0xf7,0xf0,0xd8,0xf9,0x66,0xfa,0x9f,0xd5,0x09,0xc6,
    0xc3,0x1d,0x95,0xde,0x9f,0x84,0x11,0xf4,0xd8,0x51,0x3f,0xaa,0x90,0x3d,0x74,0x47,
    0xc3,0x0e,0x74,0xbb,0x90,0xbc,0x3f,0x92,0xd8,0x7e,0x11,0x13,0x9f,0x23,0x95,0x5e,
    0xc3,0x00,0x09,0x5b,0x9f,0x62,0x66,0xa1,0xd8,0x52,0xf7,0x67,0x90,0xca,0xd6,0x4a
};
static const uint8_t kgm_mend[272] = {
    0xb8,0xd5,0x3d,0xb2,0xe9,0xaf,0x78,0x8c,0x83,0x33,0x71,0x51,0x76,0xa0,0xcd,0x37,
    0x2f,0x3e,0x35,0x8d,0xa9,0xbe,0x98,0xb7,0xe7,0x8c,0x22,0xce,0x5a,0x61,0xdf,0x68,
    0x69,0x89,0xfe,0xa5,0xb6,0xde,0xa9,0x77,0xfc,0xc8,0xbd,0xbd,0xe5,0x6d,0x3e,0x5a,
    0x36,0xef,0x69,0x4e,0xbe,0xe1,0xe9,0x66,0x1c,0xf3,0xd9,0x02,0xb6,0xf2,0x12,0x9b,
    0x44,0xd0,0x6f,0xb9,0x35,0x89,0xb6,0x46,0x6d,0x73,0x82,0x06,0x69,0xc1,0xed,0xd7,
    0x85,0xc2,0x30,0xdf,0xa2,0x62,0xbe,0x79,0x2d,0x62,0x62,0x3d,0x0d,0x7e,0xbe,0x48,
    0x89,0x23,0x02,0xa0,0xe4,0xd5,0x75,0x51,0x32,0x02,0x53,0xfd,0x16,0x3a,0x21,0x3b,
    0x16,0x0f,0xc3,0xb2,0xbb,0xb3,0xe2,0xba,0x3a,0x3d,0x13,0xec,0xf6,0x01,0x45,0x84,
    0xa5,0x70,0x0f,0x93,0x49,0x0c,0x64,0xcd,0x31,0xd5,0xcc,0x4c,0x07,0x01,0x9e,0x00,
    0x1a,0x23,0x90,0xbf,0x88,0x1e,0x3b,0xab,0xa6,0x3e,0xc4,0x73,0x47,0x10,0x7e,0x3b,
    0x5e,0xbc,0xe3,0x00,0x84,0xff,0x09,0xd4,0xe0,0x89,0x0f,0x5b,0x58,0x70,0x4f,0xfb,
    0x65,0xd8,0x5c,0x53,0x1b,0xd3,0xc8,0xc6,0xbf,0xef,0x98,0xb0,0x50,0x4f,0x0f,0xea,
    0xe5,0x83,0x58,0x8c,0x28,0x2c,0x84,0x67,0xcd,0xd0,0x9e,0x47,0xdb,0x27,0x50,0xca,
    0xf4,0x63,0x63,0xe8,0x97,0x7f,0x1b,0x4b,0x0c,0xc2,0xc1,0x21,0x4c,0xcc,0x58,0xf5,
    0x94,0x52,0xa3,0xf3,0xd3,0xe0,0x68,0xf4,0x00,0x23,0xf3,0x5e,0x0a,0x7b,0x93,0xdd,
    0xab,0x12,0xb2,0x13,0xe8,0x84,0xd7,0xa7,0x9f,0x0f,0x32,0x4c,0x55,0x1d,0x04,0x36,
    0x52,0xdc,0x03,0xf3,0xf9,0x4e,0x42,0xe9,0x3d,0x61,0xef,0x7c,0xb6,0xb3,0x93,0x50
};



static void set_note(QwConvertReport *report, const char *note)
{
    if (report) snprintf(report->note, sizeof(report->note), "%s", note);
}

static int set_output(uint8_t *data, size_t size, uint8_t **out, size_t *out_size)
{
    if (!out || !out_size) { free(data); return QW_STATUS_ERR_INVALID_ARGUMENT; }
    *out = data;
    *out_size = size;
    return QW_STATUS_OK;
}

#if defined(QUIREWISE_ENCRYPTED_MUSIC_PARSER_ONLY)
static int encrypted_audio_prefix(const uint8_t *bytes, size_t size,
                                  const uint8_t *signature,
                                  size_t signature_size)
{
    return bytes && size >= signature_size &&
           memcmp(bytes, signature, signature_size) == 0;
}

static QwInputFormat encrypted_audio_detect_format(const void *data, size_t size)
{
    static const uint8_t voc_signature[] = {
        'C', 'r', 'e', 'a', 't', 'i', 'v', 'e', ' ', 'V', 'o', 'i', 'c', 'e',
        ' ', 'F', 'i', 'l', 'e', 0x1au
    };
    static const uint8_t wma_signature[16] = {
        0x30,0x26,0xb2,0x75,0x8e,0x66,0xcf,0x11,
        0xa6,0xd9,0x00,0xaa,0x00,0x62,0xce,0x6c
    };
    static const uint8_t w64_riff[16] = {
        0x72,0x69,0x66,0x66,0x2e,0x91,0xcf,0x11,
        0xa5,0xd6,0x28,0xdb,0x04,0xc1,0x00,0x00
    };
    static const uint8_t ac3_signature[2] = {0x0b,0x77};
    const uint8_t *bytes = (const uint8_t *)data;
    if (!bytes || !size) return QW_FORMAT_UNKNOWN;
    if (size >= 4u && memcmp(bytes, "fLaC", 4u) == 0) return QW_FORMAT_FLAC;
    if (size >= 4u && memcmp(bytes, "OggS", 4u) == 0) return QW_FORMAT_OGG;
    if (size >= 4u && memcmp(bytes, "caff", 4u) == 0) return QW_FORMAT_CAF;
    if (size >= 4u && memcmp(bytes, "MAC ", 4u) == 0) return QW_FORMAT_APE;
    if (size >= 4u && memcmp(bytes, "wvpk", 4u) == 0) return QW_FORMAT_WAVPACK;
    if (size >= 4u && memcmp(bytes, "TTA1", 4u) == 0) return QW_FORMAT_TTA;
    if (size >= 4u && memcmp(bytes, "tBaK", 4u) == 0) return QW_FORMAT_TAK;
    if (size >= 4u && (memcmp(bytes, "MP+", 3u) == 0 ||
                       memcmp(bytes, "MPCK", 4u) == 0)) return QW_FORMAT_MPC;
    if (size >= 4u && memcmp(bytes, "DSD ", 4u) == 0) return QW_FORMAT_DSF;
    if (size >= 12u && memcmp(bytes, "FRM8", 4u) == 0 &&
        memcmp(bytes + 12u, "DSD ", 4u) == 0) return QW_FORMAT_DFF;
    if (size >= 12u && memcmp(bytes, "FORM", 4u) == 0 &&
        (memcmp(bytes + 8u, "AIFF", 4u) == 0 ||
         memcmp(bytes + 8u, "AIFC", 4u) == 0)) return QW_FORMAT_AIFF;
    if (size >= 12u &&
        (memcmp(bytes, "RIFF", 4u) == 0 || memcmp(bytes, "RIFX", 4u) == 0 ||
         memcmp(bytes, "RF64", 4u) == 0 || memcmp(bytes, "BW64", 4u) == 0) &&
        memcmp(bytes + 8u, "WAVE", 4u) == 0) return QW_FORMAT_WAV;
    if (size >= 12u && memcmp(bytes + 4u, "ftyp", 4u) == 0) return QW_FORMAT_M4A;
    if (encrypted_audio_prefix(bytes, size, (const uint8_t *)".snd", 4u))
        return QW_FORMAT_AU;
    if (encrypted_audio_prefix(bytes, size, voc_signature, sizeof(voc_signature)))
        return QW_FORMAT_VOC;
    if (encrypted_audio_prefix(bytes, size, wma_signature, sizeof(wma_signature)))
        return QW_FORMAT_WMA;
    if (encrypted_audio_prefix(bytes, size, w64_riff, sizeof(w64_riff)))
        return QW_FORMAT_W64;
    if (encrypted_audio_prefix(bytes, size, ac3_signature, sizeof(ac3_signature)))
        return QW_FORMAT_AC3;
    if (size >= 4u && memcmp(bytes, "\x1a\x45\xdf\xa3", 4u) == 0)
        return QW_FORMAT_MATROSKA;
    if (size >= 5u && memcmp(bytes, "#!AMR", 5u) == 0)
        return QW_FORMAT_AMR;
    if (size >= 4u && (memcmp(bytes, "\x7f\xfe\x80\x01", 4u) == 0 ||
                       memcmp(bytes, "\xfe\x7f\x01\x80", 4u) == 0 ||
                       memcmp(bytes, "\x1f\xff\xe8\x00", 4u) == 0 ||
                       memcmp(bytes, "\xff\x1f\x00\xe8", 4u) == 0))
        return QW_FORMAT_DTS;
    if (size >= 2u && bytes[0] == 0xffu && (bytes[1] & 0xf6u) == 0xf0u)
        return QW_FORMAT_AAC;
    if (size >= 2u && bytes[0] == 0xffu && (bytes[1] & 0xe0u) == 0xe0u &&
        (bytes[1] & 0x06u) != 0u) return QW_FORMAT_MP3;
    return QW_FORMAT_UNKNOWN;
}

QwInputFormat qw_audio_detect_format(const void *data, size_t size)
{
    return encrypted_audio_detect_format(data, size);
}

int qw_audio_input_format_is(QwInputFormat format)
{
    return format != QW_FORMAT_UNKNOWN;
}
#endif

static uint8_t scramble(uint8_t value)
{
    return (uint8_t)(value ^ ((value & 0x0fu) << 4));
}

static FILE *open_sibling_kugou_key(void)
{
#if defined(_WIN32)
    size_t capacity = 32768u;
    wchar_t *path = (wchar_t *)malloc(capacity * sizeof(*path));
    DWORD length;
    FILE *file = NULL;
    if (!path) return NULL;
    length = GetModuleFileNameW(NULL, path, (DWORD)capacity);
    if (length && length < capacity) {
        size_t directory_end = length;
        while (directory_end && path[directory_end - 1u] != L'\\' &&
               path[directory_end - 1u] != L'/') --directory_end;
        if (directory_end && directory_end + 12u < capacity) {
            static const wchar_t key_name[] = L"kugou_key.xz";
            memcpy(path + directory_end, key_name, sizeof(key_name));
            file = _wfopen(path, L"rb");
        }
    }
    free(path);
    return file;
#else
    char executable[4096];
    ssize_t length = readlink("/proc/self/exe", executable,
                              sizeof(executable) - 1u);
    if (length > 0 && (size_t)length < sizeof(executable) - 1u) {
        char *slash;
        executable[length] = '\0';
        slash = strrchr(executable, '/');
        if (slash && (size_t)(slash - executable) + sizeof("kugou_key.xz") <
                     sizeof(executable)) {
            memcpy(slash + 1u, "kugou_key.xz", sizeof("kugou_key.xz"));
            return fopen(executable, "rb");
        }
    }
    return NULL;
#endif
}

static FILE *open_music_file_utf8(const char *path)
{
    if (!path || !*path) return NULL;
#if defined(_WIN32)
    {
        wchar_t *wide_path = qw_platform_utf8_to_wide(path);
        FILE *file;
        if (!wide_path) return NULL;
        file = _wfopen(wide_path, L"rb");
        free(wide_path);
        return file;
    }
#else
    return fopen(path, "rb");
#endif
}

static void qmc1_xor(uint8_t *data, size_t size, size_t absolute_offset)
{
    for (size_t i = 0; i < size; ++i) {
        size_t pos = absolute_offset + i;
        size_t map_pos = pos <= 0x7fffu ? pos : pos % 0x7fffu;
        data[i] ^= qmc_v1_key[map_pos % sizeof(qmc_v1_key)];
    }
}

static int valid_ncm(const void *data, size_t size)
{
    return data && size >= 8u && memcmp(data, ncm_magic, sizeof(ncm_magic)) == 0;
}

static int valid_kgm(const void *data, size_t size)
{
    const uint8_t *bytes = (const uint8_t *)data;
    return bytes && size >= sizeof(kgm_magic) &&
           (memcmp(bytes, kgm_magic, sizeof(kgm_magic)) == 0 ||
            memcmp(bytes, vpr_magic, sizeof(vpr_magic)) == 0);
}

static int qmc_footer_present(const uint8_t *data, size_t size)
{
    return data && ((size >= 4u &&
                     (memcmp(data + size - 4u, "QTag", 4u) == 0 ||
                      memcmp(data + size - 4u, "STag", 4u) == 0)) ||
                    (size >= 8u && memcmp(data + size - 8u, "musicex\0", 8u) == 0));
}

static int ncm_probe(const void *data, size_t size) { return valid_ncm(data, size); }
static int kgm_probe(const void *data, size_t size) { return valid_kgm(data, size); }

static int qmc_probe(const void *data, size_t size)
{
    uint8_t sample[64];
    if (!data || size < 64u) return qmc_footer_present((const uint8_t *)data, size);
    if (qmc_footer_present((const uint8_t *)data, size)) return 1;
    memcpy(sample, data, sizeof(sample));
    qmc1_xor(sample, sizeof(sample), 0);
    return qw_audio_detect_format(sample, sizeof(sample)) != QW_FORMAT_UNKNOWN;
}

QwInputFormat qw_encrypted_music_probe_format(const void *data, size_t size)
{
    if (ncm_probe(data, size)) return QW_FORMAT_NCM;
    if (qmc_probe(data, size)) return QW_FORMAT_QMC;
    if (kgm_probe(data, size)) return QW_FORMAT_KGM;
    return QW_FORMAT_UNKNOWN;
}

static int ncm_path_probe(const char *path)
{
    return qw_encrypted_music_format_from_path_internal(path) == QW_FORMAT_NCM;
}

static int qmc_path_probe(const char *path)
{
    return qw_encrypted_music_format_from_path_internal(path) == QW_FORMAT_QMC;
}

static int kgm_path_probe(const char *path)
{
    return qw_encrypted_music_format_from_path_internal(path) == QW_FORMAT_KGM;
}

static int kwm_path_probe(const char *path)
{
    return qw_encrypted_music_format_from_path_internal(path) == QW_FORMAT_KWM;
}


















const QwPathFormatDescriptor qw_path_format_ncm = {
    QW_FORMAT_NCM, ncm_path_probe, NULL, NULL
};
const QwPathFormatDescriptor qw_path_format_qmc = {
    QW_FORMAT_QMC, qmc_path_probe, NULL, NULL
};
const QwPathFormatDescriptor qw_path_format_kgm = {
    QW_FORMAT_KGM, kgm_path_probe, NULL, NULL
};
const QwPathFormatDescriptor qw_path_format_kwm = {
    QW_FORMAT_KWM, kwm_path_probe, NULL, NULL
};

int qw_encrypted_music_format_is(QwInputFormat format)
{
    return format == QW_FORMAT_NCM || format == QW_FORMAT_QMC ||
           format == QW_FORMAT_KGM || format == QW_FORMAT_KWM;
}

static int ends_with_ci(const char *text, const char *suffix)
{
    size_t a, b;
    if (!text || !suffix) return 0;
    a = strlen(text); b = strlen(suffix);
    if (a < b) return 0;
    text += a - b;
    for (size_t i = 0; i < b; ++i) {
        unsigned char x = (unsigned char)text[i], y = (unsigned char)suffix[i];
        if (x >= 'A' && x <= 'Z') x = (unsigned char)(x + 32);
        if (y >= 'A' && y <= 'Z') y = (unsigned char)(y + 32);
        if (x != y) return 0;
    }
    return 1;
}

QwInputFormat qw_encrypted_music_format_from_path_internal(const char *path)
{
    static const char *const qmc_suffixes[] = {
        ".qmc", ".qmc0", ".qmc1", ".qmc2", ".qmc3", ".qmc4", ".qmc5", ".qmc6", ".qmc7", ".qmc8",
        ".qmcflac", ".qmcogg", ".mflac", ".mflac0", ".mflac1", ".mgg",
        ".mgg0", ".mgg1", ".mggl", ".mmp4", ".tkm", ".bkcmp3", ".bkcm4a",
        ".bkcflac", ".bkcwav", ".bkcape", ".bkcogg", ".bkcwma",
        ".666c6163", ".6d7033", ".6f6767", ".6d3461", ".776176"
    };
    if (ends_with_ci(path, ".ncm")) return QW_FORMAT_NCM;
    if (ends_with_ci(path, ".kwm")) return QW_FORMAT_KWM;
    if (ends_with_ci(path, ".kgm") || ends_with_ci(path, ".kgma") ||
        ends_with_ci(path, ".vpr") || ends_with_ci(path, ".kgm.flac") ||
        ends_with_ci(path, ".vpr.flac")) return QW_FORMAT_KGM;
    for (size_t i = 0; i < sizeof(qmc_suffixes) / sizeof(qmc_suffixes[0]); ++i) {
        if (ends_with_ci(path, qmc_suffixes[i])) return QW_FORMAT_QMC;
    }
    return QW_FORMAT_UNKNOWN;
}

static int decrypt_aes_ecb(uint8_t *data, size_t size, const uint8_t key[16])
{
    struct AES_ctx ctx;
    if (!data || !key || (size & 15u)) return 0;
    AES_init_ctx(&ctx, key);
    for (size_t i = 0; i < size; i += 16u) AES_ECB_decrypt(&ctx, data + i);
    return 1;
}

static void ncm_json_skip_space(QwNcmJsonParser *parser)
{
    while (parser->position < parser->size) {
        uint8_t ch = parser->data[parser->position];
        if (ch != ' ' && ch != '\t' && ch != '\r' && ch != '\n') break;
        ++parser->position;
    }
}

static int ncm_json_hex(uint8_t ch)
{
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return -1;
}

static int ncm_json_read_u16(QwNcmJsonParser *parser, uint16_t *value)
{
    uint16_t result = 0;
    if (!parser || !value || parser->size - parser->position < 4u) return 0;
    for (size_t i = 0; i < 4u; ++i) {
        int digit = ncm_json_hex(parser->data[parser->position + i]);
        if (digit < 0) return 0;
        result = (uint16_t)((result << 4) | (uint16_t)digit);
    }
    parser->position += 4u;
    *value = result;
    return 1;
}

static size_t ncm_json_write_utf8(uint8_t *output, uint32_t codepoint)
{
    if (codepoint <= 0x7fu) {
        output[0] = (uint8_t)codepoint;
        return 1u;
    }
    if (codepoint <= 0x7ffu) {
        output[0] = (uint8_t)(0xc0u | (codepoint >> 6));
        output[1] = (uint8_t)(0x80u | (codepoint & 0x3fu));
        return 2u;
    }
    if (codepoint <= 0xffffu) {
        output[0] = (uint8_t)(0xe0u | (codepoint >> 12));
        output[1] = (uint8_t)(0x80u | ((codepoint >> 6) & 0x3fu));
        output[2] = (uint8_t)(0x80u | (codepoint & 0x3fu));
        return 3u;
    }
    output[0] = (uint8_t)(0xf0u | (codepoint >> 18));
    output[1] = (uint8_t)(0x80u | ((codepoint >> 12) & 0x3fu));
    output[2] = (uint8_t)(0x80u | ((codepoint >> 6) & 0x3fu));
    output[3] = (uint8_t)(0x80u | (codepoint & 0x3fu));
    return 4u;
}

static int ncm_json_read_string(QwNcmJsonParser *parser, char **out)
{
    size_t remaining;
    size_t used = 0;
    uint8_t *text;

    if (!parser || !out || parser->position >= parser->size ||
        parser->data[parser->position] != '"') return 0;
    remaining = parser->size - parser->position;
    if (remaining == SIZE_MAX) return 0;
    text = (uint8_t *)malloc(remaining + 1u);
    if (!text) return 0;
    ++parser->position;

    while (parser->position < parser->size) {
        uint8_t ch = parser->data[parser->position++];
        if (ch == '"') {
            text[used] = '\0';
            *out = (char *)text;
            return 1;
        }
        if (ch < 0x20u) break;
        if (ch != '\\') {
            text[used++] = ch;
            continue;
        }
        if (parser->position >= parser->size) break;
        ch = parser->data[parser->position++];
        switch (ch) {
        case '"': case '\\': case '/': text[used++] = ch; break;
        case 'b': text[used++] = '\b'; break;
        case 'f': text[used++] = '\f'; break;
        case 'n': text[used++] = '\n'; break;
        case 'r': text[used++] = '\r'; break;
        case 't': text[used++] = '\t'; break;
        case 'u': {
            uint16_t first;
            uint32_t codepoint;
            if (!ncm_json_read_u16(parser, &first)) goto invalid;
            if (first >= 0xd800u && first <= 0xdbffu) {
                uint16_t second;
                if (parser->size - parser->position < 2u ||
                    parser->data[parser->position] != '\\' ||
                    parser->data[parser->position + 1u] != 'u') goto invalid;
                parser->position += 2u;
                if (!ncm_json_read_u16(parser, &second) ||
                    second < 0xdc00u || second > 0xdfffu) goto invalid;
                codepoint = 0x10000u +
                    (((uint32_t)first - 0xd800u) << 10) +
                    ((uint32_t)second - 0xdc00u);
            } else {
                if (first >= 0xdc00u && first <= 0xdfffu) goto invalid;
                codepoint = first;
            }
            used += ncm_json_write_utf8(text + used, codepoint);
            break;
        }
        default: goto invalid;
        }
    }

invalid:
    free(text);
    return 0;
}

static int ncm_json_skip_value(QwNcmJsonParser *parser, unsigned depth)
{
    uint8_t open;
    if (!parser || depth > 64u) return 0;
    ncm_json_skip_space(parser);
    if (parser->position >= parser->size) return 0;
    open = parser->data[parser->position];
    if (open == '"') {
        char *discard = NULL;
        int rc = ncm_json_read_string(parser, &discard);
        free(discard);
        return rc;
    }
    if (open == '{' || open == '[') {
        uint8_t close = open == '{' ? '}' : ']';
        ++parser->position;
        ncm_json_skip_space(parser);
        if (parser->position < parser->size &&
            parser->data[parser->position] == close) {
            ++parser->position;
            return 1;
        }
        for (;;) {
            if (open == '{') {
                char *key = NULL;
                if (!ncm_json_read_string(parser, &key)) return 0;
                free(key);
                ncm_json_skip_space(parser);
                if (parser->position >= parser->size ||
                    parser->data[parser->position++] != ':') return 0;
            }
            if (!ncm_json_skip_value(parser, depth + 1u)) return 0;
            ncm_json_skip_space(parser);
            if (parser->position >= parser->size) return 0;
            if (parser->data[parser->position] == close) {
                ++parser->position;
                return 1;
            }
            if (parser->data[parser->position++] != ',') return 0;
            ncm_json_skip_space(parser);
        }
    }
    {
        size_t start = parser->position;
        while (parser->position < parser->size) {
            uint8_t ch = parser->data[parser->position];
            if (ch == ',' || ch == ']' || ch == '}' || ch == ' ' ||
                ch == '\t' || ch == '\r' || ch == '\n') break;
            ++parser->position;
        }
        return parser->position > start;
    }
}

static int ncm_json_read_artist(QwNcmJsonParser *parser, char **artist)
{
    char *first_artist = NULL;
    if (parser->position >= parser->size ||
        parser->data[parser->position] != '[') {
        return ncm_json_skip_value(parser, 0u);
    }
    ++parser->position;
    ncm_json_skip_space(parser);
    if (parser->position < parser->size && parser->data[parser->position] == ']') {
        ++parser->position;
        return 1;
    }
    if (parser->position < parser->size && parser->data[parser->position] == '[') {
        ++parser->position;
        ncm_json_skip_space(parser);
        if (parser->position < parser->size && parser->data[parser->position] == ']') {
            ++parser->position;
        } else if (parser->position < parser->size && parser->data[parser->position] == '"') {
            if (!ncm_json_read_string(parser, &first_artist)) return 0;
            ncm_json_skip_space(parser);
            while (parser->position < parser->size && parser->data[parser->position] == ',') {
                ++parser->position;
                if (!ncm_json_skip_value(parser, 1u)) goto invalid;
                ncm_json_skip_space(parser);
            }
            if (parser->position >= parser->size || parser->data[parser->position++] != ']')
                goto invalid;
        } else {
            if (!ncm_json_skip_value(parser, 1u)) goto invalid;
            ncm_json_skip_space(parser);
            while (parser->position < parser->size && parser->data[parser->position] == ',') {
                ++parser->position;
                if (!ncm_json_skip_value(parser, 1u)) goto invalid;
                ncm_json_skip_space(parser);
            }
            if (parser->position >= parser->size || parser->data[parser->position++] != ']')
                goto invalid;
        }
    } else if (parser->position < parser->size && parser->data[parser->position] == '"') {
        if (!ncm_json_read_string(parser, &first_artist)) return 0;
    } else if (!ncm_json_skip_value(parser, 0u)) {
        return 0;
    }

    ncm_json_skip_space(parser);
    while (parser->position < parser->size && parser->data[parser->position] == ',') {
        ++parser->position;
        if (!ncm_json_skip_value(parser, 0u)) goto invalid;
        ncm_json_skip_space(parser);
    }
    if (parser->position >= parser->size || parser->data[parser->position++] != ']')
        goto invalid;
    free(*artist);
    *artist = first_artist;
    return 1;

invalid:
    free(first_artist);
    return 0;
}

static int ncm_json_parse_metadata(const uint8_t *data, size_t size,
                                   QwNcmMetadata *metadata)
{
    QwNcmJsonParser parser = {data, size, 0u};
    int valid = 0;
    if (!data || !metadata) return 0;
    ncm_json_skip_space(&parser);
    if (parser.position >= parser.size || parser.data[parser.position++] != '{') return 0;
    ncm_json_skip_space(&parser);
    if (parser.position < parser.size && parser.data[parser.position] == '}') {
        ++parser.position;
        return 1;
    }
    for (;;) {
        char *key = NULL;
        char **target = NULL;
        if (!ncm_json_read_string(&parser, &key)) goto done;
        ncm_json_skip_space(&parser);
        if (parser.position >= parser.size || parser.data[parser.position++] != ':') {
            free(key);
            goto done;
        }
        ncm_json_skip_space(&parser);
        if (strcmp(key, "musicName") == 0) target = &metadata->title;
        else if (strcmp(key, "album") == 0) target = &metadata->album;
        if (strcmp(key, "artist") == 0) {
            if (!ncm_json_read_artist(&parser, &metadata->artist)) {
                free(key);
                goto done;
            }
        } else if (target && parser.position < parser.size &&
                   parser.data[parser.position] == '"') {
            char *value = NULL;
            if (!ncm_json_read_string(&parser, &value)) {
                free(key);
                goto done;
            }
            free(*target);
            *target = value;
        } else if (!ncm_json_skip_value(&parser, 0u)) {
            free(key);
            goto done;
        }
        free(key);
        ncm_json_skip_space(&parser);
        if (parser.position >= parser.size) goto done;
        if (parser.data[parser.position] == '}') {
            ++parser.position;
            ncm_json_skip_space(&parser);
            valid = parser.position == parser.size;
            break;
        }
        if (parser.data[parser.position++] != ',') goto done;
        ncm_json_skip_space(&parser);
    }

done:
    if (!valid) {
        free(metadata->title);
        free(metadata->artist);
        free(metadata->album);
        metadata->title = metadata->artist = metadata->album = NULL;
    }
    return valid;
}

static void ncm_decode_metadata(const uint8_t *input, size_t size,
                                QwNcmMetadata *metadata)
{
    static const uint8_t prefix[] = "163 key(Don't modify):";
    static const uint8_t meta_key[16] = {
        0x23,0x31,0x34,0x6c,0x6a,0x6b,0x5f,0x21,
        0x5c,0x5d,0x26,0x30,0x55,0x3c,0x27,0x28
    };
    uint8_t *encoded = NULL;
    uint8_t *plain = NULL;
    size_t plain_size;

    if (!input || !size || size > NCM_MAX_METADATA_SIZE || !metadata) return;
    encoded = (uint8_t *)malloc(size);
    if (!encoded) return;
    for (size_t i = 0; i < size; ++i) encoded[i] = input[i] ^ 0x63u;
    if (size <= sizeof(prefix) - 1u ||
        memcmp(encoded, prefix, sizeof(prefix) - 1u) != 0 ||
        !base64_decode(encoded + sizeof(prefix) - 1u,
                       size - (sizeof(prefix) - 1u), &plain, &plain_size) ||
        !plain_size || (plain_size & 15u) ||
        !decrypt_aes_ecb(plain, plain_size, meta_key)) {
        goto done;
    }
    {
        uint8_t padding = plain[plain_size - 1u];
        if (!padding || padding > 16u || padding > plain_size) goto done;
        for (size_t i = 0; i < padding; ++i) {
            if (plain[plain_size - 1u - i] != padding) goto done;
        }
        plain_size -= padding;
    }
    if (plain_size > 6u && memcmp(plain, "music:", 6u) == 0) {
        (void)ncm_json_parse_metadata(plain + 6u, plain_size - 6u, metadata);
    }

done:
    free(encoded);
    free(plain);
}

#if !defined(QUIREWISE_ENCRYPTED_MUSIC_PARSER_ONLY)
static int ncm_read_metadata(const uint8_t *input,
                             size_t size,
                             QwNcmMetadata *metadata)
{
    size_t position = NCM_HEADER_SIZE;
    size_t key_size;
    size_t metadata_size;
    size_t image_space;
    size_t image_size;
    if (!input || !metadata || size < NCM_HEADER_SIZE + 4u ||
        !valid_ncm(input, size)) return QW_STATUS_ERR_FORMAT;
    key_size = qw_read_u32_le(input + position);
    position += 4u;
    if (!key_size || (key_size & 15u) || key_size > size - position) {
        return QW_STATUS_ERR_FORMAT;
    }
    position += key_size;
    if (size - position < 4u) return QW_STATUS_ERR_FORMAT;
    metadata_size = qw_read_u32_le(input + position);
    position += 4u;
    if (metadata_size > size - position) return QW_STATUS_ERR_FORMAT;
    ncm_decode_metadata(input + position, metadata_size, metadata);
    position += metadata_size;
    if (size - position < 13u) return QW_STATUS_ERR_FORMAT;
    position += 5u;
    image_space = qw_read_u32_le(input + position);
    position += 4u;
    image_size = qw_read_u32_le(input + position);
    position += 4u;
    if (image_space > size - position || image_size > size - position) {
        return QW_STATUS_ERR_FORMAT;
    }
    if (image_size) {
        const uint8_t *cover = input + position;
        if ((image_size >= 8u &&
             memcmp(cover, "\x89PNG\r\n\x1a\n", 8u) == 0) ||
            (image_size >= 3u && cover[0] == 0xffu &&
             cover[1] == 0xd8u && cover[2] == 0xffu)) {
            metadata->cover_data = cover;
            metadata->cover_size = image_size;
        }
    }
    return QW_STATUS_OK;
}
#endif

static int ncm_decrypt(const uint8_t *input, size_t size,
                       uint8_t **out, size_t *out_size,
                       QwNcmMetadata *metadata,
                       QwConvertReport *report)
{
    uint8_t *key_blob = NULL, *plain_key = NULL, *audio = NULL;
    size_t pos = NCM_HEADER_SIZE, key_len, meta_len, image_space, image_size;
    size_t plain_size, audio_size, key_size;
    uint8_t sbox[256], box[256];
    uint8_t core_key[16];
    int rc = QW_STATUS_ERR_FORMAT;
    memcpy(core_key, ncm_core_key, sizeof(core_key));
    if (size < 14u || !valid_ncm(input, size)) goto done;
    key_len = qw_read_u32_le(input + pos); pos += 4u;
    if (!key_len || key_len > size - pos || (key_len & 15u)) goto done;
    key_blob = (uint8_t *)malloc(key_len);
    if (!key_blob) return QW_STATUS_ERR_OUT_OF_MEMORY;
    for (size_t i = 0; i < key_len; ++i) key_blob[i] = input[pos + i] ^ 0x64u;
    pos += key_len;
    if (!decrypt_aes_ecb(key_blob, key_len, core_key)) goto done;
    {
        uint8_t pad = key_blob[key_len - 1u];
        if (!pad || pad > 16u || pad > key_len) goto done;
        for (size_t i = 0; i < pad; ++i) if (key_blob[key_len - 1u - i] != pad) goto done;
        plain_size = key_len - pad;
    }
    if (plain_size <= 17u || memcmp(key_blob, "neteasecloudmusic", 17u) != 0) goto done;
    plain_key = key_blob + 17u;
    key_size = plain_size - 17u;
    if (pos + 4u > size) goto done;
    meta_len = qw_read_u32_le(input + pos); pos += 4u;
    if (meta_len > size - pos) goto done;
    if (metadata) ncm_decode_metadata(input + pos, meta_len, metadata);
    pos += meta_len;
    if (size - pos < 5u) goto done;
    pos += 5u;
    if (size - pos < 8u) goto done;
    image_space = qw_read_u32_le(input + pos); pos += 4u;
    image_size = qw_read_u32_le(input + pos); pos += 4u;
    if (image_space > size - pos) goto done;
    if (image_size > size - pos) goto done;
    if (metadata && image_size) {
        const uint8_t *cover = input + pos;
        if ((image_size >= 8u &&
             memcmp(cover, "\x89PNG\r\n\x1a\n", 8u) == 0) ||
            (image_size >= 3u && cover[0] == 0xffu &&
             cover[1] == 0xd8u && cover[2] == 0xffu)) {
            metadata->cover_data = cover;
            metadata->cover_size = image_size;
        }
    }
    pos += image_space;
    audio_size = size - pos;
    if (!audio_size) goto done;
    audio = (uint8_t *)malloc(audio_size);
    if (!audio) { rc = QW_STATUS_ERR_OUT_OF_MEMORY; goto done; }
    for (size_t i = 0; i < 256u; ++i) sbox[i] = (uint8_t)i;
    {
        uint8_t j = 0;
        for (size_t i = 0; i < 256u; ++i) {
            uint8_t tmp;
            j = (uint8_t)(j + sbox[i] + plain_key[i % key_size]);
            tmp = sbox[i]; sbox[i] = sbox[j]; sbox[j] = tmp;
        }
    }
    for (size_t i = 0; i < 256u; ++i)
        box[i] = sbox[(uint8_t)(sbox[i] + sbox[(uint8_t)(i + sbox[i])])];
    for (size_t i = 0; i < audio_size; ++i) audio[i] = input[pos + i] ^ box[(i + 1u) & 255u];
    rc = set_output(audio, audio_size, out, out_size);
    audio = NULL;
    if (rc != QW_STATUS_OK) set_note(report, "NCM output allocation failed");
done:
    free(key_blob);
    free(audio);
    if (rc != QW_STATUS_OK && report && !report->note[0]) set_note(report, "NCM structure or encrypted key is invalid");
    return rc;
}

#if !defined(QUIREWISE_ENCRYPTED_MUSIC_PARSER_ONLY)
typedef struct QwNcmAudioDocumentContext {
    QwNcmMetadata metadata;
    QwAudioTag tags[3];
    QwAudioTag *combined_tags;
    size_t combined_tag_count;
    uint8_t *decoded_data;
    size_t decoded_size;
    QwAudioDocument decoded_document;
} QwNcmAudioDocumentContext;

typedef struct QwEncryptedAudioDocumentContext {
    uint8_t *decoded_data;
    size_t decoded_size;
    QwAudioDocument decoded_document;
} QwEncryptedAudioDocumentContext;

static void ncm_audio_document_dispose(void *opaque)
{
    QwNcmAudioDocumentContext *context =
        (QwNcmAudioDocumentContext *)opaque;
    if (!context) return;
    qw_audio_document_dispose(&context->decoded_document);
    free(context->decoded_data);
    free(context->combined_tags);
    ncm_metadata_clear(&context->metadata);
    free(context);
}

static void encrypted_audio_document_dispose(void *opaque)
{
    QwEncryptedAudioDocumentContext *context =
        (QwEncryptedAudioDocumentContext *)opaque;
    if (!context) return;
    qw_audio_document_dispose(&context->decoded_document);
    free(context->decoded_data);
    free(context);
}

int qw_encrypted_music_format_open_document(
    const struct QwSource *source,
    QwInputFormat input_format,
    const QwConvertOptions *options,
    QwAudioDocument *out_document,
    QwConvertReport *report)
{
    QwNcmAudioDocumentContext *context;
    size_t tag_count = 0u;
    int rc;
    if (!qw_encrypted_music_format_is(input_format)) return QW_STATUS_ERR_UNSUPPORTED;
    if (!qw_audio_document_from_source(source, input_format, options,
                                       out_document)) {
        return QW_STATUS_ERR_FORMAT;
    }
    if (input_format != QW_FORMAT_NCM) {
        QwEncryptedAudioDocumentContext *decoded_context;
        QwSource decoded_source;
        QwInputFormat decoded_format;
        if (!source || !source->data || !source->size) {
            qw_audio_document_dispose(out_document);
            return QW_STATUS_ERR_FORMAT;
        }
        decoded_context = (QwEncryptedAudioDocumentContext *)calloc(
            1u, sizeof(*decoded_context));
        if (!decoded_context) {
            qw_audio_document_dispose(out_document);
            return QW_STATUS_ERR_OUT_OF_MEMORY;
        }
        rc = qw_encrypted_music_decrypt(
            input_format, source->data, source->size,
            out_document->qmc_ekey, out_document->qmc_ekey_db_path,
            source->path, &decoded_context->decoded_data,
            &decoded_context->decoded_size, report);
        if (rc != QW_STATUS_OK) {
            encrypted_audio_document_dispose(decoded_context);
            qw_audio_document_dispose(out_document);
            return rc;
        }
        decoded_format = qw_audio_detect_format(decoded_context->decoded_data,
                                                decoded_context->decoded_size);
        if (decoded_format == QW_FORMAT_UNKNOWN) {
            encrypted_audio_document_dispose(decoded_context);
            qw_audio_document_dispose(out_document);
            set_note(report, "decrypted music payload is not a supported audio stream");
            return QW_STATUS_ERR_DECODE;
        }
        memset(&decoded_source, 0, sizeof(decoded_source));
        decoded_source.kind = QW_SOURCE_MEMORY;
        decoded_source.data = decoded_context->decoded_data;
        decoded_source.size = decoded_context->decoded_size;
        rc = qw_audio_format_open_document(
            &decoded_source, decoded_format, options,
            &decoded_context->decoded_document, report);
        if (rc != QW_STATUS_OK) {
            encrypted_audio_document_dispose(decoded_context);
            qw_audio_document_dispose(out_document);
            return rc;
        }
        out_document->stream_data = decoded_context->decoded_data;
        out_document->stream_size = decoded_context->decoded_size;
        out_document->decoded_format = decoded_format;
        out_document->sample_rate = decoded_context->decoded_document.sample_rate;
        out_document->channels = decoded_context->decoded_document.channels;
        out_document->bitrate = decoded_context->decoded_document.bitrate;
        out_document->duration_us = decoded_context->decoded_document.duration_us;
        out_document->tags = decoded_context->decoded_document.tags;
        out_document->tag_count = decoded_context->decoded_document.tag_count;
        out_document->cover_data = decoded_context->decoded_document.cover_data;
        out_document->cover_size = decoded_context->decoded_document.cover_size;
        out_document->chapters = decoded_context->decoded_document.chapters;
        out_document->chapter_count = decoded_context->decoded_document.chapter_count;
        out_document->context = decoded_context;
        out_document->dispose = encrypted_audio_document_dispose;
        return QW_STATUS_OK;
    }
    context = (QwNcmAudioDocumentContext *)calloc(1u, sizeof(*context));
    if (!context) {
        qw_audio_document_dispose(out_document);
        return QW_STATUS_ERR_OUT_OF_MEMORY;
    }
    rc = ncm_read_metadata(source->data, source->size, &context->metadata);
    if (rc != QW_STATUS_OK) {
        ncm_audio_document_dispose(context);
        if (report && !report->note[0]) {
            set_note(report, "NCM metadata header is invalid");
        }
        return rc;
    }
    rc = qw_encrypted_music_decrypt(
        input_format, source->data, source->size,
        out_document->qmc_ekey, out_document->qmc_ekey_db_path,
        source->path, &context->decoded_data, &context->decoded_size, report);
    if (rc != QW_STATUS_OK) {
        ncm_audio_document_dispose(context);
        qw_audio_document_dispose(out_document);
        return rc;
    }
    out_document->decoded_format = qw_audio_detect_format(
        context->decoded_data, context->decoded_size);
    if (out_document->decoded_format == QW_FORMAT_UNKNOWN) {
        ncm_audio_document_dispose(context);
        qw_audio_document_dispose(out_document);
        set_note(report, "decrypted NCM payload is not a supported audio stream");
        return QW_STATUS_ERR_DECODE;
    }
    {
        QwSource decoded_source;
        size_t merged_count;
        memset(&decoded_source, 0, sizeof(decoded_source));
        decoded_source.kind = QW_SOURCE_MEMORY;
        decoded_source.data = context->decoded_data;
        decoded_source.size = context->decoded_size;
        rc = qw_audio_format_open_document(
            &decoded_source, out_document->decoded_format, options,
            &context->decoded_document, report);
        if (rc != QW_STATUS_OK) {
            ncm_audio_document_dispose(context);
            qw_audio_document_dispose(out_document);
            return rc;
        }
        if (context->metadata.title) {
            context->tags[tag_count].name = "title";
            context->tags[tag_count++].value = context->metadata.title;
        }
        if (context->metadata.artist) {
            context->tags[tag_count].name = "artist";
            context->tags[tag_count++].value = context->metadata.artist;
        }
        if (context->metadata.album) {
            context->tags[tag_count].name = "album";
            context->tags[tag_count++].value = context->metadata.album;
        }
        if (context->decoded_document.tag_count > SIZE_MAX - tag_count) {
            ncm_audio_document_dispose(context);
            qw_audio_document_dispose(out_document);
            return QW_STATUS_ERR_OUT_OF_MEMORY;
        }
        merged_count = tag_count + context->decoded_document.tag_count;
        if (merged_count > SIZE_MAX / sizeof(*context->combined_tags)) {
            ncm_audio_document_dispose(context);
            qw_audio_document_dispose(out_document);
            return QW_STATUS_ERR_OUT_OF_MEMORY;
        }
        if (merged_count) {
            context->combined_tags = (QwAudioTag *)malloc(
                merged_count * sizeof(*context->combined_tags));
            if (!context->combined_tags) {
                ncm_audio_document_dispose(context);
                qw_audio_document_dispose(out_document);
                return QW_STATUS_ERR_OUT_OF_MEMORY;
            }
            if (tag_count) {
                memcpy(context->combined_tags, context->tags,
                       tag_count * sizeof(*context->combined_tags));
            }
            if (context->decoded_document.tag_count) {
                memcpy(context->combined_tags + tag_count,
                       context->decoded_document.tags,
                       context->decoded_document.tag_count *
                           sizeof(*context->combined_tags));
            }
        }
        context->combined_tag_count = merged_count;
    }
    out_document->stream_data = context->decoded_data;
    out_document->stream_size = context->decoded_size;
    out_document->sample_rate = context->decoded_document.sample_rate;
    out_document->channels = context->decoded_document.channels;
    out_document->bitrate = context->decoded_document.bitrate;
    out_document->codec_name = context->decoded_document.codec_name;
    out_document->duration_us = context->decoded_document.duration_us;
    out_document->tags = context->combined_tags;
    out_document->tag_count = context->combined_tag_count;
    out_document->cover_data = context->metadata.cover_data
        ? context->metadata.cover_data : context->decoded_document.cover_data;
    out_document->cover_size = context->metadata.cover_data
        ? context->metadata.cover_size : context->decoded_document.cover_size;
    out_document->chapters = context->decoded_document.chapters;
    out_document->chapter_count = context->decoded_document.chapter_count;
    out_document->context = context;
    out_document->dispose = ncm_audio_document_dispose;
    return QW_STATUS_OK;
}
#endif

static int decompress_kugou_key(size_t wanted, const char *key_path,
                                uint8_t **out)
{
    const char *candidates[2] = { "third_party/music/kugou_key.xz", "kugou_key.xz" };
    uint8_t *compressed = NULL, *decoded = NULL;
    size_t compressed_size = 0;
    FILE *file = NULL;
    lzma_stream stream = LZMA_STREAM_INIT;
    lzma_ret lrc;
    int ok = 0;
    if (!wanted || wanted > 73155904u) return 0;
    if (key_path && *key_path) file = open_music_file_utf8(key_path);
    if (file) goto have_key_file;
 #if defined(_WIN32)
    {
        const wchar_t *override_path = _wgetenv(L"QUIREWISE_KGM_KEY");
        if (override_path && *override_path) file = _wfopen(override_path, L"rb");
    }
#else
    {
        const char *override_path = getenv("QUIREWISE_KGM_KEY");
        if (override_path && *override_path) file = open_music_file_utf8(override_path);
    }
#endif
have_key_file:
    for (size_t i = 0; i < 2u && !file; ++i) {
        if (candidates[i] && *candidates[i]) file = open_music_file_utf8(candidates[i]);
    }
    if (!file) file = open_sibling_kugou_key();
    if (!file) return 0;
    if (fseek(file, 0, SEEK_END) != 0) goto done;
    {
        long n = ftell(file);
        if (n <= 0 || fseek(file, 0, SEEK_SET) != 0) goto done;
        compressed_size = (size_t)n;
    }
    compressed = (uint8_t *)malloc(compressed_size);
    decoded = (uint8_t *)malloc(wanted);
    if (!compressed || !decoded || fread(compressed, 1, compressed_size, file) != compressed_size) goto done;
    lrc = lzma_stream_decoder(&stream, UINT64_MAX, 0);
    if (lrc != LZMA_OK) goto done;
    stream.next_in = compressed;
    stream.avail_in = compressed_size;
    stream.next_out = decoded;
    stream.avail_out = wanted;
    for (;;) {
        lrc = lzma_code(&stream, LZMA_FINISH);
        if (stream.avail_out == 0) { ok = 1; break; }
        if (lrc == LZMA_STREAM_END) break;
        if (lrc != LZMA_OK) break;
    }
    lzma_end(&stream);
done:
    if (file) fclose(file);
    free(compressed);
    if (!ok) { free(decoded); return 0; }
    *out = decoded;
    return 1;
}

static int qmc_derive_master(const uint8_t *ekey, size_t ekey_size,
                             uint8_t **master, size_t *master_size);
static int qmc_apply_master(uint8_t *audio, size_t audio_size,
                            const uint8_t *master, size_t master_size);

static int kgm_decrypt(const uint8_t *input, size_t size,
                       uint8_t **out, size_t *out_size,
                       const char *qmc_ekey,
                       const char *kgm_key_path,
                       QwConvertReport *report)
{
    size_t audio_offset, audio_size, blocks;
    size_t master_size = 0;
    uint32_t version;
    uint8_t *pub = NULL, *audio = NULL, *master = NULL;
    int rc = QW_STATUS_ERR_FORMAT;
    if (!valid_kgm(input, size) || size < KGM_HEADER_SIZE) goto done;
    audio_offset = qw_read_u32_le(input + 0x10u);
    version = qw_read_u32_le(input + 0x14u);
    if (version == 5u) {
        uint32_t hash_size;
        size_t hash_end;
        size_t hash_length;
        size_t ekey_size;
        char *hash = NULL;
        char *database_ekey = NULL;
#if !defined(QUIREWISE_ENCRYPTED_MUSIC_PARSER_ONLY)
        char database_error[192] = {0};
#endif
        const char *effective_ekey = qmc_ekey;
        if (size < 0x48u) goto done;
        hash_size = qw_read_u32_le(input + 0x44u);
        if (!hash_size || (size_t)hash_size > size - 0x48u) {
            set_note(report, "KGM v5 audio hash is malformed");
            return QW_STATUS_ERR_FORMAT;
        }
        hash_end = 0x48u + (size_t)hash_size;
        if (audio_offset < hash_end || audio_offset >= size) goto done;
        hash_length = hash_size;
        while (hash_length && input[0x48u + hash_length - 1u] == 0u) --hash_length;
        if (!hash_length || memchr(input + 0x48u, 0, hash_length)) {
            set_note(report, "KGM v5 audio hash is not a valid text identifier");
            return QW_STATUS_ERR_FORMAT;
        }
        hash = (char *)malloc(hash_length + 1u);
        if (!hash) return QW_STATUS_ERR_OUT_OF_MEMORY;
        memcpy(hash, input + 0x48u, hash_length);
        hash[hash_length] = '\0';
        if (!effective_ekey || !*effective_ekey) {
#if defined(QUIREWISE_ENCRYPTED_MUSIC_PARSER_ONLY)
            set_note(report, "KGM v5 requires an explicit EKey in the standalone parser");
            free(hash);
            return QW_STATUS_ERR_UNSUPPORTED;
#else
            if (!qw_kgm_database_lookup_ekey(hash, &database_ekey,
                                             database_error,
                                             sizeof(database_error))) {
                set_note(report, database_error[0]
                                     ? database_error
                                     : "could not find the matching KGM v5 eKey");
                free(hash);
                return QW_STATUS_ERR_UNSUPPORTED;
            }
            effective_ekey = database_ekey;
#endif
        }
        free(hash);
        ekey_size = strlen(effective_ekey);
        if (!ekey_size || ekey_size > 0x10000u ||
            !qmc_derive_master((const uint8_t *)effective_ekey, ekey_size,
                               &master, &master_size)) {
            set_note(report, "KGM v5 eKey is invalid or unsupported");
            free(master);
            free(database_ekey);
            return QW_STATUS_ERR_UNSUPPORTED;
        }
        audio_size = size - audio_offset;
        audio = (uint8_t *)malloc(audio_size);
        if (!audio) {
            free(master);
            free(database_ekey);
            return QW_STATUS_ERR_OUT_OF_MEMORY;
        }
        memcpy(audio, input + audio_offset, audio_size);
        rc = qmc_apply_master(audio, audio_size, master, master_size);
        free(master);
        master = NULL;
        free(database_ekey);
        if (rc != QW_STATUS_OK) goto done;
        rc = set_output(audio, audio_size, out, out_size);
        audio = NULL;
        goto done;
    }
    if (audio_offset < KGM_HEADER_SIZE || audio_offset >= size) goto done;
    audio_size = size - audio_offset;
    blocks = audio_size / 16u + (audio_size % 16u != 0u);
    if (!decompress_kugou_key(blocks, kgm_key_path, &pub)) {
        set_note(report, "KGM public key is missing or too short; pass --kgm-key or set QUIREWISE_KGM_KEY");
        return QW_STATUS_ERR_UNSUPPORTED;
    }
    audio = (uint8_t *)malloc(audio_size);
    if (!audio) return QW_STATUS_ERR_OUT_OF_MEMORY;
    for (size_t i = 0; i < audio_size; ++i) {
        size_t block = i / 16u;
        uint8_t own = i % 17u == 16u ? 0u : input[0x1cu + (i % 17u)];
        uint8_t transformed = scramble((uint8_t)(input[audio_offset + i] ^ own));
        uint8_t mask = scramble((uint8_t)(pub[block] ^ kgm_mend[i % sizeof(kgm_mend)]));
        audio[i] = transformed ^ mask;
    }
    free(pub);
    rc = set_output(audio, audio_size, out, out_size);
    audio = NULL;
done:
    free(pub); free(audio);
    if (rc != QW_STATUS_OK && report && !report->note[0]) set_note(report, "KGM header is malformed or unsupported");
    return rc;
}

static int kwm_audio_head(const uint8_t *input, size_t size, const uint8_t key[32])
{
    uint8_t probe[4096];
    size_t count = size > KWM_HEADER_SIZE ? size - KWM_HEADER_SIZE : 0;
    if (count > sizeof(probe)) count = sizeof(probe);
    for (size_t i = 0; i < count; ++i) probe[i] = input[KWM_HEADER_SIZE + i] ^ key[i & 31u];
    size_t first = 0;
    while (first < count && probe[first] == 0) ++first;
    return qw_audio_detect_format(probe + first, count - first);
}

static int kwm_decrypt(const uint8_t *input, size_t size,
                       uint8_t **out, size_t *out_size,
                       QwConvertReport *report)
{
    const size_t key_size = 32u;
    size_t body_size, block_count, scan_block_count;
    const uint8_t *selected = NULL;
    const uint8_t *first_duplicate = NULL;
    const uint8_t *scan_tail;
    uint8_t rotated[32];
    uint8_t *audio;
    if (!input || size < KWM_HEADER_SIZE + 64u) {
        set_note(report, "KWM file is too short");
        return QW_STATUS_ERR_FORMAT;
    }
    body_size = size - KWM_HEADER_SIZE;
    block_count = body_size / key_size;
    scan_block_count = block_count < 2048u ? block_count : 2048u;
    scan_tail = input + KWM_HEADER_SIZE;
    for (size_t i = 1; i < scan_block_count; ++i) {
        const uint8_t *prev = input + KWM_HEADER_SIZE + (i - 1u) * key_size;
        const uint8_t *cur = input + KWM_HEADER_SIZE + i * key_size;
        scan_tail = cur;
        if (memcmp(prev, cur, key_size) == 0) {
            if (!first_duplicate) first_duplicate = cur;
            if (qw_audio_input_format_is(kwm_audio_head(input, size, cur))) {
                selected = cur;
                break;
            }
        }
    }
    if (!selected) {
        size_t i = block_count < 64u ? block_count : 64u;
        for (size_t j = 0; j < i; ++j) {
            const uint8_t *candidate = input + KWM_HEADER_SIZE + j * key_size;
            if (qw_audio_input_format_is(kwm_audio_head(input, size, candidate))) {
                selected = candidate;
                break;
            }
        }
    }
    if (!selected) {
        if (first_duplicate) {
            selected = first_duplicate;
        } else {
            memcpy(rotated, scan_tail + 16u, 16u);
            memcpy(rotated + 16u, scan_tail, 16u);
            selected = rotated;
        }
    }
    audio = (uint8_t *)malloc(body_size);
    if (!audio) return QW_STATUS_ERR_OUT_OF_MEMORY;
    for (size_t i = 0; i < body_size; ++i)
        audio[i] = input[KWM_HEADER_SIZE + i] ^ selected[i & 31u];
    return set_output(audio, body_size, out, out_size);
}

static int base64_value(unsigned char ch)
{
    if (ch >= 'A' && ch <= 'Z') return ch - 'A';
    if (ch >= 'a' && ch <= 'z') return ch - 'a' + 26;
    if (ch >= '0' && ch <= '9') return ch - '0' + 52;
    if (ch == '+') return 62;
    if (ch == '/') return 63;
    return -1;
}

static int base64_decode(const uint8_t *text, size_t size, uint8_t **out, size_t *out_size)
{
    uint8_t *decoded;
    size_t used = 0;
    unsigned acc = 0;
    int bits = 0, padding = 0;
    if (!text || !size || size / 4u + 1u > SIZE_MAX / 3u) return 0;
    decoded = (uint8_t *)malloc((size / 4u + 1u) * 3u);
    if (!decoded) return 0;
    for (size_t i = 0; i < size; ++i) {
        if (text[i] == '=') { padding++; continue; }
        int value = base64_value(text[i]);
        if (value < 0 || padding) { free(decoded); return 0; }
        acc = (acc << 6) | (unsigned)value;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            decoded[used++] = (uint8_t)(acc >> bits);
            acc &= (1u << bits) - 1u;
        }
    }
    if (padding > 2 || bits == 6 || (padding && (size % 4u)) ||
        (padding == 1 && bits != 2) || (padding == 2 && bits != 4)) {
        free(decoded); return 0;
    }
    *out = decoded; *out_size = used;
    return 1;
}

static uint32_t tea_mix(uint32_t value, uint32_t sum, uint32_t k1, uint32_t k2)
{
    return (((value << 4) + k1) ^ (sum + value) ^ ((value >> 5) + k2));
}

static void tea_decrypt_block(const uint8_t input[8], const uint8_t key[16], uint8_t output[8])
{
    uint32_t v0 = qw_read_u32_be(input), v1 = qw_read_u32_be(input + 4u);
    uint32_t k[4] = {qw_read_u32_be(key), qw_read_u32_be(key+4), qw_read_u32_be(key+8), qw_read_u32_be(key+12)};
    uint32_t sum = 0x9e3779b9u * 16u;
    for (unsigned i = 0; i < 16u; ++i) {
        v1 -= tea_mix(v0, sum, k[2], k[3]);
        v0 -= tea_mix(v1, sum, k[0], k[1]);
        sum -= 0x9e3779b9u;
    }
    output[0]=(uint8_t)(v0>>24); output[1]=(uint8_t)(v0>>16);
    output[2]=(uint8_t)(v0>>8); output[3]=(uint8_t)v0;
    output[4]=(uint8_t)(v1>>24); output[5]=(uint8_t)(v1>>16);
    output[6]=(uint8_t)(v1>>8); output[7]=(uint8_t)v1;
}

static int tea_cbc_decrypt(const uint8_t *input, size_t size, const uint8_t key[16],
                           uint8_t **out, size_t *out_size)
{
    uint8_t *plain, iv_prev[8] = {0}, iv_cur[8] = {0};
    size_t body_end, body_start;
    if (!input || size < 16u || (size & 7u)) return 0;
    plain = (uint8_t *)malloc(size);
    if (!plain) return 0;
    for (size_t pos = 0; pos < size; pos += 8u) {
        uint8_t mixed[8], decoded[8], next_iv[8];
        for (size_t j = 0; j < 8u; ++j) mixed[j] = input[pos+j] ^ iv_cur[j];
        tea_decrypt_block(mixed, key, decoded);
        memcpy(next_iv, decoded, 8u);
        for (size_t j = 0; j < 8u; ++j) plain[pos+j] = decoded[j] ^ iv_prev[j];
        memcpy(iv_prev, input + pos, 8u);
        memcpy(iv_cur, next_iv, 8u);
    }
    body_start = 1u + (plain[0] & 7u) + 2u;
    body_end = size - 7u;
    if (body_start > body_end) { free(plain); return 0; }
    for (size_t i = body_end; i < size; ++i) if (plain[i] != 0) { free(plain); return 0; }
    memmove(plain, plain + body_start, body_end - body_start);
    *out = plain; *out_size = body_end - body_start;
    return 1;
}

static void make_simple_key(uint8_t key[8])
{
    for (size_t i = 0; i < 8u; ++i) {
        float angle = 106.0f + (float)i * 0.1f;
        float value = fabsf(tanf(angle)) * 100.0f;
        if (value > 255.0f) value = 255.0f;
        key[i] = (uint8_t)value;
    }
}

static int ekey_v1(const uint8_t *ekey, size_t ekey_size,
                   uint8_t **master, size_t *master_size)
{
    uint8_t *decoded = NULL, *plain = NULL, tea_key[16], simple[8];
    size_t decoded_size = 0, plain_size = 0;
    int ok = 0;
    if (!base64_decode(ekey, ekey_size, &decoded, &decoded_size) || decoded_size < 18u) goto done;
    make_simple_key(simple);
    for (size_t i = 0; i < 8u; ++i) { tea_key[2u*i] = simple[i]; tea_key[2u*i+1u] = decoded[i]; }
    if (!tea_cbc_decrypt(decoded + 8u, decoded_size - 8u, tea_key, &plain, &plain_size) || !plain_size) goto done;
    *master = (uint8_t *)malloc(8u + plain_size);
    if (!*master) goto done;
    memcpy(*master, decoded, 8u); memcpy(*master + 8u, plain, plain_size);
    *master_size = 8u + plain_size;
    ok = 1;
done:
    free(decoded); free(plain);
    return ok;
}

static int qmc_derive_master(const uint8_t *ekey, size_t ekey_size,
                             uint8_t **master, size_t *master_size)
{
    static const uint8_t encv2_prefix[] = "UVFNdXNpYyBFbmNWMixLZXk6";
    static const uint8_t encv2_key1[16] = {
        0x33,0x38,0x36,0x5a,0x4a,0x59,0x21,0x40,
        0x23,0x2a,0x24,0x25,0x5e,0x26,0x29,0x28
    };
    static const uint8_t encv2_key2[16] = {
        0x2a,0x2a,0x23,0x21,0x28,0x23,0x24,0x25,
        0x26,0x5e,0x61,0x31,0x63,0x5a,0x2c,0x54
    };
    if (ekey_size >= sizeof(encv2_prefix) - 1u &&
        memcmp(ekey, encv2_prefix, sizeof(encv2_prefix) - 1u) == 0) {
        uint8_t *cipher = NULL, *stage1 = NULL, *stage2 = NULL;
        size_t cipher_size = 0, stage1_size = 0, stage2_size = 0;
        size_t plain_size;
        int ok = 0;
        if (!base64_decode(ekey + sizeof(encv2_prefix) - 1u,
                           ekey_size - (sizeof(encv2_prefix) - 1u),
                           &cipher, &cipher_size) ||
            !tea_cbc_decrypt(cipher, cipher_size, encv2_key1,
                             &stage1, &stage1_size) ||
            !tea_cbc_decrypt(stage1, stage1_size, encv2_key2,
                             &stage2, &stage2_size)) goto encv2_done;
        plain_size = 0;
        while (plain_size < stage2_size && stage2[plain_size]) ++plain_size;
        if (plain_size) ok = ekey_v1(stage2, plain_size, master, master_size);
encv2_done:
        free(cipher); free(stage1); free(stage2);
        return ok;
    }
    return ekey_v1(ekey, ekey_size, master, master_size);
}

static int qmc_apply_master(uint8_t *audio, size_t audio_size,
                            const uint8_t *master, size_t master_size)
{
    if (!audio || !master || !master_size) return QW_STATUS_ERR_FORMAT;
    if (master_size <= 300u) {
        uint8_t map[128];
        for (size_t i = 0; i < sizeof(map); ++i) {
            size_t idx = (i * i + 71214u) % master_size;
            unsigned shift = (unsigned)((idx + 4u) & 7u);
            uint8_t v = master[idx];
            map[i] = shift ? (uint8_t)((v << shift) | (v >> (8u - shift))) : v;
        }
        for (size_t i = 0; i < audio_size; ++i) {
            size_t pos = i <= 0x7fffu ? i : i % 0x7fffu;
            audio[i] ^= map[pos % sizeof(map)];
        }
    } else {
        size_t n = master_size;
        uint8_t *state = (uint8_t *)malloc(n);
        uint8_t stream[0x1400 + 512u];
        double hash = 1.0;
        unsigned ii = 0, jj = 0;
        if (!state) return QW_STATUS_ERR_OUT_OF_MEMORY;
        for (size_t i = 0; i < n; ++i) state[i] = (uint8_t)i;
        for (size_t i = 0; i < n; ++i) {
            jj = (unsigned)((jj + state[i] + master[i % n]) % n);
            uint8_t t = state[i]; state[i] = state[jj]; state[jj] = t;
        }
        for (size_t i = 0; i < n; ++i) {
            if (!master[i]) continue;
            uint32_t next = (uint32_t)((uint64_t)hash * master[i]);
            if (!next || (double)next <= hash) break;
            hash = next;
        }
        for (size_t i = 0; i < sizeof(stream); ++i) {
            ii = (unsigned)((ii + 1u) % n);
            jj = (unsigned)((jj + state[ii]) % n);
            uint8_t t = state[ii]; state[ii] = state[jj]; state[jj] = t;
            stream[i] = state[(state[ii] + state[jj]) % n];
        }
        for (size_t pos = 0; pos < audio_size;) {
            size_t take, seg, off, seed, skip;
            if (pos < 0x80u) {
                take = 0x80u - pos;
                if (take > audio_size - pos) take = audio_size - pos;
                for (size_t j = 0; j < take; ++j) {
                    size_t p = pos + j;
                    double denom = (double)(p + 1u) * master[p % n];
                    size_t sk = denom ? (size_t)(hash / denom * 100.0) : 0u;
                    audio[p] ^= master[sk % n];
                }
            } else {
                seg = pos / 0x1400u; off = pos % 0x1400u;
                seed = master[seg % n];
                {
                    double denom = (double)(seg + 1u) * seed;
                    skip = denom ? ((size_t)(hash / denom * 100.0) & 0x1ffu) : 0u;
                }
                take = 0x1400u - off;
                if (take > audio_size - pos) take = audio_size - pos;
                for (size_t j = 0; j < take; ++j) audio[pos + j] ^= stream[skip + off + j];
            }
            pos += take;
        }
        free(state);
    }
    return QW_STATUS_OK;
}

static size_t qmc_footer_size(const uint8_t *data, size_t size,
                              uint8_t **ekey, size_t *ekey_size)
{
    size_t payload_size;
    *ekey = NULL; *ekey_size = 0;
    if (size >= 8u && memcmp(data + size - 4u, "QTag", 4u) == 0) {
        payload_size = qw_read_u32_be(data + size - 8u);
        if (payload_size > size - 8u || payload_size > 1024u) return 0;
        const uint8_t *payload = data + size - 8u - payload_size;
        const uint8_t *comma = (const uint8_t *)memchr(payload, ',', payload_size);
        if (!comma) return 0;
        *ekey_size = (size_t)(comma - payload);
        *ekey = (uint8_t *)malloc(*ekey_size);
        if (!*ekey) return 0;
        memcpy(*ekey, payload, *ekey_size);
        return payload_size + 8u;
    }
    if (size >= 4u) {
        payload_size = qw_read_u32_le(data + size - 4u);
        if (payload_size && payload_size <= 0x500u && payload_size <= size - 4u) {
            const uint8_t *payload = data + size - 4u - payload_size;
            size_t n = 0;
            while (n < payload_size && payload[n]) ++n;
            if (n && base64_value(payload[0]) >= 0) {
                *ekey = (uint8_t *)malloc(n);
                if (!*ekey) return 0;
                memcpy(*ekey, payload, n); *ekey_size = n;
                return payload_size + 4u;
            }
        }
    }
    return 0;
}

static size_t qmc_external_key_footer_size(const uint8_t *data, size_t size)
{
    if (!data || size < 8u) return 0;
    if (memcmp(data + size - 4u, "STag", 4u) == 0) {
        size_t payload_size = qw_read_u32_be(data + size - 8u);
        if (payload_size > 0x500u || payload_size > size - 8u) return 0;
        return payload_size + 8u;
    }
    if (size >= 16u && memcmp(data + size - 8u, "musicex\0", 8u) == 0) {
        uint32_t payload_size = qw_read_u32_le(data + size - 16u);
        uint32_t version = qw_read_u32_le(data + size - 12u);
        if (payload_size != 0xc0u || version != 1u ||
            size < (size_t)payload_size + 12u) return 0;
        return (size_t)payload_size + 12u;
    }
    return 0;
}

static char *qmc_copy_footer_text(const uint8_t *data, size_t size)
{
    char *copy;
    if (!data || !size || size == SIZE_MAX) return NULL;
    copy = (char *)malloc(size + 1u);
    if (!copy) return NULL;
    memcpy(copy, data, size);
    copy[size] = '\0';
    return copy;
}

static char *qmc_read_utf16le_ascii(const uint8_t *data, size_t size)
{
    char *text;
    size_t used = 0;
    if (!data || size < 2u) return NULL;
    text = (char *)malloc(size / 2u + 1u);
    if (!text) return NULL;
    for (size_t i = 0; i + 1u < size; i += 2u) {
        uint8_t lo = data[i], hi = data[i + 1u];
        if (lo == 0u && hi == 0u) break;
        if (hi != 0u || lo >= 0x80u) break;
        text[used++] = (char)lo;
    }
    text[used] = '\0';
    if (!used) {
        free(text);
        return NULL;
    }
    return text;
}

static size_t qmc_footer_identifiers(const uint8_t *data, size_t size,
                                     size_t footer_size,
                                     char **names, size_t names_capacity)
{
    if (!data || !names || !names_capacity || footer_size > size) return 0;
    if (size >= 8u && memcmp(data + size - 4u, "STag", 4u) == 0) {
        size_t payload_size = qw_read_u32_be(data + size - 8u);
        const uint8_t *payload;
        const uint8_t *first_comma;
        const uint8_t *second_comma;
        size_t count = 0;
        if (payload_size > 0x500u || payload_size > size - 8u) return 0;
        payload = data + size - 8u - payload_size;
        first_comma = (const uint8_t *)memchr(payload, ',', payload_size);
        if (!first_comma) return 0;
        second_comma = (const uint8_t *)memchr(first_comma + 1u, ',',
            payload_size - (size_t)(first_comma + 1u - payload));
        if (!second_comma || second_comma - first_comma != 2 ||
            first_comma[1] != '2' ||
            memchr(second_comma + 1u, ',',
                   payload_size - (size_t)(second_comma + 1u - payload))) return 0;
        for (const uint8_t *cursor = payload; cursor < first_comma; ++cursor) {
            if (*cursor < '0' || *cursor > '9') return 0;
        }
        if (first_comma > payload) {
            names[count] = qmc_copy_footer_text(
                payload, (size_t)(first_comma - payload));
            if (names[count]) ++count;
        }
        if (names_capacity > count &&
            second_comma + 1u < payload + payload_size) {
            names[count] = qmc_copy_footer_text(
                second_comma + 1u,
                (size_t)(payload + payload_size - (second_comma + 1u)));
            if (names[count]) ++count;
        }
        return count;
    }
    if (size >= 8u && memcmp(data + size - 8u, "musicex\0", 8u) == 0 &&
        footer_size >= 28u) {
        uint32_t payload_size = qw_read_u32_le(data + size - 16u);
        size_t inner_size;
        const uint8_t *inner;
        size_t count = 0;
        if (payload_size < 16u || footer_size != (size_t)payload_size + 12u ||
            (inner_size = (size_t)payload_size - 16u) < 172u) return 0;
        inner = data + size - footer_size + 16u;
        if (names_capacity > count) {
            names[count] = qmc_read_utf16le_ascii(inner + 12u, 60u);
            if (names[count]) ++count;
        }
        if (names_capacity > count) {
            names[count] = qmc_read_utf16le_ascii(inner + 72u, 100u);
            if (names[count]) ++count;
        }
        return count;
    }
    return 0;
}

static int qmc_decrypt(const uint8_t *input, size_t size,
                       uint8_t **out, size_t *out_size,
                       const char *external_ekey,
                       const char *ekey_database_path,
                       const char *input_path,
                       QwConvertReport *report)
{
    uint8_t *ekey = NULL, *master = NULL, *audio = NULL, *decrypted = NULL;
    size_t ekey_size = 0, master_size = 0, footer_size, audio_size = size;
    int is_v2 = 0;
#if defined(QUIREWISE_ENCRYPTED_MUSIC_PARSER_ONLY)
    (void)ekey_database_path;
    (void)input_path;
#endif
    if (!input || !size) return QW_STATUS_ERR_INVALID_ARGUMENT;
    footer_size = qmc_footer_size(input, size, &ekey, &ekey_size);
    if (footer_size && ekey && ekey_size) {
        is_v2 = qmc_derive_master(ekey, ekey_size, &master, &master_size);
    }
    if (!is_v2) {
        free(ekey); ekey = NULL; ekey_size = 0;
        free(master); master = NULL; master_size = 0;
        if (report) report->note[0] = '\0';
        {
            const char *resolved_ekey = external_ekey;
            size_t external_footer = qmc_external_key_footer_size(input, size);
            char *database_ekey = NULL;
#if !defined(QUIREWISE_ENCRYPTED_MUSIC_PARSER_ONLY)
            if ((!resolved_ekey || !*resolved_ekey) &&
                ekey_database_path && *ekey_database_path &&
                (external_footer || footer_size || qmc_footer_present(input, size))) {
                char *footer_names[3] = {NULL, NULL, NULL};
                const char *lookup_names[4];
                size_t footer_name_count;
                size_t lookup_name_count = 0;
                char database_error[192] = {0};
                const char *base = input_path;
                const char *slash;
                if (!external_footer && footer_size) external_footer = footer_size;
                if (!external_footer) {
                    set_note(report, "QMC footer does not contain a supported resource id for --ekey-db lookup");
                    return QW_STATUS_ERR_UNSUPPORTED;
                }
                if (base) {
                    slash = strrchr(base, '/');
                    if (slash && slash + 1u > base) base = slash + 1u;
                    slash = strrchr(base, '\\');
                    if (slash && slash + 1u > base) base = slash + 1u;
                    if (*base) lookup_names[lookup_name_count++] = base;
                }
                footer_name_count = qmc_footer_identifiers(
                    input, size, external_footer, footer_names,
                    sizeof(footer_names) / sizeof(footer_names[0]));
                for (size_t i = 0; i < footer_name_count; ++i) {
                    lookup_names[lookup_name_count++] = footer_names[i];
                }
                if (!qw_qmc_database_lookup_ekey(
                        ekey_database_path, lookup_names, lookup_name_count,
                        &database_ekey, database_error, sizeof(database_error))) {
                    for (size_t i = 0; i < footer_name_count; ++i) free(footer_names[i]);
                    if (database_error[0]) set_note(report, database_error);
                    else set_note(report, "no matching QMC EKey was found in --ekey-db");
                    free(ekey); free(master);
                    return QW_STATUS_ERR_UNSUPPORTED;
                }
                for (size_t i = 0; i < footer_name_count; ++i) free(footer_names[i]);
                resolved_ekey = database_ekey;
            }
#endif
            if (resolved_ekey && *resolved_ekey) {
                if (!external_footer && footer_size) external_footer = footer_size;
                if (!external_footer ||
                    !qmc_derive_master((const uint8_t *)resolved_ekey,
                                       strlen(resolved_ekey), &master, &master_size)) {
                    free(database_ekey);
                    set_note(report, "provided QMC EKey is invalid or the encrypted footer is unsupported");
                    free(master);
                    return QW_STATUS_ERR_UNSUPPORTED;
                }
                free(database_ekey);
                footer_size = external_footer;
                is_v2 = 1;
            } else if (size >= 4u && memcmp(input + size - 4u, "STag", 4u) == 0) {
#if defined(QUIREWISE_ENCRYPTED_MUSIC_PARSER_ONLY)
                set_note(report, "QMC STag requires an explicit EKey in the standalone parser");
#else
                set_note(report, "QMC STag has no embedded EKey; use --ekey or --ekey-db with an authorized local EKey");
#endif
                return QW_STATUS_ERR_UNSUPPORTED;
            } else if (size >= 8u && memcmp(input + size - 8u, "musicex\0", 8u) == 0) {
#if defined(QUIREWISE_ENCRYPTED_MUSIC_PARSER_ONLY)
                set_note(report, "QMC MusicEx requires an explicit EKey in the standalone parser");
#else
                set_note(report, "QMC MusicEx has no embedded EKey; use --ekey or --ekey-db with an authorized local EKey");
#endif
                return QW_STATUS_ERR_UNSUPPORTED;
            }
        }
    }
    if (is_v2) {
        if (footer_size > size) { free(ekey); free(master); return QW_STATUS_ERR_FORMAT; }
        audio_size = size - footer_size;
    }
    audio = (uint8_t *)malloc(audio_size);
    if (!audio) { free(ekey); free(master); return QW_STATUS_ERR_OUT_OF_MEMORY; }
    memcpy(audio, input, audio_size);
    if (!is_v2) {
        qmc1_xor(audio, audio_size, 0);
    } else {
        int apply_rc = qmc_apply_master(audio, audio_size, master, master_size);
        if (apply_rc != QW_STATUS_OK) {
            free(audio); free(master); free(ekey);
            return apply_rc;
        }
    }
    decrypted = audio; audio = NULL;
    if (qw_audio_detect_format(decrypted, audio_size) == QW_FORMAT_UNKNOWN) {
        free(decrypted); free(master); free(ekey);
        set_note(report, "QMC decrypted bytes do not match a supported audio stream");
        return QW_STATUS_ERR_DECODE;
    }
    free(master); free(ekey);
    return set_output(decrypted, audio_size, out, out_size);
}

int qw_encrypted_music_decrypt_ex(QwInputFormat input_format,
                                  const void *data,
                                  size_t size,
                                  const char *qmc_ekey,
                                  const char *qmc_ekey_db_path,
                                  const char *input_path,
                                  const char *kgm_key_path,
                                  uint8_t **out_data,
                                  size_t *out_size,
                                  QwConvertReport *report)
{
    const uint8_t *input = (const uint8_t *)data;
    int rc;
    if (!data || !size || !out_data || !out_size ||
        !qw_encrypted_music_format_is(input_format)) {
        return QW_STATUS_ERR_INVALID_ARGUMENT;
    }
    *out_data = NULL;
    *out_size = 0u;
    switch (input_format) {
    case QW_FORMAT_NCM:
        rc = ncm_decrypt(input, size, out_data, out_size, NULL, report);
        break;
    case QW_FORMAT_QMC:
        rc = qmc_decrypt(input, size, out_data, out_size,
                         qmc_ekey, qmc_ekey_db_path, input_path, report);
        break;
    case QW_FORMAT_KGM:
        rc = kgm_decrypt(input, size, out_data, out_size, qmc_ekey,
                         kgm_key_path, report);
        break;
    case QW_FORMAT_KWM:
        rc = kwm_decrypt(input, size, out_data, out_size, report);
        break;
    default:
        return QW_STATUS_ERR_UNSUPPORTED;
    }
    if (rc != QW_STATUS_OK) return rc;
    if (qw_audio_detect_format(*out_data, *out_size) == QW_FORMAT_UNKNOWN) {
        free(*out_data);
        *out_data = NULL;
        *out_size = 0u;
        set_note(report, "decrypted music payload is not a supported audio stream");
        return QW_STATUS_ERR_DECODE;
    }
    return QW_STATUS_OK;
}

int qw_encrypted_music_decrypt(QwInputFormat input_format,
                               const void *data,
                               size_t size,
                               const char *qmc_ekey,
                               const char *qmc_ekey_db_path,
                               const char *input_path,
                               uint8_t **out_data,
                               size_t *out_size,
                               QwConvertReport *report)
{
    return qw_encrypted_music_decrypt_ex(
        input_format, data, size, qmc_ekey, qmc_ekey_db_path,
        input_path, NULL, out_data, out_size, report);
}

#if !defined(QUIREWISE_ENCRYPTED_MUSIC_PARSER_ONLY)
int qw_encrypted_music_transcode(const void *data, size_t size,
                                 QwInputFormat input_format,
                                 QwOutputFormat output_format,
                                 const char *qmc_ekey,
                                 const char *qmc_ekey_db_path,
                                 const char *input_path,
                                 size_t audio_track,
                                 uint8_t **out_data, size_t *out_size,
                                 QwConvertReport *report)
{
    const uint8_t *input = (const uint8_t *)data;
    uint8_t *clear = NULL, *converted = NULL;
    size_t clear_size = 0, converted_size = 0;
    QwInputFormat clear_format;
    QwNcmMetadata ncm_metadata = {0};
    QwAudioMetadata audio_metadata = {0};
    int rc;
    if (!data || !size || !out_data || !out_size ||
        !qw_encrypted_music_format_is(input_format) ||
        !qw_audio_format_is(output_format)) return QW_STATUS_ERR_INVALID_ARGUMENT;
    *out_data = NULL; *out_size = 0;
    switch (input_format) {
    case QW_FORMAT_NCM:
        rc = ncm_decrypt(input, size, &clear, &clear_size,
                         &ncm_metadata, report);
        break;
    case QW_FORMAT_QMC:
    case QW_FORMAT_KGM:
    case QW_FORMAT_KWM:
        rc = qw_encrypted_music_decrypt(
            input_format, input, size, qmc_ekey, qmc_ekey_db_path,
            input_path, &clear, &clear_size, report);
        break;
    default: return QW_STATUS_ERR_UNSUPPORTED;
    }
    if (rc != QW_STATUS_OK) {
        ncm_metadata_clear(&ncm_metadata);
        return rc;
    }
    clear_format = qw_audio_detect_format(clear, clear_size);
    if (clear_format == QW_FORMAT_UNKNOWN) {
        free(clear);
        ncm_metadata_clear(&ncm_metadata);
        set_note(report, "decrypted music payload is not a supported MP3/FLAC/M4A/AAC/WAV/OGG stream");
        return QW_STATUS_ERR_DECODE;
    }
    audio_metadata.title = ncm_metadata.title;
    audio_metadata.artist = ncm_metadata.artist;
    audio_metadata.album = ncm_metadata.album;
    audio_metadata.cover_data = ncm_metadata.cover_data;
    audio_metadata.cover_size = ncm_metadata.cover_size;
    rc = qw_audio_transcode_with_metadata(
        clear, clear_size, clear_format, output_format, audio_track,
        input_format == QW_FORMAT_NCM ? &audio_metadata : NULL,
        &converted, &converted_size, report);
    free(clear);
    ncm_metadata_clear(&ncm_metadata);
    if (rc != QW_STATUS_OK) return rc;
    *out_data = converted; *out_size = converted_size;
    return QW_STATUS_OK;
}
#endif









#if !defined(QUIREWISE_ENCRYPTED_MUSIC_PARSER_ONLY)
const QwFormatDescriptor qw_format_ncm = {
    .id = QW_FORMAT_NCM,
    .name = "NCM",
    .capabilities = (QW_FORMAT_CAP_CONTAINER) | (0u),
    .probe = ncm_probe,
    .open_memory = NULL,
    .open_layout = NULL,
    .open_audio = qw_encrypted_music_format_open_document,
    .max_input_bytes = QW_FORMAT_INPUT_LIMIT_DEFAULT,
    .resource_profile = QW_FORMAT_INPUT_RESOURCE_PROFILE_DEFAULT,
    .convert_to_pdf = NULL
};

const QwFormatDescriptor qw_format_qmc = {
    .id = QW_FORMAT_QMC,
    .name = "QMC",
    .capabilities = (QW_FORMAT_CAP_CONTAINER) | (0u),
    .probe = qmc_probe,
    .open_memory = NULL,
    .open_layout = NULL,
    .open_audio = qw_encrypted_music_format_open_document,
    .max_input_bytes = QW_FORMAT_INPUT_LIMIT_DEFAULT,
    .resource_profile = QW_FORMAT_INPUT_RESOURCE_PROFILE_DEFAULT,
    .convert_to_pdf = NULL
};

const QwFormatDescriptor qw_format_kgm = {
    .id = QW_FORMAT_KGM,
    .name = "KGM",
    .capabilities = (QW_FORMAT_CAP_CONTAINER) | (0u),
    .probe = kgm_probe,
    .open_memory = NULL,
    .open_layout = NULL,
    .open_audio = qw_encrypted_music_format_open_document,
    .max_input_bytes = QW_FORMAT_INPUT_LIMIT_DEFAULT,
    .resource_profile = QW_FORMAT_INPUT_RESOURCE_PROFILE_DEFAULT,
    .convert_to_pdf = NULL
};

const QwFormatDescriptor qw_format_kwm = {
    .id = QW_FORMAT_KWM,
    .name = "KWM",
    .capabilities = (QW_FORMAT_CAP_CONTAINER) | (0u),
    .probe = NULL,
    .open_memory = NULL,
    .open_layout = NULL,
    .open_audio = qw_encrypted_music_format_open_document,
    .max_input_bytes = QW_FORMAT_INPUT_LIMIT_DEFAULT,
    .resource_profile = QW_FORMAT_INPUT_RESOURCE_PROFILE_DEFAULT,
    .convert_to_pdf = NULL
};
#endif
