#ifndef UCI_H
#define UCI_H

#include "board.h"
#include <stdbool.h>
#include <stddef.h>

// Pure UCI text helpers: no process I/O, no raylib, fully unit-testable.

#define UCI_PV_MAX 192
#define UCI_MOVE_MAX 8

// One "info ... multipv N score ... pv ..." line
typedef struct {
    int depth;
    int multipv;   // 1-based
    int score_cp;  // centipawns from the side to move (valid when !is_mate)
    int mate_in;   // moves to mate, signed from the side to move (when is_mate)
    bool is_mate;
    char pv[UCI_PV_MAX];           // space-separated long-algebraic moves
    char first_move[UCI_MOVE_MAX]; // first pv move, "" if none
} uci_info;

// Parses an info line. Returns false for lines without a score/pv (e.g.
// "info string ..." or currmove lines), which callers should ignore.
bool uci_parse_info(const char *line, uci_info *out);

// Parses "bestmove e2e4 [ponder ...]". Returns false if the line is not a
// bestmove line. "(none)" yields an empty move.
bool uci_parse_bestmove(const char *line, char *move, size_t cap);

// "e7e8q" -> squares and promotion piece (EMPTY when none)
bool uci_move_to_squares(const char *move, board_pos *src, board_pos *dest,
                         piece_type *promotion);
// squares -> "e7e8q" (promotion EMPTY omits the suffix)
void uci_squares_to_move(board_pos src, board_pos dest, piece_type promotion,
                         char *out, size_t cap);

// Turns a score into "white perspective" centipawns, folding mate scores to
// +-(MATE_CP - plies) so they compare and sort with ordinary evaluations.
#define UCI_MATE_CP 10000
int uci_score_cp_for_white(const uci_info *info, bool black_to_move);

// Human-readable score for the side given: "+0.35", "-1.20", "M3", "-M2"
void uci_format_score(int cp_for_side, char *out, size_t cap);

// Rewrites a FEN so the *other* side is to move and the en passant square is
// cleared. Used for the "what does my opponent threaten" null-move search.
// Returns false on a malformed FEN.
bool uci_fen_flip_side(const char *fen, char *out, size_t cap);

// Move quality buckets by centipawn loss (thresholds match common practice)
typedef enum {
    GRADE_BEST,
    GRADE_GOOD,
    GRADE_INACCURACY,
    GRADE_MISTAKE,
    GRADE_BLUNDER,
    GRADE_COUNT,
} move_grade;

move_grade uci_grade_move(int cp_loss);
const char *uci_grade_name(move_grade g);

#endif // UCI_H
