/* Copyright (C) 2026 Adam Cír - SPDX-License-Identifier: GPL-3.0-only */
#ifndef ADAM_OLLAMA_H
#define ADAM_OLLAMA_H
#include "stream.h"
#include <stdatomic.h>
#include <stddef.h>

typedef struct {
    const char *role;
    const char *content;
} AdamChatMessage;

typedef struct {
    const char *endpoint;
    const char *model;
    const char *thinking; /* off | low | medium | high */
    int context_length;
    const AdamChatMessage *messages;
    size_t message_count;
    const atomic_bool *cancel;
} AdamOllamaRequest;

int adam_ollama_chat(const AdamOllamaRequest *request,
                     AdamStreamCallback callback, void *userdata);
#endif
