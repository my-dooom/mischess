// test_rules.c -- rule-level tests for the chess engine
// compile with: cmake --build build --target chess_tests && ctest --test-dir build
#include "board.h"
#include "fen.h"
#include "game.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int tests_run = 0;
static int tests_failed = 0;

#define ASSERT(cond, msg)                                                      \
    do {                                                                       \
        tests_run++;                                                           \
        if (!(cond)) {                                                         \
            fprintf(stderr, "FAIL [%s:%d] %s\n", __FILE__, __LINE__, msg);    \
            tests_failed++;                                                    \
        }                                                                      \
    } while (0)

// ---------------------------------------------------------------------------
// helpers
// ---------------------------------------------------------------------------

static bool has_legal_move_to(piece board[8][8], board_pos src,
                               board_pos dest) {
    possible_moves moves = {0};
    generate_legal_moves(board, src, &moves);
    bool found = false;
    for (size_t i = 0; i < moves.count; i++) {
        if ((int)moves.pos[i].y == dest.row &&
            (int)moves.pos[i].x == dest.col) {
            found = true;
            break;
        }
    }
    free(moves.pos);
    return found;
}

static bool no_legal_moves(piece board[8][8], color side) {
    for (int r = 0; r < 8; r++) {
        for (int c = 0; c < 8; c++) {
            if (board[r][c].type == EMPTY || board[r][c].color != side)
                continue;
            possible_moves moves = {0};
            generate_legal_moves(board, (board_pos){r, c}, &moves);
            bool has = moves.count > 0;
            free(moves.pos);
            if (has)
                return false;
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
// tests
// ---------------------------------------------------------------------------

// pinned pawn on a diagonal must not be able to move
static void test_pinned_pawn_cannot_move(void) {
    piece board[8][8];
    game_state state;
    // black bishop on a5 (row 3, col 0) pins white pawn on d2 (row 6, col 3)
    // to the white king on e1 (row 7, col 4)
    load_fen("8/8/8/b7/8/8/3P4/4K3 w - - 0 1", board, &state);
    game = state; // move gen reads the global for castling/en passant
    update_capture_matrices(board);

    possible_moves moves = {0};
    generate_legal_moves(board, (board_pos){6, 3}, &moves); // d2 pawn
    ASSERT(moves.count == 0, "pinned pawn on d2 should have no legal moves");
    free(moves.pos);
}

// check detection: king is in check after opponent move
static void test_check_detection(void) {
    piece board[8][8];
    game_state state;
    // black queen on e4 attacks white king on e1
    load_fen("8/8/8/8/4q3/8/8/4K3 w - - 0 1", board, &state);
    game = state;
    update_capture_matrices(board);
    compute_check_status(board, &state);

    ASSERT(state.is_in_check[White], "white king should be in check from e4 queen");
    ASSERT(!state.is_in_check[Black], "black king should not be in check");
}

// checkmate: fool's mate position
static void test_checkmate(void) {
    piece board[8][8];
    game_state state;
    // fool's mate final position: white is checkmated
    load_fen("rnb1kbnr/pppp1ppp/8/4p3/6Pq/5P2/PPPPP2P/RNBQKBNR w KQkq - 1 3",
             board, &state);
    game = state;
    update_capture_matrices(board);
    compute_check_status(board, &state);

    ASSERT(state.is_in_check[White], "white should be in check in fool's mate");
    ASSERT(no_legal_moves(board, White), "white should have no legal moves in fool's mate");
}

// stalemate: classic stalemate position
static void test_stalemate(void) {
    piece board[8][8];
    game_state state;
    // white king a8 vs black king c7 and black queen b6 -- white to move
    load_fen("K7/8/1q6/2k5/8/8/8/8 w - - 0 1", board, &state);
    game = state;
    update_capture_matrices(board);
    compute_check_status(board, &state);

    ASSERT(!state.is_in_check[White], "white should not be in check in stalemate position");
    ASSERT(no_legal_moves(board, White), "white should have no legal moves in stalemate");
}

// en passant capture is legal
static void test_en_passant(void) {
    piece board[8][8];
    game_state state;
    // white pawn e5, black pawn d5 just pushed two squares (en passant on d6)
    load_fen("8/8/8/3pP3/8/8/8/4K2k w - d6 0 1", board, &state);
    game = state; // generate_pawn_moves reads game.en_passant_square
    update_capture_matrices(board);

    // e5 pawn should be able to capture on d6
    bool can_ep = has_legal_move_to(board, (board_pos){3, 4}, (board_pos){2, 3});
    ASSERT(can_ep, "e5 pawn should be able to capture en passant on d6");
}

// en passant is NOT available when the en passant square is not set
static void test_no_en_passant_without_double_push(void) {
    piece board[8][8];
    game_state state;
    // same position but no en passant target
    load_fen("8/8/8/3pP3/8/8/8/4K2k w - - 0 1", board, &state);
    game = state;
    update_capture_matrices(board);

    bool can_ep = has_legal_move_to(board, (board_pos){3, 4}, (board_pos){2, 3});
    ASSERT(!can_ep, "en passant should not be available without a prior double push");
}

// short castling is legal when path is clear and not attacked
static void test_short_castle_legal(void) {
    piece board[8][8];
    game_state state;
    load_fen("r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1", board, &state);
    game = state; // generate_king_moves reads game.can_castle_short
    update_capture_matrices(board);

    bool can_castle = has_legal_move_to(board, (board_pos){7, 4}, (board_pos){7, 6});
    ASSERT(can_castle, "white king should be able to short castle");
}

// short castling blocked when king passes through check
static void test_short_castle_blocked_by_attack(void) {
    piece board[8][8];
    game_state state;
    // black rook on f8 attacks f1, blocking white short castle
    load_fen("5r2/8/8/8/8/8/8/4K2R w K - 0 1", board, &state);
    game = state;
    update_capture_matrices(board);

    bool can_castle = has_legal_move_to(board, (board_pos){7, 4}, (board_pos){7, 6});
    ASSERT(!can_castle, "short castle should be blocked when f1 is attacked");
}

// pawn promotion: pawn on 7th rank should reach back rank
static void test_pawn_promotion_square_reachable(void) {
    piece board[8][8];
    game_state state;
    // white pawn on e7, no pieces blocking
    load_fen("8/4P3/8/8/8/8/8/4K2k w - - 0 1", board, &state);
    game = state;
    update_capture_matrices(board);

    bool can_promote = has_legal_move_to(board, (board_pos){1, 4}, (board_pos){0, 4});
    ASSERT(can_promote, "e7 pawn should be able to advance to e8 for promotion");
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main(void) {
    test_pinned_pawn_cannot_move();
    test_check_detection();
    test_checkmate();
    test_stalemate();
    test_en_passant();
    test_no_en_passant_without_double_push();
    test_short_castle_legal();
    test_short_castle_blocked_by_attack();
    test_pawn_promotion_square_reachable();

    if (tests_failed == 0) {
        printf("All %d tests passed\n", tests_run);
        return 0;
    }
    printf("%d/%d tests FAILED\n", tests_failed, tests_run);
    return 1;
}
