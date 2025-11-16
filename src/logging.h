/**
 * @file logging.h
 * @brief Logging framework with level-based filtering and TTY color support
 *
 * Provides structured logging with environment variable configuration:
 * - SM_LOG_LEVEL: Set minimum log level (ERROR/WARN/INFO/DEBUG)
 * - Automatic TTY detection for colored output
 * - Debug builds include file:line information
 *
 * @example
 *   InitializeLogging();  // Call once at startup
 *   LogError("Failed to load ROM: %s", filename);
 *   LogWarn("Using fallback renderer");
 *   LogInfo("Window created: %dx%d", width, height);
 *   LogDebug("Frame %d: PPU state = 0x%02x", frame, ppu_state);
 */
#ifndef SM_LOGGING_H_
#define SM_LOGGING_H_

#include <stdarg.h>

/**
 * Log severity levels (ordered by severity, lower = more severe)
 */
typedef enum {
  LOG_ERROR = 0,  /**< Critical errors - always shown, cannot be filtered */
  LOG_WARN  = 1,  /**< Warnings - default minimum level */
  LOG_INFO  = 2,  /**< Informational messages - enabled with verbose mode */
  LOG_DEBUG = 3   /**< Debug messages - verbose internal state */
} LogLevel;

/**
 * Set minimum log level for filtering
 * @param level Messages below this level will be suppressed
 * @note Typically configured via SM_LOG_LEVEL environment variable
 */
void SetLogLevel(LogLevel level);

/**
 * Get current minimum log level
 * @return Current log level threshold
 */
LogLevel GetLogLevel(void);

/**
 * Core logging function with printf-style formatting
 * @param level Severity level of this message
 * @param file Source file name (NULL to omit, __FILE__ in debug builds)
 * @param line Source line number (0 to omit, __LINE__ in debug builds)
 * @param fmt Printf-style format string
 * @note Prefer using LogError/LogWarn/LogInfo/LogDebug macros
 */
void LogPrint(LogLevel level, const char *file, int line, const char *fmt, ...);

/**
 * Core logging function with va_list arguments
 * @param level Severity level
 * @param file Source file name (NULL to omit)
 * @param line Source line number (0 to omit)
 * @param fmt Printf-style format string
 * @param args Variable argument list
 */
void LogPrintV(LogLevel level, const char *file, int line, const char *fmt, va_list args);

/**
 * Convenience logging macros with automatic file/line tracking
 * In debug builds, includes source location
 * In release builds, omits location for smaller code size
 */
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

/**
 * Initialize logging subsystem
 * - Reads SM_LOG_LEVEL environment variable
 * - Detects TTY for color support
 * - Must be called once at program startup
 */
void InitializeLogging(void);

#endif  // SM_LOGGING_H_
