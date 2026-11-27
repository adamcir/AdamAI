/* AdamAI - GPL-3.0-only */
#define _XOPEN_SOURCE 700
#include "adam/text_selection.h"
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <stdint.h>

static int cell_width(wchar_t ch) { int n = wcwidth(ch); return n < 1 ? 1 : n; }
static int point_cmp(AdamTextPoint a, AdamTextPoint b) {
    if (a.row != b.row) return a.row < b.row ? -1 : 1;
    return a.col == b.col ? 0 : (a.col < b.col ? -1 : 1);
}
char *adam_text_selection_copy(const AdamTextLine *lines, size_t count,
                               AdamTextPoint first, AdamTextPoint last,
                               size_t max_bytes) {
    if (!lines || !count || !max_bytes || point_cmp(first,last)==0) return NULL;
    if (point_cmp(first,last)>0) { AdamTextPoint t=first;first=last;last=t; }
    if (last.row < 0 || first.row >= (int)count) return NULL;
    if (first.row < 0) first.row=0;
    if (last.row >= (int)count) last.row=(int)count-1;
    /* Four bytes per Unicode codepoint; cap both conversions and allocation. */
    size_t max_wide = max_bytes / 4;
    if (!max_wide) max_wide = 1;
    wchar_t *out=calloc(max_wide+1,sizeof(*out));
    if (!out) return NULL;
    size_t used=0;
    for (int r=first.row; r<=last.row && used<max_wide; ++r) {
        if (r>first.row) out[used++]=L'\n';
        const AdamTextLine *line=&lines[r];
        int col=line->indent;
        const wchar_t *s=line->text ? line->text : L"";
        for (size_t j=0;s[j] && used<max_wide;j++) {
            int cw=cell_width(s[j]);
            int stop=col>INT_MAX-cw ? INT_MAX : col+cw;
            if ((r>first.row || stop>first.col) &&
                (r<last.row || col<last.col)) out[used++]=s[j];
            col=stop;
        }
    }
    if (!used) {free(out);return NULL;}
    out[used]=0;
    size_t bytes=wcstombs(NULL,out,0);
    if (bytes==(size_t)-1 || bytes>max_bytes) { free(out);return NULL; }
    char *result=malloc(bytes+1);
    if(result) wcstombs(result,out,bytes+1);
    free(out);
    return result;
}
bool adam_prompt_replace(wchar_t *buffer, size_t *length, size_t *cursor,
                         size_t capacity, size_t start, size_t end,
                         const wchar_t *insert, size_t count) {
    if (!buffer || !length || !cursor || !capacity || !insert ||
        start>end || end>*length || *length>=capacity ||
        count>capacity-1-(*length-(end-start))) return false;
    size_t tail=*length-end;
    wmemmove(buffer+start+count,buffer+end,tail+1);
    if(count) wmemcpy(buffer+start,insert,count);
    *length=*length-(end-start)+count;
    *cursor=start+count;
    return true;
}
