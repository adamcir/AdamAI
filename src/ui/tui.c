/* AdamAI - GPL-3.0-only
 * Original ncursesw user interface with a compact coding-assistant layout.
 * Visual inspiration: OpenCode, Codex CLI, Claude Code. No code copied.
 */
#define _XOPEN_SOURCE 700
#define _XOPEN_SOURCE_EXTENDED 1
#include "adam/tui.h"
#include "adam/editor_layout.h"
#include "adam/chat.h"
#include "adam/ollama.h"
#include "adam/text_wrap.h"
#include <ncursesw/curses.h>
#include <curl/curl.h>
#include <limits.h>
#include <locale.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wchar.h>
#include <wctype.h>

#define INPUT_CAP 8192
#define MODEL_CAP 128
#define HISTORY_CAP 32
#define MAX_CHAT_WIDTH 94
#define SIDEBAR_WIDTH 29
#define MAX_EDITOR_ROWS 5

/* Pair IDs are constant. All UI colors are configurable through set_theme(). */
enum {
    C_NORMAL = 1, C_ACCENT, C_MUTED, C_USER, C_THINK,
    C_GOOD, C_ERROR, C_PANEL, C_FRAME, C_SELECT, C_WARNING, C_SOFT
};

typedef struct {
    wchar_t *text;
    int color;
    int indent;
    bool bold;
    bool panel;
} ViewLine;

typedef struct {
    ViewLine *data;
    size_t count, cap;
} View;

typedef struct {
    const char *name;
    const char *description;
    bool argument;
} Command;

static const Command commands[] = {
    {"/help", "Keyboard shortcuts and commands", false},
    {"/new", "Start a new conversation", false},
    {"/clear", "Clear conversation history", false},
    {"/model", "Choose an Ollama model", true},
    {"/thinking", "Toggle reasoning display", false},
    {"/sidebar", "Toggle workspace sidebar", false},
    {"/theme", "Switch dark / light appearance", false},
    {"/status", "Show current session settings", false},
    {"/quit", "Exit AdamAI", false}
};
#define COMMAND_COUNT (sizeof(commands) / sizeof(commands[0]))

typedef struct {
    AdamChat chat;
    AdamStream stream;
    wchar_t input[INPUT_CAP];
    size_t input_len, cursor;
    wchar_t *history[HISTORY_CAP];
    size_t history_count;
    int history_index; /* -1: current draft, 0: most recent history */
    wchar_t *draft;
    bool running, busy, show_thinking, saw_error;
    bool sidebar_open, palette_closed, help_open, light_theme;
    int command_index;
    int scroll; /* wrapped lines above bottom */
    char status[256];
    char model[MODEL_CAP];
    const char *endpoint, *think_level;
    unsigned spin;
    unsigned session;
} App;

static void set_status(App *a, const char *s) {
    snprintf(a->status, sizeof(a->status), "%s", s ? s : "");
}

static int clamp_int(int value, int lo, int hi) {
    return value < lo ? lo : value > hi ? hi : value;
}

static int glyph_width(wchar_t c) {
    int width = wcwidth(c);
    return width < 0 ? 1 : width;
}

/* A malformed UTF-8 byte is displayed as '?' rather than crashing the TUI. */
static wchar_t *to_wide(const char *s) {
    if (!s) s = "";
    size_t n = mbstowcs(NULL, s, 0);
    if (n == (size_t)-1) {
        n = strlen(s);
        wchar_t *result = calloc(n + 1, sizeof(*result));
        if (!result) return NULL;
        for (size_t i = 0; i < n; ++i) {
            unsigned char ch = (unsigned char)s[i];
            result[i] = ch >= 32 && ch < 127 ? (wchar_t)ch : L'?';
        }
        return result;
    }
    wchar_t *result = calloc(n + 1, sizeof(*result));
    if (result) mbstowcs(result, s, n + 1);
    return result;
}

static void free_view(View *v) {
    for (size_t i = 0; i < v->count; ++i) free(v->data[i].text);
    free(v->data);
    memset(v, 0, sizeof(*v));
}

static bool view_line(View *v, const wchar_t *s, size_t n, int color, int indent,
                      bool bold, bool panel) {
    if (v->count == v->cap) {
        size_t next = v->cap ? v->cap * 2 : 32;
        if (next < v->cap || next > SIZE_MAX / sizeof(*v->data)) return false;
        ViewLine *data = realloc(v->data, next * sizeof(*data));
        if (!data) return false;
        v->data = data;
        v->cap = next;
    }
    if (n > (SIZE_MAX / sizeof(wchar_t)) - 1) return false;
    wchar_t *copy = calloc(n + 1, sizeof(*copy));
    if (!copy) return false;
    if (n) wmemcpy(copy, s, n);
    v->data[v->count++] = (ViewLine){copy, color, indent, bold, panel};
    return true;
}

static void view_label(View *v, const char *s, int color, int indent,
                       bool bold, bool panel) {
    wchar_t *w = to_wide(s);
    if (!w) return;
    view_line(v, w, wcslen(w), color, indent, bold, panel);
    free(w);
}

static void view_gap(View *v) { view_line(v, L"", 0, C_NORMAL, 0, false, false); }

/* Wide-character-aware wrapping. Width measures terminal cells, not codepoints. */
typedef struct {
    View *view;
    int color, indent;
    bool panel;
} WrapView;

static bool view_wrap_emit(const wchar_t *line, size_t count, void *userdata) {
    WrapView *w = userdata;
    return view_line(w->view, line, count, w->color, w->indent, false, w->panel);
}

/* Normal text: word boundaries. Code: preserve exact spaces and fixed-width wrapping. */
static void view_wrap_mode(View *v, const char *source, int width, int color,
                           int indent, bool panel, AdamWrapMode mode) {
    if (width <= indent || !source) return;
    wchar_t *wide = to_wide(source);
    if (!wide) return;
    WrapView ctx = {v, color, indent, panel};
    (void)adam_wrap_lines(wide, width - indent, mode, view_wrap_emit, &ctx);
    free(wide);
}

static void view_wrap(View *v, const char *source, int width, int color,
                      int indent, bool panel) {
    view_wrap_mode(v, source, width, color, indent, panel, ADAM_WRAP_WORDS);
}

static void view_wrap_code(View *v, const char *source, int width, int color,
                           int indent, bool panel) {
    view_wrap_mode(v, source, width, color, indent, panel, ADAM_WRAP_VERBATIM);
}

/* Compact block-level Markdown presentation for common coding answers.
 * Rendering only; the conversation retains exact original bytes. */
static void view_markdown(View *v, const char *source, int width) {
    if (!source) return;
    bool code = false;
    const char *p = source;
    while (*p) {
        const char *end = strchr(p, '\n');
        size_t len = end ? (size_t)(end - p) : strlen(p);
        if (len > SIZE_MAX - 1) return;
        char *line = malloc(len + 1);
        if (!line) return;
        memcpy(line, p, len);
        line[len] = '\0';
        if (!strncmp(line, "```", 3)) {
            if (!code) {
                char label[96];
                snprintf(label, sizeof(label), "  CODE  %.*s", 64, line + 3);
                view_label(v, label, C_THINK, 0, true, true);
            } else {
                view_line(v, L"", 0, C_PANEL, 0, false, true);
            }
            code = !code;
        } else if (code) {
            if (*line) view_wrap_code(v, line, width - 2, C_PANEL, 2, true);
            else view_line(v, L"", 0, C_PANEL, 0, false, true);
        } else if (!*line) {
            view_gap(v);
        } else if (line[0] == '#' && line[1] == ' ') {
            view_label(v, line + 2, C_ACCENT, 2, true, false);
        } else if (line[0] == '#' && line[1] == '#' && line[2] == ' ') {
            view_label(v, line + 3, C_ACCENT, 2, true, false);
        } else if (line[0] == '-' && line[1] == ' ') {
            size_t n = strlen(line + 2) + 8;
            char *bullet = malloc(n);
            if (bullet) {
                snprintf(bullet, n, "• %s", line + 2);
                view_wrap(v, bullet, width - 2, C_NORMAL, 3, false);
                free(bullet);
            }
        } else if (line[0] == '>' && line[1] == ' ') {
            view_wrap(v, line + 2, width - 2, C_MUTED, 3, false);
        } else {
            view_wrap(v, line, width - 2, C_NORMAL, 2, false);
        }
        free(line);
        if (!end) break;
        p = end + 1;
    }
}

static void build_view(View *v, const App *a, int width) {
    for (size_t i = 0; i < a->chat.count; ++i) {
        const AdamMessage *m = &a->chat.items[i];
        view_gap(v);
        if (m->role == ADAM_ROLE_USER) {
            view_label(v, "YOU", C_USER, 2, true, true);
            view_wrap(v, m->content, width - 2, C_PANEL, 2, true);
            view_line(v, L"", 0, C_NORMAL, 0, false, true);
        } else {
            view_label(v, "◆  AdamAI", C_ACCENT, 0, true, false);
            if (m->thinking && *m->thinking) {
                view_label(v, a->show_thinking ? "  ▾  Thinking" :
                           "  ▸  Thinking  ·  Ctrl+T to expand", C_THINK, 0, false, false);
                if (a->show_thinking) {
                    view_wrap(v, m->thinking, width - 2, C_MUTED, 2, false);
                    view_gap(v);
                }
            }
            if (m->content && *m->content)
                view_markdown(v, m->content, width);
            else if (a->busy && i == a->chat.count - 1)
                view_label(v, m->thinking && *m->thinking ? "  Reasoning..." :
                           "  Waiting for model...", C_MUTED, 0, false, false);
        }
    }
    if (v->count) view_gap(v);
}

static void attrs(int color, bool bold) {
    attrset(COLOR_PAIR(color) | (bold ? A_BOLD : A_NORMAL));
}

/* Draw at most width TERMINAL CELLS. */
static void wline_at(int y, int x, int width, const wchar_t *s, int color, bool bold) {
    if (!s || width <= 0 || y < 0 || y >= LINES || x < 0 || x >= COLS) return;
    int max_cells = width < COLS - x ? width : COLS - x;
    int cells = 0;
    size_t n = 0;
    while (s[n]) {
        int cw = glyph_width(s[n]);
        if (cells + cw > max_cells) break;
        cells += cw;
        ++n;
    }
    attrs(color, bold);
    if (n) mvaddnwstr(y, x, s, (int)n);
    attrs(C_NORMAL, false);
}
static void line_at(int y, int x, int width, const char *s, int color, bool bold) {
    wchar_t *w = to_wide(s);
    if (!w) return;
    wline_at(y, x, width, w, color, bold);
    free(w);
}
static void fill_span(int y, int x, int width, int color) {
    if (y < 0 || y >= LINES || width <= 0 || x < 0 || x >= COLS) return;
    attrs(color, false);
    mvhline(y, x, ' ', width < COLS - x ? width : COLS - x);
    attrs(C_NORMAL, false);
}
static void horizontal(int y, int x, int width, int color) {
    if (y < 0 || y >= LINES || width <= 0) return;
    attrs(color, false);
    mvhline(y, x, ACS_HLINE, width);
    attrs(C_NORMAL, false);
}
static void frame(int y, int x, int h, int w, int border, int fill) {
    if (w < 2 || h < 2 || y < 0 || x < 0 || x + w > COLS || y + h > LINES) return;
    for (int r = y + 1; r < y + h - 1; ++r) fill_span(r, x + 1, w - 2, fill);
    horizontal(y, x + 1, w - 2, border);
    horizontal(y + h - 1, x + 1, w - 2, border);
    attrs(border, false);
    mvaddch(y, x, ACS_ULCORNER);
    mvaddch(y, x + w - 1, ACS_URCORNER);
    mvaddch(y + h - 1, x, ACS_LLCORNER);
    mvaddch(y + h - 1, x + w - 1, ACS_LRCORNER);
    for (int r = y + 1; r < y + h - 1; ++r) {
        mvaddch(r, x, ACS_VLINE);
        mvaddch(r, x + w - 1, ACS_VLINE);
    }
    attrs(C_NORMAL, false);
}

static void set_theme(bool light) {
    if (!has_colors()) return;
    start_color();
    use_default_colors();

    /* AdamAI Classic: light green + orange, designed for dark and light TUI.
     * 256-color terminals preserve the softer palette; basic terminals get
     * green/yellow equivalents. */
    if (COLORS >= 256 && COLOR_PAIRS > C_SOFT) {
        int bg = light ? 255 : 234;
        init_pair(C_NORMAL,  light ? 235 : 252, bg);
        init_pair(C_ACCENT,  light ?  28 : 150, bg);  /* light green / forest green */
        init_pair(C_MUTED,   light ? 243 : 245, bg);
        init_pair(C_USER,    light ? 166 : 214, light ? 254 : 236); /* orange */
        init_pair(C_THINK,   light ? 166 : 215, bg);  /* softer orange */
        init_pair(C_GOOD,    light ?  28 : 156, bg);  /* active / ready */
        init_pair(C_ERROR,   light ? 160 : 203, bg);
        init_pair(C_PANEL,   light ? 235 : 252, light ? 254 : 236);
        init_pair(C_FRAME,   light ? 108 :  65, bg);  /* muted green border */
        init_pair(C_SELECT,  light ? 235 : 234, light ? 215 : 150);
        init_pair(C_WARNING, light ? 166 : 214, bg);
        init_pair(C_SOFT,    light ?  28 : 150, light ? 253 : 236);
        bkgd(COLOR_PAIR(C_NORMAL));
    } else {
        init_pair(C_NORMAL, COLOR_WHITE, -1);
        init_pair(C_ACCENT, COLOR_GREEN, -1);
        init_pair(C_MUTED, COLOR_WHITE, -1);
        init_pair(C_USER, COLOR_YELLOW, -1);
        init_pair(C_THINK, COLOR_YELLOW, -1);
        init_pair(C_GOOD, COLOR_GREEN, -1);
        init_pair(C_ERROR, COLOR_RED, -1);
        init_pair(C_PANEL, COLOR_WHITE, -1);
        init_pair(C_FRAME, COLOR_GREEN, -1);
        init_pair(C_SELECT, COLOR_BLACK, COLOR_GREEN);
        init_pair(C_WARNING, COLOR_YELLOW, -1);
        init_pair(C_SOFT, COLOR_GREEN, -1);
        bkgd(COLOR_PAIR(C_NORMAL));
    }
}

static void cwd_short(char *out, size_t size) {
    char cwd[PATH_MAX];
    if (!getcwd(cwd, sizeof(cwd))) { snprintf(out, size, "unknown workspace"); return; }
    const char *home = getenv("HOME");
    if (home && *home && strncmp(cwd, home, strlen(home)) == 0 &&
        (cwd[strlen(home)] == '/' || !cwd[strlen(home)]))
        snprintf(out, size, "~%s", cwd + strlen(home));
    else snprintf(out, size, "%s", cwd);
}

static void draw_sidebar(const App *a, int left, int top, int height) {
    if (height < 10) return;
    int inner = left + 2, width = COLS - inner - 2;
    attrs(C_FRAME, false);
    mvvline(top, left, ACS_VLINE, height);
    attrs(C_NORMAL, false);
    line_at(top, inner, width, "PROJECT", C_ACCENT, true);
    char cwd[PATH_MAX];
    cwd_short(cwd, sizeof(cwd));
    const char *project = strrchr(cwd, '/');
    line_at(top + 2, inner, width, project && project[1] ? project + 1 : cwd,
            C_NORMAL, true);
    line_at(top + 4, inner, width, "MODEL", C_THINK, true);
    line_at(top + 5, inner, width, a->model, C_NORMAL, false);
    line_at(top + 7, inner, width, "PROVIDER", C_MUTED, true);
    line_at(top + 8, inner, width, "Ollama  /  local", C_NORMAL, false);
    if (height > 14) {
        line_at(top + 10, inner, width, "SESSION", C_MUTED, true);
        char info[96];
        snprintf(info, sizeof(info), "Chat %02u  ·  %zu messages", a->session, a->chat.count);
        line_at(top + 11, inner, width, info, C_NORMAL, false);
        line_at(top + 13, inner, width, "REASONING", C_THINK, true);
        line_at(top + 14, inner, width, a->show_thinking ? "Visible" : "Collapsed", C_NORMAL, false);
    }
    if (height > 20) {
        line_at(top + 17, inner, width, "WORKFLOW", C_MUTED, true);
        line_at(top + 18, inner, width, "Chat mode", C_GOOD, false);
        line_at(top + 19, inner, width, "Agent tools: coming soon", C_MUTED, false);
    }
    if (height > 26) {
        line_at(top + height - 5, inner, width, "Ctrl+P  commands", C_MUTED, false);
        line_at(top + height - 4, inner, width, "Ctrl+T  thinking", C_MUTED, false);
        line_at(top + height - 3, inner, width, "Ctrl+B  sidebar", C_MUTED, false);
    }
}

static void draw_welcome(const App *a, int x, int y, int width, int height) {
    int mid = y + height / 2;
    if (height < 10) {
        line_at(y + 1, x + 2, width - 4, "◆  AdamAI", C_ACCENT, true);
        line_at(y + 3, x + 2, width - 4, "Ask anything.  /help  ·  Ctrl+P", C_MUTED, false);
        return;
    }
    const char *title = "◆  AdamAI";
    const char *sub = "Your local development assistant";
    int title_w = 9, sub_w = 32;
    line_at(mid - 4, x + (width - title_w) / 2, width, title, C_ACCENT, true);
    line_at(mid - 2, x + (width - sub_w) / 2, width, sub, C_NORMAL, false);
    line_at(mid, x + 2, width - 4, "Ask a question, explore code, or plan your next change.", C_MUTED, false);
    char hint[256];
    snprintf(hint, sizeof(hint), "Model: %.100s   ·   /help   ·   Ctrl+P commands", a->model);
    line_at(mid + 2, x + 2, width - 4, hint, C_MUTED, false);
}

static int command_matches(const App *a, int *out, int max_count) {
    if (a->palette_closed || a->input_len == 0 || a->input[0] != L'/') return 0;
    size_t prefix = 0;
    for (size_t i = 1; i < a->input_len; ++i) {
        if (a->input[i] == L' ' || a->input[i] == L'\n') return 0;
        ++prefix;
    }
    int count = 0;
    for (size_t i = 0; i < COMMAND_COUNT; ++i) {
        wchar_t *name = to_wide(commands[i].name + 1);
        if (!name) continue;
        bool match = prefix <= wcslen(name) &&
                     wcsncmp(a->input + 1, name, prefix) == 0;
        free(name);
        if (match && count < max_count) out[count++] = (int)i;
    }
    return count;
}

static void draw_palette(App *a, int x, int composer_top, int width) {
    int found[COMMAND_COUNT];
    int n = command_matches(a, found, (int)COMMAND_COUNT);
    if (n == 0) return;
    int show = n < 6 ? n : 6;
    a->command_index = clamp_int(a->command_index, 0, n - 1);
    int selected = a->command_index;
    int offset = selected >= show ? selected - show + 1 : 0;
    int y = composer_top - show - 3;
    if (y < 3) return;
    frame(y, x, show + 3, width, C_FRAME, C_PANEL);
    line_at(y, x + 2, width - 4, " COMMANDS  ·  ↑↓ select  Enter run  Esc close", C_ACCENT, true);
    for (int i = 0; i < show; ++i) {
        int which = found[offset + i];
        int row = y + 1 + i;
        if (offset + i == selected) fill_span(row, x + 1, width - 2, C_SELECT);
        int tone = offset + i == selected ? C_SELECT : C_PANEL;
        line_at(row, x + 2, 15, commands[which].name, tone, true);
        line_at(row, x + 18, width - 21, commands[which].description, tone, false);
    }
}

static void draw_help(int x, int width, int top, int available) {
    if (available < 13 || width < 45) return;
    int h = available < 21 ? available : 21;
    int y = top + (available - h) / 2;
    int w = width < 68 ? width : 68;
    int left = x + (width - w) / 2;
    frame(y, left, h, w, C_ACCENT, C_PANEL);
    line_at(y + 1, left + 3, w - 6, "AdamAI  /  SHORTCUTS", C_ACCENT, true);
    const char *rows[] = {
        "Enter         Send message or run command",
        "Ctrl+O        Insert newline in composer",
        "Ctrl+P        Open slash-command palette",
        "Ctrl+B        Show / hide workspace sidebar",
        "Ctrl+T        Expand / collapse reasoning",
        "Ctrl+L        Start new conversation",
        "Ctrl+C        Cancel streaming response",
        "Ctrl+Q        Exit AdamAI",
        "PgUp/PgDn     Scroll through conversation",
        "Up/Down       Command selection / prompt history",
        "Ctrl+A/E/U    Start / end / clear input",
        "F1            Show or hide shortcuts",
        "",
        "No agent tools yet: commands cannot change files."
    };
    size_t count = sizeof(rows) / sizeof(rows[0]);
    for (size_t i = 0; i < count && (int)i < h - 4; ++i)
        line_at(y + 3 + (int)i, left + 3, w - 6, rows[i], i == count - 1 ? C_WARNING : C_PANEL, false);
    line_at(y + h - 2, left + 3, w - 6, "ESC / F1 to close", C_MUTED, false);
}

/* The editor and cursor use the exact same word-aware layout.  Without a
 * shared map, the visible text can wrap differently from the caret. */
static void draw_editor(const App *a, const AdamEditorCell *cells,
                        int x, int top, int width, int rows,
                        int *cy, int *cx) {
    const int text_x = x + 4;
    const int text_w = width - 7;
    if (text_w < 1) return;

    const AdamEditorCell cursor = cells[a->cursor];
    int scroll_rows = cursor.row >= rows ? cursor.row - rows + 1 : 0;
    *cy = top + 1 + cursor.row - scroll_rows;
    *cx = text_x + cursor.col;

    line_at(top + 1, x + 2, 2, "❯", C_ACCENT, true);
    if (!a->input_len) {
        line_at(top + 1, text_x, text_w,
                "Ask AdamAI, or type / for commands...", C_MUTED, false);
        return;
    }

    attrs(C_NORMAL, false);
    for (size_t i = 0; i < a->input_len; ++i) {
        AdamEditorCell cell = cells[i];
        if (!cell.visible || cell.row < scroll_rows || cell.row >= scroll_rows + rows)
            continue;
        wchar_t ch = a->input[i];
        if (ch == L'\t') ch = L' ';
        if (iswcntrl(ch) || glyph_width(ch) > text_w) ch = L'?';
        if (cell.col + glyph_width(ch) <= text_w)
            mvaddnwstr(top + 1 + cell.row - scroll_rows, text_x + cell.col, &ch, 1);
    }
    attrs(C_NORMAL, false);
}

static void draw_composer(const App *a, const AdamEditorCell *cells,
                          int x, int y, int width, int rows, int *cy, int *cx) {
    /* A low-contrast, transparent-feeling panel instead of the large solid
     * gray MESSAGE block.  Nothing is drawn on top of the editor rows. */
    frame(y, x, rows + 2, width, a->busy ? C_ACCENT : C_FRAME, C_NORMAL);
    draw_editor(a, cells, x, y, width, rows, cy, cx);
    int hint_y = y + rows + 2;
    line_at(hint_y, x + 1, width - 2,
            a->busy ? "● Generating  ·  Ctrl+C stop  ·  Ctrl+T thinking" :
                      "Enter send  ·  Ctrl+O newline  ·  Ctrl+P commands", 
            a->busy ? C_ACCENT : C_MUTED, false);
}

static void draw(App *a) {
    erase();
    int h = LINES, w = COLS;
    if (h < 18 || w < 52) {
        line_at(h / 2, 1, w - 2, "Please resize terminal to at least 52 × 18", C_ERROR, true);
        refresh();
        return;
    }
    bool sidebar = a->sidebar_open && w >= 108;
    int content_right = sidebar ? w - SIDEBAR_WIDTH : w;
    int cw = content_right - 8;
    if (cw > MAX_CHAT_WIDTH) cw = MAX_CHAT_WIDTH;
    int cx = (content_right - cw) / 2;
    int top = 3;
    AdamEditorCell editor_cells[INPUT_CAP + 1];
    int editor_total_rows = adam_editor_layout(a->input, a->input_len,
                                               cw - 7, editor_cells);
    int edit_rows = clamp_int(editor_total_rows, 1, MAX_EDITOR_ROWS);
    int composer_top = h - edit_rows - 4;
    int available = composer_top - top;
    fill_span(0, 0, w, C_NORMAL);
    line_at(0, 2, content_right - 4, "◆  AdamAI", C_ACCENT, true);
    char right[192];
    snprintf(right, sizeof(right), " %s   %s ", a->model, a->busy ? "● Generating" : "● Local");
    int n = (int)strlen(right);
    if (n < content_right - 23)
        line_at(0, content_right - n - 2, n, right, a->busy ? C_WARNING : C_GOOD, false);
    char cwd[PATH_MAX + 32];
    cwd_short(cwd, sizeof(cwd));
    char work[PATH_MAX + 64];
    snprintf(work, sizeof(work), "WORKSPACE  /  %.200s", cwd);
    line_at(1, cx, cw, work, C_MUTED, false);
    horizontal(2, 0, content_right, C_FRAME);
    if (!a->chat.count) {
        draw_welcome(a, cx, top, cw, available);
    } else {
        View lines = {0};
        build_view(&lines, a, cw);
        int max_scroll = lines.count > (size_t)available ? (int)lines.count - available : 0;
        a->scroll = clamp_int(a->scroll, 0, max_scroll);
        int start = (int)lines.count - available - a->scroll;
        if (start < 0) start = 0;
        for (int row = 0; row < available; ++row) {
            size_t idx = (size_t)start + (size_t)row;
            if (idx >= lines.count) break;
            const ViewLine *line = &lines.data[idx];
            int y = top + row;
            if (line->panel) {
                fill_span(y, cx, cw, C_PANEL);
                attrs(C_ACCENT, false);
                mvaddch(y, cx, ACS_VLINE);
                attrs(C_NORMAL, false);
            }
            wline_at(y, cx + line->indent, cw - line->indent - 1,
                     line->text, line->color, line->bold);
        }
        if (a->scroll > 0) {
            char label[32];
            snprintf(label, sizeof(label), "↑ %d lines", a->scroll);
            line_at(top, cx + cw - 14, 14, label, C_WARNING, false);
        }
        free_view(&lines);
    }
    if (sidebar) draw_sidebar(a, content_right, top, h - top - 1);
    int cy = composer_top + 1, cx_cursor = cx + 4;
    draw_composer(a, editor_cells, cx, composer_top, cw, edit_rows, &cy, &cx_cursor);
    if (!a->help_open) draw_palette(a, cx, composer_top, cw);
    if (a->help_open) draw_help(cx, cw, 2, composer_top + 4);
    fill_span(h - 1, 0, w, C_NORMAL);
    char footer[384];
    snprintf(footer, sizeof(footer), " Chat %02u  ·  %zu messages  ·  %s  ·  %s", a->session,
             a->chat.count, a->busy ? "Streaming" : "Ready", a->status);
    line_at(h - 1, 2, w - 4, footer, a->saw_error ? C_ERROR : C_MUTED, false);
    if (a->help_open) { curs_set(0); }
    else {
        curs_set(1);
        move(clamp_int(cy, 0, h - 1), clamp_int(cx_cursor, 0, w - 1));
    }
    refresh();
}

static void input_set(App *a, const wchar_t *s) {
    if (!s) s = L"";
    size_t n = wcslen(s);
    if (n >= INPUT_CAP) n = INPUT_CAP - 1;
    wmemcpy(a->input, s, n);
    a->input[n] = 0;
    a->input_len = a->cursor = n;
    a->command_index = 0;
    a->palette_closed = false;
}

static void history_add(App *a, const wchar_t *s) {
    wchar_t *copy = wcsdup(s);
    if (!copy) return;
    if (a->history_count == HISTORY_CAP) {
        free(a->history[0]);
        memmove(a->history, a->history + 1, (HISTORY_CAP - 1) * sizeof(*a->history));
        --a->history_count;
    }
    a->history[a->history_count++] = copy;
    free(a->draft); a->draft = NULL;
    a->history_index = -1;
}

static void history_move(App *a, int direction) {
    if (!a->history_count) return;
    if (a->history_index == -1) {
        free(a->draft);
        a->draft = wcsdup(a->input);
    }
    int next = a->history_index + direction;
    if (next < -1 || next >= (int)a->history_count) return;
    a->history_index = next;
    input_set(a, next == -1 ? a->draft : a->history[a->history_count - 1 - (size_t)next]);
}

static void new_chat(App *a) {
    if (a->busy) { set_status(a, "Cancel generation first (Ctrl+C)"); return; }
    adam_chat_clear(&a->chat);
    ++a->session;
    a->scroll = 0;
    a->saw_error = false;
    set_status(a, "New conversation");
}

static void show_help_message(App *a) {
    a->help_open = !a->help_open;
    set_status(a, a->help_open ? "Shortcuts (Esc to close)" : "Ready");
}

static void command(App *a, const char *input) {
    if (!strcmp(input, "/quit") || !strcmp(input, "/exit")) { a->running = false; return; }
    if (!strcmp(input, "/new") || !strcmp(input, "/clear")) { new_chat(a); return; }
    if (!strcmp(input, "/help")) { show_help_message(a); return; }
    if (!strcmp(input, "/thinking")) {
        a->show_thinking = !a->show_thinking;
        set_status(a, a->show_thinking ? "Reasoning expanded" : "Reasoning collapsed");
        return;
    }
    if (!strcmp(input, "/sidebar")) {
        a->sidebar_open = !a->sidebar_open;
        set_status(a, a->sidebar_open ? "Sidebar visible on wide terminals" : "Sidebar hidden");
        return;
    }
    if (!strcmp(input, "/theme")) {
        a->light_theme = !a->light_theme;
        set_theme(a->light_theme);
        set_status(a, a->light_theme ? "Light theme" : "Dark theme");
        return;
    }
    if (!strcmp(input, "/status")) {
        char s[256];
        snprintf(s, sizeof(s), "Ollama / %.100s / %.100s", a->model, a->busy ? "streaming" : "idle");
        set_status(a, s);
        return;
    }
    if (!strcmp(input, "/model")) {
        input_set(a, L"/model ");
        set_status(a, "Type an Ollama model name after /model");
        return;
    }
    if (!strncmp(input, "/model ", 7) && input[7]) {
        if (a->busy) { set_status(a, "Cancel generation before switching model"); return; }
        snprintf(a->model, sizeof(a->model), "%s", input + 7);
        new_chat(a);
        set_status(a, "Model changed (new conversation)");
        return;
    }
    set_status(a, "Unknown command; press Ctrl+P to browse");
}

static void submit(App *a) {
    if (!a->input_len) return;
    if (a->busy) { set_status(a, "Ctrl+C to stop before sending another prompt"); return; }
    size_t size = wcstombs(NULL, a->input, 0);
    if (size == (size_t)-1 || size > INPUT_CAP * 4) {
        set_status(a, "Invalid characters in composer"); return;
    }
    char *text = malloc(size + 1);
    if (!text) { set_status(a, "Out of memory"); return; }
    wcstombs(text, a->input, size + 1);
    bool is_command = text[0] == '/';
    if (!is_command) history_add(a, a->input);
    input_set(a, L"");
    a->scroll = 0;
    a->saw_error = false;
    if (is_command) { command(a, text); free(text); return; }
    if (!adam_chat_add(&a->chat, ADAM_ROLE_USER, text)) {
        set_status(a, "Could not append message"); free(text); return;
    }
    free(text);
    if (!adam_chat_add(&a->chat, ADAM_ROLE_ASSISTANT, "")) {
        set_status(a, "Out of memory"); return;
    }
    if (!adam_ollama_start(&a->stream, &a->chat, a->model, a->endpoint, a->think_level)) {
        adam_text_append(&a->chat.items[a->chat.count - 1].content, "Could not launch model request.");
        set_status(a, "Could not launch request");
        a->saw_error = true;
        return;
    }
    a->busy = true;
    set_status(a, "Connecting to model...");
}

static void process_stream(App *a) {
    if (!a->busy) return;
    AdamStreamEvent *event;
    while ((event = adam_ollama_poll(&a->stream))) {
        AdamMessage *m = &a->chat.items[a->chat.count - 1];
        switch (event->type) {
            case ADAM_STREAM_THINKING:
                adam_text_append(&m->thinking, event->text);
                set_status(a, "Reasoning...");
                break;
            case ADAM_STREAM_TEXT:
                adam_text_append(&m->content, event->text);
                set_status(a, "Writing response...");
                break;
            case ADAM_STREAM_ERROR:
                if (m->content && *m->content) adam_text_append(&m->content, "\n");
                adam_text_append(&m->content, "[Error] ");
                adam_text_append(&m->content, event->text ? event->text : "Unknown network error");
                a->saw_error = true;
                set_status(a, "Request failed");
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
    AdamMessage *m = &a->chat.items[a->chat.count - 1];
    adam_text_append(&m->content, "\n[Generation cancelled]");
    set_status(a, "Generation cancelled");
}

static void insert_char(App *a, wchar_t ch) {
    if (a->input_len + 1 >= INPUT_CAP) return;
    wmemmove(a->input + a->cursor + 1, a->input + a->cursor,
             a->input_len - a->cursor + 1);
    a->input[a->cursor++] = ch;
    ++a->input_len;
    a->palette_closed = false;
    a->command_index = 0;
    a->history_index = -1;
}
static void backspace(App *a) {
    if (!a->cursor) return;
    wmemmove(a->input + a->cursor - 1, a->input + a->cursor,
             a->input_len - a->cursor + 1);
    --a->cursor; --a->input_len;
    a->command_index = 0;
    a->palette_closed = false;
}

static void palette_enter(App *a) {
    int matches[COMMAND_COUNT];
    int count = command_matches(a, matches, (int)COMMAND_COUNT);
    if (count) {
        int index = matches[clamp_int(a->command_index, 0, count - 1)];
        wchar_t *name = to_wide(commands[index].name);
        if (!name) return;
        input_set(a, name);
        free(name);
        if (commands[index].argument) { insert_char(a, L' '); a->palette_closed = true; }
        else submit(a);
    } else submit(a);
}

static void input_key(App *a, wint_t key, bool special) {
    if (a->help_open) {
        if (key == 27 || (special && key == KEY_F(1)) || key == 13) a->help_open = false;
        return;
    }
    int found[COMMAND_COUNT];
    int matches = command_matches(a, found, (int)COMMAND_COUNT);
    if (special) {
        switch (key) {
            case KEY_LEFT: if (a->cursor) --a->cursor; break;
            case KEY_RIGHT: if (a->cursor < a->input_len) ++a->cursor; break;
            case KEY_HOME: a->cursor = 0; break;
            case KEY_END: a->cursor = a->input_len; break;
            case KEY_BACKSPACE: backspace(a); break;
            case KEY_DC:
                if (a->cursor < a->input_len) {
                    wmemmove(a->input + a->cursor, a->input + a->cursor + 1,
                             a->input_len - a->cursor);
                    --a->input_len;
                }
                break;
            case KEY_UP:
                if (matches) a->command_index = (a->command_index + matches - 1) % matches;
                else history_move(a, +1);
                break;
            case KEY_DOWN:
                if (matches) a->command_index = (a->command_index + 1) % matches;
                else history_move(a, -1);
                break;
            case KEY_PPAGE:
                /* Scroll up by exactly one rendered conversation line. */
                if (a->scroll < INT_MAX) ++a->scroll;
                break;
            case KEY_NPAGE:
                /* Scroll down by exactly one rendered conversation line. */
                if (a->scroll > 0) --a->scroll;
                break;
            case KEY_ENTER: if (matches) palette_enter(a); else submit(a); break;
            case KEY_F(1): show_help_message(a); break;
            case KEY_RESIZE: break;
#ifdef KEY_MOUSE
            case KEY_MOUSE: {
                MEVENT event;
                if (getmouse(&event) == OK) {
#ifdef BUTTON4_PRESSED
                    if (event.bstate & BUTTON4_PRESSED) a->scroll += 4;
#endif
#ifdef BUTTON5_PRESSED
                    if (event.bstate & BUTTON5_PRESSED) {
                        a->scroll -= 4; if (a->scroll < 0) a->scroll = 0;
                    }
#endif
                }
                break;
            }
#endif
            default: break;
        }
        return;
    }
    switch (key) {
        case 17: a->running = false; break; /* Ctrl+Q */
        case 3: if (a->busy) cancel_stream(a); else set_status(a, "Ctrl+Q to quit"); break;
        case 20: a->show_thinking = !a->show_thinking; break; /* Ctrl+T */
        case 2: a->sidebar_open = !a->sidebar_open; break; /* Ctrl+B */
        case 16: /* Ctrl+P: command palette */
            if (!a->input_len) input_set(a, L"/");
            else if (a->input[0] == L'/') a->palette_closed = !a->palette_closed;
            else set_status(a, "Clear input to use command palette");
            break;
        case 12: new_chat(a); break; /* Ctrl+L */
        case 15: insert_char(a, L'\n'); break; /* Ctrl+O */
        case 21: input_set(a, L""); break; /* Ctrl+U */
        case 1: a->cursor = 0; break;
        case 5: a->cursor = a->input_len; break;
        case 27: /* Esc dismisses suggestions and restores an empty composer. */
            if (a->input_len == 1 && a->input[0] == L'/') input_set(a, L"");
            a->palette_closed = true;
            break;
        case 127: case 8: backspace(a); break;
        case L'\n': case L'\r': if (matches) palette_enter(a); else submit(a); break;
        default:
            if (iswprint((wint_t)key)) insert_char(a, (wchar_t)key);
            break;
    }
}

int adam_tui_run(void) {
    App app = {0};
    adam_chat_init(&app.chat);
    app.running = true;
    app.show_thinking = false;
    app.sidebar_open = true;
    app.session = 1;
    app.history_index = -1;
    const char *model = getenv("ADAM_MODEL");
    snprintf(app.model, sizeof(app.model), "%s", model && *model ? model : "gpt-oss:20b");
    app.endpoint = getenv("ADAM_OLLAMA_URL");
    if (!app.endpoint || !*app.endpoint) app.endpoint = "http://127.0.0.1:11434";
    app.think_level = getenv("ADAM_THINK");
    if (!app.think_level || !*app.think_level) app.think_level = "medium";
    const char *theme = getenv("ADAM_TUI_THEME");
    app.light_theme = theme && !strcmp(theme, "light");
    set_status(&app, "Type /help or press Ctrl+P");
    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
        fprintf(stderr, "Could not initialize libcurl\n"); return 1;
    }
    if (!initscr()) {
        fprintf(stderr, "Could not initialize ncursesw\n");
        curl_global_cleanup();
        return 1;
    }
    raw(); noecho(); keypad(stdscr, TRUE); timeout(60);
    set_theme(app.light_theme);
#ifdef BUTTON4_PRESSED
    mousemask(BUTTON4_PRESSED | BUTTON5_PRESSED, NULL);
#endif
    while (app.running) {
        process_stream(&app);
        draw(&app);
        wint_t key;
        int result = get_wch(&key);
        if (result != ERR) input_key(&app, key, result == KEY_CODE_YES);
    }
    cancel_stream(&app);
    endwin();
    for (size_t i = 0; i < app.history_count; ++i) free(app.history[i]);
    free(app.draft);
    adam_chat_clear(&app.chat);
    curl_global_cleanup();
    return 0;
}
