#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Tracks how many times each curated Dummy Mode game has been launched.
// This is NOT a real high-score system - most of these third-party games
// don't persist a score at all, and the few that do each use their own
// incompatible, unofficial save format that would be fragile to parse and
// could break on any game update. Play counts are something we can track
// ourselves reliably via the loader's own app-launch event.

#define GAME_STATS_COUNT 10

typedef struct {
    // Matches the loader's app "name" field for path-based menu tiles,
    // which is actually the FAP path, not the display name (see
    // loader_menu_apps_callback: name = menu_app->path ?: menu_app->name)
    const char* fap_path;
    const char* label;
} GameStatsEntry;

extern const GameStatsEntry game_stats_entries[GAME_STATS_COUNT];

/** Call with the loader's app "name" on every app launch. No-ops if it
 * doesn't match a tracked game. */
void game_stats_notify_launch(const char* app_name);

uint32_t game_stats_get_play_count(size_t index);

/** Number of distinct tracked games played at least once. */
size_t game_stats_get_distinct_played(void);

#ifdef __cplusplus
}
#endif
