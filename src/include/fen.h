#ifndef FEN_H
#define FEN_H
#include "board.h"
#include <stdio.h>
#define LONGEST_FEN 90

extern char fen_table[LONGEST_FEN];

int update_fen_table(piece board[8][8]);

static inline void print_fen() { printf("FEN: %s\n", fen_table); }

#endif // FEN_H
