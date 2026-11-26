/* Adam AI - ncursesw terminal interface
 * Copyright (C) 2026 Adam Cír
 * SPDX-License-Identifier: GPL-3.0-only
 * Original interface inspired by the OpenCode workflow; no UI code copied.
 */
#define _XOPEN_SOURCE 700
#include "ollama.h"
#include <curl/curl.h>
#include <locale.h>
#include <limits.h>
#include <stdint.h>
#include <ncurses.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <wctype.h>
#include <unistd.h>

#define INPUT_MAX 8192
#define HISTORY_MAX 64
#define MESSAGE_LIMIT 400

enum { STYLE_DEFAULT = 1, STYLE_HEADER, STYLE_USER, STYLE_ADAM,
       STYLE_THINK, STYLE_ERROR, STYLE_BORDER, STYLE_INPUT, STYLE_HINT };

typedef struct {
    char *role;
    char *content;
    char *thinking;
} Message;

typedef struct Event {
    AdamStreamType type;
    char *text;
    struct Event *next;
} Event;

typedef struct {
    int style;
    wchar_t *text;
} Line;

typedef struct {
    Line *lines;
    size_t count;
    size_t capacity;
} LineSet;

typedef struct App {
    Message *messages;
    size_t message_count;
    size_t message_capacity;
    char input[INPUT_MAX];
    size_t cursor;
    char *prompts[HISTORY_MAX];
    size_t prompt_count;
    int prompt_position;
    int scroll;               /* lines away from bottom; 0 follows streaming */
    size_t pending_message_index;
    bool show_thinking;
    bool running;
    bool busy;
    bool finished;
    char model[128];
    char endpoint[512];
    char thinking[16];
    int context_length;
    pthread_t worker;
    pthread_mutex_t event_lock;
    Event *event_head;
    Event *event_tail;
    atomic_bool cancel;
} App;

typedef struct {
    App *app;
    AdamChatMessage *history;
    size_t count;
    char *endpoint;
    char *model;
    char *thinking;
    int context_length;
} WorkerArgs;

static char *dupstr(const char *text) {
    char *result = strdup(text ? text : "");
    if (!result) { endwin(); fputs("Adam AI: Out of memory\n", stderr); exit(1); }
    return result;
}

static void append(char **target, const char *text) {
    if (!text || !*text) return;
    size_t old = strlen(*target), add = strlen(text);
    if (add > SIZE_MAX - old - 1) return;
    char *next = realloc(*target, old + add + 1);
    if (!next) return;
    memcpy(next + old, text, add + 1);
    *target = next;
}

static void push_message(App *app, const char *role, const char *text) {
    if (app->message_count == MESSAGE_LIMIT) {
        free(app->messages[0].role);
        free(app->messages[0].content);
        free(app->messages[0].thinking);
        memmove(app->messages, app->messages + 1, (MESSAGE_LIMIT - 1) * sizeof(Message));
        app->message_count--;
    }
    if (app->message_count == app->message_capacity) {
        size_t next = app->message_capacity ? app->message_capacity * 2 : 16;
        Message *items = realloc(app->messages, next * sizeof(*items));
        if (!items) return;
        app->messages = items;
        app->message_capacity = next;
    }
    app->messages[app->message_count++] = (Message){dupstr(role), dupstr(text), dupstr("")};
}

static void clear_messages(App *app) {
    for (size_t i = 0; i < app->message_count; i++) {
        free(app->messages[i].role);
        free(app->messages[i].content);
        free(app->messages[i].thinking);
    }
    app->message_count = 0;
    app->scroll = 0;
}

static void enqueue(App *app, AdamStreamType type, const char *text) {
    Event *item = calloc(1, sizeof(*item));
    if (!item) return;
    item->type = type;
    item->text = dupstr(text);
    pthread_mutex_lock(&app->event_lock);
    if (app->event_tail) app->event_tail->next = item;
    else app->event_head = item;
    app->event_tail = item;
    pthread_mutex_unlock(&app->event_lock);
}

static void worker_event(AdamStreamType type, const char *text, void *userdata) {
    enqueue(userdata, type, text);
}

static void *worker_main(void *ptr) {
    WorkerArgs *args = ptr;
    AdamOllamaRequest request = {
        .endpoint = args->endpoint, .model = args->model,
        .thinking = args->thinking, .context_length = args->context_length,
        .messages = args->history, .message_count = args->count,
        .cancel = &args->app->cancel
    };
    adam_ollama_chat(&request, worker_event, args->app);
    /* Internal notification only after the network request has exited. */
    enqueue(args->app, ADAM_STREAM_DONE, "__WORKER_EXITED__");
    for (size_t i = 0; i < args->count; i++) {
        free((char *)args->history[i].role);
        free((char *)args->history[i].content);
    }
    free(args->history);
    free(args->endpoint);
    free(args->model);
    free(args->thinking);
    free(args);
    return NULL;
}

static void process_events(App *app) {
    pthread_mutex_lock(&app->event_lock);
    Event *events = app->event_head;
    app->event_head = app->event_tail = NULL;
    pthread_mutex_unlock(&app->event_lock);
    for (Event *e = events; e;) {
        Event *next = e->next;
        if (e->type == ADAM_STREAM_DONE && strcmp(e->text, "__WORKER_EXITED__") == 0) {
            if (app->busy) pthread_join(app->worker, NULL);
            app->busy = false;
        } else if (app->message_count) {
            Message *last = &app->messages[app->pending_message_index < app->message_count
                                              ? app->pending_message_index : app->message_count - 1];
            switch (e->type) {
            case ADAM_STREAM_THINKING: append(&last->thinking, e->text); break;
            case ADAM_STREAM_TEXT: append(&last->content, e->text); break;
            case ADAM_STREAM_ERROR:
                append(&last->content, "\n[Error] ");
                append(&last->content, e->text);
                break;
            case ADAM_STREAM_DONE: break;
            }
        }
        free(e->text);
        free(e);
        e = next;
    }
}

static void add_line(LineSet *set, const wchar_t *content, size_t count, int style) {
    if (set->count == set->capacity) {
        size_t next = set->capacity ? set->capacity * 2 : 128;
        Line *items = realloc(set->lines, next * sizeof(*items));
        if (!items) return;
        set->lines = items;
        set->capacity = next;
    }
    wchar_t *line = calloc(count + 1, sizeof(wchar_t));
    if (!line) return;
    wmemcpy(line, content, count);
    set->lines[set->count++] = (Line){style, line};
}

static void add_text(LineSet *set, const char *utf8, int width, int style) {
    if (width <= 0) return;
    wchar_t *current = calloc((size_t)width + 4, sizeof(*current));
    if (!current) return;
    size_t used = 0;
    int column = 0;
    mbstate_t state = {0};
    const char *p = utf8;
    while (p && *p) {
        wchar_t wc = L'?';
        size_t n = mbrtowc(&wc, p, MB_CUR_MAX, &state);
        if (n == (size_t)-1 || n == (size_t)-2 || n == 0) {
            memset(&state, 0, sizeof(state));
            wc = L'?'; n = 1;
        }
        p += n;
        if (wc == L'\r') continue;
        if (wc == L'\n') {
            add_line(set, current, used, style);
            used = 0; column = 0;
            continue;
        }
        if (wc == L'\t') wc = L' ';
        if (iswcntrl(wc)) wc = L' ';
        int cw = wcwidth(wc);
        if (cw < 0) {wc = L'?'; cw = 1;}
        if (column + cw > width && used) {
            add_line(set, current, used, style);
            used = 0; column = 0;
        }
        if (cw > width) continue;
        if (used < (size_t)width + 2) current[used++] = wc;
        column += cw;
    }
    add_line(set, current, used, style);
    free(current);
}

static void free_lines(LineSet *set) {
    for (size_t i = 0; i < set->count; i++) free(set->lines[i].text);
    free(set->lines);
    memset(set, 0, sizeof(*set));
}

static void style(int color) {
    if (has_colors()) attrset(COLOR_PAIR(color));
    else attrset(A_NORMAL);
}

static void draw_rule(int y, int width) {
    style(STYLE_BORDER);
    mvhline(y, 0, ACS_HLINE, width);
}

static void draw_input(App *app, int y, int width) {
    style(STYLE_INPUT);
    mvaddstr(y, 2, "> ");
    wchar_t converted[INPUT_MAX];
    size_t n = mbstowcs(converted, app->input, INPUT_MAX - 1);
    if (n == (size_t)-1) n = 0;
    converted[n] = L'\0';
    char cursor_text[INPUT_MAX];
    memcpy(cursor_text, app->input, app->cursor);
    cursor_text[app->cursor] = '\0';
    size_t cursor_w = mbstowcs(NULL, cursor_text, 0);
    if (cursor_w == (size_t)-1) cursor_w = n;
    int visible = width - 7;
    if (visible < 1) return;
    size_t left = (cursor_w >= (size_t)visible) ? cursor_w - (size_t)visible + 1 : 0;
    if (left > n) left = n;
    /* Keep a visible single-line editor. Wide chars are not split by bytes. */
    for (size_t i = left; i < n; i++) {
        if (iswcntrl(converted[i])) converted[i] = L' ';
    }
    mvaddnwstr(y, 4, converted + left, visible);
    if (!*app->input) {
        style(STYLE_HINT);
        mvaddnstr(y, 4, "Ask Adam AI anything...", visible);
    }
    int x = 4 + (int)(cursor_w - left);
    move(y, x >= width - 2 ? width - 2 : x);
}

static void render(App *app) {
    int height, width;
    getmaxyx(stdscr, height, width);
    erase();
    if (height < 11 || width < 46) {
        mvaddstr(1, 1, "Terminal too small (min 46 x 11)");
        mvaddstr(3, 1, "Resize or press Ctrl+D to exit");
        refresh();
        return;
    }
    style(STYLE_HEADER);
    attron(A_BOLD);
    mvaddstr(0, 2, "ADAM AI");
    attroff(A_BOLD);
    mvaddnstr(0, 14, "chat  /  Adava Development", width - 18);
    style(STYLE_HINT);
    char status[700];
    snprintf(status, sizeof(status), "MODEL %s    THINK %s    %s",
             app->model, app->thinking, app->busy ? "GENERATING..." : "READY");
    mvaddnstr(1, 2, status, width - 4);
    draw_rule(2, width);

    LineSet all = {0};
    int text_width = width - 7;
    if (app->message_count == 0) {
        add_text(&all, "Welcome to Adam AI", text_width, STYLE_ADAM);
        add_text(&all, "Local assistant for development and engineering.", text_width, STYLE_HINT);
        add_text(&all, "Type /help for commands, then send a message.", text_width, STYLE_HINT);
    }
    for (size_t i = 0; i < app->message_count; i++) {
        const Message *m = &app->messages[i];
        add_text(&all, strcmp(m->role, "user") == 0 ? "YOU" : "ADAM AI",
                 text_width, strcmp(m->role, "user") == 0 ? STYLE_USER : STYLE_ADAM);
        if (*m->thinking) {
            add_text(&all, app->show_thinking ? "  THINKING [Ctrl+T: hide]" :
                     "  THINKING [Ctrl+T: expand]", text_width, STYLE_THINK);
            if (app->show_thinking) add_text(&all, m->thinking, text_width, STYLE_THINK);
        }
        if (*m->content)
            add_text(&all, m->content, text_width, STYLE_DEFAULT);
        else if (i == app->message_count - 1 && app->busy)
            add_text(&all, "...", text_width, STYLE_HINT);
        add_text(&all, "", text_width, STYLE_DEFAULT);
    }
    int top = 3, bottom = height - 5;
    int available = bottom - top + 1;
    int max_scroll = (int)all.count > available ? (int)all.count - available : 0;
    if (app->scroll > max_scroll) app->scroll = max_scroll;
    int start = (int)all.count - available - app->scroll;
    if (start < 0) start = 0;
    for (int y = top; y <= bottom; y++) {
        size_t idx = (size_t)(start + y - top);
        if (idx >= all.count) break;
        style(all.lines[idx].style);
        mvaddnwstr(y, 3, all.lines[idx].text, text_width);
    }
    if (app->scroll > 0) {
        style(STYLE_HINT);
        char info[45];
        snprintf(info, sizeof(info), "^ %d lines", app->scroll);
        mvaddnstr(3, width - 16, info, 14);
    }
    free_lines(&all);
    draw_rule(height - 4, width);
    draw_input(app, height - 3, width);
    style(STYLE_HINT);
    mvaddnstr(height - 1, 2,
              "Enter send | PgUp/PgDn scroll | Ctrl+T thinking | Ctrl+C stop | Ctrl+D exit | /help",
              width - 4);
    refresh();
}

static size_t prev_char(const char *s, size_t index) {
    if (!index) return 0;
    index--;
    while (index && ((unsigned char)s[index] & 0xC0) == 0x80) index--;
    return index;
}

static size_t next_char(const char *s, size_t index) {
    if (!s[index]) return index;
    index++;
    while (s[index] && ((unsigned char)s[index] & 0xC0) == 0x80) index++;
    return index;
}

static void set_input(App *app, const char *text) {
    snprintf(app->input, sizeof(app->input), "%s", text);
    app->cursor = strlen(app->input);
}

static void add_prompt_history(App *app, const char *text) {
    if (app->prompt_count == HISTORY_MAX) {
        free(app->prompts[0]);
        memmove(app->prompts, app->prompts + 1, (HISTORY_MAX - 1) * sizeof(char *));
        app->prompt_count--;
    }
    app->prompts[app->prompt_count++] = dupstr(text);
    app->prompt_position = -1;
}

static void local_reply(App *app, const char *text) {
    push_message(app, "assistant", text);
    app->scroll = 0;
}

static void send_prompt(App *app) {
    char *text = app->input;
    while (*text == ' ' || *text == '\t') text++;
    if (!*text) return;
    if (strcmp(text, "/quit") == 0 || strcmp(text, "/exit") == 0) {
        if (!app->busy) app->running = false;
        else local_reply(app, "Stop the active generation first (Ctrl+C).");
    } else if (strcmp(text, "/help") == 0) {
        local_reply(app, "COMMANDS\n/help - show help\n/clear - clear conversation\n/model NAME - choose Ollama model\n/think off|low|medium|high - set reasoning\n/quit - quit\n\nCtrl+T - show/hide thinking, Ctrl+C - cancel, Ctrl+D - quit\nPageUp/PageDown - scroll, Up/Down - prompt history.");
    } else if (strcmp(text, "/clear") == 0) {
        if (app->busy) local_reply(app, "Cannot clear while generating.");
        else clear_messages(app);
    } else if (strncmp(text, "/model ", 7) == 0 && !app->busy) {
        if (strlen(text + 7) < sizeof(app->model) && text[7]) {
            snprintf(app->model, sizeof(app->model), "%s", text + 7);
            local_reply(app, "Model changed. The next message will use the selected model.");
        }
    } else if (strncmp(text, "/think ", 7) == 0 && !app->busy) {
        const char *v = text + 7;
        if (strcmp(v, "off") == 0 || strcmp(v, "low") == 0 ||
            strcmp(v, "medium") == 0 || strcmp(v, "high") == 0) {
            snprintf(app->thinking, sizeof(app->thinking), "%s", v);
            local_reply(app, "Thinking level updated.");
        } else local_reply(app, "Use /think off, low, medium, or high.");
    } else if (*text == '/') {
        local_reply(app, "Unknown command. Type /help.");
    } else if (app->busy) {
        local_reply(app, "Finish or cancel the current response first.");
    } else {
        push_message(app, "user", text);
        WorkerArgs *args = calloc(1, sizeof(*args));
        if (!args) {local_reply(app, "Cannot allocate request."); goto reset;}
        args->app = app;
        args->endpoint = dupstr(app->endpoint);
        args->model = dupstr(app->model);
        args->thinking = dupstr(app->thinking);
        args->context_length = app->context_length;
        size_t first = app->message_count > 28 ? app->message_count - 28 : 0;
        args->count = app->message_count - first + 1;
        args->history = calloc(args->count, sizeof(*args->history));
        if (!args->history) {
            free(args->endpoint); free(args->model); free(args->thinking); free(args);
            local_reply(app, "Cannot allocate history."); goto reset;
        }
        args->history[0] = (AdamChatMessage){dupstr("system"),
            dupstr("You are Adam AI, a helpful technical assistant developed for Adava Development. "
                   "Respond in the user's language. Be precise and transparent about tool limitations.")};
        for (size_t i = first; i < app->message_count; i++) {
            const Message *m = &app->messages[i];
            args->history[i - first + 1] = (AdamChatMessage){dupstr(m->role), dupstr(m->content)};
        }
        push_message(app, "assistant", "");
        app->pending_message_index = app->message_count - 1;
        app->busy = true;
        atomic_store(&app->cancel, false);
        if (pthread_create(&app->worker, NULL, worker_main, args) != 0) {
            app->busy = false;
            append(&app->messages[app->message_count - 1].content, "Cannot start inference thread.");
            for (size_t i = 0; i < args->count; i++) {
                free((char *)args->history[i].role); free((char *)args->history[i].content);
            }
            free(args->history); free(args->endpoint); free(args->model); free(args->thinking); free(args);
        }
    }
reset:
    add_prompt_history(app, app->input);
    app->input[0] = '\0';
    app->cursor = 0;
    app->scroll = 0;
}

static void on_key(App *app, wint_t key, int code) {
    if (code == KEY_CODE_YES) {
        switch (key) {
        case KEY_RESIZE: break;
        case KEY_PPAGE: app->scroll += 10; break;
        case KEY_NPAGE: app->scroll = app->scroll > 10 ? app->scroll - 10 : 0; break;
        case KEY_END: if (!app->input[0]) app->scroll = 0; else app->cursor = strlen(app->input); break;
        case KEY_HOME: app->cursor = 0; break;
        case KEY_LEFT: app->cursor = prev_char(app->input, app->cursor); break;
        case KEY_RIGHT: app->cursor = next_char(app->input, app->cursor); break;
        case KEY_BACKSPACE:
            if (app->cursor) {
                size_t start = prev_char(app->input, app->cursor);
                memmove(app->input + start, app->input + app->cursor,
                        strlen(app->input + app->cursor) + 1);
                app->cursor = start;
            }
            break;
        case KEY_DC:
            if (app->input[app->cursor]) {
                size_t end = next_char(app->input, app->cursor);
                memmove(app->input + app->cursor, app->input + end,
                        strlen(app->input + end) + 1);
            }
            break;
        case KEY_UP:
            if (app->prompt_count && app->prompt_position != 0) {
                if (app->prompt_position < 0) app->prompt_position = (int)app->prompt_count - 1;
                else app->prompt_position--;
                set_input(app, app->prompts[app->prompt_position]);
            }
            break;
        case KEY_DOWN:
            if (app->prompt_position >= 0) {
                app->prompt_position++;
                if ((size_t)app->prompt_position >= app->prompt_count) {
                    app->prompt_position = -1; set_input(app, "");
                } else set_input(app, app->prompts[app->prompt_position]);
            }
            break;
#ifdef BUTTON4_PRESSED
        case KEY_MOUSE: {
            MEVENT ev;
            if (getmouse(&ev) == OK) {
                if (ev.bstate & BUTTON4_PRESSED) app->scroll += 3;
                if (ev.bstate & BUTTON5_PRESSED) app->scroll = app->scroll > 3 ? app->scroll - 3 : 0;
            }
            break;
        }
#endif
        default: break;
        }
        return;
    }
    if (key == 4) { /* Ctrl+D */
        if (!app->busy) app->running = false;
        else atomic_store(&app->cancel, true);
    } else if (key == 3) { /* Ctrl+C */
        if (app->busy) atomic_store(&app->cancel, true);
        else set_input(app, "");
    } else if (key == 20) { /* Ctrl+T */
        app->show_thinking = !app->show_thinking;
    } else if (key == 21) { /* Ctrl+U */
        set_input(app, "");
    } else if (key == 1) { app->cursor = 0;
    } else if (key == 5) { app->cursor = strlen(app->input);
    } else if (key == L'\n' || key == L'\r') {
        send_prompt(app);
    } else if (key == 127 || key == 8) {
        on_key(app, KEY_BACKSPACE, KEY_CODE_YES);
    } else if (key >= 32 && key != 127 && key <= 0x10ffff) {
        char encoded[MB_LEN_MAX];
        mbstate_t state = {0};
        size_t n = wcrtomb(encoded, (wchar_t)key, &state);
        size_t old = strlen(app->input);
        if (n != (size_t)-1 && old + n < sizeof(app->input)) {
            memmove(app->input + app->cursor + n, app->input + app->cursor,
                    old - app->cursor + 1);
            memcpy(app->input + app->cursor, encoded, n);
            app->cursor += n;
        }
    }
}

static void destroy(App *app) {
    if (app->busy) {
        atomic_store(&app->cancel, true);
        pthread_join(app->worker, NULL);
        app->busy = false;
    }
    process_events(app);
    clear_messages(app);
    free(app->messages);
    for (size_t i = 0; i < app->prompt_count; i++) free(app->prompts[i]);
    pthread_mutex_destroy(&app->event_lock);
}

int main(void) {
    setlocale(LC_ALL, "");
    if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO)) {
        fputs("Adam AI TUI needs an interactive UTF-8 terminal.\n", stderr);
        return 1;
    }
    App app = {.running = true, .context_length = 8192, .show_thinking = false,
               .prompt_position = -1};
    const char *value = getenv("ADAM_MODEL");
    snprintf(app.model, sizeof(app.model), "%s", value && *value ? value : "gpt-oss:20b");
    value = getenv("ADAM_OLLAMA_URL");
    snprintf(app.endpoint, sizeof(app.endpoint), "%s", value && *value ? value : "http://127.0.0.1:11434");
    value = getenv("ADAM_THINK");
    snprintf(app.thinking, sizeof(app.thinking), "%s", value && *value ? value : "medium");
    value = getenv("ADAM_CTX");
    if (value) {
        int context = atoi(value);
        if (context >= 512 && context <= 131072) app.context_length = context;
    }
    pthread_mutex_init(&app.event_lock, NULL);
    atomic_init(&app.cancel, false);
    curl_global_init(CURL_GLOBAL_DEFAULT);
    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    timeout(70);
    mousemask(BUTTON4_PRESSED | BUTTON5_PRESSED, NULL);
    curs_set(1);
    if (has_colors()) {
        start_color(); use_default_colors();
        init_pair(STYLE_DEFAULT, COLOR_WHITE, -1);
        init_pair(STYLE_HEADER, COLOR_CYAN, -1);
        init_pair(STYLE_USER, COLOR_GREEN, -1);
        init_pair(STYLE_ADAM, COLOR_CYAN, -1);
        init_pair(STYLE_THINK, COLOR_YELLOW, -1);
        init_pair(STYLE_ERROR, COLOR_RED, -1);
        init_pair(STYLE_BORDER, COLOR_BLUE, -1);
        init_pair(STYLE_INPUT, COLOR_WHITE, -1);
        init_pair(STYLE_HINT, COLOR_WHITE, -1);
    }
    while (app.running) {
        process_events(&app);
        render(&app);
        wint_t key;
        int code = get_wch(&key);
        if (code != ERR) on_key(&app, key, code);
    }
    endwin();
    destroy(&app);
    curl_global_cleanup();
    return 0;
}
