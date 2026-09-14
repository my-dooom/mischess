#ifndef COACH_H
#define COACH_H

#include "board.h"
#include "engine.h"
#include "fen.h"
#include "game.h"
#include "uci.h"

// The coach plays the opponent with a UCI engine and, while you think, runs
// a full-strength analysis of your position in the background. That gives it
// everything it needs to grade your move the instant you play it, show what
// the opponent threatens, offer hints, and adapt the engine's playing
// strength to how well you are doing.

#define COACH_CANDIDATES 3
#define COACH_LINE_MAX 96

typedef enum {
    COACH_OFF,         // no engine available: plain two-player game
    COACH_BOOT,        // waiting for uciok / readyok
    COACH_IDLE,        // game over or nothing to do
    COACH_THREAT,      // null-move search: what does the opponent threaten
    COACH_ANALYZE,     // infinite MultiPV analysis while the human thinks
    COACH_STOPPING,    // sent "stop", waiting for bestmove to discard
    COACH_EVAL_BEFORE, // human moved before analysis ran: evaluate old position
    COACH_EVAL_AFTER,  // evaluate the position after the human move
    COACH_PLAY,        // engine chooses its reply at limited strength
} coach_phase;

typedef struct {
    bool valid;
    move_grade grade;
    int cp_loss;
    char played_san[SAN_MAX];
    char best_san[SAN_MAX];          // what the engine preferred
    char best_line[COACH_LINE_MAX];  // its continuation in SAN
    int eval_before_cp;              // human perspective, before the move
    int eval_after_cp;               // human perspective, after the move
} coach_feedback;

typedef struct {
    bool valid;
    char move_uci[UCI_MOVE_MAX];
    char san[SAN_MAX];
    int cp_for_human; // how bad it would be if the opponent got this move
} coach_threat;

typedef struct {
    engine_proc proc;
    coach_phase phase;
    char engine_name[64];
    char last_error[128];

    color human_color;
    int engine_elo;

    // live analysis of the current human-turn position
    uci_info candidates[COACH_CANDIDATES];
    bool candidates_valid[COACH_CANDIDATES];
    bool analysis_black_to_move;
    int eval_cp_white; // latest evaluation for the eval bar

    // analysis snapshot taken when the human moved
    bool before_valid;
    int before_best_cp_human;
    char before_best_move[UCI_MOVE_MAX];
    char before_best_pv[UCI_PV_MAX];
    piece before_board[8][8];
    game_state before_state;
    char before_fen[LONGEST_FEN];
    char played_move[UCI_MOVE_MAX];

    // last search result while a one-shot search runs
    uci_info search_last;
    bool search_last_valid;

    coach_threat threat;
    coach_feedback feedback;

    // running statistics for the adaptive Elo and the summary
    int grade_counts[GRADE_COUNT];
    int moves_graded;
    int total_cp_loss;
    int recent_cp_loss[6];
    int recent_count;
    int hints_used;

    bool show_hint;
    bool show_candidates;
    bool show_threat;

    // The hint and candidate lines shown to the player are a frozen copy of
    // the analysis, taken once it is deep enough and kept until the position
    // changes, so what is on screen does not keep shifting under the eye.
    uci_info hint_lines[COACH_CANDIDATES];
    bool hint_valid[COACH_CANDIDATES];
    bool hint_frozen;
    bool hint_black_to_move;
    bool needs_restart; // position changed under a running search

    // set when the engine's reply has been applied; main animates it
    bool engine_moved;
    board_pos engine_move_src;
    board_pos engine_move_dest;
} coach;

extern coach the_coach;

// Tries env MISCHESS_ENGINE, then `engines/stockfish[.exe]` next to the
// executable, then `stockfish` on PATH. Coach stays OFF when none works.
void coach_init(coach *c, const char *explicit_path);
void coach_shutdown(coach *c);

// Call once per frame. May apply the engine's move to board/state.
void coach_update(coach *c, piece board[8][8], game_state *state);

// Notify the coach after the human's move was applied with make_move.
void coach_on_human_move(coach *c, piece board[8][8], game_state *state,
                         board_pos src, board_pos dest, piece_type promo);
// Notify after undo / reset / side switch so searches are restarted.
void coach_on_position_changed(coach *c, piece board[8][8],
                               game_state *state);
void coach_switch_sides(coach *c, piece board[8][8], game_state *state);
// Clears per-game feedback and statistics (the adaptive Elo is kept).
void coach_new_game(coach *c, piece board[8][8], game_state *state);

bool coach_active(const coach *c);
bool coach_engine_thinking(const coach *c);
bool coach_is_human_turn(const coach *c, const game_state *state);

// Returns true once per engine move, handing out its squares.
bool coach_take_engine_move(coach *c, board_pos *src, board_pos *dest);

// Call when the player asks for a hint or the candidate lines: takes the
// snapshot right away if the analysis is already deep enough, otherwise it
// is taken as soon as it is.
void coach_request_hints(coach *c);

// "e2e4" of the frozen hint move, or NULL while no snapshot exists
const char *coach_hint_move(const coach *c);

#endif // COACH_H
