/* Adam AI - GPL-3.0-only */
#ifndef ADAM_CHAT_H
#define ADAM_CHAT_H
#include <stddef.h>
#include <stdbool.h>

typedef enum { ADAM_ROLE_USER, ADAM_ROLE_ASSISTANT } AdamRole;
typedef struct {
    AdamRole role;
    char *content;  /* UTF-8 */
    char *thinking; /* UTF-8; only populated for models exposing a reasoning channel */
} AdamMessage;
typedef struct {
    AdamMessage *items;
    size_t count;
    size_t capacity;
} AdamChat;

void adam_chat_init(AdamChat *chat);
void adam_chat_clear(AdamChat *chat);
AdamMessage *adam_chat_add(AdamChat *chat, AdamRole role, const char *content);
bool adam_text_append(char **target, const char *delta);
#endif
