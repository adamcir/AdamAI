/* AdamAI - GPL-3.0-only
 * Unicode-aware selection and replacement helpers, independent of curses.
 */
#ifndef ADAM_TEXT_SELECTION_H
#define ADAM_TEXT_SELECTION_H
#include <stdbool.h>
#include <stddef.h>
#include <wchar.h>

typedef struct { int row, col; } AdamTextPoint;
typedef struct { const wchar_t *text; int indent; } AdamTextLine;
/* Selection coordinates are (wrapped-row, terminal-cell-column).
 * Returns UTF-8 text (caller frees), NULL for empty selection/allocation failure.
 * max_bytes bounds clipboard memory. Newlines join selected visual lines. */
char *adam_text_selection_copy(const AdamTextLine *lines, size_t count,
                               AdamTextPoint first, AdamTextPoint last,
                               size_t max_bytes);
/* Replace [start,end) in the composer with `count` wide characters.
 * The cursor advances to the end of inserted text; capacity includes NUL.
 * False leaves the buffer unchanged. */
bool adam_prompt_replace(wchar_t *buffer, size_t *length, size_t *cursor,
                         size_t capacity, size_t start, size_t end,
                         const wchar_t *insert, size_t count);
#endif
