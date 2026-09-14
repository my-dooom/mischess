// test_rules.c -- rule-level tests for the chess engine
// compile with: cmake --build build --target chess_tests && ctest --test-dir build
#include "board.h"
#include "fen.h"
#include "game.h"
#include "notation.h"
#include "uci.h"
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
// attack model: what a piece controls is not the same as where it can move
// ---------------------------------------------------------------------------

// loads a FEN into the global game/board the way the app does
static void setup(const char *fen) {
    free_game_state(&game);
    load_fen(fen, board, &game);
    update_capture_matrices(board);
    compute_check_status(board, &game);
}

static bool game_can_move(board_pos src, board_pos dest) {
    return has_legal_move_to(board, src, dest);
}

// a pawn does not attack the square in front of it
static void test_king_may_step_in_front_of_pawn(void) {
    // black pawn e4 (row 4), white king e2 (row 6): e3 is not attacked
    setup("4k3/8/8/8/4p3/8/4K3/8 w - - 0 1");
    ASSERT(game_can_move((board_pos){6, 4}, (board_pos){5, 4}),
           "king should be able to step to e3 directly in front of a black pawn");
    // ...but d3 and f3 are attacked by that pawn
    ASSERT(!game_can_move((board_pos){6, 4}, (board_pos){5, 3}),
           "king must not step to d3 which the e4 pawn attacks");
}

// a pawn attacks its diagonals even when they are empty
static void test_castle_blocked_by_pawn_attack(void) {
    // black pawn g2 attacks f1 -> white cannot castle short
    setup("4k3/8/8/8/8/8/6p1/4K2R w K - 0 1");
    ASSERT(!game_can_move((board_pos){7, 4}, (board_pos){7, 6}),
           "short castle must be blocked when a pawn attacks f1");
}

// king cannot capture a defended piece
static void test_king_cannot_capture_defended_piece(void) {
    // black knight d2 defended by black bishop a5 (row 3), white king e1
    setup("4k3/8/8/b7/8/8/3n4/4K3 w - - 0 1");
    ASSERT(!game_can_move((board_pos){7, 4}, (board_pos){6, 3}),
           "king must not capture a knight defended by a bishop");
}

// ---------------------------------------------------------------------------
// make_move: full move application
// ---------------------------------------------------------------------------

static const char *last_san(void) {
    return game.history[game.history_count - 1].san;
}

static void test_make_move_rejects_illegal(void) {
    setup("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
    ASSERT(!make_move(board, &game, (board_pos){6, 4}, (board_pos){3, 4}, EMPTY),
           "e2-e5 must be rejected");
    ASSERT(!make_move(board, &game, (board_pos){1, 4}, (board_pos){3, 4}, EMPTY),
           "black must not move on the white turn");
    ASSERT(game.history_count == 0, "rejected moves leave no history");
    ASSERT(make_move(board, &game, (board_pos){6, 4}, (board_pos){4, 4}, EMPTY),
           "e2-e4 must be accepted");
    ASSERT(strcmp(last_san(), "e4") == 0, "SAN for e2-e4 should be e4");
    ASSERT(game.turn == true, "turn passes to black after a move");
    ASSERT(game.en_passant_square.row == 5 && game.en_passant_square.col == 4,
           "double push sets the en passant square to e3");
}

static void test_fools_mate_via_make_move(void) {
    setup("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
    make_move(board, &game, (board_pos){6, 5}, (board_pos){5, 5}, EMPTY); // f3
    make_move(board, &game, (board_pos){1, 4}, (board_pos){3, 4}, EMPTY); // e5
    make_move(board, &game, (board_pos){6, 6}, (board_pos){4, 6}, EMPTY); // g4
    int ok = make_move(board, &game, (board_pos){0, 3}, (board_pos){4, 7},
                       EMPTY); // Qh4#
    ASSERT(ok, "Qh4 must be legal");
    ASSERT(game.game_over && game.result == RESULT_CHECKMATE,
           "fools mate must end the game in checkmate");
    ASSERT(strcmp(last_san(), "Qh4#") == 0, "mate SAN should carry #");
    ASSERT(strcmp(result_to_string(&game), "Checkmate! Black wins") == 0,
           "black should be reported as the winner");
    ASSERT(!make_move(board, &game, (board_pos){6, 0}, (board_pos){5, 0}, EMPTY),
           "no moves are accepted after game over");
}

static void test_check_suffix(void) {
    setup("3k4/8/8/8/8/8/8/4R1K1 w - - 0 1");
    make_move(board, &game, (board_pos){7, 4}, (board_pos){0, 4}, EMPTY); // Re8+
    ASSERT(strcmp(last_san(), "Re8+") == 0, "check SAN should carry +");
    ASSERT(game.is_in_check[Black], "black must be in check");
}

static void test_promotion_choice(void) {
    setup("4k3/1P6/8/8/8/8/8/4K3 w - - 0 1");
    make_move(board, &game, (board_pos){1, 1}, (board_pos){0, 1}, KNIGHT);
    ASSERT(board[0][1].type == KNIGHT && board[0][1].color == White,
           "pawn should promote to the chosen knight");
    ASSERT(strcmp(last_san(), "b8=N") == 0, "underpromotion SAN");

    setup("3rk3/4P3/8/8/8/8/8/4K3 w - - 0 1");
    make_move(board, &game, (board_pos){1, 4}, (board_pos){0, 3}, EMPTY);
    ASSERT(board[0][3].type == QUEEN, "EMPTY promotion defaults to queen");
    ASSERT(strcmp(last_san(), "exd8=Q+") == 0,
           "capture-promotion SAN with check");
}

static void test_en_passant_via_make_move(void) {
    setup("4k3/8/8/3pP3/8/8/8/4K3 w - d6 0 1");
    ASSERT(make_move(board, &game, (board_pos){3, 4}, (board_pos){2, 3}, EMPTY),
           "en passant capture must be legal");
    ASSERT(board[3][3].type == EMPTY, "captured pawn is removed from d5");
    ASSERT(strcmp(last_san(), "exd6") == 0, "en passant SAN");
    ASSERT(game.halfmove_clock == 0, "pawn move resets the halfmove clock");
}

static void test_castling_via_make_move(void) {
    setup("r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1");
    ASSERT(make_move(board, &game, (board_pos){7, 4}, (board_pos){7, 2}, EMPTY),
           "long castle must be legal");
    ASSERT(board[7][2].type == KING && board[7][3].type == ROOK &&
               board[7][0].type == EMPTY && board[7][4].type == EMPTY,
           "king and rook end up on c1/d1");
    ASSERT(strcmp(last_san(), "O-O-O") == 0, "long castle SAN");
    ASSERT(!game.can_castle_short[White] && !game.can_castle_long[White],
           "castling revokes both white rights");
    ASSERT(game.can_castle_short[Black] && game.can_castle_long[Black],
           "black keeps its rights");

    // capturing a rook on its home square removes that right
    setup("r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1");
    make_move(board, &game, (board_pos){7, 0}, (board_pos){0, 0}, EMPTY); // Rxa8+
    ASSERT(!game.can_castle_long[Black],
           "capturing the a8 rook revokes black long castle");
    ASSERT(!game.can_castle_long[White],
           "moving the a1 rook revokes white long castle");
    ASSERT(game.can_castle_short[Black], "black short castle is untouched");
}

static void test_san_disambiguation(void) {
    // rooks a1 and h1 can both reach d1
    setup("4k3/8/8/8/8/8/4K3/R6R w - - 0 1");
    make_move(board, &game, (board_pos){7, 0}, (board_pos){7, 3}, EMPTY);
    ASSERT(strcmp(last_san(), "Rad1") == 0,
           "same-rank rooks disambiguate by file");

    // knights b1 and b5 can both reach c3
    setup("4k3/8/8/1N6/8/8/8/1N2K3 w - - 0 1");
    make_move(board, &game, (board_pos){7, 1}, (board_pos){5, 2}, EMPTY);
    ASSERT(strcmp(last_san(), "N1c3") == 0,
           "same-file knights disambiguate by rank");
}

// ---------------------------------------------------------------------------
// draws
// ---------------------------------------------------------------------------

static void test_insufficient_material(void) {
    // rook captures the last black pawn leaving K+R vs K -> not a draw
    setup("4k3/8/8/8/8/8/p7/R3K3 w - - 0 1");
    make_move(board, &game, (board_pos){7, 0}, (board_pos){6, 0}, EMPTY);
    ASSERT(!game.game_over, "K+R vs K is not insufficient material");

    // bishop b1 captures the last pawn on a2 leaving K+B vs K
    setup("4k3/8/8/8/8/8/p7/1B2K3 w - - 0 1");
    make_move(board, &game, (board_pos){7, 1}, (board_pos){6, 0}, EMPTY);
    ASSERT(game.game_over && game.result == RESULT_INSUFFICIENT_MATERIAL,
           "K+B vs K is a draw");

    // same-colored bishops: a2 (row 6, col 0) and g8 (row 0, col 6) share
    // square color
    setup("4k1b1/8/8/8/8/8/p7/1B2K3 w - - 0 1");
    make_move(board, &game, (board_pos){7, 1}, (board_pos){6, 0}, EMPTY);
    ASSERT(game.game_over && game.result == RESULT_INSUFFICIENT_MATERIAL,
           "K+B vs K+B with same-colored bishops is a draw");

    // opposite-colored bishops (a2 vs f8) can still mate in theory: no draw
    setup("4kb2/8/8/8/8/8/p7/1B2K3 w - - 0 1");
    make_move(board, &game, (board_pos){7, 1}, (board_pos){6, 0}, EMPTY);
    ASSERT(!game.game_over, "opposite-colored bishops are not a dead draw");
}

static void test_fifty_move_rule(void) {
    setup("4k3/8/8/8/8/8/8/R3K3 w - - 99 60");
    make_move(board, &game, (board_pos){7, 0}, (board_pos){7, 1}, EMPTY);
    ASSERT(game.halfmove_clock == 100, "quiet move bumps the clock to 100");
    ASSERT(game.game_over && game.result == RESULT_FIFTY_MOVES,
           "100 halfmoves without pawn move or capture is a draw");
}

static void test_threefold_repetition(void) {
    setup("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
    board_pos g1 = {7, 6}, f3 = {5, 5}, g8 = {0, 6}, f6 = {2, 5};
    for (int i = 0; i < 2; i++) {
        make_move(board, &game, g1, f3, EMPTY);
        make_move(board, &game, g8, f6, EMPTY);
        make_move(board, &game, f3, g1, EMPTY);
        ASSERT(!game.game_over, "not yet a repetition");
        make_move(board, &game, f6, g8, EMPTY);
    }
    ASSERT(game.game_over && game.result == RESULT_REPETITION,
           "starting position occurring three times is a draw");
}

// ---------------------------------------------------------------------------
// undo
// ---------------------------------------------------------------------------

static void test_undo(void) {
    setup("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
    ASSERT(!undo_move(board, &game), "nothing to undo at the start");
    make_move(board, &game, (board_pos){6, 4}, (board_pos){4, 4}, EMPTY); // e4
    make_move(board, &game, (board_pos){1, 3}, (board_pos){3, 3}, EMPTY); // d5
    make_move(board, &game, (board_pos){4, 4}, (board_pos){3, 3}, EMPTY); // exd5
    ASSERT(board[3][3].color == White && board[3][3].type == PAWN,
           "pawn captured on d5");
    ASSERT(undo_move(board, &game), "undo succeeds");
    ASSERT(board[3][3].color == Black && board[4][4].type == PAWN,
           "undo restores the captured pawn and the capturer");
    ASSERT(game.turn == false && game.history_count == 2,
           "undo restores turn and history");
    ASSERT(game.halfmove_clock == 0, "undo restores the halfmove clock");
    ASSERT(make_move(board, &game, (board_pos){4, 4}, (board_pos){3, 3}, EMPTY),
           "the same capture is legal again after undo");

    // undo out of a finished game
    setup("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
    make_move(board, &game, (board_pos){6, 5}, (board_pos){5, 5}, EMPTY);
    make_move(board, &game, (board_pos){1, 4}, (board_pos){3, 4}, EMPTY);
    make_move(board, &game, (board_pos){6, 6}, (board_pos){4, 6}, EMPTY);
    make_move(board, &game, (board_pos){0, 3}, (board_pos){4, 7}, EMPTY);
    ASSERT(game.game_over, "mated");
    undo_move(board, &game);
    ASSERT(!game.game_over && game.result == RESULT_NONE,
           "undo clears game over");
    ASSERT(game.can_castle_short[Black], "castling rights survive undo");
}


// ---------------------------------------------------------------------------
// UCI text layer (used by the coach)
// ---------------------------------------------------------------------------

static void test_uci_parse_info(void) {
    uci_info info;
    bool ok = uci_parse_info(
        "info depth 18 seldepth 25 multipv 2 score cp -35 nodes 123 nps 1 "
        "time 5 pv e7e5 g1f3 b8c6",
        &info);
    ASSERT(ok, "info line with score and pv parses");
    ASSERT(info.depth == 18 && info.multipv == 2, "depth and multipv parsed");
    ASSERT(!info.is_mate && info.score_cp == -35, "cp score parsed");
    ASSERT(strcmp(info.first_move, "e7e5") == 0, "first pv move parsed");
    ASSERT(strcmp(info.pv, "e7e5 g1f3 b8c6") == 0, "whole pv parsed");

    ok = uci_parse_info("info depth 5 score mate -3 pv h7h6", &info);
    ASSERT(ok && info.is_mate && info.mate_in == -3, "mate score parsed");
    ASSERT(info.multipv == 1, "multipv defaults to 1");

    ASSERT(!uci_parse_info("info string NNUE evaluation using nn.nnue", &info),
           "info string lines are ignored");
    ASSERT(!uci_parse_info("info depth 3 currmove e2e4 currmovenumber 1", &info),
           "currmove lines without a score are ignored");
    ASSERT(!uci_parse_info("bestmove e2e4", &info), "bestmove is not info");
}

static void test_uci_parse_bestmove(void) {
    char mv[UCI_MOVE_MAX];
    ASSERT(uci_parse_bestmove("bestmove e2e4 ponder e7e5", mv, sizeof(mv)) &&
               strcmp(mv, "e2e4") == 0,
           "bestmove with ponder");
    ASSERT(uci_parse_bestmove("bestmove e7e8q", mv, sizeof(mv)) &&
               strcmp(mv, "e7e8q") == 0,
           "promotion bestmove");
    ASSERT(uci_parse_bestmove("bestmove (none)", mv, sizeof(mv)) && mv[0] == 0,
           "(none) yields an empty move");
    ASSERT(!uci_parse_bestmove("info depth 1", mv, sizeof(mv)),
           "non-bestmove line rejected");
}

static void test_uci_moves(void) {
    board_pos src, dest;
    piece_type promo;
    ASSERT(uci_move_to_squares("e2e4", &src, &dest, &promo), "e2e4 parses");
    ASSERT(src.row == 6 && src.col == 4 && dest.row == 4 && dest.col == 4,
           "e2e4 maps to rows 6->4, col 4");
    ASSERT(promo == EMPTY, "no promotion");
    ASSERT(uci_move_to_squares("b7a8n", &src, &dest, &promo) && promo == KNIGHT,
           "promotion suffix parsed");
    ASSERT(!uci_move_to_squares("e2", &src, &dest, &promo), "too short");
    ASSERT(!uci_move_to_squares("z9e4", &src, &dest, &promo), "bad file");

    char out[UCI_MOVE_MAX];
    uci_squares_to_move((board_pos){6, 4}, (board_pos){4, 4}, EMPTY, out,
                        sizeof(out));
    ASSERT(strcmp(out, "e2e4") == 0, "squares back to e2e4");
    uci_squares_to_move((board_pos){1, 1}, (board_pos){0, 0}, QUEEN, out,
                        sizeof(out));
    ASSERT(strcmp(out, "b7a8q") == 0, "promotion suffix written");
}

static void test_uci_scores(void) {
    uci_info info = {0};
    info.score_cp = 50;
    ASSERT(uci_score_cp_for_white(&info, false) == 50, "white to move: as is");
    ASSERT(uci_score_cp_for_white(&info, true) == -50, "black to move: negated");
    info.is_mate = true;
    info.mate_in = 2;
    ASSERT(uci_score_cp_for_white(&info, false) == UCI_MATE_CP - 2,
           "mate folds to a large cp value");
    info.mate_in = -1;
    ASSERT(uci_score_cp_for_white(&info, true) == UCI_MATE_CP - 1,
           "black getting mated is good for white");

    char txt[16];
    uci_format_score(35, txt, sizeof(txt));
    ASSERT(strcmp(txt, "+0.35") == 0, "cp formats as pawns");
    uci_format_score(-120, txt, sizeof(txt));
    ASSERT(strcmp(txt, "-1.20") == 0, "negative cp formats");
    uci_format_score(UCI_MATE_CP - 3, txt, sizeof(txt));
    ASSERT(strcmp(txt, "M3") == 0, "mate formats as M3");
    uci_format_score(-(UCI_MATE_CP - 1), txt, sizeof(txt));
    ASSERT(strcmp(txt, "-M1") == 0, "getting mated formats as -M1");
}

static void test_uci_flip_side(void) {
    char out[LONGEST_FEN];
    ASSERT(uci_fen_flip_side(
               "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1",
               out, sizeof(out)),
           "flip succeeds");
    ASSERT(strcmp(out, "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR w KQkq - 0 1") == 0,
           "side flipped and en passant cleared");
    ASSERT(!uci_fen_flip_side("garbage", out, sizeof(out)), "malformed FEN rejected");
}

static void test_uci_grades(void) {
    ASSERT(uci_grade_move(0) == GRADE_BEST, "0 cp loss is best");
    ASSERT(uci_grade_move(25) == GRADE_GOOD, "25 cp is good");
    ASSERT(uci_grade_move(60) == GRADE_INACCURACY, "60 cp is an inaccuracy");
    ASSERT(uci_grade_move(150) == GRADE_MISTAKE, "150 cp is a mistake");
    ASSERT(uci_grade_move(400) == GRADE_BLUNDER, "400 cp is a blunder");
}

static void test_uci_line_to_san(void) {
    setup("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
    char out[128];
    uci_line_to_san(board, &game, "e2e4 e7e5 g1f3 b8c6 f1b5", 5, out, sizeof(out));
    ASSERT(strcmp(out, "1. e4 e5 2. Nf3 Nc6 3. Bb5") == 0, "opening line to SAN");
    ASSERT(board[6][4].type == PAWN && game.history_count == 0,
           "conversion leaves the live position untouched");
    ASSERT(game.can_castle_short[White] && game.en_passant_square.row == -1,
           "conversion leaves the move-gen context untouched");

    // starts from a black-to-move position and truncates at max_moves
    setup("rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1");
    uci_line_to_san(board, &game, "e7e5 g1f3 b8c6", 2, out, sizeof(out));
    ASSERT(strcmp(out, "1... e5 2. Nf3") == 0, "black-first numbering and truncation");

    // an illegal move in the line stops the conversion
    setup("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
    uci_line_to_san(board, &game, "e2e4 e7e6 e4e6", 5, out, sizeof(out));
    ASSERT(strcmp(out, "1. e4 e6") == 0, "stops at the first illegal move");
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

    test_king_may_step_in_front_of_pawn();
    test_castle_blocked_by_pawn_attack();
    test_king_cannot_capture_defended_piece();

    test_make_move_rejects_illegal();
    test_fools_mate_via_make_move();
    test_check_suffix();
    test_promotion_choice();
    test_en_passant_via_make_move();
    test_castling_via_make_move();
    test_san_disambiguation();

    test_insufficient_material();
    test_fifty_move_rule();
    test_threefold_repetition();

    test_undo();

    test_uci_parse_info();
    test_uci_parse_bestmove();
    test_uci_moves();
    test_uci_scores();
    test_uci_flip_side();
    test_uci_grades();
    test_uci_line_to_san();

    if (tests_failed == 0) {
        printf("All %d tests passed\n", tests_run);
        return 0;
    }
    printf("%d/%d tests FAILED\n", tests_failed, tests_run);
    return 1;
}
