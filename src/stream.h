/* Adam AI - streamed Ollama response parser
 * Copyright (C) 2026 Adam Cír
 * SPDX-License-Identifier: GPL-3.0-only
 */
#ifndef ADAM_STREAM_H
#define ADAM_STREAM_H
#include <stddef.h>

typedef enum {
    ADAM_STREAM_THINKING,
    ADAM_STREAM_TEXT,
    ADAM_STREAM_ERROR,
    ADAM_STREAM_DONE
} AdamStreamType;

typedef void (*AdamStreamCallback)(AdamStreamType type, const char *text, void *userdata);

typedef struct {
    char *pending;
    size_t length;
    size_t capacity;
    int done;
} AdamStreamParser;

void adam_stream_init(AdamStreamParser *parser);
int adam_stream_feed(AdamStreamParser *parser, const char *data, size_t size,
                     AdamStreamCallback callback, void *userdata);
void adam_stream_destroy(AdamStreamParser *parser);
#endif
