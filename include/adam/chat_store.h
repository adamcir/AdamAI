/* AdamAI - GPL-3.0-only */
#ifndef ADAM_CHAT_STORE_H
#define ADAM_CHAT_STORE_H
#include "adam/chat.h"
#include <stdbool.h>
#include <stddef.h>
#include <time.h>

#define ADAM_CHAT_ID_SIZE 64
#define ADAM_CHAT_TITLE_SIZE 96
#define ADAM_CHAT_MODEL_SIZE 128
#define ADAM_CHAT_LIST_LIMIT 128

typedef struct {
    char id[ADAM_CHAT_ID_SIZE];
    char title[ADAM_CHAT_TITLE_SIZE];
    char model[ADAM_CHAT_MODEL_SIZE];
    time_t updated_at;
    long updated_nsec;
} AdamStoredChat;

/* Creates ~/adam-ai/chats with private permissions; directory is absolute. */
bool adam_chat_store_init(char *directory, size_t length, char *error, size_t error_size);
/* Generates a safe unique filename component (without .json). */
bool adam_chat_store_new_id(char out[ADAM_CHAT_ID_SIZE]);
/* Writes JSON atomically via a private temporary file and rename(). */
bool adam_chat_store_save(const char *directory, const AdamStoredChat *metadata,
                          const AdamChat *chat, char *error, size_t error_size);
/* On failure, neither the existing chat nor metadata are changed. */
bool adam_chat_store_load(const char *directory, const char *id,
                          AdamStoredChat *metadata, AdamChat *chat,
                          char *error, size_t error_size);
/* Most recently modified first. Does not follow symlinks. */
size_t adam_chat_store_list(const char *directory, AdamStoredChat *items, size_t capacity);
bool adam_chat_store_delete(const char *directory, const char *id,
                            char *error, size_t error_size);
#endif
