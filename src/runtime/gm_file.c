/*
 * gm_file - see gm_file.h. Streams via C99 stdio; one-character lookahead is
 * done with fgetc/ungetc (C99 guarantees one pushback character), which is
 * all the HTML5 read semantics need.
 */
#include "gm_file.h"

#include "sp_platform.h"

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
