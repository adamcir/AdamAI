/* AdamAI - GPL-3.0-only */
#define _POSIX_C_SOURCE 200809L
#include "adam/chat_store.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>

int main(void) {
    char tmp[] = "/tmp/adam-ai-chat-test-XXXXXX";
    char *dir = mkdtemp(tmp);
    assert(dir);
    char store[4096], err[256] = "";
    assert(setenv("HOME", dir, 1) == 0);
    assert(adam_chat_store_init(store, sizeof(store), err, sizeof(err)));
    assert(strstr(store, "/adam-ai/chats"));

    AdamStoredChat initial = {0};
    assert(adam_chat_store_new_id(initial.id));
    assert(strlen(initial.id) < ADAM_CHAT_ID_SIZE);
    strcpy(initial.title, "Příliš žluťoučký kůň");
    strcpy(initial.model, "adam1:1.0");
    AdamChat chat;
    adam_chat_init(&chat);
    assert(adam_chat_add(&chat, ADAM_ROLE_USER, "Ahoj!\nJaký je návrh?"));
    AdamMessage *answer = adam_chat_add(&chat, ADAM_ROLE_ASSISTANT, "Kód: int main() {}\nČesky.");
    assert(answer);
    assert(adam_text_append(&answer->thinking, "Zvažuju alternativy."));
    assert(adam_chat_store_save(store, &initial, &chat, err, sizeof(err)));

    char filepath[4096];
    assert(snprintf(filepath, sizeof(filepath), "%s/%s.json", store, initial.id) < (int)sizeof(filepath));
    struct stat st;
    assert(stat(filepath, &st) == 0);
    assert((st.st_mode & 0777) == 0600);

    AdamStoredChat list[ADAM_CHAT_LIST_LIMIT];
    assert(adam_chat_store_list(store, list, ADAM_CHAT_LIST_LIMIT) == 1);
    assert(strcmp(list[0].title, initial.title) == 0);
    AdamChat loaded;
    adam_chat_init(&loaded);
    AdamStoredChat metadata = {0};
    assert(adam_chat_store_load(store, initial.id, &metadata, &loaded, err, sizeof(err)));
    assert(strcmp(metadata.model, "adam1:1.0") == 0);
    assert(loaded.count == 2);
    assert(strcmp(loaded.items[0].content, chat.items[0].content) == 0);
    assert(strcmp(loaded.items[1].thinking, chat.items[1].thinking) == 0);
    assert(!adam_chat_store_load(store, "../etc/passwd", &metadata, &loaded, err, sizeof(err)));
    assert(loaded.count == 2); /* failed load must not erase the current chat */

    assert(adam_chat_store_delete(store, initial.id, err, sizeof(err)));
    assert(adam_chat_store_list(store, list, ADAM_CHAT_LIST_LIMIT) == 0);
    adam_chat_clear(&loaded);
    adam_chat_clear(&chat);
    assert(rmdir(store) == 0);
    char parent[4096];
    assert(snprintf(parent, sizeof(parent), "%s/adam-ai", dir) < (int)sizeof(parent));
    assert(rmdir(parent) == 0);
    assert(rmdir(dir) == 0);
    puts("chat store tests OK");
    return 0;
}
