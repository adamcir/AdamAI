/* Adam AI - GPL-3.0-only */
#include "adam/chat.h"
#include "adam/ollama.h"
#include <assert.h>
#include <string.h>
#include <stdlib.h>

typedef struct { char text[128]; char think[128]; int done; int errors; } Capture;
static void record(AdamStreamType type, const char *text, void *userdata) {
    Capture *c = userdata;
    if (type == ADAM_STREAM_TEXT) strncat(c->text, text, sizeof(c->text)-strlen(c->text)-1);
    else if (type == ADAM_STREAM_THINKING) strncat(c->think, text, sizeof(c->think)-strlen(c->think)-1);
    else if (type == ADAM_STREAM_DONE) c->done++;
    else c->errors++;
}
int main(void) {
    AdamChat chat;
    adam_chat_init(&chat);
    assert(adam_chat_add(&chat, ADAM_ROLE_USER, "Ahoj") != NULL);
    AdamMessage *assistant = adam_chat_add(&chat, ADAM_ROLE_ASSISTANT, "");
    assert(assistant != NULL);
    assert(adam_text_append(&assistant->content, "Aho"));
    assert(adam_text_append(&assistant->content, "j!"));
    assert(strcmp(assistant->content, "Ahoj!") == 0);
    Capture c = {0};
    assert(adam_ollama_decode_line("{\"message\":{\"thinking\":\"plan\",\"content\":\"hello\"},\"done\":false}", record, &c) == 2);
    assert(adam_ollama_decode_line("{\"message\":{\"content\":\" world\"},\"done\":true}", record, &c) == 2);
    assert(strcmp(c.think, "plan") == 0);
    assert(strcmp(c.text, "hello world") == 0);
    assert(c.done == 1);
    assert(adam_ollama_decode_line("{\"error\":\"oops\"}", record, &c) == 1);
    assert(c.errors == 1);
    adam_chat_clear(&chat);
    return 0;
}
