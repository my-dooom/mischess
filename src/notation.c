#include "notation.h"
#include "fen.h"
#include "uci.h"
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

void uci_line_to_san(piece board[8][8], const game_state *state,
                     const char *uci_line, int max_moves, char *out,
                     size_t cap) {
    out[0] = 0;
    piece copy[8][8];
    memcpy(copy, board, sizeof(copy));
    game_state tmp = *state;
    tmp.history = NULL;
    tmp.history_count = 0;
    tmp.history_capacity = 0;
    tmp.possible_moves.pos = NULL;
    tmp.possible_moves.count = 0;
    tmp.possible_moves.capacity = 0;
    tmp.game_over = false;

    // make_move on a foreign state rewrites the global move-gen context
    board_pos saved_ep = game.en_passant_square;
    bool saved_short[2], saved_long[2];
    memcpy(saved_short, game.can_castle_short, sizeof(saved_short));
    memcpy(saved_long, game.can_castle_long, sizeof(saved_long));

    size_t n = 0;
    int count = 0;
    const char *p = uci_line;
    while (*p && count < max_moves) {
        while (*p == ' ') p++;
        char mv[UCI_MOVE_MAX] = {0};
        size_t len = 0;
        while (p[len] && p[len] != ' ' && len < sizeof(mv) - 1) {
            mv[len] = p[len];
            len++;
        }
        p += len;
        if (len < 4)
            break;
        board_pos src, dest;
        piece_type promo;
        if (!uci_move_to_squares(mv, &src, &dest, &promo))
            break;
        // number the line like a score sheet: "3. Nf3" / "3... Nc6"
        char prefix[8] = "";
        if (count == 0 || !tmp.turn)
            snprintf(prefix, sizeof(prefix), "%zu%s", tmp.move_count / 2 + 1,
                     tmp.turn ? "..." : ".");
        if (!make_move(copy, &tmp, src, dest, promo))
            break;
        const char *san = tmp.history[tmp.history_count - 1].san;
        int written = snprintf(out + n, cap - n, "%s%s%s%s", n ? " " : "",
                               prefix, prefix[0] ? " " : "", san);
        if (written < 0 || (size_t)written >= cap - n)
            break;
        n += (size_t)written;
        count++;
    }
    free(tmp.history);

    game.en_passant_square = saved_ep;
    memcpy(game.can_castle_short, saved_short, sizeof(saved_short));
    memcpy(game.can_castle_long, saved_long, sizeof(saved_long));
}

void uci_line_to_san_current(const char *uci_line, int max_moves, char *out,
                             size_t cap) {
    // keyed on the line text plus the ply, which changes whenever the
    // position does
    static char cached_key[8][UCI_PV_MAX + 32];
    static char cached_val[8][256];
    static int next_slot = 0;
    char key[UCI_PV_MAX + 32];
    snprintf(key, sizeof(key), "%zu|%d|%s", game.history_count, max_moves,
             uci_line);
    for (int i = 0; i < 8; i++) {
        if (strcmp(cached_key[i], key) == 0) {
            snprintf(out, cap, "%s", cached_val[i]);
            return;
        }
    }
    int slot = next_slot;
    next_slot = (next_slot + 1) % 8;
    uci_line_to_san(board, &game, uci_line, max_moves, cached_val[slot],
                    sizeof(cached_val[slot]));
    snprintf(cached_key[slot], sizeof(cached_key[slot]), "%s", key);
    snprintf(out, cap, "%s", cached_val[slot]);
}
