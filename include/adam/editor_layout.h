/* Adam AI - GPL-3.0-only
 * Word-aware visual wrapping for the ncursesw message composer.
 * This module performs NO mutation of the user's message.
 */
#ifndef ADAM_EDITOR_LAYOUT_H
#define ADAM_EDITOR_LAYOUT_H

#include <stdbool.h>
#include <stddef.h>
#include <wchar.h>

typedef struct {
    int row;      /* 0-based visual row */
    int col;      /* 0-based terminal-cell column */
    bool visible; /* false for explicit newlines and skipped wrap spaces */
} AdamEditorCell;

/* positions must contain at least length + 1 elements.
 * positions[length] is the position of the insertion cursor at the end.
 * A word is moved as a unit when it fits on the next line.  Oversized words
 * are split at a terminal-cell boundary.  Returns visual row count.
 * Input is kept byte-for-byte unchanged and is not modified in-place.
 */
int adam_editor_layout(const wchar_t *text, size_t length, int width,
                       AdamEditorCell *positions);

#endif
