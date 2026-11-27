/* AdamAI - GPL-3.0-only */
#define _POSIX_C_SOURCE 200809L
#include "adam/clipboard.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void file_contents_equal(const char *path, const char *expected) {
    FILE *f = fopen(path, "rb");
    assert(f);
    char buf[500] = {0};
    size_t n = fread(buf, 1, sizeof(buf)-1, f);
    fclose(f);
    assert(n == strlen(expected));
    assert(!memcmp(buf, expected, n));
}
static void make_executable(const char *path) {
    FILE *f = fopen(path, "wb");
    assert(f);
    assert(fputs("#!/bin/sh\n/bin/cat > \"$ADAM_CLIP_TEST_OUTPUT\"\n", f) >= 0);
    assert(fclose(f) == 0);
    assert(chmod(path,0700) == 0);
}
int main(void) {
    char dir[] = "/tmp/adamai-clipboard-test-XXXXXX";
    assert(mkdtemp(dir));
    char path[512], output[512];
    snprintf(output, sizeof(output), "%s/copied.txt", dir);
    assert(setenv("ADAM_CLIP_TEST_OUTPUT", output, 1) == 0);
    assert(setenv("PATH", dir, 1) == 0);
    assert(setenv("WAYLAND_DISPLAY", "test-wayland-0", 1) == 0);
    assert(setenv("DISPLAY", ":99", 1) == 0);

    snprintf(path, sizeof(path), "%s/wl-copy", dir);
    make_executable(path);
    assert(adam_clipboard_copy_system("Příliš žluťoučký kůň\n第二行") == ADAM_CLIPBOARD_WAYLAND);
    file_contents_equal(output,"Příliš žluťoučký kůň\n第二行");
    assert(unlink(path) == 0);
    assert(unlink(output) == 0);

    snprintf(path, sizeof(path), "%s/xclip", dir);
    make_executable(path);
    assert(adam_clipboard_copy_system("hello from AdamAI") == ADAM_CLIPBOARD_X11);
    file_contents_equal(output,"hello from AdamAI");
    assert(unlink(path) == 0);
    assert(unlink(output) == 0);

    snprintf(path, sizeof(path), "%s/xsel", dir);
    make_executable(path);
    assert(adam_clipboard_copy_system("xsel fallback") == ADAM_CLIPBOARD_X11);
    file_contents_equal(output,"xsel fallback");
    assert(unlink(path) == 0);
    assert(unlink(output) == 0);

    assert(unsetenv("WAYLAND_DISPLAY") == 0);
    assert(unsetenv("DISPLAY") == 0);
    assert(adam_clipboard_copy_system("no desktop") == ADAM_CLIPBOARD_UNAVAILABLE);
    assert(adam_clipboard_copy_system("") == ADAM_CLIPBOARD_UNAVAILABLE);
    assert(adam_clipboard_copy_system(NULL) == ADAM_CLIPBOARD_UNAVAILABLE);
    assert(rmdir(dir) == 0);
    puts("Desktop clipboard tests passed");
    return 0;
}
