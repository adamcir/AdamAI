/* Adam AI - GPL-3.0-only */
#define _XOPEN_SOURCE 700
#include "adam/text_wrap.h"
#include <stdlib.h>
#include <stdint.h>
#include <wctype.h>

/* Terminal cell width, not number of Unicode code points. */
static int cell_width(wchar_t c) {
    int w = wcwidth(c);
    return w < 0 ? 1 : w;
}
typedef struct {
    wchar_t *chars;
    size_t used;
    int cells, width;
    AdamWrapLineFn emit;
    void *userdata;
} Wrap;
static bool flush(Wrap *w) {
    if (!w->emit(w->chars, w->used, w->userdata)) return false;
    w->used = 0;
    w->cells = 0;
    return true;
}
static bool append(Wrap *w, wchar_t c) {
    int cw = cell_width(c);
    if (cw > w->width) { c = L'?'; cw = 1; }
    if (w->used && cw > w->width - w->cells && !flush(w)) return false;
    w->chars[w->used++] = c;
    w->cells += cw;
    return true;
}
static bool append_word(Wrap *w, const wchar_t *word, size_t n, bool space) {
    /* Clamp so huge tokens do not overflow an int during measuring. */
    int width = 0;
    for (size_t i = 0; i < n; ++i) {
        int cw = cell_width(word[i]);
        if (cw > w->width || width > w->width - cw) {
            width = w->width + 1;
            break;
        }
        width += cw;
    }
    if (w->used && width > w->width - w->cells - (space ? 1 : 0)) {
        if (!flush(w)) return false;
    }
    if (space && w->used && !append(w, L' ')) return false;
    /* Only tokens too long for a whole line may be split. */
    for (size_t i = 0; i < n; ++i) {
        wchar_t c = iswcntrl(word[i]) ? L'?' : word[i];
        if (!append(w, c)) return false;
    }
    return true;
}
bool adam_wrap_lines(const wchar_t *source, int columns, AdamWrapMode mode,
                     AdamWrapLineFn emit, void *userdata) {
    if (!source || !emit || columns <= 0) return false;
    size_t n = wcslen(source);
    if (n > (SIZE_MAX / sizeof(wchar_t)) - 1) return false;
    Wrap w = {.chars = malloc((n + 1) * sizeof(wchar_t)),
              .width = columns, .emit = emit, .userdata = userdata};
    if (!w.chars) return false;
    bool ok = true;
    if (mode == ADAM_WRAP_VERBATIM) {
        for (size_t i = 0; i < n && ok; ++i) {
            wchar_t c = source[i];
            if (c == L'\r') continue;
            if (c == L'\n') { ok = flush(&w); continue; }
            if (c == L'\t') c = L' ';
            if (iswcntrl(c)) c = L'?';
            ok = append(&w, c);
        }
    } else {
        size_t i = 0;
        bool space = false;
        while (i < n && ok) {
            wchar_t c = source[i];
            if (c == L'\r') { ++i; continue; }
            if (c == L'\n') {
                ok = flush(&w);
                space = false;
                ++i;
                continue;
            }
            if (iswspace(c)) { space = true; ++i; continue; }
            size_t start = i;
            while (i < n && source[i] != L'\n' &&
                   source[i] != L'\r' && !iswspace(source[i])) ++i;
            ok = append_word(&w, source + start, i - start, space);
            space = false;
        }
    }
    if (ok && w.used) ok = flush(&w);
    free(w.chars);
    return ok;
}
