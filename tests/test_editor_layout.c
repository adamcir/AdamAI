/* Adam AI - GPL-3.0-only */
#define _XOPEN_SOURCE 700
#include "adam/editor_layout.h"
#include <assert.h>
#include <locale.h>
#include <stddef.h>
#include <wchar.h>

static int layout(const wchar_t *text, int width, AdamEditorCell *map) {
    return adam_editor_layout(text, wcslen(text), width, map);
}

int main(void) {
    setlocale(LC_ALL, "C.UTF-8");
    AdamEditorCell map[128];

    /* The word 'Lorem' must move entirely to the following line. */
    const wchar_t *sentence = L"testuji něco dlouhé, třeba Lorem Ipsum";
    int count = layout(sentence, 30, map);
    assert(count >= 2);
    const wchar_t *lorem = wcsstr(sentence, L"Lorem");
    assert(lorem);
    size_t i = (size_t)(lorem - sentence);
    assert(map[i].col == 0);
    for (int j = 0; j < 5; ++j) assert(map[i + (size_t)j].row == map[i].row);
    assert(map[wcslen(sentence)].row == count - 1);

    /* Regression: the real Czech prompt must not cut "Lorem" into "Lor"/"em". */
    const wchar_t *screenshot =
        L"Jenom tady testuji jednu věc, napiš zde něco co má 6 odstavců "
        L"a je to dlouhé, třeba Lorem Ipsum";
    assert(layout(screenshot, 87, map) == 2);
    size_t lorem_at = (size_t)(wcsstr(screenshot, L"Lorem") - screenshot);
    assert(map[lorem_at].row == 1 && map[lorem_at].col == 0);
    assert(map[lorem_at + 5].row == 1);

    /* A blank at a wrap point is invisible, but remains in the raw text. */
    const wchar_t *simple = L"hello Lorem";
    assert(layout(simple, 8, map) == 2);
    assert(!map[5].visible);
    assert(map[6].row == 1 && map[6].col == 0);
    assert(map[11].row == 1 && map[11].col == 5);

    /* Explicit newline and Unicode characters. */
    const wchar_t *lines = L"Příliš\nžluťoučký";
    assert(layout(lines, 12, map) == 2);
    assert(map[7].row == 1 && map[7].col == 0);

    /* Very long words are still split and never rendered outside the field. */
    assert(layout(L"abcdefghij", 4, map) == 3);
    assert(map[4].row == 1 && map[4].col == 0);
    assert(map[8].row == 2 && map[8].col == 0);

    assert(layout(L"", 8, map) == 1);
    assert(map[0].row == 0 && map[0].col == 0);
    assert(layout(L"\n\nX", 4, map) == 3);
    assert(map[2].row == 2 && map[2].col == 0);
    assert(layout(L"1234\nX", 4, map) == 2);
    assert(map[5].row == 1 && map[5].col == 0);

    /* East Asian wide glyphs occupy two terminal cells. */
    assert(layout(L"A界B", 3, map) == 2);
    assert(map[1].row == 0 && map[1].col == 1);
    assert(map[2].row == 1 && map[2].col == 0);
    return 0;
}
