// Enable POSIX functions in strict C standards mode
#ifndef _WIN32
  #define _POSIX_C_SOURCE 200809L
#endif

#include "logging.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#ifdef _WIN32
  #include <io.h>
  #define isatty _isatty
  #define fileno _fileno
#else
  #include <unistd.h>
#endif

// ANSI color codes for terminal output
#define COLOR_RESET   "\033[0m"
#define COLOR_RED     "\033[31m"
#define COLOR_YELLOW  "\033[33m"
#define COLOR_CYAN    "\033[36m"
#define COLOR_GRAY    "\033[90m"

static LogLevel g_log_level = LOG_WARN;  // Default: show errors and warnings
static bool g_use_colors = false;        // Detect terminal support at init

void InitializeLogging(void) {
  // Check if stderr is a TTY (supports colors)
  g_use_colors = isatty(fileno(stderr));

  // Allow environment variable to override log level
  const char *log_env = getenv("SM_LOG_LEVEL");
  if (log_env) {
    if (strcmp(log_env, "ERROR") == 0 || strcmp(log_env, "0") == 0) {
      g_log_level = LOG_ERROR;
    } else if (strcmp(log_env, "WARN") == 0 || strcmp(log_env, "1") == 0) {
      g_log_level = LOG_WARN;
    } else if (strcmp(log_env, "INFO") == 0 || strcmp(log_env, "2") == 0) {
      g_log_level = LOG_INFO;
    } else if (strcmp(log_env, "DEBUG") == 0 || strcmp(log_env, "3") == 0) {
      g_log_level = LOG_DEBUG;
    }
  }
}

void SetLogLevel(LogLevel level) {
  g_log_level = level;
}

LogLevel GetLogLevel(void) {
  return g_log_level;
}

void LogPrintV(LogLevel level, const char *file, int line, const char *fmt, va_list args) {
  // Filter messages below current log level
  if (level > g_log_level)
    return;

  // Select prefix and color based on level
  const char *prefix = "";
  const char *color = "";
  const char *reset = "";

  if (g_use_colors) {
    reset = COLOR_RESET;
    switch (level) {
      case LOG_ERROR: prefix = "ERROR"; color = COLOR_RED; break;
      case LOG_WARN:  prefix = "WARN";  color = COLOR_YELLOW; break;
      case LOG_INFO:  prefix = "INFO";  color = COLOR_CYAN; break;
      case LOG_DEBUG: prefix = "DEBUG"; color = COLOR_GRAY; break;
    }
  } else {
    switch (level) {
      case LOG_ERROR: prefix = "ERROR"; break;
      case LOG_WARN:  prefix = "WARN"; break;
      case LOG_INFO:  prefix = "INFO"; break;
      case LOG_DEBUG: prefix = "DEBUG"; break;
    }
  }

  // Print prefix
  fprintf(stderr, "%s[%s]%s ", color, prefix, reset);

  // Print file/line in debug builds
  if (file && line > 0) {
    fprintf(stderr, "%s(%s:%d)%s ", color, file, line, reset);
  }

  // Print message
  vfprintf(stderr, fmt, args);

  // Ensure newline
  if (fmt[strlen(fmt) - 1] != '\n') {
    fprintf(stderr, "\n");
  }
}

void LogPrint(LogLevel level, const char *file, int line, const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  LogPrintV(level, file, line, fmt, args);
  va_end(args);
}
