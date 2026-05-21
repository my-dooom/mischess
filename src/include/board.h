#ifndef BOARD_H
#define BOARD_H

#include "raylib.h"

//------------------------------------------------------------------------------------
// DATA STRUCTURES
//------------------------------------------------------------------------------------
//// in board.h
typedef struct {
    int row;
    int col;
} board_pos;

typedef enum {
    EMPTY,
    PAWN,
    KNIGHT,
    BISHOP,
    ROOK,
    QUEEN,
    KING,
} piece_type;

typedef enum {
    White,
    Black,
} color;

typedef struct {
    piece_type type;
    color color;
    bool has_moved; // Indicates if the piece has moved, relevant for castling
                    // and pawn first move
    char capture_matrix[8][8]; // [r][c] = 1 if enemy at (r,c) can capture this piece
    char defence_matrix[8][8]; // [r][c] = 1 if friendly at (r,c) defends this piece
} piece;

extern piece board[8][8];
extern color current_turn;
extern board_pos selected;
extern board_pos en_passant_square;
extern bool is_in_check[2]; // is_in_check[White] and is_in_check[Black]

void initialize_board(piece (*board)[8]);

#endif // BOARD_H
