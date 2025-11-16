#ifndef ZELDA3_UTIL_H_
#define ZELDA3_UTIL_H_

#include "types.h"

typedef struct SDL_Window SDL_Window;

struct RendererFuncs {
  bool (*Initialize)(SDL_Window *window);
  void (*Destroy)();
  void (*BeginDraw)(int width, int height, uint8 **pixels, int *pitch);
  void (*EndDraw)();
};


typedef struct ByteArray {
  uint8 *data;
  size_t size, capacity;
} ByteArray;

/**
 * Allocate memory with automatic error handling
 * @param size Number of bytes to allocate
 * @return Pointer to allocated memory (never NULL - dies on failure)
 */
void *xmalloc(size_t size);

/**
 * Reallocate memory with automatic error handling
 * @param ptr Existing memory pointer (NULL for initial allocation)
 * @param size New size in bytes
 * @return Pointer to reallocated memory (never NULL - dies on failure)
 */
void *xrealloc(void *ptr, size_t size);

/** Resize dynamic byte array to new size */
void ByteArray_Resize(ByteArray *arr, size_t new_size);

/** Free dynamic byte array resources */
void ByteArray_Destroy(ByteArray *arr);

/** Append raw data to byte array */
void ByteArray_AppendData(ByteArray *arr, const uint8 *data, size_t data_size);

/** Append single byte to array */
void ByteArray_AppendByte(ByteArray *arr, uint8 v);

/**
 * Read entire file into memory
 * @param name File path to read
 * @param length Output parameter for file size (may be NULL)
 * @return Allocated buffer containing file contents, or NULL on error
 */
uint8 *ReadWholeFile(const char *name, size_t *length);

/**
 * Extract next token from string up to delimiter
 * @param s Pointer to string pointer (updated to position after delimiter)
 * @param sep Delimiter character
 * @return Token string (NUL-terminated), or NULL if no more tokens
 */
char *NextDelim(char **s, int sep);

/**
 * Get next line from text, stripping comments (# to end of line)
 * @param s Pointer to string pointer (updated to next line)
 * @return Line string, or NULL if end of text
 */
char *NextLineStripComments(char **s);

/**
 * Parse next string, respecting quotes
 * @param s Pointer to string pointer (updated past parsed string)
 * @return Unquoted string, or NULL if parse fails
 */
char *NextPossiblyQuotedString(char **s);

/**
 * Split "key=value" string at '=' separator
 * @param p String to split (modified in-place)
 * @return Pointer to value part, or NULL if no '=' found
 */
char *SplitKeyValue(char *p);

/**
 * Case-insensitive string comparison
 * @return true if strings are equal (ignoring case)
 */
bool StringEqualsNoCase(const char *a, const char *b);

/**
 * Check if string starts with prefix (case-insensitive)
 * @return Pointer past prefix in 'a' if match, NULL otherwise
 */
const char *StringStartsWithNoCase(const char *a, const char *b);

/**
 * Parse boolean value from string
 * Accepts: "true"/"false", "on"/"off", "yes"/"no", "1"/"0" (case-insensitive)
 * @param value String to parse
 * @param result Output pointer for parsed value (may be NULL for validation only)
 * @return true if parsing succeeded, false otherwise
 */
bool ParseBool(const char *value, bool *result);

/**
 * Skip past prefix in string (case-sensitive)
 * @return Pointer past prefix if match, NULL otherwise
 */
const char *SkipPrefix(const char *big, const char *little);

/**
 * Set string value with automatic memory management
 * Frees old value and duplicates new string
 * @param rv Pointer to string pointer to update
 * @param s New string value
 */
void StrSet(char **rv, const char *s);

/**
 * Format string with printf-style arguments
 * @return Allocated formatted string (caller must free)
 */
char *StrFmt(const char *fmt, ...);

/**
 * Replace filename portion of path with new path
 * @param old_path Original path with filename
 * @param new_path New filename/relative path
 * @return Allocated string with combined path (caller must free)
 */
char *ReplaceFilenameWithNewPath(const char *old_path, const char *new_path);

#endif  // ZELDA3_UTIL_H_