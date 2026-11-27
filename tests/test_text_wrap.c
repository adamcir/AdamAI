/* Adam AI - GPL-3.0-only */
#include "adam/text_wrap.h"
#include <assert.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>

typedef struct { wchar_t lines[64][128]; size_t count; } Collected;
static bool collect(const wchar_t *line, size_t n, void *userdata) {
    Collected *result = userdata;
    if (result->count >= 64 || n > 127) return false;
    wmemcpy(result->lines[result->count], line, n);
    result->lines[result->count][n] = L'\0';
    ++result->count;
    return true;
}
static void check(const wchar_t *input, int width, AdamWrapMode mode,
                  const wchar_t *expected[], size_t count) {
    Collected got = {0};
    assert(adam_wrap_lines(input, width, mode, collect, &got));
    assert(got.count == count);
    for (size_t i = 0; i < count; ++i)
        if (wcscmp(got.lines[i], expected[i])) {
            fprintf(stderr, "Unexpected line %zu\n", i);
            abort();
        }
}
int main(void) {
    setlocale(LC_ALL, "");
    const wchar_t *lorem[] = {
        L"Lorem ipsum dolor sit amet,",
        L"consectetur adipiscing elit.",
        L"Donec aliquet, ligula at",
        L"pellentesque consectetur, orci"
    };
    check(L"Lorem ipsum dolor sit amet, consectetur adipiscing elit. "
          L"Donec aliquet, ligula at pellentesque consectetur, orci",
          32, ADAM_WRAP_WORDS, lorem, 4);
    const wchar_t *longword[] = {L"Hello", L"supercalifr", L"agilistic", L"done"};
    check(L"Hello supercalifragilistic done", 11, ADAM_WRAP_WORDS, longword, 4);
    const wchar_t *unicode[] = {L"Příliš", L"žluťoučký", L"kůň"};
    check(L"Příliš žluťoučký kůň", 10, ADAM_WRAP_WORDS, unicode, 3);
    const wchar_t *newlines[] = {L"One", L"", L"Two"};
    check(L"One\n\nTwo", 10, ADAM_WRAP_WORDS, newlines, 3);
    const wchar_t *literal[] = {L"  int  x", L" =  123;"};
    check(L"  int  x =  123;", 8, ADAM_WRAP_VERBATIM, literal, 2);
    puts("Text wrapping OK");
    return 0;
}
