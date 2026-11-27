/* AdamAI - GPL-3.0-only
 * Terminal-cell-aware Markdown formatting, independent of ncurses.
 */
#ifndef ADAM_MARKDOWN_H
#define ADAM_MARKDOWN_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <wchar.h>

enum {
    ADAM_MD_BOLD   = 1u << 0,
    ADAM_MD_ITALIC = 1u << 1,
    ADAM_MD_CODE   = 1u << 2,
    ADAM_MD_LINK   = 1u << 3,
    ADAM_MD_STRIKE = 1u << 4
};

typedef enum {
    ADAM_MD_PARAGRAPH, ADAM_MD_HEADING, ADAM_MD_LIST,
    ADAM_MD_QUOTE, ADAM_MD_CODE_LINE, ADAM_MD_CODE_TITLE,
    ADAM_MD_TABLE, ADAM_MD_TABLE_HEADER, ADAM_MD_RULE, ADAM_MD_BLANK
} AdamMarkdownKind;

typedef struct {
    const wchar_t *text;     /* NUL-terminated; valid only during callback */
    const uint8_t *styles;   /* one style mask per wchar_t, may be NULL */
    size_t length;
    int indent;              /* terminal cell offset */
    AdamMarkdownKind kind;
} AdamMarkdownLine;

typedef bool (*AdamMarkdownEmit)(const AdamMarkdownLine *line, void *userdata);

/* Renders a bounded-width Markdown document through an emit callback.
 * Handles inline emphasis, code, links, fences, headings, lists, quotes,
 * separators and responsive GitHub-flavored Markdown pipe tables.
 * Never executes HTML/terminal sequences. Original chat content is unchanged.
 */
bool adam_markdown_render(const char *markdown, int columns,
                          AdamMarkdownEmit emit, void *userdata);
#endif
