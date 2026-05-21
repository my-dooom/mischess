#include "board.h"

piece board[8][8];
color current_turn = White;
board_pos selected = {-1, -1};
board_pos en_passant_square = {-1, -1};

void initialze_capture_matrices() {
    for (int row = 0; row < 8; row++) {
        for (int col = 0; col < 8; col++) {
            for (int r = 0; r < 8; r++) {
                for (int c = 0; c < 8; c++) {
                    board[row][col].capture_matrix[r][c] = 0;
                    board[row][col].defence_matrix[r][c] = 0;
                }
            }
        }
    }
}

void initialize_board(piece (*board)[8]) {

    initialze_capture_matrices();
    // Initialize all squares to EMPTY
    for (int i = 0; i < 8; i++) {
        for (int j = 0; j < 8; j++) {
            board[i][j].type = EMPTY;
            board[i][j].color = White; // Default color, can be changed later
        }
    }

    // Place pawns
    for (int j = 0; j < 8; j++) {
        board[1][j].type = PAWN;
        board[1][j].has_moved = false;
        board[1][j].color = Black;
        board[6][j].type = PAWN;
        board[6][j].has_moved = false;
        board[6][j].color = White;
    }

    // Place other pieces
    piece_type pieces[] = {ROOK, KNIGHT, BISHOP, QUEEN,
                           KING, BISHOP, KNIGHT, ROOK};
    for (int j = 0; j < 8; j++) {
        board[0][j].type = pieces[j];
        board[0][j].color = Black;
        board[0][j].has_moved = false;
        board[7][j].type = pieces[j];
        board[7][j].color = White;
        board[7][j].has_moved = false;
    }
}
