#include "fen.h"

char fen_table[LONGEST_FEN];

int update_fen_table(piece board[8][8]) {
    int idx = 0;
    for (int r = 0; r < 8; r++) {
        int empty = 0;
        for (int c = 0; c < 8; c++) {
            piece p = board[r][c];
            if (p.type == EMPTY) {
                empty++;
            } else {
                if (empty) {
                    fen_table[idx++] = '0' + empty;
                    empty = 0;
                }
                char fen_char;
                switch (p.type) {
                case PAWN:
                    fen_char = (p.color == White) ? 'P' : 'p';
                    break;
                case ROOK:
                    fen_char = (p.color == White) ? 'R' : 'r';
                    break;
                case KNIGHT:
                    fen_char = (p.color == White) ? 'N' : 'n';
                    break;
                case BISHOP:
                    fen_char = (p.color == White) ? 'B' : 'b';
                    break;
                case QUEEN:
                    fen_char = (p.color == White) ? 'Q' : 'q';
                    break;
                case KING:
                    fen_char = (p.color == White) ? 'K' : 'k';
                    break;
                default:
                    fen_char = '?';
                    break;
                }
                fen_table[idx++] = fen_char;
            }
        }
        if (empty)
            fen_table[idx++] = '0' + empty;
        if (r < 7)
            fen_table[idx++] = '/';
    }
    fen_table[idx] = '\0';
    return 1;
}
