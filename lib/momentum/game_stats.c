#include "game_stats.h"

#include <furi.h>
#include <storage/storage.h>
#include <toolbox/stream/stream.h>
#include <toolbox/stream/file_stream.h>
#include <string.h>

#define GAME_STATS_PATH INT_PATH(".game_stats.txt")

const GameStatsEntry game_stats_entries[GAME_STATS_COUNT] = {
    {"/ext/apps/Games/snake20.fap", "Snake 2.0"},
    {"/ext/apps/Games/tetris.fap", "Tetris"},
    {"/ext/apps/Games/game_2048.fap", "2048"},
    {"/ext/apps/Games/flappy_bird.fap", "Flappy Bird"},
    {"/ext/apps/Games/arkanoid.fap", "Arkanoid"},
    {"/ext/apps/Games/flipper_pong.fap", "Pong"},
    {"/ext/apps/Games/tictactoe.fap", "Tic Tac Toe"},
    {"/ext/apps/Games/doom.fap", "DOOM"},
    {"/ext/apps/Games/chess.fap", "Chess"},
    {"/ext/apps/Games/solitaire.fap", "Solitaire"},
};

static uint32_t play_counts[GAME_STATS_COUNT];
static bool loaded = false;

static void game_stats_load(void) {
    for(size_t i = 0; i < GAME_STATS_COUNT; i++) {
        play_counts[i] = 0;
    }

    Storage* storage = furi_record_open(RECORD_STORAGE);
    Stream* stream = file_stream_alloc(storage);
    FuriString* line = furi_string_alloc();

    if(file_stream_open(stream, GAME_STATS_PATH, FSAM_READ, FSOM_OPEN_EXISTING)) {
        uint32_t version;
        if(stream_read_line(stream, line) &&
           sscanf(furi_string_get_cstr(line), "Game Stats Version %lu", &version) == 1 &&
           version == 1) {
            for(size_t i = 0; i < GAME_STATS_COUNT && stream_read_line(stream, line); i++) {
                unsigned count;
                if(sscanf(furi_string_get_cstr(line), "%u", &count) == 1) {
                    play_counts[i] = count;
                }
            }
        }
    }

    furi_string_free(line);
    file_stream_close(stream);
    stream_free(stream);
    furi_record_close(RECORD_STORAGE);
    loaded = true;
}

static void game_stats_save(void) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    Stream* stream = file_stream_alloc(storage);

    if(file_stream_open(stream, GAME_STATS_PATH, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
        FuriString* line = furi_string_alloc();
        furi_string_printf(line, "Game Stats Version 1\n");
        stream_write_string(stream, line);
        for(size_t i = 0; i < GAME_STATS_COUNT; i++) {
            furi_string_printf(line, "%lu\n", (unsigned long)play_counts[i]);
            stream_write_string(stream, line);
        }
        furi_string_free(line);
    }

    file_stream_close(stream);
    stream_free(stream);
    furi_record_close(RECORD_STORAGE);
}

void game_stats_notify_launch(const char* app_name) {
    if(!app_name) return;
    if(!loaded) game_stats_load();

    for(size_t i = 0; i < GAME_STATS_COUNT; i++) {
        if(strcmp(app_name, game_stats_entries[i].fap_path) == 0) {
            play_counts[i]++;
            game_stats_save();
            break;
        }
    }
}

uint32_t game_stats_get_play_count(size_t index) {
    if(!loaded) game_stats_load();
    if(index >= GAME_STATS_COUNT) return 0;
    return play_counts[index];
}

size_t game_stats_get_distinct_played(void) {
    if(!loaded) game_stats_load();
    size_t n = 0;
    for(size_t i = 0; i < GAME_STATS_COUNT; i++) {
        if(play_counts[i] > 0) n++;
    }
    return n;
}
