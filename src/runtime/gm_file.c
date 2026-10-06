/*
 * gm_file - see gm_file.h. Streams via C99 stdio; one-character lookahead is
 * done with fgetc/ungetc (C99 guarantees one pushback character), which is
 * all the HTML5 read semantics need.
 */
#include "gm_file.h"

#include "gm_value.h"
#include "sp_platform.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

typedef struct gm_text_file {
    FILE *fp;
    bool writing;
} gm_text_file_t;

static gm_text_file_t s_files[GM_FILE_MAX_HANDLES];

static char s_strings[GM_FILE_STRING_RING][GM_FILE_STRING_MAX];
static unsigned s_string_next;

static char s_working_dir[GM_FILE_STRING_MAX] = "./";

static char *next_string_buffer(void)
{
    char *buf = s_strings[s_string_next];
    s_string_next = (s_string_next + 1u) % GM_FILE_STRING_RING;
    buf[0] = '\0';
    return buf;
}

static gm_text_file_t *get_file(int handle, bool want_write)
{
    gm_text_file_t *f;

    if (handle < 0 || handle >= GM_FILE_MAX_HANDLES) {
        return NULL;
    }
    f = &s_files[handle];
    if (f->fp == NULL || f->writing != want_write) {
        return NULL;
    }
    return f;
}

static int peek_char(FILE *fp)
{
    int c = fgetc(fp);
    if (c != EOF) {
        ungetc(c, fp);
    }
    return c;
}

static bool is_newline(int c)
{
    return c == '\r' || c == '\n';
}

/* Opens a regular file for reading. POSIX stdio lets fopen() succeed on a
 * directory with the first read failing, so probe one byte and reject that. */
static FILE *open_regular_for_read(const char *path)
{
    FILE *fp;
    int c;

    if (path == NULL || path[0] == '\0') {
        return NULL;
    }
    fp = fopen(path, "rb");
    if (fp == NULL) {
        return NULL;
    }
    c = fgetc(fp);
    if (c == EOF) {
        if (ferror(fp)) {
            fclose(fp);
            return NULL;
        }
    } else {
        ungetc(c, fp);
    }
    return fp;
}

static int alloc_handle(FILE *fp, bool writing)
{
    int i;

    /* Lowest free slot first. (yyList.Alloc reuses the most recently freed
     * index instead; handle values are opaque to the game.) */
    for (i = 0; i < GM_FILE_MAX_HANDLES; ++i) {
        if (s_files[i].fp == NULL) {
            s_files[i].fp = fp;
            s_files[i].writing = writing;
            return i;
        }
    }
    fclose(fp);
    return -1;
}

int gm_file_text_open_read(const char *path)
{
    FILE *fp = open_regular_for_read(path);
    return fp != NULL ? alloc_handle(fp, false) : -1;
}

int gm_file_text_open_write(const char *path)
{
    FILE *fp;

    if (path == NULL || path[0] == '\0') {
        return -1;
    }
    /* Binary mode so writeln emits exactly "\r\n" on every platform. */
    fp = fopen(path, "wb");
    return fp != NULL ? alloc_handle(fp, true) : -1;
}

void gm_file_text_close(int handle)
{
    gm_text_file_t *f;

    if (handle < 0 || handle >= GM_FILE_MAX_HANDLES) {
        return;
    }
    f = &s_files[handle];
    if (f->fp != NULL) {
        fclose(f->fp);
        f->fp = NULL;
        f->writing = false;
    }
}

void gm_file_text_close_all(void)
{
    int i;

    for (i = 0; i < GM_FILE_MAX_HANDLES; ++i) {
        gm_file_text_close(i);
    }
}

/* Function_File.js L342: characters up to, not including, CR or LF. */
const char *gm_file_text_read_string(int handle)
{
    char *out = next_string_buffer();
    gm_text_file_t *f = get_file(handle, false);
    size_t len = 0;
    int c;

    if (f == NULL) {
        return out;
    }
    while ((c = fgetc(f->fp)) != EOF) {
        if (is_newline(c)) {
            ungetc(c, f->fp);
            break;
        }
        if (len + 1 < GM_FILE_STRING_MAX) {
            out[len++] = (char)c;
        }
    }
    out[len] = '\0';
    return out;
}

/* Function_File.js L422: rest of the line plus the terminator. On the first
 * CR or LF, one more CR or LF is also consumed if present (so "\r\n" is one
 * terminator; note "\n\n" is too, exactly as in the HTML5 runner). */
const char *gm_file_text_readln(int handle)
{
    char *out = next_string_buffer();
    gm_text_file_t *f = get_file(handle, false);
    size_t len = 0;
    int c;

    if (f == NULL) {
        return out;
    }
    while ((c = fgetc(f->fp)) != EOF) {
        if (len + 1 < GM_FILE_STRING_MAX) {
            out[len++] = (char)c;
        }
        if (is_newline(c)) {
            c = peek_char(f->fp);
            if (is_newline(c)) {
                (void)fgetc(f->fp);
                if (len + 1 < GM_FILE_STRING_MAX) {
                    out[len++] = (char)c;
                }
            }
            break;
        }
    }
    out[len] = '\0';
    return out;
}

bool gm_file_text_eof(int handle)
{
    gm_text_file_t *f = get_file(handle, false);
    if (f == NULL) {
        return true;
    }
    return peek_char(f->fp) == EOF;
}

void gm_file_text_write_string(int handle, const char *str)
{
    gm_text_file_t *f = get_file(handle, true);
    if (f == NULL || str == NULL) {
        return;
    }
    fputs(str, f->fp);
}

void gm_file_text_writeln(int handle)
{
    gm_text_file_t *f = get_file(handle, true);
    if (f == NULL) {
        return;
    }
    fputs("\r\n", f->fp);
}

bool gm_file_exists(const char *path)
{
    FILE *fp = open_regular_for_read(path);
    if (fp == NULL) {
        return false;
    }
    fclose(fp);
    return true;
}

bool gm_file_delete(const char *path)
{
    if (path == NULL || path[0] == '\0') {
        return false;
    }
    return remove(path) == 0;
}

bool gm_directory_exists(const char *path)
{
    if (path == NULL || path[0] == '\0' || g_platform.directory_exists == NULL) {
        return false;
    }
    return g_platform.directory_exists(g_platform.user_data, path);
}

const char *gm_working_directory(void)
{
    return s_working_dir;
}

bool gm_set_working_directory(const char *dir)
{
    size_t len;

    if (dir == NULL) {
        return false;
    }
    if (dir[0] == '\0') {
        dir = "./";
    }
    len = strlen(dir);
    if (len > GM_FILE_STRING_MAX - 2) {
        return false;
    }
    memcpy(s_working_dir, dir, len);
    if (len == 0 || (dir[len - 1] != '/' && dir[len - 1] != '\\')) {
        s_working_dir[len++] = '/';
    }
    s_working_dir[len] = '\0';
    return true;
}

/* ------------------------------------------------------------------ INI files */

typedef struct ini_key {
    int section;
    char key[GM_INI_NAME_MAX];
    char value[GM_INI_VALUE_MAX];
} ini_key_t;

static struct {
    bool open;
    bool changed;
    char path[GM_FILE_STRING_MAX];
    int nsections;
    int nkeys;
    char sections[GM_INI_MAX_SECTIONS][GM_INI_NAME_MAX];
    ini_key_t keys[GM_INI_MAX_KEYS];
    char text[GM_INI_TEXT_MAX];
    size_t text_len;
} s_ini;

static void copy_trunc(char *dst, size_t cap, const char *src, size_t len)
{
    if (len >= cap) {
        len = cap - 1;
    }
    memcpy(dst, src, len);
    dst[len] = '\0';
}

static bool is_blank(char c)
{
    return c == ' ' || c == '\t';
}

/* Names are compared after the same truncation they get when stored. */
static bool name_eq(const char *stored, const char *name)
{
    return strncmp(stored, name, GM_INI_NAME_MAX - 1) == 0;
}

static int ini_find_section(const char *name)
{
    int i;

    for (i = 0; i < s_ini.nsections; ++i) {
        if (name_eq(s_ini.sections[i], name)) {
            return i;
        }
    }
    return -1;
}

static int ini_section(const char *name, size_t len)
{
    char buf[GM_INI_NAME_MAX];
    int i;

    copy_trunc(buf, sizeof(buf), name, len);
    i = ini_find_section(buf);
    if (i >= 0 || s_ini.nsections >= GM_INI_MAX_SECTIONS) {
        return i;
    }
    memcpy(s_ini.sections[s_ini.nsections], buf, sizeof(buf));
    return s_ini.nsections++;
}

static ini_key_t *ini_find_key(const char *section, const char *key)
{
    int sec = ini_find_section(section);
    int i;

    if (sec < 0) {
        return NULL;
    }
    for (i = 0; i < s_ini.nkeys; ++i) {
        if (s_ini.keys[i].section == sec && name_eq(s_ini.keys[i].key, key)) {
            return &s_ini.keys[i];
        }
    }
    return NULL;
}

/* yyIniFile.SetKey without the change flag (shared with the loader). */
static bool ini_set(int sec, const char *key, size_t key_len, const char *value, size_t value_len)
{
    char name[GM_INI_NAME_MAX];
    ini_key_t *k = NULL;
    int i;

    if (sec < 0) {
        return false;
    }
    copy_trunc(name, sizeof(name), key, key_len);
    for (i = 0; i < s_ini.nkeys; ++i) {
        if (s_ini.keys[i].section == sec && strcmp(s_ini.keys[i].key, name) == 0) {
            k = &s_ini.keys[i];
            break;
        }
    }
    if (k == NULL) {
        if (s_ini.nkeys >= GM_INI_MAX_KEYS) {
            return false;
        }
        k = &s_ini.keys[s_ini.nkeys++];
        k->section = sec;
        memcpy(k->key, name, sizeof(name));
    }
    copy_trunc(k->value, sizeof(k->value), value, value_len);
    return true;
}

/* One line of yyIniFile.ReadIniFile: a `[section]` header or a `key = value`
 * pair of the current section. Text before the first section, blank lines
 * and lines starting with '#' or ';' are ignored. */
static void ini_parse_line(const char *p, int *sec)
{
    const char *eq, *end, *v, *ws;
    char t1 = '#', t2 = ';';

    while (is_blank(*p)) {
        p++;
    }
    if (*p == '\0' || *p == '#' || *p == ';') {
        return;
    }
    if (*p == '[') {
        end = strchr(p + 1, ']');
        if (end != NULL) {
            *sec = ini_section(p + 1, (size_t)(end - (p + 1)));
        }
        return;
    }
    eq = strchr(p, '=');
    if (*sec < 0 || eq == NULL) {
        return;
    }
    end = eq;
    while (end > p && is_blank(end[-1])) {
        end--;
    }

    v = eq + 1;
    while (is_blank(*v)) {
        v++;
    }
    if (*v == '"' || *v == '\'') {
        /* Quoted: up to the matching quote (or the end of the line). */
        t1 = t2 = *v++;
    } else if (*v == '[' || *v == '{') {
        t1 = t2 = '\0'; /* JSON: no trailing comments */
    }
    ws = NULL;
    for (eq = v; *eq != '\0' && *eq != t1 && *eq != t2; ++eq) {
        if (is_blank(*eq)) {
            if (ws == NULL) {
                ws = eq;
            }
        } else {
            ws = NULL;
        }
        if (*eq == '\\' && eq[1] != '\0') {
            eq++; /* no escapes: the backslash and the next char are kept */
        }
    }
    if (ws == NULL || t1 == '"' || t1 == '\'') {
        ws = eq;
    }
    (void)ini_set(*sec, p, (size_t)(end - p), v, (size_t)(ws - v));
}

static void ini_load(FILE *fp)
{
    char line[GM_FILE_STRING_MAX];
    size_t len = 0;
    int sec = -1;
    int c;

    do {
        c = fgetc(fp);
        if (c == EOF || c == '\r' || c == '\n') {
            line[len] = '\0';
            ini_parse_line(line, &sec);
            len = 0;
        } else if (len + 1 < sizeof(line)) {
            line[len++] = (char)c;
        }
    } while (c != EOF);
}

/* CheckWorkingDirectory: relative names live in working_directory. */
static void ini_resolve(const char *name, char *out, size_t cap)
{
    size_t wd = strlen(s_working_dir);
    bool absolute = name[0] == '/' || name[0] == '\\' || (name[0] != '\0' && name[1] == ':');

    if (absolute || strncmp(name, s_working_dir, wd) == 0) {
        copy_trunc(out, cap, name, strlen(name));
    } else {
        snprintf(out, cap, "%s%s", s_working_dir, name);
    }
}

void gm_ini_open(const char *path)
{
    FILE *fp;

    if (s_ini.open) {
        (void)gm_ini_close();
    }
    s_ini.nsections = 0;
    s_ini.nkeys = 0;
    s_ini.changed = false;
    ini_resolve(path != NULL ? path : "", s_ini.path, sizeof(s_ini.path));
    fp = open_regular_for_read(s_ini.path);
    if (fp != NULL) {
        ini_load(fp);
        fclose(fp);
    }
    s_ini.open = true;
}

static void ini_emit(FILE *fp, const char *s)
{
    size_t n = strlen(s);

    if (fp != NULL) {
        fputs(s, fp);
    }
    if (s_ini.text_len + n >= sizeof(s_ini.text)) {
        n = sizeof(s_ini.text) - 1 - s_ini.text_len;
    }
    memcpy(s_ini.text + s_ini.text_len, s, n);
    s_ini.text_len += n;
    s_ini.text[s_ini.text_len] = '\0';
}

/* yyIniFile.WriteIniFile */
const char *gm_ini_close(void)
{
    FILE *fp = NULL;
    int sec, i;

    s_ini.text_len = 0;
    s_ini.text[0] = '\0';
    if (!s_ini.open) {
        return s_ini.text;
    }
    if (s_ini.changed) {
        fp = fopen(s_ini.path, "wb");
    }
    for (sec = 0; sec < s_ini.nsections; ++sec) {
        ini_emit(fp, "[");
        ini_emit(fp, s_ini.sections[sec]);
        ini_emit(fp, "]\r\n");
        for (i = 0; i < s_ini.nkeys; ++i) {
            const ini_key_t *k = &s_ini.keys[i];
            const char *quote;

            if (k->section != sec) {
                continue;
            }
            /* Double quotes unless the value has some, then single, then none. */
            quote = strchr(k->value, '"') == NULL ? "\"" : strchr(k->value, '\'') == NULL ? "'" : "";
            ini_emit(fp, k->key);
            ini_emit(fp, "=");
            ini_emit(fp, quote);
            ini_emit(fp, k->value);
            ini_emit(fp, quote);
            ini_emit(fp, "\r\n");
        }
    }
    if (fp != NULL) {
        fclose(fp);
    }
    s_ini.open = false;
    s_ini.changed = false;
    s_ini.nsections = 0;
    s_ini.nkeys = 0;
    return s_ini.text;
}

bool gm_ini_is_open(void)
{
    return s_ini.open;
}

const char *gm_ini_read_string(const char *section, const char *key, const char *def)
{
    const ini_key_t *k = NULL;

    if (s_ini.open && section != NULL && key != NULL) {
        k = ini_find_key(section, key);
    }
    return k != NULL ? k->value : (def != NULL ? def : "");
}

float gm_ini_read_real(const char *section, const char *key, float def)
{
    const ini_key_t *k = NULL;
    bool ok;
    float r;

    if (s_ini.open && section != NULL && key != NULL) {
        k = ini_find_key(section, key);
    }
    if (k == NULL) {
        return def;
    }
    r = gm_string_parse_real(k->value, &ok);
    return ok ? r : def;
}

bool gm_ini_write_string(const char *section, const char *key, const char *value)
{
    if (!s_ini.open || section == NULL || key == NULL) {
        return false;
    }
    if (value == NULL) {
        value = "";
    }
    if (!ini_set(ini_section(section, strlen(section)), key, strlen(key), value, strlen(value))) {
        return false;
    }
    s_ini.changed = true;
    return true;
}

bool gm_ini_write_real(const char *section, const char *key, float value)
{
    char buf[64];

    /* JS ToString for integers; other values with enough digits to round-trip
     * a float. */
    if (isfinite(value) && fabsf(value) < 1e21f && floorf(value) == value) {
        snprintf(buf, sizeof(buf), "%.0f", value == 0.0f ? 0.0 : (double)value); /* -0 -> "0" */
    } else if (isnan(value)) {
        snprintf(buf, sizeof(buf), "NaN");
    } else if (isinf(value)) {
        snprintf(buf, sizeof(buf), value < 0.0f ? "-Infinity" : "Infinity");
    } else {
        snprintf(buf, sizeof(buf), "%.9g", (double)value);
    }
    return gm_ini_write_string(section, key, buf);
}
