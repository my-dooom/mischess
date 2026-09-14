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

// Converts a space-separated UCI line ("e2e4 e7e5 g1f3") played from the
// given position into SAN ("e4 e5 Nf3"), at most max_moves moves. Works on
// copies; the live game and the move-generation context are left untouched.
void uci_line_to_san(piece board[8][8], const game_state *state,
                     const char *uci_line, int max_moves, char *out,
                     size_t cap);

// Same, from the live game position, with a small cache so callers may
// invoke it every frame.
void uci_line_to_san_current(const char *uci_line, int max_moves, char *out,
                             size_t cap);

#endif // NOTATION_H
