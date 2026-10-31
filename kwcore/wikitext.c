#include "wikitext.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char *p;
    size_t len, cap;
} Out;

static void put(Out *o, char c)
{
    if (o->len + 1 < o->cap)
        o->p[o->len++] = c;
}

static void puts_n(Out *o, const char *s, size_t n)
{
    for (size_t i = 0; i < n; i++)
        put(o, s[i]);
}

static int starts(const char *s, const char *prefix)
{
    return strncmp(s, prefix, strlen(prefix)) == 0;
}

static int starts_ci(const char *s, const char *prefix)
{
    for (; *prefix; s++, prefix++)
        if (tolower((unsigned char)*s) != tolower((unsigned char)*prefix))
            return 0;
    return 1;
}

/*
 * Pass 1: drops constructs that can span lines: comments, templates (nested),
 * <ref> footnotes and __MAGIC__ words. Writes into `w`, returns its length.
 */
static size_t strip_blocks(const char *src, size_t n, char *w)
{
    size_t o = 0;
    int braces = 0;
    for (size_t i = 0; i < n;) {
        const char *s = src + i;
        if (starts(s, "<!--")) {
            const char *end = strstr(s + 4, "-->");
            i = end ? (size_t)(end - src) + 3 : n;
        } else if (starts(s, "{{")) {
            braces++;
            i += 2;
        } else if (braces && starts(s, "}}")) {
            braces--;
            i += 2;
        } else if (braces) {
            i++;
        } else if (starts_ci(s, "<ref")) {
            const char *close = strchr(s, '>');
            if (close && close[-1] == '/') {
                i = (size_t)(close - src) + 1;
            } else {
                const char *end = strstr(s, "</ref>");
                i = end ? (size_t)(end - src) + 6 : n;
            }
        } else if (starts(s, "__") && isupper((unsigned char)s[2])) {
            const char *end = strstr(s + 2, "__");
            i = end ? (size_t)(end - src) + 2 : i + 2;
        } else {
            w[o++] = src[i++];
        }
    }
    w[o] = '\0';
    return o;
}

/* Copies UTF-8 at *s as something the P4's fonts can show. Advances *s. */
static void put_utf8(Out *o, const unsigned char **s)
{
    const unsigned char *p = *s;
    if (p[0] < 0x80) {
        put(o, (char)p[0]);
        *s += 1;
        return;
    }
    int len = p[0] >= 0xF0 ? 4 : p[0] >= 0xE0 ? 3 : 2;
    for (int i = 1; i < len; i++)
        if (!p[i]) {
            *s += i;
            return;
        }
    unsigned cp = len == 2 ? ((p[0] & 0x1Fu) << 6) | (p[1] & 0x3Fu)
                : len == 3 ? ((p[0] & 0x0Fu) << 12) | ((p[1] & 0x3Fu) << 6) | (p[2] & 0x3Fu)
                           : 0;
    switch (cp) {
    case 0x00B0: put(o, (char)0xC2); put(o, (char)0xB0); break; /* degree sign */
    case 0x00A0: put(o, ' '); break;
    case 0x00D7: put(o, 'x'); break;
    case 0x2013: case 0x2014: put(o, '-'); break;
    case 0x2018: case 0x2019: put(o, '\''); break;
    case 0x201C: case 0x201D: put(o, '"'); break;
    case 0x2026: puts_n(o, "...", 3); break;
    default: break; /* no glyph: drop */
    }
    *s += len;
}

static const struct {
    const char *name;
    char c;
} ENTITIES[] = { { "&nbsp;", ' ' }, { "&amp;", '&' }, { "&lt;", '<' }, { "&gt;", '>' }, { "&quot;", '"' }, { "&#39;", '\'' } };

/* Renders one line's inline markup: links, bold/italics, tags, entities. */
static void render_inline(Out *o, const char *s)
{
    while (*s) {
        if (starts(s, "[[")) {
            const char *end = strstr(s, "]]");
            if (!end)
                break;
            const char *inner = s + 2;
            if (!(starts_ci(inner, "Category:") || starts_ci(inner, "File:") || starts_ci(inner, "Image:"))) {
                const char *bar = memchr(inner, '|', (size_t)(end - inner));
                const char *text = bar ? bar + 1 : inner;
                for (const unsigned char *t = (const unsigned char *)text; t < (const unsigned char *)end;)
                    put_utf8(o, &t);
            }
            s = end + 2;
        } else if (s[0] == '[' && (starts(s + 1, "http://") || starts(s + 1, "https://"))) {
            const char *end = strchr(s, ']');
            if (!end)
                break;
            const char *space = memchr(s, ' ', (size_t)(end - s));
            if (space)
                puts_n(o, space + 1, (size_t)(end - space - 1));
            s = end + 1;
        } else if (starts(s, "''")) {
            while (*s == '\'')
                s++;
        } else if (starts_ci(s, "<br")) {
            const char *end = strchr(s, '>');
            put(o, ' ');
            s = end ? end + 1 : s + 3;
        } else if (s[0] == '<' && (isalpha((unsigned char)s[1]) || s[1] == '/')) {
            const char *end = strchr(s, '>');
            s = end ? end + 1 : s + 1;
        } else if (s[0] == '&') {
            size_t i;
            for (i = 0; i < sizeof(ENTITIES) / sizeof(ENTITIES[0]); i++)
                if (starts(s, ENTITIES[i].name))
                    break;
            if (i < sizeof(ENTITIES) / sizeof(ENTITIES[0])) {
                put(o, ENTITIES[i].c);
                s += strlen(ENTITIES[i].name);
            } else {
                put(o, *s++);
            }
        } else {
            put_utf8(o, (const unsigned char **)&s);
        }
    }
}

/* Trims trailing spaces of the line being written since `start`; drops it if empty. */
static void end_line(Out *o, size_t start, size_t marker_len)
{
    while (o->len > start && o->p[o->len - 1] == ' ')
        o->len--;
    if (o->len <= start + marker_len)
        o->len = start;
    else
        put(o, '\n');
}

size_t kw_wikitext_render(const char *src, size_t src_len, char *out, size_t out_len)
{
    if (!out_len)
        return 0;
    Out o = { out, 0, out_len };
    char *w = malloc(src_len + 1);
    char *line = malloc(src_len + 1);
    if (!w || !line) {
        free(w);
        free(line);
        out[0] = '\0';
        return 0;
    }
    size_t n = strip_blocks(src, src_len, w);

    for (size_t i = 0; i < n;) {
        size_t j = i;
        while (j < n && w[j] != '\n')
            j++;
        size_t len = j - i;
        memcpy(line, w + i, len);
        line[len] = '\0';
        i = j + 1;

        char *s = line;
        while (*s == ' ' || *s == '\t' || *s == '\r')
            s++;
        size_t sl = strlen(s);
        while (sl && (s[sl - 1] == ' ' || s[sl - 1] == '\r' || s[sl - 1] == '\t'))
            s[--sl] = '\0';
        if (!*s || starts(s, "{|") || starts(s, "|}") || starts(s, "|-") || starts_ci(s, "#REDIRECT"))
            continue;

        size_t start = o.len;
        if (s[0] == '=') {
            int level = 0;
            while (s[level] == '=')
                level++;
            char *end = s + strlen(s);
            while (end > s + level && end[-1] == '=')
                *--end = '\0';
            s += level;
            while (*s == ' ')
                s++;
            if (level <= 2) {
                /* "17524/P1116/004374 - Oxygen ..." -> "Oxygen ..." */
                char *dash = strstr(s, " - ");
                if (dash && !memchr(s, ' ', (size_t)(dash - s)))
                    s = dash + 3;
                puts_n(&o, "= ", 2);
            } else {
                puts_n(&o, "# ", 2);
            }
            render_inline(&o, s);
            end_line(&o, start, 2);
        } else if (s[0] == '*' || s[0] == '#') {
            int depth = 0;
            while (s[depth] == '*' || s[depth] == '#')
                depth++;
            s += depth;
            while (*s == ' ')
                s++;
            int marker = depth > 1 ? 3 : 2;
            puts_n(&o, depth > 1 ? "** " : "* ", (size_t)marker);
            render_inline(&o, s);
            end_line(&o, start, (size_t)marker);
        } else {
            if (s[0] == '|' || s[0] == '!') { /* table cell: keep its text */
                s++;
                while (*s == ' ')
                    s++;
            }
            for (char *c = s; (c = strstr(c, "||")) != NULL;)
                memcpy(c, "  ", 2);
            puts_n(&o, "  ", 2);
            render_inline(&o, s);
            end_line(&o, start, 2);
        }
    }
    free(w);
    free(line);
    o.p[o.len] = '\0';
    return o.len;
}

int kw_wikitext_redirect(const char *src, char *out, size_t out_len)
{
    while (*src == ' ' || *src == '\n' || *src == '\r')
        src++;
    if (!starts_ci(src, "#REDIRECT"))
        return 0;
    const char *open = strstr(src, "[[");
    const char *close = open ? strstr(open, "]]") : NULL;
    if (!close || out_len == 0)
        return 0;
    size_t n = (size_t)(close - open - 2);
    const char *bar = memchr(open + 2, '|', n);
    if (bar)
        n = (size_t)(bar - open - 2);
    if (n >= out_len)
        n = out_len - 1;
    memcpy(out, open + 2, n);
    out[n] = '\0';
    return 1;
}
