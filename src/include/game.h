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

typedef struct {
    bool turn;
    board_pos current_selection;
    possible_moves possible_moves;
    board_pos en_passant_square;
    bool can_castle_short[2];
    bool can_castle_long[2];
    size_t move_count;
    size_t halfmove_clock;
    bool game_over;

} game_state;

#define NULL_POS ((board_pos){-1, -1})

static inline color turn_to_color(bool turn) { return turn ? Black : White; }

extern game_state game;

void init_game_state(game_state *state);

void check_possible_moves(piece board[8][8], board_pos pos,
                          possible_moves *moves);
void generate_legal_moves(piece board[8][8], board_pos pos,
                          possible_moves *moves);

int move_piece(piece board[8][8], board_pos src, board_pos dest);

int long_castle(piece board[8][8], color player_color);
int short_castle(piece board[8][8], color player_color);

void update_capture_matrices(piece board[8][8]);
#endif // GAME_H
