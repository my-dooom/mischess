#ifndef GAME_H
#define GAME_H

#include "board.h"
#include <stdio.h>
#include <stdlib.h>

#define da_append(xs, x)                                                       \
    do {                                                                       \
        if ((xs).count == (xs).capacity) {                                     \
            (xs).capacity = (xs).capacity ? (xs).capacity * 2 : 4;             \
            (xs).pos = realloc((xs).pos, (xs).capacity * sizeof(*(xs).pos));   \
        }                                                                      \
        (xs).pos[(xs).count++] = (x);                                          \
    } while (0)

typedef struct {
    Vector2 *pos;
    size_t count;
    size_t capacity;
} possible_moves;

void check_possible_moves(piece board[8][8], board_pos pos,
                          possible_moves *moves);

int move_piece(piece board[8][8], board_pos src, board_pos dest);

int long_castle(piece board[8][8], color player_color);
int short_castle(piece board[8][8], color player_color);

void update_capture_matrices(piece board[8][8]);
#endif // GAME_H
