#ifndef QUIREWISE_SOURCE_INTERNAL_H
#define QUIREWISE_SOURCE_INTERNAL_H

#include <stddef.h>
#include <stdint.h>

typedef enum QwSourceKind {
    QW_SOURCE_MEMORY = 1,
    QW_SOURCE_MAPPED_FILE,
    QW_SOURCE_DIRECTORY
} QwSourceKind;

/* Borrowed memory/path views; mapped-file resources are owned by QwSource. */
typedef struct QwSource {
    QwSourceKind kind;
    const uint8_t *data;
    size_t size;
    const char *path;
    void *view;
    void *file_handle;
    void *mapping_handle;
    int descriptor;
    int mapped;
} QwSource;

int qw_source_from_memory(QwSource *source, const void *data, size_t size);
int qw_source_open_path(QwSource *source, const char *path);
void qw_source_close(QwSource *source);

#endif
