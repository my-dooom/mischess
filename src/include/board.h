#ifndef BOARD_H
#define BOARD_H

#include "raylib.h" //------------------------------------------------------------------------------------
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
    bool attacked_by[2]; // attacked_by[White/Black] = true if any piece of
                         // that color can reach this square
} piece;

extern piece board[8][8];

void initialize_board(piece (*board)[8]);

#endif // BOARD_H
