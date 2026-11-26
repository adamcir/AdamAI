/* Adam AI - GPL-3.0-only */
#ifndef ADAM_OLLAMA_H
#define ADAM_OLLAMA_H
#include "adam/chat.h"
#include "adam/fs_tool.h"
#include <stdbool.h>
#include <pthread.h>
#include <stdatomic.h>

typedef enum { ADAM_STREAM_THINKING, ADAM_STREAM_TEXT, ADAM_STREAM_ERROR, ADAM_STREAM_TOOL, ADAM_STREAM_APPROVAL, ADAM_STREAM_DONE } AdamStreamType;
typedef struct AdamStreamEvent {
    AdamStreamType type;
    char *text;
    struct AdamStreamEvent *next;
} AdamStreamEvent;
typedef struct {
    pthread_t thread;
    pthread_mutex_t lock;
    AdamStreamEvent *head;
    AdamStreamEvent *tail;
    atomic_bool cancelled;
    char *payload;
    char *url;
    bool started;
    bool web_enabled;
    bool fs_enabled;
    AdamFs fs;
    pthread_cond_t approval_cond;
    bool approval_pending;
    int approval_decision; /* -1 waiting, 0 deny, 1 allow */
} AdamStream;

/* Starts one async Ollama /api/chat request; the UI thread owns this object. */
bool adam_ollama_start(AdamStream *stream, const AdamChat *chat,
                      const char *model, const char *endpoint, const char *think,
                      bool web_enabled, bool fs_enabled, const char *fs_root);
/* Only explicit TUI actions may approve a pending write_file call. */
bool adam_ollama_answer_approval(AdamStream *stream, bool approve);
/* Returns next event (caller frees text/event), or NULL when no event is available. */
AdamStreamEvent *adam_ollama_poll(AdamStream *stream);
void adam_ollama_stop(AdamStream *stream);

/* Pure decoding helper: returns number of events (0..3), for unit tests. */
int adam_ollama_decode_line(const char *line,
                           void (*emit)(AdamStreamType, const char *, void *), void *userdata);
#endif
