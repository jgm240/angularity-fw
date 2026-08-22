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

typedef enum {
    AppSoundSettings, // Gears turning
    AppSoundSubGhz, // Morse-style radio beep
    AppSoundInfrared, // Descending laser zap
    AppSoundIButton, // Big-button thunk
    AppSoundU2f, // Lock opening
    AppSoundBadUsb, // Rapid typing blips
    AppSoundGpio, // Jumper pins snapping into a header
    AppSoundPassport, // Book pages fluttering
    AppSoundClock, // Steady tick-tock
} AppSound;

#define APP_SOUND_COUNT (AppSoundClock + 1)

/** Contextual "opening this app" cue, matched by the main menu's app name. */
void sound_pack_play_app_open(AppSound app);

void sound_pack_play_boot(void);

/** Blocks until playback finishes - call this right before the actual
 * power cutoff, since nothing runs after it. */
void sound_pack_play_shutdown_blocking(void);

#ifdef __cplusplus
}
#endif
