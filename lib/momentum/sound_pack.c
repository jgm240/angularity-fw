#include "sound_pack.h"
#include "settings.h"

#include <furi.h>
#include <notification/notification_messages.h>

static void play(const NotificationSequence* sequence) {
    if(!sequence) return;
    NotificationApp* notification = furi_record_open(RECORD_NOTIFICATION);
    notification_message(notification, sequence);
    furi_record_close(RECORD_NOTIFICATION);
}

void sound_pack_play_lock(void) {
    const NotificationSequence* sequence = NULL;
    switch(momentum_settings.sound_pack) {
    case SoundPackClassic:
        sequence = &sequence_sound_pack_classic_lock;
        break;
    case SoundPackChime:
        sequence = &sequence_sound_pack_chime_lock;
        break;
    case SoundPackRetro:
        sequence = &sequence_sound_pack_retro_lock;
        break;
    case SoundPackMinimal:
        sequence = &sequence_sound_pack_minimal_lock;
        break;
    case SoundPackArcade:
        sequence = &sequence_sound_pack_arcade_lock;
        break;
    case SoundPackOff:
    default:
        break;
    }
    play(sequence);
}

void sound_pack_play_unlock(void) {
    const NotificationSequence* sequence = NULL;
    switch(momentum_settings.sound_pack) {
    case SoundPackClassic:
        sequence = &sequence_sound_pack_classic_unlock;
        break;
    case SoundPackChime:
        sequence = &sequence_sound_pack_chime_unlock;
        break;
    case SoundPackRetro:
        sequence = &sequence_sound_pack_retro_unlock;
        break;
    case SoundPackMinimal:
        sequence = &sequence_sound_pack_minimal_unlock;
        break;
    case SoundPackArcade:
        sequence = &sequence_sound_pack_arcade_unlock;
        break;
    case SoundPackOff:
    default:
        break;
    }
    play(sequence);
}

void sound_pack_play_error(void) {
    // Off still gets the generic error beep, matching pre-existing behavior
    const NotificationSequence* sequence = &sequence_error;
    switch(momentum_settings.sound_pack) {
    case SoundPackClassic:
        sequence = &sequence_sound_pack_classic_error;
        break;
    case SoundPackChime:
        sequence = &sequence_sound_pack_chime_error;
        break;
    case SoundPackRetro:
        sequence = &sequence_sound_pack_retro_error;
        break;
    case SoundPackMinimal:
        sequence = &sequence_sound_pack_minimal_error;
        break;
    case SoundPackArcade:
        sequence = &sequence_sound_pack_arcade_error;
        break;
    case SoundPackOff:
    default:
        break;
    }
    play(sequence);
}

void sound_pack_play_click(void) {
    const NotificationSequence* sequence = NULL;
    switch(momentum_settings.sound_pack) {
    case SoundPackClassic:
        sequence = &sequence_sound_pack_classic_click;
        break;
    case SoundPackChime:
        sequence = &sequence_sound_pack_chime_click;
        break;
    case SoundPackRetro:
        sequence = &sequence_sound_pack_retro_click;
        break;
    case SoundPackMinimal:
        sequence = &sequence_sound_pack_minimal_click;
        break;
    case SoundPackArcade:
        sequence = &sequence_sound_pack_arcade_click;
        break;
    case SoundPackOff:
    default:
        break;
    }
    play(sequence);
}

// Indexed [AppSound][SoundPack]; SoundPackOff entries are left NULL (play()
// already no-ops on NULL), so only Classic..Arcade need populating.
static const NotificationSequence* const app_open_sequences[APP_SOUND_COUNT][SoundPackCount] = {
    [AppSoundSettings] =
        {
            [SoundPackClassic] = &sequence_sound_pack_classic_app_open,
            [SoundPackChime] = &sequence_sound_pack_chime_app_open,
            [SoundPackRetro] = &sequence_sound_pack_retro_app_open,
            [SoundPackMinimal] = &sequence_sound_pack_minimal_app_open,
            [SoundPackArcade] = &sequence_sound_pack_arcade_app_open,
        },
    [AppSoundSubGhz] =
        {
            [SoundPackClassic] = &sequence_sound_pack_classic_app_subghz,
            [SoundPackChime] = &sequence_sound_pack_chime_app_subghz,
            [SoundPackRetro] = &sequence_sound_pack_retro_app_subghz,
            [SoundPackMinimal] = &sequence_sound_pack_minimal_app_subghz,
            [SoundPackArcade] = &sequence_sound_pack_arcade_app_subghz,
        },
    [AppSoundInfrared] =
        {
            [SoundPackClassic] = &sequence_sound_pack_classic_app_infrared,
            [SoundPackChime] = &sequence_sound_pack_chime_app_infrared,
            [SoundPackRetro] = &sequence_sound_pack_retro_app_infrared,
            [SoundPackMinimal] = &sequence_sound_pack_minimal_app_infrared,
            [SoundPackArcade] = &sequence_sound_pack_arcade_app_infrared,
        },
    [AppSoundIButton] =
        {
            [SoundPackClassic] = &sequence_sound_pack_classic_app_ibutton,
            [SoundPackChime] = &sequence_sound_pack_chime_app_ibutton,
            [SoundPackRetro] = &sequence_sound_pack_retro_app_ibutton,
            [SoundPackMinimal] = &sequence_sound_pack_minimal_app_ibutton,
            [SoundPackArcade] = &sequence_sound_pack_arcade_app_ibutton,
        },
    [AppSoundU2f] =
        {
            [SoundPackClassic] = &sequence_sound_pack_classic_app_u2f,
            [SoundPackChime] = &sequence_sound_pack_chime_app_u2f,
            [SoundPackRetro] = &sequence_sound_pack_retro_app_u2f,
            [SoundPackMinimal] = &sequence_sound_pack_minimal_app_u2f,
            [SoundPackArcade] = &sequence_sound_pack_arcade_app_u2f,
        },
    [AppSoundBadUsb] =
        {
            [SoundPackClassic] = &sequence_sound_pack_classic_app_badusb,
            [SoundPackChime] = &sequence_sound_pack_chime_app_badusb,
            [SoundPackRetro] = &sequence_sound_pack_retro_app_badusb,
            [SoundPackMinimal] = &sequence_sound_pack_minimal_app_badusb,
            [SoundPackArcade] = &sequence_sound_pack_arcade_app_badusb,
        },
    [AppSoundGpio] =
        {
            [SoundPackClassic] = &sequence_sound_pack_classic_app_gpio,
            [SoundPackChime] = &sequence_sound_pack_chime_app_gpio,
            [SoundPackRetro] = &sequence_sound_pack_retro_app_gpio,
            [SoundPackMinimal] = &sequence_sound_pack_minimal_app_gpio,
            [SoundPackArcade] = &sequence_sound_pack_arcade_app_gpio,
        },
    [AppSoundPassport] =
        {
            [SoundPackClassic] = &sequence_sound_pack_classic_app_passport,
            [SoundPackChime] = &sequence_sound_pack_chime_app_passport,
            [SoundPackRetro] = &sequence_sound_pack_retro_app_passport,
            [SoundPackMinimal] = &sequence_sound_pack_minimal_app_passport,
            [SoundPackArcade] = &sequence_sound_pack_arcade_app_passport,
        },
    [AppSoundClock] =
        {
            [SoundPackClassic] = &sequence_sound_pack_classic_app_clock,
            [SoundPackChime] = &sequence_sound_pack_chime_app_clock,
            [SoundPackRetro] = &sequence_sound_pack_retro_app_clock,
            [SoundPackMinimal] = &sequence_sound_pack_minimal_app_clock,
            [SoundPackArcade] = &sequence_sound_pack_arcade_app_clock,
        },
};

void sound_pack_play_app_open(AppSound app) {
    play(app_open_sequences[app][momentum_settings.sound_pack]);
}

void sound_pack_play_boot(void) {
    const NotificationSequence* sequence = NULL;
    switch(momentum_settings.sound_pack) {
    case SoundPackClassic:
        sequence = &sequence_sound_pack_classic_boot;
        break;
    case SoundPackChime:
        sequence = &sequence_sound_pack_chime_boot;
        break;
    case SoundPackRetro:
        sequence = &sequence_sound_pack_retro_boot;
        break;
    case SoundPackMinimal:
        sequence = &sequence_sound_pack_minimal_boot;
        break;
    case SoundPackArcade:
        sequence = &sequence_sound_pack_arcade_boot;
        break;
    case SoundPackOff:
    default:
        break;
    }
    play(sequence);
}

void sound_pack_play_shutdown_blocking(void) {
    const NotificationSequence* sequence = NULL;
    switch(momentum_settings.sound_pack) {
    case SoundPackClassic:
        sequence = &sequence_sound_pack_classic_shutdown;
        break;
    case SoundPackChime:
        sequence = &sequence_sound_pack_chime_shutdown;
        break;
    case SoundPackRetro:
        sequence = &sequence_sound_pack_retro_shutdown;
        break;
    case SoundPackMinimal:
        sequence = &sequence_sound_pack_minimal_shutdown;
        break;
    case SoundPackArcade:
        sequence = &sequence_sound_pack_arcade_shutdown;
        break;
    case SoundPackOff:
    default:
        break;
    }
    if(!sequence) return;
    NotificationApp* notification = furi_record_open(RECORD_NOTIFICATION);
    notification_message_block(notification, sequence);
    furi_record_close(RECORD_NOTIFICATION);
}
