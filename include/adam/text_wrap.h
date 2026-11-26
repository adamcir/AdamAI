/* Adam AI - GPL-3.0-only */
#ifndef ADAM_TEXT_WRAP_H
#define ADAM_TEXT_WRAP_H
#include <stdbool.h>
#include <stddef.h>
#include <wchar.h>
typedef enum {
    ADAM_WRAP_WORDS,      /* Prose: wrap at word boundaries. */
    ADAM_WRAP_VERBATIM    /* Code: preserve whitespace. */
} AdamWrapMode;
typedef bool (*AdamWrapLineFn)(const wchar_t *line, size_t count, void *userdata);
bool adam_wrap_lines(const wchar_t *source, int columns, AdamWrapMode mode,
                     AdamWrapLineFn emit, void *userdata);
#endif
