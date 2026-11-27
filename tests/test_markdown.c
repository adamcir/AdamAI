/* AdamAI - GPL-3.0-only */
#define _XOPEN_SOURCE 700
#include "adam/markdown.h"
#include <assert.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <stdint.h>

typedef struct {
    wchar_t text[150][256];
    uint8_t style[150][256];
    AdamMarkdownKind kind[150];
    size_t count;
    int limit;
} Results;
static bool collect(const AdamMarkdownLine *line,void *userdata){
    Results *r=userdata;
    assert(r->count<150 && line->length<256);
    assert(line->indent>=0 && line->indent<r->limit);
    size_t n=line->length;
    memcpy(r->text[r->count],line->text,n*sizeof(wchar_t));
    r->text[r->count][n]=0;
    if(line->styles)memcpy(r->style[r->count],line->styles,n);
    int columns=line->indent;
    for(size_t i=0;i<n;i++){
        int cw=wcwidth(line->text[i]);columns+=cw<1?1:cw;
    }
    if(columns>r->limit){
        fwprintf(stderr,L"Overflow %d/%d: %ls\n",columns,r->limit,r->text[r->count]);abort();
    }
    r->kind[r->count++]=line->kind;
    return true;
}
static bool has(Results *r,const wchar_t *part){
    for(size_t i=0;i<r->count;i++)if(wcsstr(r->text[i],part))return true;
    return false;
}
static int lines(Results *r,AdamMarkdownKind kind){
    int c=0;for(size_t i=0;i<r->count;i++)if(r->kind[i]==kind)c++;return c;
}
int main(void){
    if (!setlocale(LC_ALL, "C.UTF-8")) assert(setlocale(LC_ALL, ""));
    Results r={.limit=88};
    const char *input=
       "# Výsledek vyhledávání\n\n"
       "Ano, adresa **Višňová 398** existuje v *obci* `Stěžery`!\n"
       "**Důkazy:**\n\n"
       "| Zdroj | Odkaz | Co potvrzuje |\n"
       "|---|---|---|\n"
       "| Mapy.com | [Mapa](https://mapy.com/cs/?source=stre&id=123) | Ulice Višňová |\n"
       "| Finance | <https://rejstriky.finance.cz/adresa-stezery-visnova-398-0-71023755> | číslo 398 |\n\n"
       "> Citace s **důrazem**\n"
       "- První položka\n"
       "2. Druhá položka\n"
       "---\n"
       "```c\n"
       "  int main(void) {\n"
       "    return 0;\n"
       "  }\n"
       "```\n"
       "Escaped \\*literally\\* and ~~removed~~.\n";
    assert(adam_markdown_render(input,r.limit,collect,&r));
    assert(has(&r,L"Višňová 398"));
    assert(!has(&r,L"**Důkazy:**"));
    assert(has(&r,L"Důkazy:"));
    assert(lines(&r,ADAM_MD_HEADING)>=1);
    assert(lines(&r,ADAM_MD_TABLE_HEADER)>=1);
    assert(lines(&r,ADAM_MD_TABLE)>=2);
    assert(lines(&r,ADAM_MD_CODE_LINE)>=3);
    assert(lines(&r,ADAM_MD_CODE_TITLE)==1);
    assert(lines(&r,ADAM_MD_LIST)==2);
    assert(has(&r,L"Escaped *literally*"));
    int strong=0,link=0,code=0,italic=0;
    for(size_t i=0;i<r.count;i++){
        size_t n=wcslen(r.text[i]);
        for(size_t j=0;j<n;j++){
           strong+=(r.style[i][j]&ADAM_MD_BOLD)!=0;
           link+=(r.style[i][j]&ADAM_MD_LINK)!=0;
           code+=(r.style[i][j]&ADAM_MD_CODE)!=0;
           italic+=(r.style[i][j]&ADAM_MD_ITALIC)!=0;
        }
    }
    assert(strong>6&&link>2&&code>3&&italic>2);
    Results small={.limit=38};
    assert(adam_markdown_render(input,small.limit,collect,&small));
    assert(lines(&small,ADAM_MD_TABLE)>=3);
    assert(has(&small,L"Zdroj:"));
    assert(has(&small,L"Co potvrzuje:"));
    Results escaping={.limit=70};
    assert(adam_markdown_render("Raw & text and `**literal**`",escaping.limit,collect,&escaping));
    assert(has(&escaping,L"**literal**"));
    Results wrap={.limit=24};
    assert(adam_markdown_render("**Příliš žluťoučký kůň úpěl**",wrap.limit,collect,&wrap));
    assert(wrap.count>=2);
    puts("Markdown terminal parser: inline, headings, tables, narrow, links, Unicode, code OK");
    return 0;
}
