#ifndef COACH_RENDER_H
#define COACH_RENDER_H

#include "coach.h"
#include "game.h"

// arrows for hint / candidate moves / threat, drawn over the board
void draw_coach_overlay(float scale, const coach *c, const game_state *state);

// the coach block of the side panel; returns the y below the drawn content
int draw_coach_panel(int x0, int y0, int w, int h, const coach *c,
                     const game_state *state);

// key legend pinned to the bottom of the panel
void draw_key_legend(int x0, int y_bottom, int w, int h);

#endif // COACH_RENDER_H
