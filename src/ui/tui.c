/* Adam AI - GPL-3.0-only
 * Original ncursesw interface, inspired by the conversation-centered OpenCode workflow.
 * No OpenCode source code is used.
 */
#define _XOPEN_SOURCE 700
#define _XOPEN_SOURCE_EXTENDED 1
#include "adam/tui.h"
#include "adam/chat.h"
#include "adam/ollama.h"
#include <ncursesw/curses.h>
#include <locale.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <wctype.h>
#include <unistd.h>
#include <limits.h>
#include <curl/curl.h>

#define INPUT_CAP 8192
#define MODEL_CAP 128

enum { C_TEXT=1, C_ACCENT, C_DIM, C_USER, C_REASON, C_GOOD, C_ERROR };

typedef struct {
    wchar_t *text;
    int color;
} ViewLine;
typedef struct {
    ViewLine *data;
    size_t count;
    size_t cap;
} View;

typedef struct {
    AdamChat chat;
    AdamStream stream;
    wchar_t input[INPUT_CAP];
    size_t input_len;
    size_t cursor;
    bool running;
    bool busy;
    bool show_thinking;
    bool saw_error;
    int scroll;  /* number of wrapped lines above bottom */
    char status[256];
    char model[MODEL_CAP];
    const char *endpoint;
    const char *think_level;
    unsigned spin;
} App;

static void set_status(App *a, const char *s) {
    snprintf(a->status, sizeof(a->status), "%s", s ? s : "");
}
static void free_view(View *v) {
    for (size_t i = 0; i < v->count; ++i) free(v->data[i].text);
    free(v->data);
    memset(v, 0, sizeof(*v));
}
static bool view_line(View *v, const wchar_t *text, size_t count, int color) {
    if (v->count == v->cap) {
        size_t next = v->cap ? v->cap * 2 : 32;
        if (next < v->cap || next > SIZE_MAX / sizeof(*v->data)) return false;
        ViewLine *p = realloc(v->data, next * sizeof(*p));
        if (!p) return false;
        v->data = p;
        v->cap = next;
    }
    wchar_t *copy = calloc(count + 1, sizeof(wchar_t));
    if (!copy) return false;
    if (count) wmemcpy(copy, text, count);
    v->data[v->count++] = (ViewLine){.text = copy, .color = color};
    return true;
}
static wchar_t *to_wide(const char *source) {
    if (!source) source = "";
    size_t len = mbstowcs(NULL, source, 0);
    if (len == (size_t)-1) {
        /* Malformed UTF-8 should never crash/redraw the terminal. */
        len = strlen(source);
        wchar_t *fallback = calloc(len + 1, sizeof(wchar_t));
        if (!fallback) return NULL;
        for (size_t i = 0; i < len; ++i) {
            unsigned char ch = (unsigned char)source[i];
            fallback[i] = ch >= 32 && ch < 127 ? (wchar_t)ch : L'?';
        }
        return fallback;
    }
    wchar_t *out = calloc(len + 1, sizeof(wchar_t));
    if (out) mbstowcs(out, source, len + 1);
    return out;
}
static int glyph_width(wchar_t ch) {
    int w = wcwidth(ch);
    return w < 0 ? 1 : w;
}
static void wrap_text(View *v, const char *text, int width, int color) {
    if (width <= 0) return;
    wchar_t *wide = to_wide(text);
    if (!wide) return;
    wchar_t *line = calloc((size_t)width + 2, sizeof(wchar_t));
    if (!line) { free(wide); return; }
    size_t used = 0;
    int columns = 0;
    for (size_t i = 0; wide[i]; ++i) {
        wchar_t ch = wide[i];
        if (ch == L'\r') continue;
        if (ch == L'\n') {
            view_line(v, line, used, color);
            used = 0;
            columns = 0;
            continue;
        }
        if (ch == L'\t') ch = L' ';
        if (iswcntrl(ch)) ch = L'?';
        int cw = glyph_width(ch);
        if (cw > width) { ch = L'?'; cw = 1; }
        if (columns + cw > width) {
            view_line(v, line, used, color);
            used = 0;
            columns = 0;
        }
        if (used >= (size_t)width + 1) {
            view_line(v, line, used, color);
            used = 0; columns = 0;
        }
        line[used++] = ch;
        columns += cw;
    }
    if (used || !v->count) view_line(v, line, used, color);
    free(line);
    free(wide);
}
static void build_view(View *v, const App *a, int width) {
    for (size_t i = 0; i < a->chat.count; ++i) {
        const AdamMessage *msg = &a->chat.items[i];
        if (v->count) view_line(v, L"", 0, C_DIM);
        wrap_text(v, msg->role == ADAM_ROLE_USER ? "YOU" : "ADAM", width,
                  msg->role == ADAM_ROLE_USER ? C_USER : C_ACCENT);
        if (msg->thinking && *msg->thinking && a->show_thinking) {
            wrap_text(v, "THINKING", width, C_REASON);
            wrap_text(v, msg->thinking, width, C_DIM);
        }
        if (msg->content && *msg->content) {
            wrap_text(v, msg->content, width, C_TEXT);
        } else if (msg->role == ADAM_ROLE_ASSISTANT && a->busy && i == a->chat.count - 1) {
            wrap_text(v, a->saw_error ? "Request failed" : "Generating ...", width, C_DIM);
        }
    }
}
static void line_at(int y, int x, int w, const char *text, int color) {
    if (w <= 0 || y < 0 || y >= LINES || x < 0 || x >= COLS) return;
    wchar_t *wide = to_wide(text);
    if (!wide) return;
    attron(COLOR_PAIR(color));
    mvaddnwstr(y, x, wide, w);
    attroff(COLOR_PAIR(color));
    free(wide);
}
static void fill_row(int y, int color) {
    attron(COLOR_PAIR(color));
    mvhline(y, 0, ' ', COLS);
    attroff(COLOR_PAIR(color));
}
static void separator(int y) {
    attron(COLOR_PAIR(C_DIM));
    mvhline(y, 0, ACS_HLINE, COLS);
    attroff(COLOR_PAIR(C_DIM));
}
static int cursor_visual_width(const wchar_t *s, size_t from, size_t to) {
    int width = 0;
    for (size_t i = from; i < to; ++i) width += glyph_width(s[i]);
    return width;
}
static void draw_sidebar(const App *a, int x, int y, int height, int width) {
    if (height <= 0 || width < 18) return;
    attron(COLOR_PAIR(C_DIM));
    mvvline(y, x, ACS_VLINE, height);
    attroff(COLOR_PAIR(C_DIM));
    int col = x + 2;
    int available = width - 3;
    line_at(y, col, available, "SESSION", C_ACCENT);
    line_at(y + 2, col, available, "Model", C_DIM);
    line_at(y + 3, col, available, a->model, C_TEXT);
    line_at(y + 5, col, available, "Backend", C_DIM);
    line_at(y + 6, col, available, "Ollama / local", C_TEXT);
    line_at(y + 8, col, available, "State", C_DIM);
    line_at(y + 9, col, available, a->busy ? "Generating..." : "Ready", a->busy ? C_ACCENT : C_GOOD);
    if (height > 18) {
        line_at(y + 11, col, available, "Workspace", C_DIM);
        char cwd[PATH_MAX];
        if (getcwd(cwd, sizeof(cwd))) {
            const char *name = strrchr(cwd, '/');
            line_at(y + 12, col, available, name && name[1] ? name + 1 : cwd, C_TEXT);
        }
        line_at(y + 14, col, available, "Reasoning", C_DIM);
        line_at(y + 15, col, available, a->show_thinking ? "Visible (Ctrl+T)" : "Hidden (Ctrl+T)", C_TEXT);
        line_at(y + 17, col, available, "Commands: /help", C_DIM);
    }
}
static void draw(App *a) {
    erase();
    int h = LINES, w = COLS;
    if (w < 42 || h < 13) {
        line_at(h / 2, 1, w - 2, "Resize terminal: at least 42 x 13", C_ERROR);
        refresh();
        return;
    }
    const bool sidebar = w >= 104;
    int content_w = sidebar ? w - 32 : w;
    int text_x = 3;
    int wrap_w = content_w - text_x - 2;
    int top = 3;
    int composer_top = h - 5;
    int available = composer_top - top;
    fill_row(0, C_ACCENT);
    attron(COLOR_PAIR(C_ACCENT) | A_BOLD);
    mvaddstr(0, 2, " ADAM AI ");
    attroff(COLOR_PAIR(C_ACCENT) | A_BOLD);
    line_at(1, 2, w - 4, "Local AI assistant  /  Adava Development", C_DIM);
    separator(2);
    View lines = {0};
    build_view(&lines, a, wrap_w);
    int max_scroll = lines.count > (size_t)available ? (int)lines.count - available : 0;
    if (a->scroll > max_scroll) a->scroll = max_scroll;
    int start = (int)lines.count - available - a->scroll;
    if (start < 0) start = 0;
    for (int row = 0; row < available; ++row) {
        size_t idx = (size_t)start + (size_t)row;
        if (idx >= lines.count) break;
        attron(COLOR_PAIR(lines.data[idx].color));
        mvaddnwstr(top + row, text_x, lines.data[idx].text, wrap_w);
        attroff(COLOR_PAIR(lines.data[idx].color));
    }
    free_view(&lines);
    if (sidebar) draw_sidebar(a, content_w, top, available, w - content_w);
    separator(composer_top);
    line_at(composer_top + 1, 2, content_w - 4, a->busy ? "Generating response ..." : "Message Adam AI  (Enter: send)", C_DIM);
    attron(COLOR_PAIR(C_ACCENT) | A_BOLD);
    mvaddstr(composer_top + 2, 2, "> ");
    attroff(COLOR_PAIR(C_ACCENT) | A_BOLD);
    int input_space = content_w - 7;
    size_t begin = 0;
    while (begin < a->cursor && cursor_visual_width(a->input, begin, a->cursor) >= input_space) ++begin;
    attron(COLOR_PAIR(C_TEXT));
    mvaddnwstr(composer_top + 2, 4, a->input + begin, input_space);
    attroff(COLOR_PAIR(C_TEXT));
    line_at(composer_top + 3, 2, content_w - 4,
            "Ctrl+T thinking  Ctrl+L new chat  Ctrl+C cancel  Ctrl+Q quit  PgUp/PgDn scroll", C_DIM);
    fill_row(h - 1, C_DIM);
    char footer[384];
    snprintf(footer, sizeof(footer), " %.100s | %s | %.200s%s", a->model,
             a->busy ? "STREAMING" : "READY", a->status,
             a->scroll ? " | scrolled up" : "");
    line_at(h - 1, 1, w - 3, footer, a->saw_error ? C_ERROR : C_DIM);
    int xcursor = 4 + cursor_visual_width(a->input, begin, a->cursor);
    if (xcursor >= content_w - 2) xcursor = content_w - 3;
    move(composer_top + 2, xcursor);
    refresh();
}

static void new_chat(App *a) {
    if (a->busy) { set_status(a, "Cancel generation first (Ctrl+C)"); return; }
    adam_chat_clear(&a->chat);
    a->scroll = 0;
    set_status(a, "New conversation");
}
static void command(App *a, const char *input) {
    if (!strcmp(input, "/quit") || !strcmp(input, "/exit")) { a->running = false; return; }
    if (!strcmp(input, "/clear") || !strcmp(input, "/new")) { new_chat(a); return; }
    if (!strcmp(input, "/thinking")) {
        a->show_thinking = !a->show_thinking;
        set_status(a, a->show_thinking ? "Thinking visible" : "Thinking hidden");
        return;
    }
    if (!strncmp(input, "/model ", 7) && input[7]) {
        if (a->busy) { set_status(a, "Wait until generation finishes"); return; }
        snprintf(a->model, sizeof(a->model), "%s", input + 7);
        new_chat(a);
        set_status(a, "Model changed (new conversation)");
        return;
    }
    if (!strcmp(input, "/help")) {
        adam_chat_add(&a->chat, ADAM_ROLE_ASSISTANT,
            "Adam AI TUI - Commands:\n"
            "/help - this help\n/clear or /new - new conversation\n"
            "/model NAME - change Ollama model\n/thinking - show/hide reasoning\n"
            "/quit - exit\n\n"
            "Enter: send  Ctrl+T: thinking  Ctrl+L: new session\n"
            "Ctrl+C: cancel request  Ctrl+Q: quit\n"
            "PgUp/PgDn: scroll  Home/End: edit input\n"
            "The agent has no filesystem or command execution tools yet.");
        a->scroll = 0;
        return;
    }
    set_status(a, "Unknown command; use /help");
}
static void submit(App *a) {
    if (!a->input_len) return;
    if (a->busy) { set_status(a, "Wait for the current response or Ctrl+C to cancel"); return; }
    size_t needed = wcstombs(NULL, a->input, 0);
    if (needed == (size_t)-1 || needed > INPUT_CAP * 4) {
        set_status(a, "Input has invalid characters"); return;
    }
    char *text = malloc(needed + 1);
    if (!text) { set_status(a, "Out of memory"); return; }
    wcstombs(text, a->input, needed + 1);
    a->input_len = a->cursor = 0;
    a->input[0] = 0;
    a->scroll = 0;
    a->saw_error = false;
    if (text[0] == '/') { command(a, text); free(text); return; }
    if (!adam_chat_add(&a->chat, ADAM_ROLE_USER, text)) {
        set_status(a, "Could not append message"); free(text); return;
    }
    free(text);
    /* The empty assistant message is intentionally NOT sent to Ollama. */
    if (!adam_chat_add(&a->chat, ADAM_ROLE_ASSISTANT, "")) {
        set_status(a, "Out of memory"); return;
    }
    if (!adam_ollama_start(&a->stream, &a->chat, a->model, a->endpoint, a->think_level)) {
        adam_text_append(&a->chat.items[a->chat.count - 1].content, "Could not launch model request.");
        a->saw_error = true;
        set_status(a, "Could not launch request");
        return;
    }
    a->busy = true;
    set_status(a, "Connecting to model...");
}
static void process_stream(App *a) {
    if (!a->busy) return;
    AdamStreamEvent *event;
    while ((event = adam_ollama_poll(&a->stream))) {
        AdamMessage *msg = &a->chat.items[a->chat.count - 1];
        switch (event->type) {
            case ADAM_STREAM_THINKING:
                adam_text_append(&msg->thinking, event->text);
                set_status(a, "Reasoning...");
                break;
            case ADAM_STREAM_TEXT:
                adam_text_append(&msg->content, event->text);
                set_status(a, "Writing response...");
                break;
            case ADAM_STREAM_ERROR:
                if (msg->content && *msg->content) adam_text_append(&msg->content, "\n");
                adam_text_append(&msg->content, "[Error] ");
                adam_text_append(&msg->content, event->text ? event->text : "Unknown network error");
                set_status(a, "Request failed");
                a->saw_error = true;
                break;
            case ADAM_STREAM_DONE:
                a->busy = false;
                if (!a->saw_error) set_status(a, "Response complete");
                break;
        }
        free(event->text);
        free(event);
        if (!a->busy) break;
    }
    if (!a->busy && a->stream.started) adam_ollama_stop(&a->stream);
}
static void cancel_stream(App *a) {
    if (!a->busy) return;
    adam_ollama_stop(&a->stream);
    a->busy = false;
    AdamMessage *msg = &a->chat.items[a->chat.count - 1];
    adam_text_append(&msg->content, "\n[Generation cancelled]");
    set_status(a, "Generation cancelled");
}
static void input_key(App *a, wint_t key, bool special) {
    if (special) {
        switch (key) {
            case KEY_LEFT: if (a->cursor) --a->cursor; break;
            case KEY_RIGHT: if (a->cursor < a->input_len) ++a->cursor; break;
            case KEY_HOME: a->cursor = 0; break;
            case KEY_END: a->cursor = a->input_len; break;
            case KEY_BACKSPACE:
                if (a->cursor) {
                    wmemmove(a->input + a->cursor - 1, a->input + a->cursor,
                             a->input_len - a->cursor + 1);
                    --a->cursor; --a->input_len;
                }
                break;
            case KEY_DC:
                if (a->cursor < a->input_len) {
                    wmemmove(a->input + a->cursor, a->input + a->cursor + 1,
                             a->input_len - a->cursor);
                    --a->input_len;
                }
                break;
            case KEY_PPAGE: a->scroll += (LINES > 16 ? LINES / 2 : 5); break;
            case KEY_NPAGE: a->scroll -= (LINES > 16 ? LINES / 2 : 5); if (a->scroll < 0) a->scroll = 0; break;
            case KEY_ENTER: submit(a); break;
            default: break;
        }
        return;
    }
    switch (key) {
        case 17: a->running = false; break; /* Ctrl+Q */
        case 3: if (a->busy) cancel_stream(a); else set_status(a, "Ctrl+Q to quit"); break;
        case 20: a->show_thinking = !a->show_thinking; break; /* Ctrl+T */
        case 12: new_chat(a); break; /* Ctrl+L */
        case 21: a->input_len = a->cursor = 0; a->input[0] = 0; break; /* Ctrl+U */
        case 1: a->cursor = 0; break; /* Ctrl+A */
        case 5: a->cursor = a->input_len; break; /* Ctrl+E */
        case 127:
        case 8:
            if (a->cursor) {
                wmemmove(a->input + a->cursor - 1, a->input + a->cursor,
                         a->input_len - a->cursor + 1);
                --a->cursor; --a->input_len;
            }
            break;
        case L'\n': case L'\r': submit(a); break;
        default:
            if (iswprint((wint_t)key) && a->input_len + 1 < INPUT_CAP) {
                wmemmove(a->input + a->cursor + 1, a->input + a->cursor,
                         a->input_len - a->cursor + 1);
                a->input[a->cursor++] = (wchar_t)key;
                ++a->input_len;
            }
            break;
    }
}
int adam_tui_run(void) {
    App app = {0};
    adam_chat_init(&app.chat);
    app.running = true;
    app.show_thinking = true;
    const char *model = getenv("ADAM_MODEL");
    snprintf(app.model, sizeof(app.model), "%s", model && *model ? model : "gpt-oss:20b");
    app.endpoint = getenv("ADAM_OLLAMA_URL");
    if (!app.endpoint || !*app.endpoint) app.endpoint = "http://127.0.0.1:11434";
    app.think_level = getenv("ADAM_THINK");
    if (!app.think_level || !*app.think_level) app.think_level = "medium";
    set_status(&app, "Type /help for commands");
    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
        fprintf(stderr, "Could not initialize libcurl\n"); return 1;
    }
    if (!initscr()) {
        fprintf(stderr, "Could not initialize ncursesw\n");
        curl_global_cleanup();
        return 1;
    }
    raw(); noecho(); keypad(stdscr, TRUE); timeout(60);
    if (has_colors()) {
        start_color(); use_default_colors();
        init_pair(C_TEXT, COLOR_WHITE, -1);
        init_pair(C_ACCENT, COLOR_CYAN, -1);
        init_pair(C_DIM, COLOR_BLUE, -1);
        init_pair(C_USER, COLOR_GREEN, -1);
        init_pair(C_REASON, COLOR_MAGENTA, -1);
        init_pair(C_GOOD, COLOR_GREEN, -1);
        init_pair(C_ERROR, COLOR_RED, -1);
    }
    while (app.running) {
        process_stream(&app);
        draw(&app);
        wint_t key;
        int result = get_wch(&key);
        if (result != ERR) input_key(&app, key, result == KEY_CODE_YES);
    }
    cancel_stream(&app);
    endwin();
    adam_chat_clear(&app.chat);
    curl_global_cleanup();
    return 0;
}
