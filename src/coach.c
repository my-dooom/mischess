#include "coach.h"
#include "fen.h"
#include "notation.h"
#include "raylib.h"
#include "strategist.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

coach the_coach = {0};

// search budgets in milliseconds
#define THREAT_MS 300
#define EVAL_MS 400
#define PLAY_MS 700

// analysis depth a hint must reach before it is shown; Stockfish gets there
// in well under a second and the move rarely changes after it
#define HINT_MIN_DEPTH 16

#define ELO_MIN 1320
#define ELO_MAX 2800
#define ELO_START 1400

//------------------------------------------------------------------------------
// engine plumbing
//------------------------------------------------------------------------------

static void send(coach *c, const char *line) {
    if (!engine_send(&c->proc, line)) {
        snprintf(c->last_error, sizeof(c->last_error), "engine stopped responding");
        c->phase = COACH_OFF;
    }
}

static void sendf(coach *c, const char *fmt, ...) {
    char buf[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    send(c, buf);
}

static bool is_searching(const coach *c) {
    switch (c->phase) {
    case COACH_THREAT:
    case COACH_ANALYZE:
    case COACH_EVAL_BEFORE:
    case COACH_EVAL_AFTER:
    case COACH_PLAY:
        return true;
    default:
        return false;
    }
}

static void current_fen(piece board[8][8], const game_state *state, char *out,
                        size_t cap) {
    update_full_fen(board, state);
    snprintf(out, cap, "%s", fen_table);
}

static int cp_white_to_human(const coach *c, int cp_white) {
    return c->human_color == White ? cp_white : -cp_white;
}

//------------------------------------------------------------------------------
// starting searches
//------------------------------------------------------------------------------

static void freeze_hints(coach *c) {
    memcpy(c->hint_lines, c->candidates, sizeof(c->hint_lines));
    memcpy(c->hint_valid, c->candidates_valid, sizeof(c->hint_valid));
    c->hint_black_to_move = c->analysis_black_to_move;
    c->hint_frozen = true;
}

static bool analysis_deep_enough(const coach *c) {
    return c->phase == COACH_ANALYZE && c->candidates_valid[0] &&
           c->candidates[0].depth >= HINT_MIN_DEPTH;
}

static void begin_analysis(coach *c, piece board[8][8], const game_state *state) {
    char fen[LONGEST_FEN];
    current_fen(board, state, fen, sizeof(fen));
    memset(c->candidates_valid, 0, sizeof(c->candidates_valid));
    memset(c->hint_valid, 0, sizeof(c->hint_valid));
    c->hint_frozen = false;
    c->analysis_black_to_move = state->turn;
    send(c, "setoption name UCI_LimitStrength value false");
    sendf(c, "setoption name MultiPV value %d", COACH_CANDIDATES);
    sendf(c, "position fen %s", fen);
    send(c, "go infinite");
    c->phase = COACH_ANALYZE;
}

static void begin_threat(coach *c, piece board[8][8], const game_state *state) {
    char fen[LONGEST_FEN], flipped[LONGEST_FEN];
    current_fen(board, state, fen, sizeof(fen));
    if (!uci_fen_flip_side(fen, flipped, sizeof(flipped))) {
        begin_analysis(c, board, state);
        return;
    }
    c->search_last_valid = false;
    send(c, "setoption name UCI_LimitStrength value false");
    send(c, "setoption name MultiPV value 1");
    sendf(c, "position fen %s", flipped);
    sendf(c, "go movetime %d", THREAT_MS);
    c->phase = COACH_THREAT;
}

static void begin_eval(coach *c, const char *fen, coach_phase phase) {
    c->search_last_valid = false;
    send(c, "setoption name UCI_LimitStrength value false");
    send(c, "setoption name MultiPV value 1");
    sendf(c, "position fen %s", fen);
    sendf(c, "go movetime %d", EVAL_MS);
    c->phase = phase;
}

static void begin_play(coach *c, piece board[8][8], const game_state *state) {
    char fen[LONGEST_FEN];
    current_fen(board, state, fen, sizeof(fen));
    c->search_last_valid = false;
    send(c, "setoption name MultiPV value 1");
    send(c, "setoption name UCI_LimitStrength value true");
    sendf(c, "setoption name UCI_Elo value %d", c->engine_elo);
    sendf(c, "position fen %s", fen);
    sendf(c, "go movetime %d", PLAY_MS);
    c->phase = COACH_PLAY;
}

// decides what to do next from the current game state
static void begin_turn(coach *c, piece board[8][8], game_state *state) {
    c->needs_restart = false;
    if (state->game_over) {
        c->phase = COACH_IDLE;
        return;
    }
    color to_move = turn_to_color(state->turn);
    if (to_move == c->human_color) {
        c->show_hint = false;
        c->threat.valid = false;
        // a null-move search from an in-check position is illegal
        if (state->is_in_check[to_move]) {
            if (c->plan_pending)
                coach_ask_strategist(c, board, state);
            begin_analysis(c, board, state);
        } else {
            begin_threat(c, board, state);
        }
        return;
    }
    // engine to move: grade the human's move first if there is one to grade
    if (c->played_move[0]) {
        if (c->before_valid) {
            char fen[LONGEST_FEN];
            current_fen(board, state, fen, sizeof(fen));
            begin_eval(c, fen, COACH_EVAL_AFTER);
        } else {
            begin_eval(c, c->before_fen, COACH_EVAL_BEFORE);
        }
        return;
    }
    begin_play(c, board, state);
}

//------------------------------------------------------------------------------
// grading
//------------------------------------------------------------------------------

static void adapt_elo(coach *c, int cp_loss) {
    c->recent_cp_loss[c->recent_count % 6] = cp_loss;
    c->recent_count++;
    if (c->recent_count % 3 != 0)
        return;
    int n = c->recent_count < 6 ? c->recent_count : 6;
    int sum = 0;
    for (int i = 0; i < n; i++)
        sum += c->recent_cp_loss[i];
    int avg = sum / n;
    if (avg < 30)
        c->engine_elo += 100;
    else if (avg > 120)
        c->engine_elo -= 100;
    if (c->engine_elo < ELO_MIN) c->engine_elo = ELO_MIN;
    if (c->engine_elo > ELO_MAX) c->engine_elo = ELO_MAX;
}

// called with the evaluation of the position after the human's move
static void grade_human_move(coach *c, const game_state *state) {
    coach_feedback *f = &c->feedback;
    memset(f, 0, sizeof(*f));

    int after_white = uci_score_cp_for_white(&c->search_last, state->turn);
    f->eval_after_cp = cp_white_to_human(c, after_white);
    f->eval_before_cp = c->before_best_cp_human;
    f->cp_loss = f->eval_before_cp - f->eval_after_cp;
    if (f->cp_loss < 0)
        f->cp_loss = 0;
    bool played_best = strcmp(c->played_move, c->before_best_move) == 0;
    if (played_best)
        f->cp_loss = 0;
    f->grade = uci_grade_move(f->cp_loss);

    // what was played, from the history
    if (state->history_count > 0)
        snprintf(f->played_san, SAN_MAX, "%s",
                 state->history[state->history_count - 1].san);

    // what the engine preferred, and where it leads (a move graded Best
    // needs no correction even when it differs from the top choice)
    if (f->grade != GRADE_BEST && c->before_best_move[0]) {
        board_pos src, dest;
        piece_type promo;
        if (uci_move_to_squares(c->before_best_move, &src, &dest, &promo))
            move_to_san(c->before_board, src, dest, promo, f->best_san);
        uci_line_to_san(c->before_board, &c->before_state, c->before_best_pv, 5,
                        f->best_line, sizeof(f->best_line));
    }
    f->valid = true;

    c->grade_counts[f->grade]++;
    c->moves_graded++;
    c->total_cp_loss += f->cp_loss;
    adapt_elo(c, f->cp_loss);

    TraceLog(LOG_INFO, "Coach: %s (%s, -%d cp)%s%s", f->played_san,
             uci_grade_name(f->grade), f->cp_loss,
             f->best_san[0] ? ", better was " : "", f->best_san);
    c->played_move[0] = '\0';
    c->before_valid = false;
}

//------------------------------------------------------------------------------
// engine output
//------------------------------------------------------------------------------

static void handle_line(coach *c, const char *line, piece board[8][8],
                        game_state *state) {
    uci_info info;
    char move[UCI_MOVE_MAX];

    switch (c->phase) {
    case COACH_BOOT:
        if (strncmp(line, "id name ", 8) == 0) {
            snprintf(c->engine_name, sizeof(c->engine_name), "%s", line + 8);
        } else if (strcmp(line, "uciok") == 0) {
            send(c, "setoption name Threads value 1");
            send(c, "ucinewgame");
            send(c, "isready");
        } else if (strcmp(line, "readyok") == 0) {
            TraceLog(LOG_INFO, "Coach: %s ready", c->engine_name);
            begin_turn(c, board, state);
        }
        break;

    case COACH_THREAT:
        if (uci_parse_info(line, &info)) {
            if (info.multipv == 1) {
                c->search_last = info;
                c->search_last_valid = true;
            }
        } else if (uci_parse_bestmove(line, move, sizeof(move))) {
            c->threat.valid = false;
            if (c->needs_restart) {
                begin_turn(c, board, state);
                break;
            }
            if (move[0] && c->search_last_valid) {
                board_pos src, dest;
                piece_type promo;
                if (uci_move_to_squares(move, &src, &dest, &promo) &&
                    board[src.row][src.col].type != EMPTY) {
                    snprintf(c->threat.move_uci, sizeof(c->threat.move_uci), "%s", move);
                    move_to_san(board, src, dest, promo, c->threat.san);
                    // score is from the opponent's view (they were to move)
                    int white = uci_score_cp_for_white(&c->search_last, !state->turn);
                    c->threat.cp_for_human = cp_white_to_human(c, white);
                    c->threat.valid = true;
                }
            }
            if (c->plan_pending)
                coach_ask_strategist(c, board, state);
            begin_analysis(c, board, state);
        }
        break;

    case COACH_ANALYZE:
        if (uci_parse_info(line, &info)) {
            int i = info.multipv - 1;
            if (i >= 0 && i < COACH_CANDIDATES) {
                c->candidates[i] = info;
                c->candidates_valid[i] = true;
                if (i == 0)
                    c->eval_cp_white =
                        uci_score_cp_for_white(&info, c->analysis_black_to_move);
            }
            // a requested hint waits for a deep enough line, then locks
            if (!c->hint_frozen && (c->show_hint || c->show_candidates) &&
                analysis_deep_enough(c))
                freeze_hints(c);
        }
        // a bestmove here only arrives after "stop", handled in STOPPING
        break;

    case COACH_STOPPING:
        if (uci_parse_bestmove(line, move, sizeof(move)))
            begin_turn(c, board, state);
        break;

    case COACH_EVAL_BEFORE:
        if (uci_parse_info(line, &info)) {
            if (info.multipv == 1) {
                c->search_last = info;
                c->search_last_valid = true;
            }
        } else if (uci_parse_bestmove(line, move, sizeof(move))) {
            if (c->needs_restart) {
                begin_turn(c, board, state);
                break;
            }
            if (c->search_last_valid) {
                int white = uci_score_cp_for_white(&c->search_last,
                                                   c->before_state.turn);
                c->before_best_cp_human = cp_white_to_human(c, white);
                snprintf(c->before_best_move, sizeof(c->before_best_move), "%s",
                         c->search_last.first_move);
                snprintf(c->before_best_pv, sizeof(c->before_best_pv), "%s",
                         c->search_last.pv);
                c->before_valid = true;
                begin_turn(c, board, state); // -> EVAL_AFTER
            } else {
                c->played_move[0] = '\0';
                begin_play(c, board, state);
            }
        }
        break;

    case COACH_EVAL_AFTER:
        if (uci_parse_info(line, &info)) {
            if (info.multipv == 1) {
                c->search_last = info;
                c->search_last_valid = true;
            }
        } else if (uci_parse_bestmove(line, move, sizeof(move))) {
            if (c->needs_restart) {
                begin_turn(c, board, state);
                break;
            }
            if (c->search_last_valid)
                grade_human_move(c, state);
            else
                c->played_move[0] = '\0';
            begin_play(c, board, state);
        }
        break;

    case COACH_PLAY:
        if (uci_parse_info(line, &info)) {
            if (info.multipv == 1) {
                c->eval_cp_white = uci_score_cp_for_white(&info, state->turn);
                c->search_last = info;
                c->search_last_valid = true;
            }
        } else if (uci_parse_bestmove(line, move, sizeof(move))) {
            if (c->needs_restart) {
                begin_turn(c, board, state);
                break;
            }
            // remember what the engine meant to follow up with, while the
            // position it was thinking from is still on the board
            c->engine_plan_line[0] = '\0';
            if (c->search_last_valid && c->search_last.pv[0]) {
                uci_line_to_san(board, state, c->search_last.pv, 5,
                                c->engine_plan_line, sizeof(c->engine_plan_line));
                c->engine_plan_cp_white = c->eval_cp_white;
            }
            board_pos src, dest;
            piece_type promo;
            if (move[0] && uci_move_to_squares(move, &src, &dest, &promo) &&
                make_move(board, state, src, dest, promo)) {
                c->engine_moved = true;
                c->plan_pending = true;
                c->engine_move_src = src;
                c->engine_move_dest = dest;
                TraceLog(LOG_INFO, "Coach: engine plays %s",
                         state->history[state->history_count - 1].san);
            } else {
                TraceLog(LOG_WARNING, "Coach: engine move '%s' rejected", move);
            }
            begin_turn(c, board, state);
        }
        break;

    default:
        break;
    }
}

//------------------------------------------------------------------------------
// public API
//------------------------------------------------------------------------------

static bool try_spawn(coach *c, const char *path) {
    if (!path || !path[0])
        return false;
    if (engine_spawn(&c->proc, path)) {
        TraceLog(LOG_INFO, "Coach: started engine '%s'", path);
        return true;
    }
    return false;
}

void coach_init(coach *c, const char *explicit_path) {
    memset(c, 0, sizeof(*c));
    c->phase = COACH_OFF;
    c->human_color = White;
    c->engine_elo = ELO_START;
    c->show_threat = true;
    c->show_plan = true;

    char exe_dir[512];
    snprintf(exe_dir, sizeof(exe_dir), "%s", GetApplicationDirectory());
    char bundled[600];
#ifdef _WIN32
    snprintf(bundled, sizeof(bundled), "%sengines\\stockfish.exe", exe_dir);
#else
    snprintf(bundled, sizeof(bundled), "%sengines/stockfish", exe_dir);
#endif

    if (!try_spawn(c, explicit_path) && !try_spawn(c, getenv("MISCHESS_ENGINE")) &&
        !try_spawn(c, bundled) && !try_spawn(c, "stockfish")) {
        snprintf(c->last_error, sizeof(c->last_error),
                 "No UCI engine found. Set MISCHESS_ENGINE or drop "
                 "stockfish into engines/");
        TraceLog(LOG_WARNING, "Coach: %s", c->last_error);
        return;
    }
    c->phase = COACH_BOOT;
    send(c, "uci");
}

void coach_shutdown(coach *c) {
    if (c->proc.running)
        engine_close(&c->proc);
    c->phase = COACH_OFF;
}

void coach_update(coach *c, piece board[8][8], game_state *state) {
    if (c->phase == COACH_OFF)
        return;
    if (!c->proc.running) {
        snprintf(c->last_error, sizeof(c->last_error), "engine process exited");
        TraceLog(LOG_ERROR, "Coach: %s", c->last_error);
        c->phase = COACH_OFF;
        return;
    }
    char line[1024];
    // cap the work per frame so a chatty engine cannot stall rendering
    for (int i = 0; i < 64 && engine_poll_line(&c->proc, line, sizeof(line)); i++)
        handle_line(c, line, board, state);
}

void coach_on_human_move(coach *c, piece board[8][8], game_state *state,
                         board_pos src, board_pos dest, piece_type promo) {
    if (c->phase == COACH_OFF)
        return;
    (void)board;
    uci_squares_to_move(src, dest, promo, c->played_move, sizeof(c->played_move));

    // snapshot the position the move was played from
    const ply_record *rec = &state->history[state->history_count - 1];
    memcpy(c->before_board, rec->board, sizeof(c->before_board));
    c->before_state = *state;
    c->before_state.turn = rec->turn;
    c->before_state.en_passant_square = rec->en_passant_square;
    memcpy(c->before_state.can_castle_short, rec->can_castle_short,
           sizeof(rec->can_castle_short));
    memcpy(c->before_state.can_castle_long, rec->can_castle_long,
           sizeof(rec->can_castle_long));
    c->before_state.move_count = rec->move_count;
    c->before_state.halfmove_clock = rec->halfmove_clock;
    c->before_state.history = NULL;
    c->before_state.history_count = 0;
    c->before_state.history_capacity = 0;
    c->before_state.possible_moves.pos = NULL;
    c->before_state.possible_moves.count = 0;
    c->before_state.possible_moves.capacity = 0;
    c->before_state.game_over = false;
    update_full_fen(c->before_board, &c->before_state);
    snprintf(c->before_fen, sizeof(c->before_fen), "%s", fen_table);
    update_full_fen(board, state); // leave the global FEN on the live position

    // if the analysis was running we already know the best move
    c->before_valid = false;
    if (c->phase == COACH_ANALYZE && c->candidates_valid[0]) {
        int white = uci_score_cp_for_white(&c->candidates[0],
                                           c->analysis_black_to_move);
        c->before_best_cp_human = cp_white_to_human(c, white);
        snprintf(c->before_best_move, sizeof(c->before_best_move), "%s",
                 c->candidates[0].first_move);
        snprintf(c->before_best_pv, sizeof(c->before_best_pv), "%s",
                 c->candidates[0].pv);
        c->before_valid = true;
    }
    coach_on_position_changed(c, board, state);
}

void coach_on_position_changed(coach *c, piece board[8][8],
                               game_state *state) {
    if (c->phase == COACH_OFF || c->phase == COACH_BOOT)
        return;
    c->show_hint = false;
    c->plan_pending = false;
    strategist_clear_answer();
    if (is_searching(c)) {
        // the running search is stale; its bestmove is discarded
        c->needs_restart = true;
        send(c, "stop");
        c->phase = COACH_STOPPING;
        return;
    }
    // a bestmove is still pending; it will re-plan when it arrives
    if (c->phase == COACH_STOPPING)
        return;
    begin_turn(c, board, state);
}

void coach_ask_strategist(coach *c, piece board[8][8], const game_state *state) {
    c->plan_pending = false;
    if (strategist_get_state() == STRATEGIST_OFF)
        return;

    char fen[LONGEST_FEN];
    current_fen(board, state, fen, sizeof(fen));
    const char *me = c->human_color == White ? "White" : "Black";
    const char *them = c->human_color == White ? "Black" : "White";

    // the last few moves, numbered like a score sheet
    char recent[512] = "";
    size_t n = 0;
    size_t first = state->history_count > 10 ? state->history_count - 10 : 0;
    for (size_t i = first; i < state->history_count && n < sizeof(recent) - 16; i++) {
        const ply_record *r = &state->history[i];
        if (!r->turn || i == first)
            n += (size_t)snprintf(recent + n, sizeof(recent) - n, "%s%zu%s ",
                                  n ? " " : "", r->move_count / 2 + 1,
                                  r->turn ? "..." : ".");
        n += (size_t)snprintf(recent + n, sizeof(recent) - n, "%s", r->san);
        if (!r->turn) n += (size_t)snprintf(recent + n, sizeof(recent) - n, " ");
    }
    const char *last_san = state->history_count
                               ? state->history[state->history_count - 1].san
                               : "(none)";

    char eval[16], threat_eval[16];
    uci_format_score(cp_white_to_human(c, c->eval_cp_white), eval, sizeof(eval));
    uci_format_score(c->threat.cp_for_human, threat_eval, sizeof(threat_eval));

    char prompt[STRATEGIST_PROMPT_MAX];
    snprintf(prompt, sizeof(prompt),
             "The player has the %s pieces and it is the player's move. The opponent has %s.\n"
             "Position (FEN): %s\n"
             "Recent moves: %s\n"
             "The opponent just played %s. Engine evaluation now: %s for the player.\n"
             "%s%s%s"
             "%s%s%s%s%s"
             "Explain the opponent's plan (the %s moves in that line) and what the player must watch out for.",
             me, them, fen, recent[0] ? recent : "(game start)", last_san, eval,
             c->engine_plan_line[0] ? "Engine line the opponent was counting on (both sides' moves, standard numbering): " : "",
             c->engine_plan_line[0] ? c->engine_plan_line : "",
             c->engine_plan_line[0] ? "\n" : "",
             c->threat.valid ? "If the player passed, the opponent would play " : "",
             c->threat.valid ? c->threat.san : "",
             c->threat.valid ? " (evaluation then " : "",
             c->threat.valid ? threat_eval : "",
             c->threat.valid ? " for the player).\n" : "", them);
    strategist_ask(prompt);
}

void coach_switch_sides(coach *c, piece board[8][8], game_state *state) {
    c->human_color = opposite(c->human_color);
    c->played_move[0] = '\0';
    c->before_valid = false;
    c->feedback.valid = false;
    coach_on_position_changed(c, board, state);
}

void coach_new_game(coach *c, piece board[8][8], game_state *state) {
    c->played_move[0] = '\0';
    c->before_valid = false;
    c->feedback.valid = false;
    c->threat.valid = false;
    memset(c->grade_counts, 0, sizeof(c->grade_counts));
    c->moves_graded = 0;
    c->total_cp_loss = 0;
    c->recent_count = 0;
    c->hints_used = 0;
    c->eval_cp_white = 0;
    coach_on_position_changed(c, board, state);
}

bool coach_active(const coach *c) {
    return c->phase != COACH_OFF;
}

bool coach_engine_thinking(const coach *c) {
    return c->phase == COACH_PLAY || c->phase == COACH_EVAL_AFTER ||
           c->phase == COACH_EVAL_BEFORE;
}

bool coach_is_human_turn(const coach *c, const game_state *state) {
    return turn_to_color(state->turn) == c->human_color;
}

bool coach_take_engine_move(coach *c, board_pos *src, board_pos *dest) {
    if (!c->engine_moved)
        return false;
    c->engine_moved = false;
    *src = c->engine_move_src;
    *dest = c->engine_move_dest;
    return true;
}

void coach_request_hints(coach *c) {
    if (!c->hint_frozen && analysis_deep_enough(c))
        freeze_hints(c);
}

const char *coach_hint_move(const coach *c) {
    if (c->hint_frozen && c->hint_valid[0])
        return c->hint_lines[0].first_move;
    return NULL;
}
