/* Adam AI - GPL-3.0-only */
#include "adam/tui.h"
#include <locale.h>
#include <stdio.h>
int main(void) {
    if (!setlocale(LC_ALL, "")) fprintf(stderr, "Warning: locale unavailable; Unicode may not display correctly\n");
    return adam_tui_run();
}
