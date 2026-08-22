#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Central dispatch for every momentum_settings.sound_pack -driven cue, so
// callers (desktop, power, loader-triggered UI, gui input) don't each need
// their own copy of the pack switch statement.

void sound_pack_play_lock(void);
void sound_pack_play_unlock(void);
void sound_pack_play_error(void);

/** Short UI navigation click. No-op when sound_pack is Off. */
void sound_pack_play_click(void);

/** "Gears turning" cue for opening the Angularity Settings app. */
void sound_pack_play_app_open_settings(void);

void sound_pack_play_boot(void);

/** Blocks until playback finishes - call this right before the actual
 * power cutoff, since nothing runs after it. */
void sound_pack_play_shutdown_blocking(void);

#ifdef __cplusplus
}
#endif
