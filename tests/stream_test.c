/* Copyright (C) 2026 Adam Cír - SPDX-License-Identifier: GPL-3.0-only */
#include "stream.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    char thinking[100];
    char output[100];
    int done;
    int errors;
} Result;

static void event(AdamStreamType type, const char *text, void *context) {
    Result *r = context;
    if (type == ADAM_STREAM_THINKING) strncat(r->thinking, text, sizeof(r->thinking) - strlen(r->thinking) - 1);
    if (type == ADAM_STREAM_TEXT) strncat(r->output, text, sizeof(r->output) - strlen(r->output) - 1);
    if (type == ADAM_STREAM_ERROR) r->errors++;
    if (type == ADAM_STREAM_DONE) r->done++;
}

int main(void) {
    AdamStreamParser parser;
    adam_stream_init(&parser);
    Result result = {0};
    const char *data = "{\"message\":{\"thinking\":\"A\"},\"done\":false}\n"
                       "{\"message\":{\"content\":\"Hell\"},\"done\":false}\n"
                       "{\"message\":{\"content\":\"o!\"},\"done\":false}\n"
                       "{\"done\":true}\n";
    for (size_t i = 0; i < strlen(data); i += 3) {
        size_t n = strlen(data + i) < 3 ? strlen(data + i) : 3;
        assert(adam_stream_feed(&parser, data + i, n, event, &result) == 0);
    }
    assert(strcmp(result.thinking, "A") == 0);
    assert(strcmp(result.output, "Hello!") == 0);
    assert(result.done == 1);
    assert(result.errors == 0);
    adam_stream_destroy(&parser);
    puts("stream parser passed");
    return 0;
}
