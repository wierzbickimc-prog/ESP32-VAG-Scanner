#include "fault_table.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    int dtc;
    int derived; /* converted from a P-code; an explicit VAG entry wins */
    const char *text;
} Entry;

static char *s_buf;
static Entry *s_entries;
static int s_count;

static int cmp_entry(const void *a, const void *b)
{
    const Entry *x = a, *y = b;
    if (x->dtc != y->dtc)
        return x->dtc - y->dtc;
    return x->derived - y->derived;
}

static int cmp_dtc(const void *a, const void *b)
{
    return ((const Entry *)a)->dtc - ((const Entry *)b)->dtc;
}

/*
 * Reads one CSV field starting at *p, unquoting and NUL-terminating it in
 * place. Returns the field; *delim gets the character that ended it
 * (',', '\n' or '\0') and *p points just past it.
 */
static char *field(char **p, char *delim)
{
    char *s = *p, *out = s, *start = s;
    if (*s == '"') {
        s++;
        while (*s) {
            if (*s == '"') {
                if (s[1] == '"') {
                    *out++ = '"';
                    s += 2;
                    continue;
                }
                s++;
                break;
            }
            *out++ = *s++;
        }
        while (*s && *s != ',' && *s != '\n')
            s++;
    } else {
        while (*s && *s != ',' && *s != '\n')
            *out++ = *s++;
    }
    *delim = *s;
    *p = *s ? s + 1 : s;
    *out = '\0';
    while (out > start && (out[-1] == '\r' || out[-1] == ' '))
        *--out = '\0';
    return start;
}

static int digits(const char *s, int n)
{
    for (int i = 0; i < n; i++)
        if (!isdigit((unsigned char)s[i]))
            return 0;
    return 1;
}

/*
 * Code column: "00515", "16497/P0113/000275" or "P0300". Returns the VAG
 * 5-digit code, or -1. P-codes map as 16384 + 1024 * first digit + last three
 * (P0113 = 16497, P1101 = 17509); hex P-codes (P000A) are newer than KW1281.
 */
static int parse_code(const char *s, int *derived)
{
    *derived = 0;
    if (digits(s, 5) && (s[5] == '\0' || s[5] == '/'))
        return atoi(s);
    if (s[0] == 'P' && digits(s + 1, 4) && (s[5] == '\0' || s[5] == '/') && s[1] <= '3') {
        *derived = 1;
        return 16384 + 1024 * (s[1] - '0') + atoi(s + 2);
    }
    return -1;
}

int kw_fault_table_load(const char *csv, size_t len)
{
    free(s_buf);
    free(s_entries);
    s_buf = NULL;
    s_entries = NULL;
    s_count = 0;

    s_buf = malloc(len + 1);
    if (!s_buf)
        return 0;
    memcpy(s_buf, csv, len);
    s_buf[len] = '\0';

    int lines = 1;
    for (size_t i = 0; i < len; i++)
        lines += s_buf[i] == '\n';
    s_entries = malloc(sizeof(Entry) * lines);
    if (!s_entries)
        return 0;

    char *p = s_buf;
    int row = 0;
    while (*p) {
        char delim;
        char *code = field(&p, &delim);
        char *text = NULL;
        if (delim == ',') {
            text = field(&p, &delim);
            while (delim == ',') /* ignore extra columns */
                field(&p, &delim);
        }
        if (row++ == 0)
            continue; /* header */
        while (*code == ' ')
            code++;
        int derived;
        int dtc = parse_code(code, &derived);
        if (text && *text && dtc >= 0) {
            s_entries[s_count].dtc = dtc;
            s_entries[s_count].derived = derived;
            s_entries[s_count].text = text;
            s_count++;
        }
    }
    qsort(s_entries, s_count, sizeof(Entry), cmp_entry);

    /* drop duplicates, keeping the first (explicit before derived) */
    int kept = 0;
    for (int i = 0; i < s_count; i++)
        if (kept == 0 || s_entries[i].dtc != s_entries[kept - 1].dtc)
            s_entries[kept++] = s_entries[i];
    s_count = kept;
    return s_count;
}

const char *kw_fault_describe(int dtc)
{
    Entry key = { dtc, 0, NULL };
    Entry *e = s_count ? bsearch(&key, s_entries, s_count, sizeof(Entry), cmp_dtc) : NULL;
    return e ? e->text : NULL;
}

int kw_fault_pcode(int dtc, char *out, int out_len)
{
    int rel = dtc - 16384;
    if (rel < 0 || rel >= 4 * 1024 || rel % 1024 > 999)
        return 0;
    snprintf(out, out_len, "P%d%03d", rel / 1024, rel % 1024);
    return 1;
}

/* Optional: written by assets/tools/fetch_fault_types.py (GPL-3.0 source, so not in git). */
#if defined(__has_include) && __has_include("fault_types.h")
#include "fault_types.h"
#define HAVE_FAULT_TYPES 1
#endif

const char *kw_fault_type(int status)
{
#ifdef HAVE_FAULT_TYPES
    int t = status & 0x7F;
    const char *name = t < (int)(sizeof(kw_fault_type_names) / sizeof(kw_fault_type_names[0]))
                           ? kw_fault_type_names[t] : NULL;
    if (name && strcmp(name, "-") != 0 && strcmp(name, "No Elaboration Available") != 0)
        return name;
#else
    (void)status;
#endif
    return NULL;
}

int kw_fault_table_size(void)
{
    return s_count;
}
