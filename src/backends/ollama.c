/* Adam AI - GPL-3.0-only */
#include "adam/ollama.h"
#include <curl/curl.h>
#include <json-c/json.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define MAX_NDJSON_LINE (1024u * 1024u)
#define MAX_HISTORY_MESSAGES 64

typedef struct {
    AdamStream *stream;
    char *pending;
    size_t len;
    size_t cap;
} Receiver;

static void enqueue(AdamStreamType type, const char *text, void *userdata) {
    AdamStream *s = userdata;
    AdamStreamEvent *e = calloc(1, sizeof(*e));
    if (!e) return;
    e->type = type;
    if (text) {
        e->text = strdup(text);
        if (!e->text) { free(e); return; }
    }
    pthread_mutex_lock(&s->lock);
    if (s->tail) s->tail->next = e;
    else s->head = e;
    s->tail = e;
    pthread_mutex_unlock(&s->lock);
}

int adam_ollama_decode_line(const char *line,
                           void (*emit)(AdamStreamType, const char *, void *), void *userdata) {
    if (!line || !emit) return 0;
    struct json_object *root = json_tokener_parse(line);
    if (!root) return 0;
    int emitted = 0;
    struct json_object *v = NULL;
    if (json_object_object_get_ex(root, "error", &v) && json_object_is_type(v, json_type_string)) {
        emit(ADAM_STREAM_ERROR, json_object_get_string(v), userdata);
        ++emitted;
    }
    struct json_object *msg = NULL;
    if (json_object_object_get_ex(root, "message", &msg) && json_object_is_type(msg, json_type_object)) {
        if (json_object_object_get_ex(msg, "thinking", &v) && json_object_is_type(v, json_type_string)
            && json_object_get_string_len(v) > 0) {
            emit(ADAM_STREAM_THINKING, json_object_get_string(v), userdata);
            ++emitted;
        }
        if (json_object_object_get_ex(msg, "content", &v) && json_object_is_type(v, json_type_string)
            && json_object_get_string_len(v) > 0) {
            emit(ADAM_STREAM_TEXT, json_object_get_string(v), userdata);
            ++emitted;
        }
    }
    if (json_object_object_get_ex(root, "done", &v) && json_object_get_boolean(v)) {
        emit(ADAM_STREAM_DONE, NULL, userdata);
        ++emitted;
    }
    json_object_put(root);
    return emitted;
}

static size_t receive_chunk(char *ptr, size_t size, size_t nmemb, void *userdata) {
    Receiver *r = userdata;
    if (size && nmemb > SIZE_MAX / size) return 0;
    size_t n = size * nmemb;
    if (atomic_load(&r->stream->cancelled)) return 0;
    if (n > MAX_NDJSON_LINE || r->len > MAX_NDJSON_LINE - n) return 0;
    if (r->len + n + 1 > r->cap) {
        size_t cap = r->cap ? r->cap : 4096;
        while (cap < r->len + n + 1) cap *= 2;
        char *p = realloc(r->pending, cap);
        if (!p) return 0;
        r->pending = p;
        r->cap = cap;
    }
    memcpy(r->pending + r->len, ptr, n);
    r->len += n;
    r->pending[r->len] = '\0';
    size_t used = 0;
    for (size_t i = 0; i < r->len; ++i) {
        if (r->pending[i] != '\n') continue;
        r->pending[i] = '\0';
        if (i > used) adam_ollama_decode_line(r->pending + used, enqueue, r->stream);
        used = i + 1;
    }
    if (used) {
        memmove(r->pending, r->pending + used, r->len - used);
        r->len -= used;
        r->pending[r->len] = '\0';
    }
    return n;
}

static int progress_check(void *userdata, curl_off_t dltotal, curl_off_t dlnow,
                          curl_off_t ultotal, curl_off_t ulnow) {
    (void)dltotal; (void)dlnow; (void)ultotal; (void)ulnow;
    return atomic_load(&((AdamStream *)userdata)->cancelled) ? 1 : 0;
}

static void *stream_worker(void *arg) {
    AdamStream *s = arg;
    Receiver r = {.stream = s};
    CURL *curl = curl_easy_init();
    if (!curl) {
        enqueue(ADAM_STREAM_ERROR, "libcurl initialization failed", s);
    } else {
        struct curl_slist *headers = curl_slist_append(NULL, "Content-Type: application/json");
        curl_easy_setopt(curl, CURLOPT_URL, s->url);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, s->payload);
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, receive_chunk);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &r);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);
        curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 45L);
        curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1L);
        curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
        curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, progress_check);
        curl_easy_setopt(curl, CURLOPT_XFERINFODATA, s);
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
        CURLcode code = curl_easy_perform(curl);
        long status = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
        if (!atomic_load(&s->cancelled)) {
            if (code != CURLE_OK) {
                enqueue(ADAM_STREAM_ERROR, curl_easy_strerror(code), s);
            } else if (status < 200 || status >= 300) {
                char error[96];
                snprintf(error, sizeof(error), "Ollama returned HTTP %ld", status);
                enqueue(ADAM_STREAM_ERROR, error, s);
            }
        }
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
    }
    free(r.pending);
    enqueue(ADAM_STREAM_DONE, NULL, s);
    return NULL;
}

bool adam_ollama_start(AdamStream *s, const AdamChat *chat,
                      const char *model, const char *endpoint, const char *think) {
    if (!s || !chat || !model || !endpoint) return false;
    memset(s, 0, sizeof(*s));
    if (pthread_mutex_init(&s->lock, NULL) != 0) return false;
    atomic_init(&s->cancelled, false);
    struct json_object *root = json_object_new_object();
    struct json_object *messages = json_object_new_array();
    if (!root || !messages) { json_object_put(root); json_object_put(messages); pthread_mutex_destroy(&s->lock); return false; }
    json_object_object_add(root, "model", json_object_new_string(model));
    json_object_object_add(root, "stream", json_object_new_boolean(1));
    if (think && *think && strcmp(think, "off") != 0) {
        if (!strcmp(think, "true")) json_object_object_add(root, "think", json_object_new_boolean(1));
        else json_object_object_add(root, "think", json_object_new_string(think));
    }
    struct json_object *sys = json_object_new_object();
    json_object_object_add(sys, "role", json_object_new_string("system"));
    json_object_object_add(sys, "content", json_object_new_string(
        "You are Adam AI, a local AI assistant for Adava Development. "
        "Answer in the user's language. Be accurate and concise. "
        "You cannot access files, run programs, or browse the web yet; never claim you have done so."));
    json_object_array_add(messages, sys);
    size_t start = chat->count > MAX_HISTORY_MESSAGES ? chat->count - MAX_HISTORY_MESSAGES : 0;
    for (size_t i = start; i < chat->count; ++i) {
        const AdamMessage *m = &chat->items[i];
        if (!m->content || !*m->content) continue;
        struct json_object *item = json_object_new_object();
        json_object_object_add(item, "role", json_object_new_string(m->role == ADAM_ROLE_USER ? "user" : "assistant"));
        json_object_object_add(item, "content", json_object_new_string(m->content));
        json_object_array_add(messages, item);
    }
    json_object_object_add(root, "messages", messages);
    const char *json = json_object_to_json_string_ext(root, JSON_C_TO_STRING_PLAIN);
    s->payload = json ? strdup(json) : NULL;
    json_object_put(root);
    size_t url_size = strlen(endpoint) + 32;
    s->url = malloc(url_size);
    if (s->url) snprintf(s->url, url_size, "%s/api/chat", endpoint);
    if (!s->url || !s->payload || pthread_create(&s->thread, NULL, stream_worker, s) != 0) {
        free(s->url);
        free(s->payload);
        pthread_mutex_destroy(&s->lock);
        return false;
    }
    s->started = true;
    return true;
}

AdamStreamEvent *adam_ollama_poll(AdamStream *s) {
    if (!s || !s->started) return NULL;
    pthread_mutex_lock(&s->lock);
    AdamStreamEvent *e = s->head;
    if (e) {
        s->head = e->next;
        if (!s->head) s->tail = NULL;
        e->next = NULL;
    }
    pthread_mutex_unlock(&s->lock);
    return e;
}
void adam_ollama_stop(AdamStream *s) {
    if (!s || !s->started) return;
    atomic_store(&s->cancelled, true);
    pthread_join(s->thread, NULL);
    AdamStreamEvent *e;
    while ((e = adam_ollama_poll(s))) { free(e->text); free(e); }
    pthread_mutex_destroy(&s->lock);
    free(s->payload);
    free(s->url);
    memset(s, 0, sizeof(*s));
}
