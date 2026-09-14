#ifndef NOTATION_H
#define NOTATION_H

#include "board.h"
#include "game.h"

// Writes the SAN for a move *before* it is applied (no check/mate suffix).
// `buf` must hold at least SAN_MAX bytes.
void move_to_san(piece board[8][8], board_pos src, board_pos dest,
                 piece_type promotion, char *buf);

// Writes "<placement> <turn> <castling> <ep>" -- everything that decides
// whether two positions are the same for the repetition rule.
void position_key(piece board[8][8], const game_state *state, char *buf,
                  size_t buf_size);

#endif // NOTATION_H
