/* AdamAI - GPL-3.0-only */
#define _XOPEN_SOURCE 700
#include "adam/text_selection.h"
#include <assert.h>
#include <locale.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
int main(void){
    setlocale(LC_ALL,"C.UTF-8");
    AdamTextLine lines[]={{L"Hello Adam",2},{L"Příliš 界",2},{L"next line",0}};
    char *copy=adam_text_selection_copy(lines,3,(AdamTextPoint){0,8},(AdamTextPoint){1,8},4096);
    assert(copy && strcmp(copy,"Adam\nPříliš")==0);free(copy);
    copy=adam_text_selection_copy(lines,3,(AdamTextPoint){1,8},(AdamTextPoint){0,8},4096);
    assert(copy && strcmp(copy,"Adam\nPříliš")==0);free(copy);
    assert(!adam_text_selection_copy(lines,3,(AdamTextPoint){0,2},(AdamTextPoint){0,2},4096));
    copy=adam_text_selection_copy(lines,3,(AdamTextPoint){1,8},(AdamTextPoint){1,11},4096);
    assert(copy && strcmp(copy," 界")==0);free(copy);
    wchar_t buffer[32]=L"foo world";size_t len=wcslen(buffer),caret=4;
    assert(adam_prompt_replace(buffer,&len,&caret,32,4,9,L"čau\nOK",6));
    assert(!wcscmp(buffer,L"foo čau\nOK") && caret==10 && len==10);
    assert(!adam_prompt_replace(buffer,&len,&caret,32,0,len,L"abcdefghijklmnopqrstuvwxyz123456",32));
    assert(!wcscmp(buffer,L"foo čau\nOK"));
    assert(adam_prompt_replace(buffer,&len,&caret,32,0,len,L"",0));
    assert(len==0 && caret==0 && buffer[0]==0);
    return 0;
}
