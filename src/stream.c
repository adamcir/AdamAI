/* Adam AI - streamed Ollama response parser
 * Copyright (C) 2026 Adam Cír
 * SPDX-License-Identifier: GPL-3.0-only
 */
#include "stream.h"
#include <json-c/json.h>
#include <stdlib.h>
#include <string.h>

#define ADAM_MAX_STREAM_LINE (8u * 1024u * 1024u)

void adam_stream_init(AdamStreamParser *parser) {
    memset(parser, 0, sizeof(*parser));
}

static void parse_line(AdamStreamParser *parser, const char *line,
                       AdamStreamCallback callback, void *userdata) {
    if (!*line) return;
    struct json_tokener *tok = json_tokener_new();
    if (!tok) return;
    struct json_object *root = json_tokener_parse_ex(tok, line, (int)strlen(line));
    if (json_tokener_get_error(tok) == json_tokener_success && root) {
        struct json_object *field = NULL;
        if (json_object_object_get_ex(root, "error", &field)) {
            callback(ADAM_STREAM_ERROR, json_object_get_string(field), userdata);
        }
        if (json_object_object_get_ex(root, "message", &field) &&
            json_object_is_type(field, json_type_object)) {
            struct json_object *value = NULL;
            if (json_object_object_get_ex(field, "thinking", &value) &&
                json_object_is_type(value, json_type_string)) {
                const char *s = json_object_get_string(value);
                if (s && *s) callback(ADAM_STREAM_THINKING, s, userdata);
            }
            if (json_object_object_get_ex(field, "content", &value) &&
                json_object_is_type(value, json_type_string)) {
                const char *s = json_object_get_string(value);
                if (s && *s) callback(ADAM_STREAM_TEXT, s, userdata);
            }
        }
        if (json_object_object_get_ex(root, "done", &field) &&
            json_object_get_boolean(field) && !parser->done) {
            parser->done = 1;
            callback(ADAM_STREAM_DONE, "", userdata);
        }
    } else {
        callback(ADAM_STREAM_ERROR, "Invalid JSON from Ollama", userdata);
    }
    if (root) json_object_put(root);
    json_tokener_free(tok);
}

int adam_stream_feed(AdamStreamParser *parser, const char *data, size_t size,
                     AdamStreamCallback callback, void *userdata) {
    if (!parser || (!data && size) || !callback) return -1;
    if (size > ADAM_MAX_STREAM_LINE || parser->length > ADAM_MAX_STREAM_LINE - size)
        return -1;
    if (parser->length + size + 1 > parser->capacity) {
        size_t capacity = parser->capacity ? parser->capacity : 1024;
        while (capacity < parser->length + size + 1) capacity *= 2;
        char *next = realloc(parser->pending, capacity);
        if (!next) return -1;
        parser->pending = next;
        parser->capacity = capacity;
    }
    memcpy(parser->pending + parser->length, data, size);
    parser->length += size;
    parser->pending[parser->length] = '\0';
    char *start = parser->pending;
    char *newline = NULL;
    while ((newline = memchr(start, '\n', (size_t)(parser->pending + parser->length - start)))) {
        *newline = '\0';
        if (newline > start && newline[-1] == '\r') newline[-1] = '\0';
        parse_line(parser, start, callback, userdata);
        start = newline + 1;
    }
    size_t remaining = (size_t)(parser->pending + parser->length - start);
    memmove(parser->pending, start, remaining);
    parser->length = remaining;
    parser->pending[remaining] = '\0';
    return 0;
}

void adam_stream_destroy(AdamStreamParser *parser) {
    free(parser->pending);
    memset(parser, 0, sizeof(*parser));
}
