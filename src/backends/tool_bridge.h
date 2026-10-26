#ifndef QUIREWISE_TOOL_BRIDGE_H
#define QUIREWISE_TOOL_BRIDGE_H

#include "quirewise/convert.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/* Stage a retained input file below .tmp/work and return its UTF-8 path. */
int qw_tool_bridge_stage_memory(const char *extension,
                                const uint8_t *data,
                                size_t size,
                                char **out_path,
                                QwConvertReport *report);

/* Open a UTF-8 path using the platform's wide path API when needed. */
FILE *qw_tool_bridge_open_file(const char *path, const char *mode);

#endif
