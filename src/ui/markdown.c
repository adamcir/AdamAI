/* AdamAI - GPL-3.0-only */
#define _XOPEN_SOURCE 700
#include "adam/markdown.h"
#include <wchar.h>
#include <wctype.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <limits.h>
#include <ctype.h>
#include <stdint.h>

#define MD_TABLE_COLS 8
#define MD_MAX_SEGMENTS 512

typedef struct {
    wchar_t *chars;
    uint8_t *styles;
    size_t len, cap;
} Styled;
typedef struct { size_t start, len; } Segment;
typedef struct {
    Segment ranges[MD_MAX_SEGMENTS];
    size_t count;
} Segments;

typedef struct {
    AdamMarkdownEmit emit;
    void *userdata;
    int columns;
} Render;

static bool reserve(Styled *s, size_t count) {
    if(count > SIZE_MAX-1 || count+1 > SIZE_MAX / sizeof(wchar_t))return false;
    if(count + 1 <= s->cap)return true;
    size_t next=s->cap?s->cap:32;
    while(next<count+1){ if(next>SIZE_MAX/2)return false; next*=2; }
    wchar_t *w=malloc(next*sizeof(*w));
    uint8_t *bits=malloc(next);
    if(!w||!bits){free(w);free(bits);return false;}
    if(s->len){memcpy(w,s->chars,s->len*sizeof(*w));memcpy(bits,s->styles,s->len);}
    w[s->len]=0;
    free(s->chars);free(s->styles);
    s->chars=w;s->styles=bits;s->cap=next;
    return true;
}
static bool append(Styled *s, wchar_t ch, uint8_t style){
    if(!reserve(s,s->len+1))return false;
    if(ch==L'\x1b'||(ch<32&&ch!=L'\t'))ch=L' ';
    s->chars[s->len]=ch;s->styles[s->len]=style;
    s->chars[++s->len]=0;
    return true;
}
static bool append_n(Styled *dest, const Styled *src,size_t off,size_t count){
    if(!src||off>src->len)return false;
    if(count>src->len-off)count=src->len-off;
    if(!reserve(dest,dest->len+count))return false;
    if(count){memcpy(dest->chars+dest->len,src->chars+off,count*sizeof(wchar_t));
              memcpy(dest->styles+dest->len,src->styles+off,count);dest->len+=count;}
    dest->chars[dest->len]=0;return true;
}
static bool append_lit(Styled *dest,const wchar_t *text,uint8_t style){
    for(size_t i=0;text[i];++i)if(!append(dest,text[i],style))return false;
    return true;
}
static void free_styled(Styled *s){free(s->chars);free(s->styles);memset(s,0,sizeof(*s));}
static int cells(wchar_t w){int n=wcwidth(w);return n<1?1:n;}
static int span_cells(const Styled *s,size_t start,size_t n){
    int total=0;for(size_t i=start;i<start+n&&i<s->len;i++){
        int c=cells(s->chars[i]);if(total>INT_MAX-c)return INT_MAX;total+=c;
    }return total;
}
static wchar_t *wide(const char *src){
    size_t n=mbstowcs(NULL,src,0);
    if(n==(size_t)-1){n=strlen(src);wchar_t *w=calloc(n+1,sizeof(*w));
        if(w)for(size_t i=0;i<n;i++)w[i]=((unsigned char)src[i]<128 && (unsigned char)src[i]>=32)?(wchar_t)src[i]:L'?';
        return w;
    }
    if(n>SIZE_MAX/sizeof(wchar_t)-1)return NULL;
    wchar_t *w=calloc(n+1,sizeof(*w));if(w)mbstowcs(w,src,n+1);return w;
}
static bool emit_row(Render *r,const Styled *s,size_t off,size_t len,
                     int indent,AdamMarkdownKind kind){
    AdamMarkdownLine line={.text=s&&s->chars?s->chars+off:L"",
        .styles=s&&s->styles?s->styles+off:NULL,.length=len,
        .indent=indent,.kind=kind};
    /* The substring at off need not end in a NUL; callback must obey length. */
    return r->emit(&line,r->userdata);
}
static bool blank(Render *r){return emit_row(r,NULL,0,0,0,ADAM_MD_BLANK);}

/* Split by terminal-cell width, break at spaces when possible, and preserve
 * original per-character styling across wrap boundaries. */
static void split_segments(const Styled *s,int width,bool verbatim,Segments *out){
    if(width<1)width=1;
    size_t at=0;
    if(!s->len){out->ranges[out->count++]=(Segment){0,0};return;}
    while(at<s->len && out->count<MD_MAX_SEGMENTS){
        size_t start=at,last_space=SIZE_MAX,end=at;
        int columns=0;
        while(end<s->len){
            wchar_t ch=s->chars[end];
            int cw=cells(ch);
            if(columns+cw>width && end>at)break;
            if(columns+cw>width && end==at){end++;break;}
            if(iswspace(ch))last_space=end;
            columns+=cw;end++;
        }
        if(!verbatim && end<s->len && last_space!=SIZE_MAX && last_space>at){
            end=last_space;
        }
        size_t finish=end;
        if(!verbatim){
            while(finish>start&&iswspace(s->chars[finish-1]))finish--;
            while(end<s->len&&iswspace(s->chars[end]))end++;
        }
        out->ranges[out->count++]=(Segment){start,finish-start};
        at=end;
    }
}
static bool wrap(Render *r,const Styled *s,int indent,AdamMarkdownKind kind,bool verbatim){
    Segments parts={0};
    int width=r->columns-indent;
    if(width<1)width=1;
    split_segments(s,width,verbatim,&parts);
    for(size_t i=0;i<parts.count;i++){
        Segment seg=parts.ranges[i];
        if(!emit_row(r,s,seg.start,seg.len,indent,kind))return false;
    }
    return true;
}

static bool starts(const wchar_t *s,size_t offset,const wchar_t *token){
    return wcsncmp(s+offset,token,wcslen(token))==0;
}
static bool word_url(const wchar_t *s,size_t i){
    return starts(s,i,L"https://")||starts(s,i,L"http://");
}
static bool inline_text(const char *input,Styled *out){
    wchar_t *src=wide(input);if(!src)return false;
    size_t n=wcslen(src);
    uint8_t active=0;
    bool ok=true;
    for(size_t i=0;i<n&&ok;){
        wchar_t c=src[i];
        if(c==L'\\'&&i+1<n){ok=append(out,src[i+1],active);i+=2;continue;}
        if(c==L'`'){
            size_t run=1;while(i+run<n && src[i+run]==L'`')run++;
            size_t close=i+run;
            while(close<n){if(src[close]==L'`'&&wcsncmp(src+i,src+close,run)==0)break;close++;}
            if(close<n){
                for(size_t j=i+run;j<close&&ok;j++)ok=append(out,src[j],active|ADAM_MD_CODE);
                i=close+run;continue;
            }
        }
        if(c==L'!'&&i+1<n&&src[i+1]==L'['){
            const wchar_t *close=wcschr(src+i+2,L']');
            if(close&&close[1]==L'('){const wchar_t *stop=wcschr(close+2,L')');
                if(stop){ok=append_lit(out,L"[Image: ",active);
                    for(size_t j=i+2;j<(size_t)(close-src)&&ok;j++)ok=append(out,src[j],active);
                    if(ok)ok=append(out,L']',active);
                    i=(size_t)(stop-src)+1;continue;}}
        }
        if(c==L'['){
            const wchar_t *close=wcschr(src+i+1,L']');
            if(close&&close[1]==L'('){const wchar_t *stop=wcschr(close+2,L')');
                if(stop){for(size_t j=i+1;j<(size_t)(close-src)&&ok;j++)
                        ok=append(out,src[j],active|ADAM_MD_LINK);
                    i=(size_t)(stop-src)+1;continue;}}
        }
        if(c==L'<'&&i+1<n&&word_url(src,i+1)){
            const wchar_t *end=wcschr(src+i+1,L'>');
            if(end){for(size_t j=i+1;j<(size_t)(end-src)&&ok;j++)ok=append(out,src[j],active|ADAM_MD_LINK);
                i=(size_t)(end-src)+1;continue;}
        }
        if(word_url(src,i)){
            size_t j=i;while(j<n&&!iswspace(src[j])&&src[j]!=L'<'&&src[j]!=L'>')j++;
            for(size_t k=i;k<j&&ok;k++)ok=append(out,src[k],active|ADAM_MD_LINK);
            i=j;continue;
        }
        uint8_t flag=0;size_t run=1;
        if(starts(src,i,L"**")||starts(src,i,L"__")){flag=ADAM_MD_BOLD;run=2;}
        else if(starts(src,i,L"~~")){flag=ADAM_MD_STRIKE;run=2;}
        else if(c==L'*'||c==L'_'){flag=ADAM_MD_ITALIC;run=1;}
        if(flag){
            wchar_t delimiter[3]={c,c,0};delimiter[run]=0;
            const wchar_t *next=wcsstr(src+i+run,delimiter);
            if((active&flag)||next){
                active^=flag;i+=run;continue;
            }
        }
        ok=append(out,c,active);i++;
    }
    free(src);return ok;
}

static char *trim_dup(const char *s,size_t n){
    while(n&&isspace((unsigned char)*s)){s++;n--;}
    while(n&&isspace((unsigned char)s[n-1]))n--;
    char *result=malloc(n+1);if(result){memcpy(result,s,n);result[n]=0;}return result;
}
static char **table_cells(const char *line,size_t *count){
    char **cells_out=calloc(MD_TABLE_COLS,sizeof(*cells_out));
    if(!cells_out)return NULL;
    const char *p=line;while(isspace((unsigned char)*p))p++;
    if(*p=='|')p++;
    while(*p&&*count<MD_TABLE_COLS){
        const char *start=p;bool escaped=false;
        while(*p){if(*p=='|'&&!escaped)break;
            if(*p=='\\'&&!escaped)escaped=true;else escaped=false;
            p++;
        }
        cells_out[*count]=trim_dup(start,(size_t)(p-start));
        if(!cells_out[*count])goto bad;
        (*count)++;
        if(*p!='|')break;
        p++;
        if(!*p||strspn(p," \t")==strlen(p))break;
    }
    return cells_out;
bad:
    for(size_t i=0;i<*count;i++)free(cells_out[i]);
    free(cells_out);
    return NULL;
}
static void free_cells(char **cells_out,size_t count){
    if(!cells_out)return;
    for(size_t i=0;i<count;i++)free(cells_out[i]);
    free(cells_out);
}
static bool table_separator(const char *line,size_t expected){
    size_t n=0;char **cells_out=table_cells(line,&n);
    if(!cells_out)return false;
    bool valid=n==expected&&n>0;
    for(size_t i=0;i<n&&valid;i++){
        size_t dashes=0;
        for(const char *p=cells_out[i];*p;p++){
            if(*p=='-')dashes++;
            else if(*p!=':')valid=false;
        }
        if(dashes<3)valid=false;
    }
    free_cells(cells_out,n);return valid;
}
static bool is_table_header(char **lines,size_t count,size_t i){
    if(i+1>=count||!strchr(lines[i],'|')||!strchr(lines[i+1],'-'))return false;
    size_t n=0;char **head=table_cells(lines[i],&n);
    if(!head)return false;
    bool good=n>=2&&table_separator(lines[i+1],n);
    free_cells(head,n);return good;
}
static bool line_table_row(const char *line){return strchr(line,'|')!=NULL;}
static int between(int x,int lo,int hi){return x<lo?lo:x>hi?hi:x;}

/* Responsive table columns. At >= 50 cells use a real aligned grid; otherwise
 * use stacked key/value rows to avoid unreadable, one-letter-wide columns. */
static bool render_table(Render *r,char **lines,size_t end,size_t *next){
    size_t header_count=0;char **headers=table_cells(lines[*next],&header_count);
    if(!headers)return false;
    size_t i=*next+2;
    int avail=r->columns-2;
    bool stacked=(avail<50 || header_count>5 || avail<(int)header_count*9);
    int widths[MD_TABLE_COLS]={0};
    if(!stacked){
        int preferred[MD_TABLE_COLS]={0}, sum=0;
        for(size_t c=0;c<header_count;c++){
            Styled t={0};if(!inline_text(headers[c],&t)){free_cells(headers,header_count);return false;}
            preferred[c]=between(span_cells(&t,0,t.len)+3,9,38);
            free_styled(&t);
        }
        for(size_t row=i;row<end&&row<i+6&&line_table_row(lines[row]);row++){
            size_t n=0;char **data=table_cells(lines[row],&n);
            if(!data)continue;
            for(size_t c=0;c<n&&c<header_count;c++){
                Styled t={0};if(inline_text(data[c],&t)){
                    int cells_n=between(span_cells(&t,0,t.len)+2,9,38);
                    if(cells_n>preferred[c])preferred[c]=cells_n;
                }free_styled(&t);
            }free_cells(data,n);
        }
        for(size_t c=0;c<header_count;c++)sum+=preferred[c];
        int usable=avail-(int)(3*(header_count-1));
        int remaining=usable;
        for(size_t c=0;c<header_count;c++){
            int left=(int)(header_count-c-1);
            int val=c==header_count-1?remaining:
                between((usable*preferred[c])/(sum?sum:1),8,remaining-left*8);
            widths[c]=val;remaining-=val;
        }
    }
    if(stacked){
        for(;i<end&&line_table_row(lines[i]);i++){
            size_t n=0;char **values=table_cells(lines[i],&n);
            if(!values)break;
            for(size_t col=0;col<n&&col<header_count;col++){
                Styled text={0};bool ok=inline_text(headers[col],&text);
                if(ok)ok=append_lit(&text,L": ",ADAM_MD_BOLD);
                if(ok)ok=inline_text(values[col],&text);
                if(ok)ok=wrap(r,&text,3,ADAM_MD_TABLE,false);
                free_styled(&text);
                if(!ok){free_cells(values,n);free_cells(headers,header_count);return false;}
            }
            free_cells(values,n);
            if(!blank(r)){free_cells(headers,header_count);return false;}
        }
    }else{
        for(size_t row=*next;row<end;row=(row==*next?*next+2:row+1)){
            if(row>*next && !line_table_row(lines[row]))break;
            size_t n=0;char **values=table_cells(lines[row],&n);
            if(!values)break;
            Styled parsed[MD_TABLE_COLS]={0};Segments segments[MD_TABLE_COLS]={0};
            size_t lines_count=1;bool ok=true;
            for(size_t c=0;c<header_count&&ok;c++){
                if(c<n)ok=inline_text(values[c],&parsed[c]);
                split_segments(&parsed[c],widths[c],false,&segments[c]);
                if(segments[c].count>lines_count)lines_count=segments[c].count;
            }
            for(size_t visual=0;visual<lines_count&&ok;visual++){
                Styled combined={0};
                for(size_t c=0;c<header_count&&ok;c++){
                    size_t start=0,len=0;
                    if(visual<segments[c].count){
                        start=segments[c].ranges[visual].start;
                        len=segments[c].ranges[visual].len;
                    }
                    ok=append_n(&combined,&parsed[c],start,len);
                    int pad=widths[c]-span_cells(&parsed[c],start,len);
                    for(int j=0;j<pad&&ok;j++)ok=append(&combined,L' ',0);
                    if(c+1<header_count&&ok)ok=append_lit(&combined,L" │ ",0);
                }
                if(ok)ok=emit_row(r,&combined,0,combined.len,2,
                                   row==*next?ADAM_MD_TABLE_HEADER:ADAM_MD_TABLE);
                free_styled(&combined);
            }
            for(size_t c=0;c<header_count;c++)free_styled(&parsed[c]);
            free_cells(values,n);
            if(!ok){free_cells(headers,header_count);return false;}
            if(row==*next){
                Styled rule={0};
                for(size_t c=0;c<header_count;c++){
                    for(int j=0;j<widths[c];j++)if(!append(&rule,L'─',0))break;
                    if(c+1<header_count)append_lit(&rule,L"─┼─",0);
                }
                bool ruleok=emit_row(r,&rule,0,rule.len,2,ADAM_MD_RULE);
                free_styled(&rule);
                if(!ruleok){free_cells(headers,header_count);return false;}
            }
            i=row+1;
        }
    }
    free_cells(headers,header_count);*next=i;return true;
}

static bool render_line(Render *r,const char *src,bool *code){
    const char *p=src;while(*p==' '&&p-src<3)p++;
    if(!strncmp(p,"```",3)||!strncmp(p,"~~~",3)){
        if(*code){*code=false;return blank(r);}
        *code=true;
        Styled text={0};bool ok=append_lit(&text,L"  CODE",ADAM_MD_BOLD);
        const char *language=p+3;
        if(*language){ok=ok&&append_lit(&text,L" · ",0)&&inline_text(language,&text);}
        if(ok)ok=wrap(r,&text,0,ADAM_MD_CODE_TITLE,true);
        free_styled(&text);return ok;
    }
    if(*code){
        Styled text={0};wchar_t *w=wide(src);
        if(!w)return false;
        bool ok=append_lit(&text,L"  ",0);
        for(size_t i=0;w[i]&&ok;i++)ok=append(&text,w[i],ADAM_MD_CODE);
        if(ok)ok=wrap(r,&text,0,ADAM_MD_CODE_LINE,true);
        free(w);free_styled(&text);return ok;
    }
    if(!*p||strspn(p," \t")==strlen(p))return blank(r);
    int heading=0;while(p[heading]=='#'&&heading<6)heading++;
    if(heading&&p[heading]==' '){
        Styled text={0};bool ok=inline_text(p+heading+1,&text);
        if(ok)ok=wrap(r,&text,2,ADAM_MD_HEADING,false);
        free_styled(&text);return ok;
    }
    if(!strcmp(p,"---")||!strcmp(p,"***")||!strcmp(p,"___")){
        Styled rule={0};bool ok=true;
        for(int i=0;i<between(r->columns-3,1,120)&&ok;i++)ok=append(&rule,L'─',0);
        if(ok)ok=wrap(r,&rule,2,ADAM_MD_RULE,true);
        free_styled(&rule);return ok;
    }
    if(*p=='>'&&(p[1]==' '||p[1]==0)){
        Styled text={0};bool ok=append_lit(&text,L"│ ",0);
        if(ok)ok=inline_text(p+(p[1]==' '?2:1),&text);
        if(ok)ok=wrap(r,&text,2,ADAM_MD_QUOTE,false);
        free_styled(&text);return ok;
    }
    const char *body=NULL;
    wchar_t marker[24]={0};
    if((p[0]=='-'||p[0]=='*'||p[0]=='+')&&p[1]==' '){body=p+2;wcscpy(marker,L"• ");}
    else if(isdigit((unsigned char)*p)){
        const char *q=p;while(isdigit((unsigned char)*q)&&q-p<12)q++;
        if((*q=='.'||*q==')')&&q[1]==' '){
            size_t length=(size_t)(q-p)+2;
            for(size_t k=0;k<length&&k<20;k++)marker[k]=(unsigned char)p[k];
            body=q+2;
        }
    }
    if(body){
        Styled text={0};bool ok=append_lit(&text,marker,ADAM_MD_BOLD);
        if(ok)ok=inline_text(body,&text);
        if(ok)ok=wrap(r,&text,2,ADAM_MD_LIST,false);
        free_styled(&text);return ok;
    }
    Styled text={0};bool ok=inline_text(src,&text);
    if(ok)ok=wrap(r,&text,2,ADAM_MD_PARAGRAPH,false);
    free_styled(&text);return ok;
}

bool adam_markdown_render(const char *markdown,int columns,
                          AdamMarkdownEmit emit,void *userdata){
    if(!markdown||!emit||columns<1)return false;
    if(columns>10000)columns=10000;
    Render r={emit,userdata,columns};
    /* Split without strtok so consecutive blank lines remain meaningful. */
    size_t lines_count=1;
    for(const char *p=markdown;*p;p++)if(*p=='\n')lines_count++;
    if(lines_count>SIZE_MAX/sizeof(char *))return false;
    char **lines=calloc(lines_count,sizeof(*lines));if(!lines)return false;
    size_t at=0;const char *begin=markdown;
    bool ok=true;
    for(const char *p=markdown;;p++){
        if(*p!='\n'&&*p!=0)continue;
        size_t length=(size_t)(p-begin);
        if(length&&begin[length-1]=='\r')length--;
        lines[at]=malloc(length+1);
        if(lines[at]){memcpy(lines[at],begin,length);lines[at][length]=0;}
        if(!lines[at]){ok=false;break;}
        at++;
        if(!*p)break;
        begin=p+1;
    }
    bool code=false;
    for(size_t i=0;i<at&&ok;){
        if(!code&&is_table_header(lines,at,i)){
            size_t next=i;
            ok=render_table(&r,lines,at,&next);
            i=next>i?next:i+1;
        }else{
            ok=render_line(&r,lines[i],&code);i++;
        }
    }
    for(size_t i=0;i<at;i++)free(lines[i]);
    free(lines);
    return ok;
}
