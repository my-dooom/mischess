#include "uci.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool starts_with(const char *s, const char *prefix) {
    return strncmp(s, prefix, strlen(prefix)) == 0;
}

bool uci_parse_info(const char *line, uci_info *out) {
    if (!starts_with(line, "info "))
        return false;
    memset(out, 0, sizeof(*out));
    out->multipv = 1;
    bool has_score = false;

    // tokenise on a private copy
    char buf[1024];
    snprintf(buf, sizeof(buf), "%s", line);
    char *save = NULL;
    char *tok = strtok_r(buf, " ", &save);
    while (tok) {
        if (strcmp(tok, "depth") == 0) {
            tok = strtok_r(NULL, " ", &save);
            if (tok) out->depth = atoi(tok);
        } else if (strcmp(tok, "multipv") == 0) {
            tok = strtok_r(NULL, " ", &save);
            if (tok) out->multipv = atoi(tok);
        } else if (strcmp(tok, "score") == 0) {
            char *kind = strtok_r(NULL, " ", &save);
            char *val = strtok_r(NULL, " ", &save);
            if (kind && val) {
                if (strcmp(kind, "cp") == 0) {
                    out->score_cp = atoi(val);
                    out->is_mate = false;
                    has_score = true;
                } else if (strcmp(kind, "mate") == 0) {
                    out->mate_in = atoi(val);
                    out->is_mate = true;
                    has_score = true;
                }
            }
        } else if (strcmp(tok, "pv") == 0) {
            // everything after "pv" is the line
            size_t n = 0;
            bool first = true;
            while ((tok = strtok_r(NULL, " ", &save))) {
                if (first) {
                    snprintf(out->first_move, sizeof(out->first_move), "%s", tok);
                    first = false;
                }
                size_t len = strlen(tok);
                if (n + len + 2 > sizeof(out->pv))
                    break;
                if (n) out->pv[n++] = ' ';
                memcpy(out->pv + n, tok, len);
                n += len;
                out->pv[n] = '\0';
            }
            break;
        } else if (strcmp(tok, "string") == 0) {
            return false;
        }
        tok = strtok_r(NULL, " ", &save);
    }
    return has_score && out->first_move[0] != '\0';
}

bool uci_parse_bestmove(const char *line, char *move, size_t cap) {
    if (!starts_with(line, "bestmove"))
        return false;
    const char *p = line + strlen("bestmove");
    while (*p == ' ') p++;
    size_t n = 0;
    while (p[n] && p[n] != ' ' && p[n] != '\r' && p[n] != '\n') n++;
    if (n >= cap) n = cap - 1;
    memcpy(move, p, n);
    move[n] = '\0';
    if (strcmp(move, "(none)") == 0)
        move[0] = '\0';
    return true;
}

bool uci_move_to_squares(const char *move, board_pos *src, board_pos *dest,
                         piece_type *promotion) {
    size_t len = strlen(move);
    if (len < 4)
        return false;
    if (move[0] < 'a' || move[0] > 'h' || move[2] < 'a' || move[2] > 'h' ||
        move[1] < '1' || move[1] > '8' || move[3] < '1' || move[3] > '8')
        return false;
    src->col = move[0] - 'a';
    src->row = 8 - (move[1] - '0');
    dest->col = move[2] - 'a';
    dest->row = 8 - (move[3] - '0');
    *promotion = EMPTY;
    if (len >= 5) {
        switch (move[4]) {
        case 'q': *promotion = QUEEN;  break;
        case 'r': *promotion = ROOK;   break;
        case 'b': *promotion = BISHOP; break;
        case 'n': *promotion = KNIGHT; break;
        default: break;
        }
    }
    return true;
}

void uci_squares_to_move(board_pos src, board_pos dest, piece_type promotion,
                         char *out, size_t cap) {
    char promo = '\0';
    switch (promotion) {
    case QUEEN:  promo = 'q'; break;
    case ROOK:   promo = 'r'; break;
    case BISHOP: promo = 'b'; break;
    case KNIGHT: promo = 'n'; break;
    default: break;
    }
    if (promo)
        snprintf(out, cap, "%c%d%c%d%c", 'a' + src.col, 8 - src.row,
                 'a' + dest.col, 8 - dest.row, promo);
    else
        snprintf(out, cap, "%c%d%c%d", 'a' + src.col, 8 - src.row,
                 'a' + dest.col, 8 - dest.row);
}

int uci_score_cp_for_white(const uci_info *info, bool black_to_move) {
    int cp;
    if (info->is_mate) {
        // mate in 0 never happens in an info line; treat closer mates as
        // more extreme so M1 > M5
        int plies = abs(info->mate_in);
        cp = UCI_MATE_CP - plies;
        if (info->mate_in < 0)
            cp = -cp;
    } else {
        cp = info->score_cp;
    }
    return black_to_move ? -cp : cp;
}

void uci_format_score(int cp, char *out, size_t cap) {
    if (abs(cp) > UCI_MATE_CP - 1000) {
        int plies = UCI_MATE_CP - abs(cp);
        snprintf(out, cap, "%sM%d", cp < 0 ? "-" : "", plies);
        return;
    }
    snprintf(out, cap, "%+.2f", cp / 100.0);
}

bool uci_fen_flip_side(const char *fen, char *out, size_t cap) {
    // fields: placement turn castling ep halfmove fullmove
    char placement[96], turn[4], castling[8], ep[4];
    int halfmove = 0, fullmove = 1;
    int n = sscanf(fen, "%95s %3s %7s %3s %d %d", placement, turn, castling, ep,
                   &halfmove, &fullmove);
    if (n < 2)
        return false;
    if (n < 3) strcpy(castling, "-");
    if (turn[0] != 'w' && turn[0] != 'b')
        return false;
    snprintf(out, cap, "%s %c %s - %d %d", placement,
             turn[0] == 'w' ? 'b' : 'w', castling, halfmove, fullmove);
    return true;
}

move_grade uci_grade_move(int cp_loss) {
    if (cp_loss <= 10)  return GRADE_BEST;
    if (cp_loss <= 40)  return GRADE_GOOD;
    if (cp_loss <= 90)  return GRADE_INACCURACY;
    if (cp_loss <= 200) return GRADE_MISTAKE;
    return GRADE_BLUNDER;
}

const char *uci_grade_name(move_grade g) {
    switch (g) {
    case GRADE_BEST:       return "Best move";
    case GRADE_GOOD:       return "Good";
    case GRADE_INACCURACY: return "Inaccuracy";
    case GRADE_MISTAKE:    return "Mistake";
    case GRADE_BLUNDER:    return "Blunder";
    default:               return "";
    }
}
