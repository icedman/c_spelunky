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

/* ---- INI files (Function_ini.js, yyIniFile.js) ------------------------------------
 *
 * One INI file is open at a time and held in static tables. Relative names
 * resolve against working_directory (CheckWorkingDirectory). Sections and keys
 * keep insertion order; ini_close writes the file back only if a write
 * changed it, as `[section]` / `key="value"` lines ending in "\r\n".
 * Names and values longer than the limits below are truncated; writes beyond
 * the table capacity are dropped (return false). */
#define GM_INI_MAX_SECTIONS 16
#define GM_INI_MAX_KEYS 128
#define GM_INI_NAME_MAX 64   /* section and key names, including the NUL */
#define GM_INI_VALUE_MAX 256 /* including the NUL */
#define GM_INI_TEXT_MAX 16384

/* Opens `path` (closing, and saving, any INI already open). A missing or
 * unreadable file opens as an empty INI that ini_close will create. */
void gm_ini_open(const char *path);

/* Saves if changed and closes. Returns the INI text (static, valid until the
 * next gm_ini_close; truncated at GM_INI_TEXT_MAX - 1 characters, the file is
 * not), or "" when nothing was open. */
const char *gm_ini_close(void);

bool gm_ini_is_open(void);

/* The stored string, or `def` when the key (or the INI) is missing. */
const char *gm_ini_read_string(const char *section, const char *key, const char *def);

/* parseFloat of the stored string, or `def` when the key is missing. Values
 * that do not parse also give `def` (NaN in the HTML5 runner, which would
 * poison every later comparison against the saved score). */
float gm_ini_read_real(const char *section, const char *key, float def);

/* False when no INI is open or the tables are full. */
bool gm_ini_write_string(const char *section, const char *key, const char *value);

/* Stores the JS ToString of `value` ("" + value). */
bool gm_ini_write_real(const char *section, const char *key, float value);

#endif /* GM_FILE_H */
