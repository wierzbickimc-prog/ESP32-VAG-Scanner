#include "labels.h"

#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *part;
    int group;
    int field;
    const char *text;
} Label;

static char *s_buf;
static Label *s_labels;
static int s_count;

/* Next comma-separated field of a line, trimmed and NUL-terminated in place. */
static char *next_field(char **p)
{
    char *start = *p;
    char *comma = strchr(start, ',');
    if (comma) {
        *comma = '\0';
        *p = comma + 1;
    } else {
        *p = start + strlen(start);
    }
    while (*start == ' ')
        start++;
    char *end = start + strlen(start);
    while (end > start && (end[-1] == ' ' || end[-1] == '\r'))
        *--end = '\0';
    return start;
}

int kw_labels_load(const char *csv, size_t len)
{
    free(s_buf);
    free(s_labels);
    s_buf = NULL;
    s_labels = NULL;
    s_count = 0;

    s_buf = malloc(len + 1);
    if (!s_buf)
        return 0;
    memcpy(s_buf, csv, len);
    s_buf[len] = '\0';

    int lines = 1;
    for (size_t i = 0; i < len; i++)
        lines += s_buf[i] == '\n';
    s_labels = malloc(sizeof(Label) * lines);
    if (!s_labels)
        return 0;

    char *p = s_buf;
    while (*p) {
        char *line = p;
        char *nl = strchr(p, '\n');
        if (nl)
            *nl = '\0';
        p = nl ? nl + 1 : p + strlen(p);
        if (line[0] == '#' || line[0] == '\0' || line[0] == '\r')
            continue;

        char *q = line;
        char *part = next_field(&q);
        char *group = next_field(&q);
        char *field = next_field(&q);
        char *text = q;
        while (*text == ' ')
            text++;
        size_t n = strlen(text);
        while (n && (text[n - 1] == '\r' || text[n - 1] == ' '))
            text[--n] = '\0';
        if (!*part || !*text || atoi(field) <= 0 || strcmp(part, "part") == 0)
            continue;
        s_labels[s_count++] = (Label){ part, atoi(group), atoi(field), text };
    }
    return s_count;
}

static const char *skip_spaces(const char *s)
{
    while (*s == ' ')
        s++;
    return s;
}

/* VW engine ECU part numbers have "906" after the 3-character prefix (022906032BK). */
static int is_engine_ecu(const char *ident)
{
    return strlen(ident) >= 6 && strncmp(ident + 3, "906", 3) == 0;
}

static const char *find(const char *part_or_ident, int match_prefix, int group, int field)
{
    for (int i = 0; i < s_count; i++) {
        const Label *l = &s_labels[i];
        if (l->group != group || l->field != field)
            continue;
        if (match_prefix ? (l->part[0] != '@' && strncmp(part_or_ident, l->part, strlen(l->part)) == 0)
                         : strcmp(part_or_ident, l->part) == 0)
            return l->text;
    }
    return NULL;
}

const char *kw_label_find(const char *ident, int group, int field)
{
    if (!ident)
        return NULL;
    ident = skip_spaces(ident);
    const char *l = find(ident, 1, group, field);
    if (!l && is_engine_ecu(ident))
        l = find("@petrol", 0, group, field);
    return l;
}

int kw_labels_standard(const char *ident, int group)
{
    if (!ident)
        return 0;
    ident = skip_spaces(ident);
    int standard = 0;
    for (int f = 1; f <= 4; f++) {
        if (find(ident, 1, group, f))
            return 0;
        standard |= find("@petrol", 0, group, f) != NULL;
    }
    return standard && is_engine_ecu(ident);
}

/* By formula type, matching kw_format_sensor in kw1281.c. */
const char *kw_sensor_kind(int sensor_id)
{
    switch (sensor_id) {
    case 1:  return "Engine speed";
    case 2: case 20: case 23: case 33: return "Percentage";
    case 3: case 9: case 67: return "Angle";
    case 4: case 27: return "Ignition timing";
    case 5: case 26: return "Temperature";
    case 6: case 21: case 43: case 66: return "Voltage";
    case 7:  return "Speed";
    case 10: return "Warm-up state";
    case 12: case 64: return "Resistance";
    case 13: case 65: return "Distance";
    case 14: case 18: case 50: case 69: return "Pressure";
    case 15: case 22: return "Injection time";
    case 16: return "Status bits";
    case 17: case 63: return "Text";
    case 19: return "Fuel level";
    case 24: case 40: return "Current";
    case 25: case 53: return "Air mass";
    case 30: case 38: case 46: return "Knock retard";
    case 34: case 42: return "Power";
    case 35: case 39: case 49: case 51: return "Fuel consumption";
    case 36: return "Odometer";
    case 41: return "Battery capacity";
    case 44: return "Time";
    case 52: return "Torque";
    case 55: case 60: return "Duration";
    case 56: case 57: return "Workshop code";
    case 68: return "Yaw rate";
    case 70: return "Acceleration";
    default: return "Value";
    }
}

const char *kw_label(const char *ident, int group, int field, int sensor_id)
{
    const char *l = kw_label_find(ident, group, field);
    return l ? l : kw_sensor_kind(sensor_id);
}
