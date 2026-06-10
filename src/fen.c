#include "fen.h"
#include "game.h"
#include <string.h>

char fen_table[LONGEST_FEN];

static int write_piece_placement(piece board[8][8], char *buf) {
    int idx = 0;
    for (int r = 0; r < 8; r++) {
        int empty = 0;
        for (int c = 0; c < 8; c++) {
            piece p = board[r][c];
            if (p.type == EMPTY) {
                empty++;
            } else {
                if (empty) {
                    buf[idx++] = '0' + empty;
                    empty = 0;
                }
                char ch;
                switch (p.type) {
                case PAWN:   ch = 'P'; break;
                case ROOK:   ch = 'R'; break;
                case KNIGHT: ch = 'N'; break;
                case BISHOP: ch = 'B'; break;
                case QUEEN:  ch = 'Q'; break;
                case KING:   ch = 'K'; break;
                default:     ch = '?'; break;
                }
                buf[idx++] = (p.color == White) ? ch : (char)(ch + 32);
            }
        }
        if (empty)
            buf[idx++] = '0' + empty;
        if (r < 7)
            buf[idx++] = '/';
    }
    return idx;
}

int update_fen_table(piece board[8][8]) {
    int idx = write_piece_placement(board, fen_table);
    fen_table[idx] = '\0';
    return 1;
}

int update_full_fen(piece board[8][8], const game_state *state) {
    int idx = write_piece_placement(board, fen_table);

    // active color
    fen_table[idx++] = ' ';
    fen_table[idx++] = state->turn ? 'b' : 'w';

    // castling rights
    fen_table[idx++] = ' ';
    bool any = false;
    if (state->can_castle_short[White]) { fen_table[idx++] = 'K'; any = true; }
    if (state->can_castle_long[White])  { fen_table[idx++] = 'Q'; any = true; }
    if (state->can_castle_short[Black]) { fen_table[idx++] = 'k'; any = true; }
    if (state->can_castle_long[Black])  { fen_table[idx++] = 'q'; any = true; }
    if (!any)
        fen_table[idx++] = '-';

    // en passant target square
    fen_table[idx++] = ' ';
    if (state->en_passant_square.row >= 0) {
        fen_table[idx++] = 'a' + state->en_passant_square.col;
        fen_table[idx++] = '0' + (8 - state->en_passant_square.row);
    } else {
        fen_table[idx++] = '-';
    }

    // halfmove clock and fullmove number
    idx += sprintf(fen_table + idx, " %zu %zu",
                   state->halfmove_clock,
                   state->move_count / 2 + 1);

    fen_table[idx] = '\0';
    return 1;
}

int load_fen(const char *fen, piece board[8][8], game_state *state) {
    // clear board
    for (int r = 0; r < 8; r++)
        for (int c = 0; c < 8; c++) {
            board[r][c].type = EMPTY;
            board[r][c].color = White;
            board[r][c].has_moved = true; // assume moved unless FEN says otherwise
        }

    int r = 0, c = 0;
    const char *p = fen;
    // piece placement
    while (*p && *p != ' ') {
        if (*p == '/') {
            r++;
            c = 0;
        } else if (*p >= '1' && *p <= '8') {
            c += *p - '0';
        } else {
            piece_type type = EMPTY;
            color col = (*p >= 'A' && *p <= 'Z') ? White : Black;
            char ch = (col == White) ? *p : (char)(*p - 32);
            switch (ch) {
            case 'P': type = PAWN;   break;
            case 'R': type = ROOK;   break;
            case 'N': type = KNIGHT; break;
            case 'B': type = BISHOP; break;
            case 'Q': type = QUEEN;  break;
            case 'K': type = KING;   break;
            default:  break;
            }
            board[r][c].type = type;
            board[r][c].color = col;
            // mark pawns on starting ranks as unmoved
            if (type == PAWN)
                board[r][c].has_moved = !((col == Black && r == 1) ||
                                          (col == White && r == 6));
            c++;
        }
        p++;
    }

    if (!state)
        return 1;

    init_game_state(state);

    if (!*p) return 1;
    p++; // skip space

    // active color
    state->turn = (*p == 'b');
    p++;

    if (!*p) return 1;
    p++; // skip space

    // castling rights
    state->can_castle_short[White] = false;
    state->can_castle_long[White]  = false;
    state->can_castle_short[Black] = false;
    state->can_castle_long[Black]  = false;
    while (*p && *p != ' ') {
        switch (*p) {
        case 'K': state->can_castle_short[White] = true; break;
        case 'Q': state->can_castle_long[White]  = true; break;
        case 'k': state->can_castle_short[Black] = true; break;
        case 'q': state->can_castle_long[Black]  = true; break;
        default: break;
        }
        p++;
    }

    if (!*p) return 1;
    p++; // skip space

    // en passant
    if (*p != '-') {
        int ep_col = *p - 'a';
        p++;
        int ep_row = 8 - (*p - '0');
        state->en_passant_square = (board_pos){ep_row, ep_col};
        p++;
    } else {
        p++;
    }

    if (!*p) return 1;
    p++; // skip space

    // halfmove clock
    state->halfmove_clock = (size_t)atoi(p);
    while (*p && *p != ' ') p++;
    if (!*p) return 1;
    p++;

    // fullmove number
    int fullmove = atoi(p);
    state->move_count = (size_t)((fullmove - 1) * 2 + (state->turn ? 1 : 0));

    // sync has_moved with castling rights so generate_king_moves works
    // pieces default to has_moved=true; clear only when the right is active
    if (state->can_castle_short[White] || state->can_castle_long[White]) {
        board[7][4].has_moved = false; // white king e1
        if (state->can_castle_short[White]) board[7][7].has_moved = false;
        if (state->can_castle_long[White])  board[7][0].has_moved = false;
    }
    if (state->can_castle_short[Black] || state->can_castle_long[Black]) {
        board[0][4].has_moved = false; // black king e8
        if (state->can_castle_short[Black]) board[0][7].has_moved = false;
        if (state->can_castle_long[Black])  board[0][0].has_moved = false;
    }

    return 1;
}
