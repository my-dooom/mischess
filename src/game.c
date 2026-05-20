#include "game.h"
#include "raylib.h"

void check_possible_moves(piece board[8][8], board_pos pos,
                          possible_moves *moves) {
    piece p = board[pos.row][pos.col];
    int dir = (p.color == White) ? -1 : 1;
    switch (p.type) {
    case PAWN: {

        int next_row = pos.row + dir;
        if (next_row >= 0 && next_row < 8 &&
            board[next_row][pos.col].type == EMPTY) {
            da_append(*moves, ((Vector2){pos.col, next_row}));
            if (p.has_moved == false) {
                int double_row = pos.row + 2 * dir;
                if (double_row >= 0 && double_row < 8 &&
                    board[double_row][pos.col].type == EMPTY) {
                    da_append(*moves, ((Vector2){pos.col, double_row}));
                }
            }
        }
        // diagonal captures
        int capture_cols[] = {pos.col - 1, pos.col + 1};
        for (int i = 0; i < 2; i++) {
            int c = capture_cols[i];
            if (next_row >= 0 && next_row < 8 && c >= 0 && c < 8 &&
                board[next_row][c].type != EMPTY &&
                board[next_row][c].color != p.color) {
                da_append(*moves, ((Vector2){c, next_row}));
            }
        }
        // en passant capture
        for (int i = 0; i < 2; i++) {
            int c = capture_cols[i];
            if (c >= 0 && c < 8 && next_row == en_passant_square.row &&
                c == en_passant_square.col) {
                da_append(*moves, ((Vector2){c, next_row}));
            }
        }
        break;
    }
    case ROOK: {
        int dirs[4][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};
        for (int d = 0; d < 4; d++) {
            int dr = dirs[d][0], dc = dirs[d][1];
            int r = pos.row + dr, c = pos.col + dc;
            while (r >= 0 && r < 8 && c >= 0 && c < 8) {
                if (board[r][c].type == EMPTY) {
                    da_append(*moves, ((Vector2){c, r}));
                } else {
                    if (board[r][c].color != p.color)
                        da_append(*moves, ((Vector2){c, r})); // capture
                    break; // blocked either way
                }
                r += dr;
                c += dc;
            }
        }
        break;
    }
    case BISHOP: {
        int dirs[4][2] = {{-1, 1}, {1, 1}, {1, -1}, {-1, -1}};
        for (int d = 0; d < 4; d++) {
            int dr = dirs[d][0], dc = dirs[d][1];
            int r = pos.row + dr, c = pos.col + dc;
            while (r >= 0 && r < 8 && c >= 0 && c < 8) {
                if (board[r][c].type == EMPTY) {
                    da_append(*moves, ((Vector2){c, r}));
                } else {
                    if (board[r][c].color != p.color)
                        da_append(*moves, ((Vector2){c, r})); // capture
                    break; // blocked either way
                }
                r += dr;
                c += dc;
            }
        }
        break;
    }

    case QUEEN: {
        int dirs[8][2] = {{-1, 1}, {1, 1}, {1, -1}, {-1, -1},
                          {-1, 0}, {1, 0}, {0, -1}, {0, 1}};
        for (int d = 0; d < 8; d++) {
            int dr = dirs[d][0], dc = dirs[d][1];
            int r = pos.row + dr, c = pos.col + dc;
            while (r >= 0 && r < 8 && c >= 0 && c < 8) {
                if (board[r][c].type == EMPTY) {
                    da_append(*moves, ((Vector2){c, r}));
                } else {
                    if (board[r][c].color != p.color)
                        da_append(*moves, ((Vector2){c, r})); // capture
                    break; // blocked either way
                }
                r += dr;
                c += dc;
            }
        }
        break;
    }
    case KING: {
        int dirs[8][2] = {{-1, 1}, {1, 1}, {1, -1}, {-1, -1},
                          {-1, 0}, {1, 0}, {0, -1}, {0, 1}};
        for (int d = 0; d < 8; d++) {
            int r = pos.row + dirs[d][0], c = pos.col + dirs[d][1];
            if (r >= 0 && r < 8 && c >= 0 && c < 8 &&
                (board[r][c].type == EMPTY || board[r][c].color != p.color)) {
                da_append(*moves, ((Vector2){c, r}));
            }
        }
        // Castling
        if (!p.has_moved) {
            int row = pos.row;
            // Short castle
            if (board[row][7].type == ROOK && !board[row][7].has_moved &&
                board[row][5].type == EMPTY && board[row][6].type == EMPTY) {
                da_append(*moves, ((Vector2){6, row}));
            }
            // Long castle
            if (board[row][0].type == ROOK && !board[row][0].has_moved &&
                board[row][1].type == EMPTY && board[row][2].type == EMPTY &&
                board[row][3].type == EMPTY) {
                da_append(*moves, ((Vector2){2, row}));
            }
        }
        break;
    }
    case KNIGHT: {
        int jumps[8][2] = {{-2, -1}, {-2, 1}, {-1, -2}, {-1, 2},
                           {1, -2},  {1, 2},  {2, -1},  {2, 1}};
        for (int i = 0; i < 8; i++) {
            int r = pos.row + jumps[i][0];
            int c = pos.col + jumps[i][1];
            if (r >= 0 && r < 8 && c >= 0 && c < 8 &&
                (board[r][c].type == EMPTY || board[r][c].color != p.color)) {
                da_append(*moves, ((Vector2){c, r}));
            }
        }
        break;
    }

    default:
        break;
    }
}

int move_piece(piece board[8][8], board_pos src, board_pos dest) {
    // En passant: pawn moves diagonally to an empty square
    if (board[src.row][src.col].type == PAWN && src.col != dest.col &&
        board[dest.row][dest.col].type == EMPTY) {
        board[src.row][dest.col].type = EMPTY;
    }
    board[dest.row][dest.col] = board[src.row][src.col];
    board[src.row][src.col].type = EMPTY;
    board[dest.row][dest.col].has_moved = true;
    return 1;
}

int long_castle(piece board[8][8], color player_color) {
    int row = (player_color == White) ? 7 : 0;
    // check if sqaures between king and rook are empty
    if (board[row][1].type != EMPTY || board[row][2].type != EMPTY ||
        board[row][3].type != EMPTY) {
        return 0;
    }

    // Move king
    board[row][2] = board[row][4];
    board[row][2].has_moved = true;
    board[row][4].type = EMPTY;
    // Move rook
    board[row][3] = board[row][0];
    board[row][3].has_moved = true;
    board[row][0].type = EMPTY;
    return 1;
}
int short_castle(piece board[8][8], color player_color) {
    int row = (player_color == White) ? 7 : 0;
    // Check if squares between king and rook are empty
    if (board[row][5].type != EMPTY || board[row][6].type != EMPTY) {
        return 0;
    }
    // Move king
    board[row][6] = board[row][4];
    board[row][6].has_moved = true;
    board[row][4].type = EMPTY;
    // Move rook
    board[row][5] = board[row][7];
    board[row][5].has_moved = true;
    board[row][7].type = EMPTY;
    return 1;
}
