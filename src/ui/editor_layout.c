/* Adam AI - GPL-3.0-only */
#define _XOPEN_SOURCE 700
#include "adam/editor_layout.h"
#include <wchar.h>

static int char_width(wchar_t ch) {
    if (ch == L'\t') return 1;
    int width = wcwidth(ch);
    return width < 0 ? 1 : width;
}

static bool blank(wchar_t ch) { return ch == L' ' || ch == L'\t'; }

/* Limit measurement so huge words do not waste work or overflow int. */
static int word_width(const wchar_t *text, size_t begin, size_t length, int limit) {
    int cells = 0;
    for (size_t j = begin; j < length && text[j] != L'\n' && !blank(text[j]); ++j) {
        int cw = char_width(text[j]);
        if (cw > limit - cells) return limit + 1;
        cells += cw;
    }
    return cells;
}

int adam_editor_layout(const wchar_t *text, size_t length, int width,
                       AdamEditorCell *positions) {
    if (!text || !positions || width <= 0) return 0;
    int row = 0, col = 0;
    for (size_t i = 0; i < length; ++i) {
        wchar_t ch = text[i];
        if (ch == L'\n') {
            /* If the preceding glyph ended exactly on the right boundary,
             * its soft wrap and this explicit newline count as one break. */
            bool at_boundary = col >= width;
            if (at_boundary) { ++row; col = 0; }
            positions[i] = (AdamEditorCell){row, col, false};
            if (!at_boundary) ++row;
            col = 0;
            continue;
        }
        if (col >= width) { ++row; col = 0; }

        /* Prefer breaking at a space before a word rather than dividing
         * 'Lorem' into 'Lor' and 'em'.  Suppress only the VISUAL spaces;
         * all spaces stay intact in text and will be sent to the model. */
        if (blank(ch) && col > 0 && (i == 0 || !blank(text[i - 1]))) {
            size_t end = i;
            int blanks = 0;
            while (end < length && blank(text[end])) { ++blanks; ++end; }
            if (end < length && text[end] != L'\n') {
                int next = word_width(text, end, length, width);
                if (blanks + next > width - col) {
                    ++row;
                    col = 0;
                    while (i < end) {
                        positions[i++] = (AdamEditorCell){row, 0, false};
                    }
                    --i;
                    continue;
                }
            }
        }

        if (!blank(ch) && (i == 0 || blank(text[i - 1]) || text[i - 1] == L'\n') && col > 0) {
            if (word_width(text, i, length, width) > width - col) {
                ++row;
                col = 0;
            }
        }

        int cw = char_width(ch);
        if (cw > width) cw = 1;
        if (col + cw > width) { ++row; col = 0; }
        positions[i] = (AdamEditorCell){row, col, true};
        col += cw;
    }
    if (col >= width) { ++row; col = 0; }
    positions[length] = (AdamEditorCell){row, col, false};
    return row + 1;
}
