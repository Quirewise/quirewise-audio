#ifndef QUIREWISE_KGM_DATABASE_H
#define QUIREWISE_KGM_DATABASE_H

#include <stddef.h>

/* Returns a malloc-owned eKey for an audio hash from a local KuGou database. */
int qw_kgm_database_lookup_ekey(const char *audio_hash,
                                char **ekey_out,
                                char *error,
                                size_t error_capacity);

/* Finds a QQ Music EKey in a local Android player_process_db SQLite file. */
int qw_qmc_database_lookup_ekey(const char *database_path,
                                const char *const *names,
                                size_t name_count,
                                char **ekey_out,
                                char *error,
                                size_t error_capacity);

#endif
