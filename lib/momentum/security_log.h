#pragma once

#include <furi.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SECURITY_LOG_PATH INT_PATH(".security_log.txt")

/** Append a timestamped line to the security log. Rotates the log if it
 * grows too large. Safe to call from any thread. */
void security_log_add(const char* message);

/** Read the full security log into a string, or a placeholder if empty. */
void security_log_read(FuriString* out);

#ifdef __cplusplus
}
#endif
