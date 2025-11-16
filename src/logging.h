#ifndef SM_LOGGING_H_
#define SM_LOGGING_H_

#include <stdarg.h>

// Log levels ordered by severity (lower = more severe)
typedef enum {
  LOG_ERROR = 0,  // Always shown - critical errors
  LOG_WARN  = 1,  // Default level - warnings
  LOG_INFO  = 2,  // Verbose mode - informational messages
  LOG_DEBUG = 3   // Debug builds only - detailed debugging info
} LogLevel;

// Set the current log level (messages below this level are filtered)
void SetLogLevel(LogLevel level);

// Get the current log level
LogLevel GetLogLevel(void);

// Core logging function (use macros below for convenience)
void LogPrint(LogLevel level, const char *file, int line, const char *fmt, ...);
void LogPrintV(LogLevel level, const char *file, int line, const char *fmt, va_list args);

// Convenience macros with file/line info in debug builds
#ifdef _DEBUG
  #define LogError(fmt, ...) LogPrint(LOG_ERROR, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
  #define LogWarn(fmt, ...)  LogPrint(LOG_WARN,  __FILE__, __LINE__, fmt, ##__VA_ARGS__)
  #define LogInfo(fmt, ...)  LogPrint(LOG_INFO,  __FILE__, __LINE__, fmt, ##__VA_ARGS__)
  #define LogDebug(fmt, ...) LogPrint(LOG_DEBUG, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#else
  #define LogError(fmt, ...) LogPrint(LOG_ERROR, NULL, 0, fmt, ##__VA_ARGS__)
  #define LogWarn(fmt, ...)  LogPrint(LOG_WARN,  NULL, 0, fmt, ##__VA_ARGS__)
  #define LogInfo(fmt, ...)  LogPrint(LOG_INFO,  NULL, 0, fmt, ##__VA_ARGS__)
  #define LogDebug(fmt, ...) LogPrint(LOG_DEBUG, NULL, 0, fmt, ##__VA_ARGS__)
#endif

// Initialize logging subsystem (call from main)
void InitializeLogging(void);

#endif  // SM_LOGGING_H_
