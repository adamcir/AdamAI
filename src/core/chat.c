/* Adam AI - GPL-3.0-only */
#include "adam/chat.h"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

void adam_chat_init(AdamChat *chat) {
    if (chat) memset(chat, 0, sizeof(*chat));
}
void adam_chat_clear(AdamChat *chat) {
    if (!chat) return;
    for (size_t i = 0; i < chat->count; ++i) {
        free(chat->items[i].content);
        free(chat->items[i].thinking);
    }
    free(chat->items);
    adam_chat_init(chat);
}
bool adam_text_append(char **target, const char *delta) {
    if (!target || !delta) return false;
    size_t old_len = *target ? strlen(*target) : 0;
    size_t add_len = strlen(delta);
    if (old_len > SIZE_MAX - add_len - 1) return false;
    char *result = realloc(*target, old_len + add_len + 1);
    if (!result) return false;
    memcpy(result + old_len, delta, add_len + 1);
    *target = result;
    return true;
}
AdamMessage *adam_chat_add(AdamChat *chat, AdamRole role, const char *content) {
    if (!chat) return NULL;
    if (chat->count == chat->capacity) {
        size_t next = chat->capacity ? chat->capacity * 2 : 16;
        if (next < chat->capacity || next > SIZE_MAX / sizeof(*chat->items)) return NULL;
        AdamMessage *p = realloc(chat->items, next * sizeof(*p));
        if (!p) return NULL;
        chat->items = p;
        chat->capacity = next;
    }
    AdamMessage *msg = &chat->items[chat->count];
    memset(msg, 0, sizeof(*msg));
    msg->role = role;
    msg->content = strdup(content ? content : "");
    if (!msg->content) return NULL;
    ++chat->count;
    return msg;
}
