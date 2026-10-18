// Choosing which game to play, when both are installed.
#pragma once
#include "game.h"
#include <stdbool.h>

// A window with a tile per game (games[0], games[1]); installed[i]: its
// files were found (a game without them can't be chosen). Returns the
// chosen profile, or NULL if the window was closed.
const game_t *picker_choose(const game_t *const *games, const bool *installed, int initial);
// Whether version v's boot ROM of game g is there (main.c).
bool picker_version_there(const game_t *g, int v);
