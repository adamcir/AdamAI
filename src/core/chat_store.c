/* AdamAI - GPL-3.0-only */
#define _POSIX_C_SOURCE 200809L
#include "adam/chat_store.h"
#include <json-c/json.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define MAX_CHAT_FILE_BYTES (32u * 1024u * 1024u)
#define MAX_CHAT_MESSAGES 10000u

static void errorf(char *dst, size_t capacity, const char *fmt, ...) {
    if (!dst || !capacity) return;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(dst, capacity, fmt, ap);
    va_end(ap);
}

static bool copy_field(char *dst, size_t capacity, const char *src) {
    if (!dst || !capacity || !src || strlen(src) >= capacity) return false;
    memcpy(dst, src, strlen(src) + 1);
    return true;
}

static bool valid_id(const char *id) {
    if (!id || !*id || strlen(id) >= ADAM_CHAT_ID_SIZE) return false;
    for (const unsigned char *p = (const unsigned char *)id; *p; ++p) {
        if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
              (*p >= '0' && *p <= '9') || *p == '-' || *p == '_')) return false;
    }
    return true;
}

static bool chat_path(char *dest, size_t capacity, const char *directory, const char *id) {
    if (!directory || !valid_id(id)) return false;
    int n = snprintf(dest, capacity, "%s/%s.json", directory, id);
    return n > 0 && (size_t)n < capacity;
}

static bool check_directory(const char *path, char *error, size_t error_size) {
    if (mkdir(path, 0700) == 0) return true;
    if (errno != EEXIST) {
        errorf(error, error_size, "Cannot create %s: %s", path, strerror(errno));
        return false;
    }
    struct stat st;
    if (lstat(path, &st) != 0 || !S_ISDIR(st.st_mode)) {
        errorf(error, error_size, "%s is not a directory", path);
        return false;
    }
    return true;
}

bool adam_chat_store_init(char *directory, size_t length, char *error, size_t error_size) {
    const char *home = getenv("HOME");
    if (!home || home[0] != '/') {
        errorf(error, error_size, "HOME is not an absolute path");
        return false;
    }
    char parent[PATH_MAX];
    int n = snprintf(parent, sizeof(parent), "%s/adam-ai", home);
    if (n <= 0 || (size_t)n >= sizeof(parent)) return false;
    if (!check_directory(parent, error, error_size)) return false;
    n = snprintf(directory, length, "%s/chats", parent);
    if (n <= 0 || (size_t)n >= length) {
        errorf(error, error_size, "Chat directory path too long");
        return false;
    }
    return check_directory(directory, error, error_size);
}

bool adam_chat_store_new_id(char out[ADAM_CHAT_ID_SIZE]) {
    static unsigned counter = 0;
    struct timespec now;
    if (clock_gettime(CLOCK_REALTIME, &now) != 0) return false;
    unsigned n = ++counter;
    int result = snprintf(out, ADAM_CHAT_ID_SIZE, "chat-%lld-%09ld-%ld-%u",
                          (long long)now.tv_sec, now.tv_nsec, (long)getpid(), n);
    return result > 0 && result < ADAM_CHAT_ID_SIZE;
}

static struct json_object *read_json(const char *path) {
    int flags = O_RDONLY | O_CLOEXEC;
#ifdef O_NOFOLLOW
    flags |= O_NOFOLLOW;
#endif
    int fd = open(path, flags);
    if (fd < 0) return NULL;
    struct stat st;
    if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) ||
        st.st_size <= 0 || st.st_size > MAX_CHAT_FILE_BYTES) {
        close(fd);
        return NULL;
    }
    size_t size = (size_t)st.st_size;
    char *buf = malloc(size + 1);
    if (!buf) { close(fd); return NULL; }
    size_t have = 0;
    while (have < size) {
        ssize_t got = read(fd, buf + have, size - have);
        if (got < 0 && errno == EINTR) continue;
        if (got <= 0) break;
        have += (size_t)got;
    }
    close(fd);
    struct json_object *json = NULL;
    if (have == size) {
        buf[size] = '\0';
        struct json_tokener *tokener = json_tokener_new();
        if (tokener) {
            json = json_tokener_parse_ex(tokener, buf, (int)size);
            if (json_tokener_get_error(tokener) != json_tokener_success) {
                if (json) json_object_put(json);
                json = NULL;
            }
            json_tokener_free(tokener);
        }
    }
    free(buf);
    return json;
}

static const char *string_member(struct json_object *obj, const char *key) {
    struct json_object *member = NULL;
    if (!json_object_object_get_ex(obj, key, &member) ||
        !json_object_is_type(member, json_type_string)) return NULL;
    return json_object_get_string(member);
}

bool adam_chat_store_save(const char *directory, const AdamStoredChat *metadata,
                          const AdamChat *chat, char *error, size_t error_size) {
    char path[PATH_MAX], tmp[PATH_MAX];
    if (!metadata || !chat || !chat_path(path, sizeof(path), directory, metadata->id)) {
        errorf(error, error_size, "Invalid chat ID/path");
        return false;
    }
    struct json_object *root = json_object_new_object();
    struct json_object *messages = json_object_new_array();
    if (!root || !messages) {
        if (root) json_object_put(root);
        if (messages) json_object_put(messages);
        return false;
    }
    json_object_object_add(root, "version", json_object_new_int(1));
    json_object_object_add(root, "id", json_object_new_string(metadata->id));
    json_object_object_add(root, "title", json_object_new_string(metadata->title));
    json_object_object_add(root, "model", json_object_new_string(metadata->model));
    json_object_object_add(root, "updated_at", json_object_new_int64((int64_t)time(NULL)));
    json_object_object_add(root, "messages", messages);
    for (size_t i = 0; i < chat->count; ++i) {
        const AdamMessage *m = &chat->items[i];
        struct json_object *item = json_object_new_object();
        if (!item) { json_object_put(root); return false; }
        json_object_object_add(item, "role", json_object_new_string(
            m->role == ADAM_ROLE_USER ? "user" : "assistant"));
        json_object_object_add(item, "content", json_object_new_string(m->content ? m->content : ""));
        json_object_object_add(item, "thinking", json_object_new_string(m->thinking ? m->thinking : ""));
        json_object_array_add(messages, item);
    }
    const char *serialized = json_object_to_json_string_ext(root, JSON_C_TO_STRING_PRETTY);
    if (!serialized || strlen(serialized) > MAX_CHAT_FILE_BYTES) {
        errorf(error, error_size, "Conversation is too large to save");
        json_object_put(root);
        return false;
    }
    static unsigned temp_counter;
    int n = snprintf(tmp, sizeof(tmp), "%s.tmp-%ld-%u", path,
                     (long)getpid(), ++temp_counter);
    if (n <= 0 || (size_t)n >= sizeof(tmp)) {
        json_object_put(root);
        return false;
    }
    int fd = open(tmp, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
    if (fd < 0) {
        errorf(error, error_size, "Cannot open save file: %s", strerror(errno));
        json_object_put(root);
        return false;
    }
    size_t length = strlen(serialized), done = 0;
    bool ok = true;
    while (done < length) {
        ssize_t sent = write(fd, serialized + done, length - done);
        if (sent < 0 && errno == EINTR) continue;
        if (sent <= 0) { ok = false; break; }
        done += (size_t)sent;
    }
    if (ok && fsync(fd) != 0) ok = false;
    if (close(fd) != 0) ok = false;
    if (ok && rename(tmp, path) != 0) ok = false;
    if (!ok) {
        errorf(error, error_size, "Saving chat failed: %s", strerror(errno));
        unlink(tmp);
    }
    json_object_put(root);
    return ok;
}

bool adam_chat_store_load(const char *directory, const char *id,
                          AdamStoredChat *metadata, AdamChat *chat,
                          char *error, size_t error_size) {
    char path[PATH_MAX];
    if (!metadata || !chat || !chat_path(path, sizeof(path), directory, id)) {
        errorf(error, error_size, "Invalid chat ID/path");
        return false;
    }
    struct json_object *root = read_json(path);
    if (!root || !json_object_is_type(root, json_type_object)) {
        errorf(error, error_size, "Cannot parse chat file: %s", id);
        if (root) json_object_put(root);
        return false;
    }
    const char *file_id = string_member(root, "id");
    const char *title = string_member(root, "title");
    const char *model = string_member(root, "model");
    struct json_object *messages = NULL, *version = NULL;
    bool valid = file_id && strcmp(file_id, id) == 0 && title && model &&
                 strlen(title) < ADAM_CHAT_TITLE_SIZE &&
                 strlen(model) < ADAM_CHAT_MODEL_SIZE &&
                 json_object_object_get_ex(root, "version", &version) &&
                 json_object_get_int(version) == 1 &&
                 json_object_object_get_ex(root, "messages", &messages) &&
                 json_object_is_type(messages, json_type_array) &&
                 json_object_array_length(messages) <= MAX_CHAT_MESSAGES;
    AdamChat replacement;
    adam_chat_init(&replacement);
    if (valid) for (size_t i = 0; i < json_object_array_length(messages); ++i) {
        struct json_object *msg = json_object_array_get_idx(messages, i);
        if (!json_object_is_type(msg, json_type_object)) { valid = false; break; }
        const char *role = string_member(msg, "role");
        const char *content = string_member(msg, "content");
        const char *thinking = string_member(msg, "thinking");
        if (!role || !content || !thinking ||
            (strcmp(role, "user") && strcmp(role, "assistant"))) {
            valid = false; break;
        }
        AdamMessage *added = adam_chat_add(&replacement,
            strcmp(role, "user") ? ADAM_ROLE_ASSISTANT : ADAM_ROLE_USER, content);
        if (!added || (thinking[0] && !adam_text_append(&added->thinking, thinking))) {
            valid = false; break;
        }
    }
    if (!valid) {
        errorf(error, error_size, "Invalid or unsupported chat data: %s", id);
        adam_chat_clear(&replacement);
        json_object_put(root);
        return false;
    }
    AdamStoredChat loaded = {0};
    copy_field(loaded.id, sizeof(loaded.id), id);
    copy_field(loaded.title, sizeof(loaded.title), title);
    copy_field(loaded.model, sizeof(loaded.model), model);
    struct json_object *timestamp = NULL;
    if (json_object_object_get_ex(root, "updated_at", &timestamp))
        loaded.updated_at = (time_t)json_object_get_int64(timestamp);
    json_object_put(root);
    adam_chat_clear(chat);
    *chat = replacement;
    *metadata = loaded;
    return true;
}

static int compare_recency(const void *pa, const void *pb) {
    const AdamStoredChat *a = pa, *b = pb;
    if (a->updated_at != b->updated_at)
        return a->updated_at > b->updated_at ? -1 : 1;
    if (a->updated_nsec != b->updated_nsec)
        return a->updated_nsec > b->updated_nsec ? -1 : 1;
    return strcmp(b->id, a->id);
}

size_t adam_chat_store_list(const char *directory, AdamStoredChat *items, size_t capacity) {
    if (!directory || !items || !capacity) return 0;
    DIR *dir = opendir(directory);
    if (!dir) return 0;
    if (capacity > ADAM_CHAT_LIST_LIMIT) capacity = ADAM_CHAT_LIST_LIMIT;
    size_t count = 0;
    struct dirent *entry;
    while ((entry = readdir(dir))) {
        size_t n = strlen(entry->d_name);
        if (n <= 5 || n >= ADAM_CHAT_ID_SIZE + 5 ||
            strcmp(entry->d_name + n - 5, ".json")) continue;
        char id[ADAM_CHAT_ID_SIZE];
        memcpy(id, entry->d_name, n - 5);
        id[n - 5] = '\0';
        if (!valid_id(id)) continue;
        char path[PATH_MAX];
        if (!chat_path(path, sizeof(path), directory, id)) continue;
        struct stat st;
        if (lstat(path, &st) != 0 || !S_ISREG(st.st_mode)) continue;
        AdamStoredChat record = {0};
        copy_field(record.id, sizeof(record.id), id);
        record.updated_at = st.st_mtime;
        record.updated_nsec = st.st_mtim.tv_nsec;
        if (count < capacity) items[count++] = record;
        else {
            size_t oldest = 0;
            for (size_t k = 1; k < count; ++k)
                if (compare_recency(&items[k], &items[oldest]) > 0) oldest = k;
            if (compare_recency(&record, &items[oldest]) < 0) items[oldest] = record;
        }
    }
    closedir(dir);
    qsort(items, count, sizeof(*items), compare_recency);
    for (size_t i = 0; i < count; ++i) {
        char path[PATH_MAX];
        if (!chat_path(path, sizeof(path), directory, items[i].id)) continue;
        struct json_object *obj = read_json(path);
        const char *title = obj ? string_member(obj, "title") : NULL;
        const char *model = obj ? string_member(obj, "model") : NULL;
        if (!title || !copy_field(items[i].title, sizeof(items[i].title), title))
            copy_field(items[i].title, sizeof(items[i].title), "Untitled chat");
        if (model) copy_field(items[i].model, sizeof(items[i].model), model);
        if (obj) json_object_put(obj);
    }
    return count;
}

bool adam_chat_store_delete(const char *directory, const char *id,
                            char *error, size_t error_size) {
    char path[PATH_MAX];
    if (!chat_path(path, sizeof(path), directory, id)) {
        errorf(error, error_size, "Invalid chat ID");
        return false;
    }
    if (unlink(path) != 0) {
        errorf(error, error_size, "Cannot delete chat: %s", strerror(errno));
        return false;
    }
    return true;
}
