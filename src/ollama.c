/* Adam AI - Ollama streaming adapter
 * Copyright (C) 2026 Adam Cír
 * SPDX-License-Identifier: GPL-3.0-only
 */
#include "ollama.h"
#include <curl/curl.h>
#include <json-c/json.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

struct Transfer {
    AdamStreamParser parser;
    AdamStreamCallback callback;
    void *userdata;
    const atomic_bool *cancel;
    int parser_error;
};

static size_t receive_data(char *ptr, size_t size, size_t nmemb, void *ctx) {
    struct Transfer *transfer = ctx;
    if (size && nmemb > SIZE_MAX / size) return 0;
    size_t len = size * nmemb;
    if (atomic_load(transfer->cancel)) return 0;
    if (adam_stream_feed(&transfer->parser, ptr, len,
                         transfer->callback, transfer->userdata) != 0) {
        transfer->parser_error = 1;
        return 0;
    }
    return len;
}

static struct json_object *make_request(const AdamOllamaRequest *request) {
    struct json_object *root = json_object_new_object();
    struct json_object *messages = json_object_new_array();
    struct json_object *options = json_object_new_object();
    if (!root || !messages || !options) goto error;
    json_object_object_add(root, "model", json_object_new_string(request->model));
    json_object_object_add(root, "stream", json_object_new_boolean(1));
    if (request->thinking && strcmp(request->thinking, "off") == 0)
        json_object_object_add(root, "think", json_object_new_boolean(0));
    else if (request->thinking && *request->thinking)
        json_object_object_add(root, "think", json_object_new_string(request->thinking));
    json_object_object_add(options, "num_ctx", json_object_new_int(request->context_length));
    json_object_object_add(root, "options", options);
    options = NULL;
    for (size_t i = 0; i < request->message_count; i++) {
        struct json_object *item = json_object_new_object();
        if (!item) goto error;
        json_object_object_add(item, "role", json_object_new_string(request->messages[i].role));
        json_object_object_add(item, "content", json_object_new_string(request->messages[i].content));
        json_object_array_add(messages, item);
    }
    json_object_object_add(root, "messages", messages);
    return root;
error:
    if (options) json_object_put(options);
    if (messages) json_object_put(messages);
    if (root) json_object_put(root);
    return NULL;
}

int adam_ollama_chat(const AdamOllamaRequest *request,
                     AdamStreamCallback callback, void *userdata) {
    if (!request || !callback || !request->endpoint || !request->model ||
        !request->cancel) return -1;
    int ok = -1;
    struct json_object *payload = make_request(request);
    if (!payload) return -1;
    CURL *curl = curl_easy_init();
    if (!curl) goto finish;
    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    struct Transfer transfer = {.callback = callback, .userdata = userdata,
                                .cancel = request->cancel};
    adam_stream_init(&transfer.parser);
    size_t url_len = strlen(request->endpoint) + 10;
    char *url = malloc(url_len);
    if (!url) goto curl_finish;
    snprintf(url, url_len, "%s/api/chat", request->endpoint);
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_object_to_json_string_ext(payload, JSON_C_TO_STRING_PLAIN));
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, receive_data);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &transfer);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 8L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 0L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_NOPROXY, "*"); /* Local inference only */
    CURLcode result = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    if (atomic_load(request->cancel)) {
        callback(ADAM_STREAM_ERROR, "Generation interrupted", userdata);
    } else if (transfer.parser_error) {
        callback(ADAM_STREAM_ERROR, "Response exceeded streaming buffer limit", userdata);
    } else if (result != CURLE_OK) {
        callback(ADAM_STREAM_ERROR, curl_easy_strerror(result), userdata);
    } else if (http_code != 200) {
        char message[100];
        snprintf(message, sizeof(message), "HTTP error %ld from Ollama", http_code);
        callback(ADAM_STREAM_ERROR, message, userdata);
    } else if (!transfer.parser.done) {
        callback(ADAM_STREAM_ERROR, "Stream ended unexpectedly", userdata);
    } else {
        ok = 0;
    }
    free(url);
curl_finish:
    adam_stream_destroy(&transfer.parser);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
finish:
    json_object_put(payload);
    return ok;
}
