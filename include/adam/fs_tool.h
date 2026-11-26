/* AdamAI - GPL-3.0-only */
#ifndef ADAM_FS_TOOL_H
#define ADAM_FS_TOOL_H
#define _POSIX_C_SOURCE 200809L
#include <stdbool.h>
#include <stddef.h>
#include <limits.h>

#define ADAM_FS_READ_LIMIT (128u * 1024u)
#define ADAM_FS_WRITE_LIMIT (32u * 1024u)
#define ADAM_FS_LIST_LIMIT 128

typedef struct {
    int root_fd;
    char root[PATH_MAX];
} AdamFs;

/* The root directory is held open; all paths are relative to that fd.
 * '.' is allowed for listing; absolute paths, '..' and symlinks are refused.
 * Return values and allocated results are owned by the caller. */
bool adam_fs_init(AdamFs *fs, const char *root, char *error, size_t error_cap);
void adam_fs_close(AdamFs *fs);
bool adam_fs_valid_path(const char *path, bool allow_dot);
bool adam_fs_list(AdamFs *fs, const char *relative, char **output, char *error, size_t cap);
bool adam_fs_read(AdamFs *fs, const char *relative, char **output, char *error, size_t cap);
bool adam_fs_write(AdamFs *fs, const char *relative, const char *content,
                   size_t length, char *error, size_t cap);
/* Can only be used as an indicator for the review pane. Real checks repeat
 * inside adam_fs_write immediately before commit. */
bool adam_fs_target_info(AdamFs *fs, const char *relative, bool *exists,
                         size_t *size, char *error, size_t cap);
#endif
