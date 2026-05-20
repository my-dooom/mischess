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
} piece;

extern piece board[8][8];
extern color current_turn;
extern board_pos selected;
extern board_pos en_passant_square;

void initialize_board(piece (*board)[8]);

#endif // BOARD_H
