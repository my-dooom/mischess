#include "game.h"
#include "raylib.h"

game_state game = {0};

void init_game_state(game_state *state) {
    state->turn = false;
    state->current_selection = NULL_POS;
    state->possible_moves.pos = NULL;
    state->possible_moves.count = 0;
    state->possible_moves.capacity = 0;
    state->en_passant_square = NULL_POS;
    state->can_castle_short[White] = true;
    state->can_castle_short[Black] = true;
    state->can_castle_long[White] = true;
    state->can_castle_long[Black] = true;
    state->move_count = 0;
    state->halfmove_clock = 0;
    state->game_over = false;
}

static void generate_pawn_moves(piece board[8][8], board_pos pos,
                                possible_moves *moves) {
    piece p = board[pos.row][pos.col];
    int dir = (p.color == White) ? -1 : 1;
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

    // diagonal captures / defences
    int capture_cols[] = {pos.col - 1, pos.col + 1};
    for (int i = 0; i < 2; i++) {
        int c = capture_cols[i];
        if (next_row >= 0 && next_row < 8 && c >= 0 && c < 8 &&
            board[next_row][c].type != EMPTY) {
            if (board[next_row][c].color != p.color) {
                da_append(*moves, ((Vector2){c, next_row}));
                if (board[next_row][c].type == KING) {
                    TraceLog(LOG_INFO, "King in check at position: %c%d",
                             'A' + c, next_row + 1);
                }
            }
        }
    }
    // en passant capture
    for (int i = 0; i < 2; i++) {
        int c = capture_cols[i];
        if (c >= 0 && c < 8 && next_row == game.en_passant_square.row &&
            c == game.en_passant_square.col) {
            da_append(*moves, ((Vector2){c, next_row}));
        }
    }
}

static void generate_rook_moves(piece board[8][8], board_pos pos,
                                possible_moves *moves) {
    piece p = board[pos.row][pos.col];
    int dirs[4][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};
    for (int d = 0; d < 4; d++) {
        int dr = dirs[d][0], dc = dirs[d][1];
        int r = pos.row + dr, c = pos.col + dc;
        while (r >= 0 && r < 8 && c >= 0 && c < 8) {
            if (board[r][c].type == EMPTY) {
                da_append(*moves, ((Vector2){c, r}));
            } else {
                if (board[r][c].color != p.color) {
                    da_append(*moves, ((Vector2){c, r})); // capture
                    if (board[r][c].type == KING) {
                        TraceLog(LOG_INFO, "King in check at position: %c%d",
                                 'A' + c, r + 1);
                    }
                }
                break; // blocked either way
            }
            r += dr;
            c += dc;
        }
    }
}

static void generate_bishop_moves(piece board[8][8], board_pos pos,
                                  possible_moves *moves) {
    piece p = board[pos.row][pos.col];
    int dirs[4][2] = {{-1, 1}, {1, 1}, {1, -1}, {-1, -1}};
    for (int d = 0; d < 4; d++) {
        int dr = dirs[d][0], dc = dirs[d][1];
        int r = pos.row + dr, c = pos.col + dc;
        while (r >= 0 && r < 8 && c >= 0 && c < 8) {
            if (board[r][c].type == EMPTY) {
                da_append(*moves, ((Vector2){c, r}));
            } else {
                if (board[r][c].color != p.color) {
                    da_append(*moves, ((Vector2){c, r})); // capture
                    if (board[r][c].type == KING) {
                        static possible_moves capture_positions = {0};
                        TraceLog(LOG_INFO, "King in check at position: %c%d",
                                 'A' + c, r + 1);
                    }
                }
                break; // blocked either way
            }
            r += dr;
            c += dc;
        }
    }
}

static void generate_queen_moves(piece board[8][8], board_pos pos,
                                 possible_moves *moves) {
    piece p = board[pos.row][pos.col];
    int dirs[8][2] = {{-1, 1}, {1, 1}, {1, -1}, {-1, -1},
                      {-1, 0}, {1, 0}, {0, -1}, {0, 1}};
    for (int d = 0; d < 8; d++) {
        int dr = dirs[d][0], dc = dirs[d][1];
        int r = pos.row + dr, c = pos.col + dc;
        while (r >= 0 && r < 8 && c >= 0 && c < 8) {
            if (board[r][c].type == EMPTY) {
                da_append(*moves, ((Vector2){c, r}));
            } else {
                if (board[r][c].color != p.color) {
                    da_append(*moves, ((Vector2){c, r})); // capture
                    if (board[r][c].type == KING) {
                        TraceLog(LOG_INFO, "King in check at position: %c%d",
                                 'A' + c, r + 1);
                    }
                }
                break; // blocked either way
            }
            r += dr;
            c += dc;
        }
    }
}

static void generate_king_moves(piece board[8][8], board_pos pos,
                                possible_moves *moves) {
    piece p = board[pos.row][pos.col];
    color opp = (p.color == White) ? Black : White;
    int dirs[8][2] = {{-1, 1}, {1, 1}, {1, -1}, {-1, -1},
                      {-1, 0}, {1, 0}, {0, -1}, {0, 1}};
    for (int d = 0; d < 8; d++) {
        int r = pos.row + dirs[d][0], c = pos.col + dirs[d][1];
        if (r >= 0 && r < 8 && c >= 0 && c < 8) {
            if ((board[r][c].type == EMPTY || board[r][c].color != p.color) &&
                !board[r][c].attacked_by[opp]) {
                da_append(*moves, ((Vector2){c, r}));
            }
        }
    }
    // Castling
    if (!p.has_moved) {
        int row = pos.row;
        // Short castle — king must not be in check, pass through, or land on
        // an attacked square (cols 4, 5, 6)
        if (game.can_castle_short[p.color] && board[row][7].type == ROOK &&
            !board[row][7].has_moved &&
            board[row][5].type == EMPTY && board[row][6].type == EMPTY &&
            !board[row][4].attacked_by[opp] &&
            !board[row][5].attacked_by[opp] &&
            !board[row][6].attacked_by[opp]) {
            da_append(*moves, ((Vector2){6, row}));
        }
        // Long castle — king must not be in check, pass through, or land on
        // an attacked square (cols 4, 3, 2)
        if (game.can_castle_long[p.color] && board[row][0].type == ROOK &&
            !board[row][0].has_moved &&
            board[row][1].type == EMPTY && board[row][2].type == EMPTY &&
            board[row][3].type == EMPTY && !board[row][4].attacked_by[opp] &&
            !board[row][3].attacked_by[opp] &&
            !board[row][2].attacked_by[opp]) {
            da_append(*moves, ((Vector2){2, row}));
        }
    }
}

static void generate_knight_moves(piece board[8][8], board_pos pos,
                                  possible_moves *moves) {
    piece p = board[pos.row][pos.col];
    int jumps[8][2] = {{-2, -1}, {-2, 1}, {-1, -2}, {-1, 2},
                       {1, -2},  {1, 2},  {2, -1},  {2, 1}};
    for (int i = 0; i < 8; i++) {
        int r = pos.row + jumps[i][0];
        int c = pos.col + jumps[i][1];
        if (r >= 0 && r < 8 && c >= 0 && c < 8) {
            if (board[r][c].type == EMPTY || board[r][c].color != p.color) {
                da_append(*moves, ((Vector2){c, r}));
            }
        }
    }
}

void check_possible_moves(piece board[8][8], board_pos pos,
                          possible_moves *moves) {
    switch (board[pos.row][pos.col].type) {
    case PAWN:
        generate_pawn_moves(board, pos, moves);
        break;
    case ROOK:
        generate_rook_moves(board, pos, moves);
        break;
    case BISHOP:
        generate_bishop_moves(board, pos, moves);
        break;
    case QUEEN:
        generate_queen_moves(board, pos, moves);
        break;
    case KING:
        generate_king_moves(board, pos, moves);
        break;
    case KNIGHT:
        generate_knight_moves(board, pos, moves);
        break;
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

void update_capture_matrices(piece board[8][8]) {
    // Step 1: reset everything on every square
    for (int row = 0; row < 8; row++) {
        for (int col = 0; col < 8; col++) {
            piece *square = &board[row][col];
            square->attacked_by[White] = false;
            square->attacked_by[Black] = false;
            for (int i = 0; i < 8; i++)
                for (int j = 0; j < 8; j++) {
                }
        }
    }

    possible_moves piece_moves = {0};

    // Step 2: for every non-king piece, find its reachable squares and mark
    //         them as attacked/defended by that piece's color
    for (int row = 0; row < 8; row++) {
        for (int col = 0; col < 8; col++) {
            piece *attacker = &board[row][col];
            if (attacker->type == EMPTY || attacker->type == KING)
                continue;

            piece_moves.count = 0;
            check_possible_moves(board, (board_pos){row, col}, &piece_moves);

            for (size_t i = 0; i < piece_moves.count; i++) {
                int target_row = (int)piece_moves.pos[i].y;
                int target_col = (int)piece_moves.pos[i].x;
                board[target_row][target_col].attacked_by[attacker->color] =
                    true;
            }
        }
    }

    // Step 3: mark the 8 squares around each king as attacked by that king's
    //         color (kings are excluded from step 2 to avoid a circular
    //         dependency — kings read attacked_by to filter their own moves)
    int neighbor_dirs[8][2] = {{-1, 1}, {1, 1}, {1, -1}, {-1, -1},
                               {-1, 0}, {1, 0}, {0, -1}, {0, 1}};
    for (int row = 0; row < 8; row++) {
        for (int col = 0; col < 8; col++) {
            if (board[row][col].type != KING)
                continue;
            color king_color = board[row][col].color;
            for (int d = 0; d < 8; d++) {
                int neighbor_row = row + neighbor_dirs[d][0];
                int neighbor_col = col + neighbor_dirs[d][1];
                if (neighbor_row >= 0 && neighbor_row < 8 &&
                    neighbor_col >= 0 && neighbor_col < 8) {
                    board[neighbor_row][neighbor_col].attacked_by[king_color] =
                        true;
                }
            }
        }
    }

    // Step 4: now that attacked_by is fully populated, generate king moves
    //         (the king uses attacked_by to skip squares it cannot safely
    //         enter)
    for (int row = 0; row < 8; row++) {
        for (int col = 0; col < 8; col++) {
            if (board[row][col].type == KING) {
                piece_moves.count = 0;
                check_possible_moves(board, (board_pos){row, col},
                                     &piece_moves);
            }
        }
    }

    free(piece_moves.pos);
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
