#include "notation.h"
#include "fen.h"
#include <string.h>

static char piece_letter(piece_type t) {
    switch (t) {
    case KNIGHT: return 'N';
    case BISHOP: return 'B';
    case ROOK:   return 'R';
    case QUEEN:  return 'Q';
    case KING:   return 'K';
    default:     return '\0';
    }
}

// true if the piece on `from` has `dest` among its legal moves
static bool can_reach(piece board[8][8], board_pos from, board_pos dest) {
    possible_moves moves = {0};
    generate_legal_moves(board, from, &moves);
    bool found = false;
    for (size_t i = 0; i < moves.count; i++) {
        if ((int)moves.pos[i].y == dest.row && (int)moves.pos[i].x == dest.col) {
            found = true;
            break;
        }
    }
    free(moves.pos);
    return found;
}

void move_to_san(piece board[8][8], board_pos src, board_pos dest,
                 piece_type promotion, char *buf) {
    piece p = board[src.row][src.col];
    int n = 0;

    if (p.type == KING && abs(dest.col - src.col) == 2) {
        strcpy(buf, dest.col == 6 ? "O-O" : "O-O-O");
        return;
    }

    bool is_capture = board[dest.row][dest.col].type != EMPTY ||
                      (p.type == PAWN && src.col != dest.col);

    if (p.type == PAWN) {
        if (is_capture)
            buf[n++] = 'a' + src.col;
    } else {
        buf[n++] = piece_letter(p.type);

        // disambiguate against other pieces of the same type and color that
        // could also reach dest
        bool need_file = false, need_rank = false, ambiguous = false;
        for (int r = 0; r < 8; r++) {
            for (int c = 0; c < 8; c++) {
                if ((r == src.row && c == src.col) ||
                    board[r][c].type != p.type || board[r][c].color != p.color)
                    continue;
                if (!can_reach(board, (board_pos){r, c}, dest))
                    continue;
                ambiguous = true;
                if (c == src.col)
                    need_rank = true;
                else
                    need_file = true;
            }
        }
        if (ambiguous) {
            // file is preferred; rank only when the file does not settle it
            if (need_file && need_rank) {
                buf[n++] = 'a' + src.col;
                buf[n++] = '0' + (8 - src.row);
            } else if (need_rank) {
                buf[n++] = '0' + (8 - src.row);
            } else {
                buf[n++] = 'a' + src.col;
            }
        }
    }

    if (is_capture)
        buf[n++] = 'x';
    buf[n++] = 'a' + dest.col;
    buf[n++] = '0' + (8 - dest.row);

    if (p.type == PAWN && (dest.row == 0 || dest.row == 7)) {
        buf[n++] = '=';
        buf[n++] = piece_letter(promotion == EMPTY ? QUEEN : promotion);
    }
    buf[n] = '\0';
}

void position_key(piece board[8][8], const game_state *state, char *buf,
                  size_t buf_size) {
    char tmp[LONGEST_FEN];
    int idx = write_piece_placement(board, tmp);
    tmp[idx++] = ' ';
    tmp[idx++] = state->turn ? 'b' : 'w';
    tmp[idx++] = ' ';
    if (state->can_castle_short[White]) tmp[idx++] = 'K';
    if (state->can_castle_long[White])  tmp[idx++] = 'Q';
    if (state->can_castle_short[Black]) tmp[idx++] = 'k';
    if (state->can_castle_long[Black])  tmp[idx++] = 'q';
    tmp[idx++] = ' ';
    if (state->en_passant_square.row >= 0) {
        tmp[idx++] = 'a' + state->en_passant_square.col;
        tmp[idx++] = '0' + (8 - state->en_passant_square.row);
    } else {
        tmp[idx++] = '-';
    }
    tmp[idx] = '\0';
    snprintf(buf, buf_size, "%s", tmp);
}
