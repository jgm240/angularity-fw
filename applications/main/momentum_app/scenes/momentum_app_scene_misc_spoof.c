#include "../momentum_app.h"
#include <stdlib.h>
#include <string.h>

enum VarItemListIndex {
    VarItemListIndexFlipperName, // TODO: Split into name, mac, serial
    VarItemListIndexShellColor,
    VarItemListIndexGenerateName,
};

// Invented, name-like words (4-6 letters, well under the 8-char device
// name cap) matching the style of real Flipper factory names like
// "Umbrit", "Riho", "Ellis" - a single word, not a word+word blend
static const char* const leet_name_words[] = {
    "RIHO",   "ELLIS",  "UMBRIT", "KOBALT", "VESTA",  "NAXOR", "SOLTAK",
    "MERIDO", "TAVERN", "ZERION", "DOSKIN", "KIRABO", "NOVARI", "TASKEN",
    "VIRELO", "OSKAR",
};

static char momentum_app_leet_substitute(char c) {
    switch(c) {
    case 'A':
        return '4';
    case 'E':
        return '3';
    case 'I':
        return '1';
    case 'O':
        return '0';
    case 'S':
        return '5';
    case 'T':
        return '7';
    default:
        return 0;
    }
}

static void momentum_app_generate_leet_name(char* out, size_t max_len) {
    if(max_len == 0) {
        return;
    }

    const char* word = leet_name_words[rand() % COUNT_OF(leet_name_words)];
    size_t len = strlen(word);
    if(len > max_len - 1) {
        len = max_len - 1;
    }

    bool substituted = false;
    int last_eligible = -1;

    // Write directly into the caller's buffer (already bounds-checked
    // against max_len above) rather than through an intermediate fixed-size
    // buffer + snprintf("%s", ...), which gcc's format-truncation check
    // can't prove is safe since it can't see the runtime length bound
    for(size_t i = 0; i < len; i++) {
        char c = word[i];
        char sub = momentum_app_leet_substitute(c);
        if(sub) {
            last_eligible = (int)i;
            // ~45% chance per eligible letter, so most names get more than
            // one substitution without ending up all-digits
            if(rand() % 100 < 45) {
                c = sub;
                substituted = true;
            }
        }
        out[i] = c;
    }
    out[len] = '\0';

    // Guarantee at least one digit, since that's the whole point of asking
    // for this over the plain word - unlike real factory names (e.g. "Riho"),
    // which sometimes have none
    if(!substituted && last_eligible >= 0) {
        out[last_eligible] = momentum_app_leet_substitute(word[last_eligible]);
    }
}

const char* const shell_color_names[FuriHalVersionColorCount] = {
    "Real",
    "Black",
    "White",
    "Transparent",
};
static void momentum_app_scene_misc_spoof_shell_color_changed(VariableItem* item) {
    MomentumApp* app = variable_item_get_context(item);
    uint8_t index = variable_item_get_current_value_index(item);
    variable_item_set_current_value_text(item, shell_color_names[index]);
    momentum_settings.spoof_color = index;
    app->save_settings = true;
    app->require_reboot = true;
}

void momentum_app_scene_misc_spoof_var_item_list_callback(void* context, uint32_t index) {
    MomentumApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

void momentum_app_scene_misc_spoof_on_enter(void* context) {
    MomentumApp* app = context;
    VariableItemList* var_item_list = app->var_item_list;
    VariableItem* item;

    item = variable_item_list_add(var_item_list, "Flipper Name", 0, NULL, app);
    variable_item_set_current_value_text(item, app->device_name);

    item = variable_item_list_add(
        var_item_list,
        "Shell Color",
        FuriHalVersionColorCount,
        momentum_app_scene_misc_spoof_shell_color_changed,
        app);
    variable_item_set_current_value_index(item, momentum_settings.spoof_color);
    variable_item_set_current_value_text(item, shell_color_names[momentum_settings.spoof_color]);

    item = variable_item_list_add(var_item_list, "Generate Random Name", 0, NULL, app);
    variable_item_set_current_value_text(item, ">");

    variable_item_list_set_enter_callback(
        var_item_list, momentum_app_scene_misc_spoof_var_item_list_callback, app);

    variable_item_list_set_selected_item(
        var_item_list,
        scene_manager_get_scene_state(app->scene_manager, MomentumAppSceneMiscSpoof));

    view_dispatcher_switch_to_view(app->view_dispatcher, MomentumAppViewVarItemList);
}

bool momentum_app_scene_misc_spoof_on_event(void* context, SceneManagerEvent event) {
    MomentumApp* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        scene_manager_set_scene_state(app->scene_manager, MomentumAppSceneMiscSpoof, event.event);
        consumed = true;
        switch(event.event) {
        case VarItemListIndexFlipperName:
            scene_manager_next_scene(app->scene_manager, MomentumAppSceneMiscSpoofName);
            break;
        case VarItemListIndexGenerateName: {
            momentum_app_generate_leet_name(app->device_name, FURI_HAL_VERSION_ARRAY_NAME_LENGTH);
            VariableItem* name_item =
                variable_item_list_get(app->var_item_list, VarItemListIndexFlipperName);
            variable_item_set_current_value_text(name_item, app->device_name);
            app->save_name = true;
            app->require_reboot = true;
            break;
        }
        default:
            break;
        }
    }

    return consumed;
}

void momentum_app_scene_misc_spoof_on_exit(void* context) {
    MomentumApp* app = context;
    variable_item_list_reset(app->var_item_list);
}
