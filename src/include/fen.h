#ifndef FEN_H
#define FEN_H
#include "board.h"
#include "game.h"
#include <stdio.h>
// full FEN string is at most ~90 chars for a standard position
#define LONGEST_FEN 128

extern char fen_table[LONGEST_FEN];

int update_fen_table(piece board[8][8]);
int update_full_fen(piece board[8][8], const game_state *state);
int load_fen(const char *fen, piece board[8][8], game_state *state);

static inline void print_fen() { printf("FEN: %s\n", fen_table); }

#endif // FEN_H
