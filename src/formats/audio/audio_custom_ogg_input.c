#include "audio_custom_ogg_input.h"
#include "audio_custom_ogg_input_parser.h"
#include "audio_ogg_stream_selector.h"

#include "quirewise/status.h"

#include <stdlib.h>

int qw_audio_custom_ogg_input_parse(const uint8_t *data,
                                    size_t size,
                                    size_t requested_audio_track,
                                    QwAudioCustomInput *input)
{
    uint8_t *selected_data = NULL;
    size_t selected_size = 0u;
    int rc;
    if (!data || !size || !input) return QW_STATUS_ERR_INVALID_ARGUMENT;

    rc = qw_audio_ogg_select_audio_stream(data, size, requested_audio_track,
                                          &selected_data, &selected_size,
                                          NULL);
    if (rc == QW_STATUS_ERR_UNSUPPORTED) {
        free(selected_data);
        return QW_STATUS_ERR_UNSUPPORTED;
    }
    if (rc != QW_STATUS_OK) {
        free(selected_data);
        return rc;
    }
    input->ogg_selected_data = selected_data;
    input->ogg_selected_size = selected_size;
    rc = qw_audio_custom_ogg_input_parse_selected(
        selected_data, selected_size, input);
    if (rc <= 0) {
        qw_audio_custom_ogg_input_dispose(input);
        if (rc == 0) return QW_STATUS_ERR_UNSUPPORTED;
    }
    return rc;
}

void qw_audio_custom_ogg_input_dispose(QwAudioCustomInput *input)
{
    if (!input) return;
    free(input->ogg_packet_buffer.data);
    input->ogg_packet_buffer.data = NULL;
    input->ogg_packet_buffer.capacity = 0u;
    qw_audio_ogg_opus_source_dispose(&input->ogg_opus);
    qw_audio_ogg_vorbis_source_dispose(&input->ogg_vorbis);
    free(input->ogg_selected_data);
    input->ogg_selected_data = NULL;
    input->ogg_selected_size = 0u;
}
