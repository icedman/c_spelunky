/*
 * gm_file - GameMaker text-file and file-system subset used by Spelunky
 * Classic HD (level files, stats.txt, settings/keys JSON, locale charsets).
 *
 * Semantics follow reference/GameMaker-HTML5/scripts/functions/Function_File.js.
 * Only functions the game calls are provided; file_text_read_real/write_real,
 * file_text_eoln, file_text_open_append and file_find_* are intentionally
 * omitted until needed.
 *
 * Memory: handles and returned strings live in static storage; nothing here
 * allocates, so these calls are safe inside game events.
 */
#ifndef GM_FILE_H
#define GM_FILE_H

#include <stdbool.h>

/* Maximum simultaneously open text files ("32 max", Function_File.js L135). */
#define GM_FILE_MAX_HANDLES 32

/* Strings returned by read functions point into a ring of this many static
 * buffers; each stays valid until GM_FILE_STRING_RING further reads. Callers
 * that keep a string longer must copy it. */
#define GM_FILE_STRING_RING 8
/* Longest string a single read can return (excess characters on the line
 * are consumed but dropped). Includes the terminating NUL. */
#define GM_FILE_STRING_MAX 4096

/* Open for reading. Returns a handle >= 0, or -1 if the file is missing,
 * is a directory, or no handle is free. */
int gm_file_text_open_read(const char *path);

/* Open (create/truncate) for writing. Returns a handle >= 0, or -1. */
int gm_file_text_open_write(const char *path);

/* Close a handle. Invalid handles are ignored. */
void gm_file_text_close(int handle);

/* Close every open handle (room/game restart, test hygiene). */
void gm_file_text_close_all(void);

/* Read up to (not including) the next CR/LF. Does not consume the newline. */
const char *gm_file_text_read_string(int handle);

/* Read the rest of the current line including its line terminator
 * (up to two consecutive CR/LF characters, as in Function_File.js L422). */
const char *gm_file_text_readln(int handle);

/* True at end of file, or for invalid / write-only handles. */
bool gm_file_text_eof(int handle);

void gm_file_text_write_string(int handle, const char *str);

/* Writes "\r\n" (Function_File.js L325-326). */
void gm_file_text_writeln(int handle);

/* True only for existing, readable regular files (not directories). */
bool gm_file_exists(const char *path);

/* Returns true if the file was removed. */
bool gm_file_delete(const char *path);

/* Delegates to g_platform.directory_exists (pure C99 cannot stat). */
bool gm_directory_exists(const char *path);

/* GameMaker `working_directory` built-in (always ends with a separator). */
const char *gm_working_directory(void);

/* Sets working_directory; a trailing '/' is appended if missing. Paths longer
 * than GM_FILE_STRING_MAX - 2 characters are rejected (returns false). */
bool gm_set_working_directory(const char *dir);

#endif /* GM_FILE_H */
