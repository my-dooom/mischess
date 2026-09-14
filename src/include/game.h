#ifndef GAME_H
#define GAME_H

#include "board.h"
#include <stdio.h>
#include <stdlib.h>

#define da_append(xs, x)                                                       \
    do {                                                                       \
        if ((xs).count == (xs).capacity) {                                     \
            (xs).capacity = (xs).capacity ? (xs).capacity * 2 : 4;             \
            (xs).pos = realloc((xs).pos, (xs).capacity * sizeof(*(xs).pos));   \
        }                                                                      \
        (xs).pos[(xs).count++] = (x);                                          \
    } while (0)

typedef struct {
    Vector2 *pos;
    size_t count;
    size_t capacity;
} possible_moves;

typedef enum {
    RESULT_NONE,
    RESULT_CHECKMATE,
    RESULT_STALEMATE,
    RESULT_FIFTY_MOVES,
    RESULT_REPETITION,
    RESULT_INSUFFICIENT_MATERIAL,
} game_result;

// longest SAN is 7 chars ("exd8=Q#"), keep room for the terminator
#define SAN_MAX 8

// Snapshot of the position *before* a move plus the SAN of that move.
// Used for undo, the move list and threefold repetition.
typedef struct {
    piece board[8][8];
    bool turn;
    board_pos en_passant_square;
    bool can_castle_short[2];
    bool can_castle_long[2];
    size_t move_count;
    size_t halfmove_clock;
    board_pos src;
    board_pos dest;
    char san[SAN_MAX];
} ply_record;

typedef struct {
    bool turn;
    board_pos current_selection;
    possible_moves possible_moves;
    board_pos en_passant_square;
    bool can_castle_short[2];
    bool can_castle_long[2];
    size_t move_count;
    size_t halfmove_clock;
    bool game_over;
    game_result result;
    bool is_in_check[2]; // is_in_check[White/Black]

    // set while the UI waits for the player to pick a promotion piece
    bool promotion_pending;
    board_pos promotion_src;
    board_pos promotion_dest;

    ply_record *history;
    size_t history_count;
    size_t history_capacity;
} game_state;

#define NULL_POS ((board_pos){-1, -1})

static inline color turn_to_color(bool turn) { return turn ? Black : White; }
static inline color opposite(color c) { return c == White ? Black : White; }

extern game_state game;

void init_game_state(game_state *state);
void free_game_state(game_state *state);

void check_possible_moves(piece board[8][8], board_pos pos,
                          possible_moves *moves);
void generate_legal_moves(piece board[8][8], board_pos pos,
                          possible_moves *moves);
bool has_any_legal_move(piece board[8][8], color side);

int move_piece(piece board[8][8], board_pos src, board_pos dest);

int long_castle(piece board[8][8], color player_color);
int short_castle(piece board[8][8], color player_color);

void update_capture_matrices(piece board[8][8]);
void compute_check_status(piece board[8][8], game_state *state);

bool is_promotion_move(piece board[8][8], board_pos src, board_pos dest);

// Applies a full move: castling, en passant, promotion, castling rights,
// clocks, turn switch, check status and game-over detection. `promotion` is
// only read when the move is a pawn reaching the back rank (EMPTY -> queen).
// Returns 0 without touching anything when the move is not legal.
int make_move(piece board[8][8], game_state *state, board_pos src,
              board_pos dest, piece_type promotion);

// Reverts the last move made with make_move. Returns 0 if there is nothing to
// undo.
int undo_move(piece board[8][8], game_state *state);

const char *result_to_string(const game_state *state);

#endif // GAME_H
