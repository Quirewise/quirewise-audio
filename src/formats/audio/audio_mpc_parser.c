#include "audio_mpc_parser.h"

#include "audio_mpc_sv7_parser_internal.h"
#include "audio_mpc_sv8_parser_internal.h"
#include "quirewise/status.h"

#include <string.h>

int qw_audio_mpc_parse(const uint8_t *data,
                       size_t size,
                       QwAudioMpcSource *source)
{
    if (!data || !size || !source) return QW_STATUS_ERR_INVALID_ARGUMENT;
    memset(source, 0, sizeof(*source));
    if (size >= 4u && memcmp(data, "MPCK", 4u) == 0)
        return qw_audio_mpc_sv8_parse(data, size, source);
    return qw_audio_mpc_sv7_parse(data, size, source);
}

int qw_audio_mpc_next_packet(QwAudioMpcSource *source,
                             QwAudioMpcPacket *packet)
{
    if (!source || !packet || !source->data ||
        source->packet_position > source->stream_end_position ||
        source->stream_end_position > source->size) {
        return QW_STATUS_ERR_INVALID_ARGUMENT;
    }
    memset(packet, 0, sizeof(*packet));
    if (source->is_sv8)
        return qw_audio_mpc_sv8_next_packet(source, packet);
    return qw_audio_mpc_sv7_next_packet(source, packet);
}
