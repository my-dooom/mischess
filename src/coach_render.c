#include "coach.h"
#include "coach_render.h"
#include "notation.h"
#include "render.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static Color grade_color(move_grade g) {
    switch (g) {
    case GRADE_BEST:       return (Color){0x6c, 0xd9, 0x7a, 0xff};
    case GRADE_GOOD:       return (Color){0xa8, 0xd8, 0x8c, 0xff};
    case GRADE_INACCURACY: return (Color){0xf2, 0xd0, 0x5e, 0xff};
    case GRADE_MISTAKE:    return (Color){0xf2, 0x9a, 0x4e, 0xff};
    case GRADE_BLUNDER:    return (Color){0xe8, 0x5a, 0x5a, 0xff};
    default:               return WHITE;
    }
}

static Vector2 square_center(board_pos p, float ts) {
    return (Vector2){p.col * ts + ts / 2, p.row * ts + ts / 2};
}

// thick scales the shaft and head; > 1 drawn first in a dark colour gives
// the arrow an outline
static void draw_arrow(board_pos src, board_pos dest, float ts, Color col,
                       float thick) {
    Vector2 a = square_center(src, ts), b = square_center(dest, ts);
    Vector2 d = {b.x - a.x, b.y - a.y};
    float len = sqrtf(d.x * d.x + d.y * d.y);
    if (len < 1.0f)
        return;
    d.x /= len;
    d.y /= len;
    float head = ts * 0.35f * thick;
    float shaft_w = ts * 0.14f * thick;
    // start a little out of the source square center, stop before the head
    Vector2 start = {a.x + d.x * ts * 0.2f, a.y + d.y * ts * 0.2f};
    Vector2 tip = {b.x - d.x * ts * 0.1f, b.y - d.y * ts * 0.1f};
    Vector2 base = {tip.x - d.x * head, tip.y - d.y * head};
    DrawLineEx(start, base, shaft_w, col);
    Vector2 n = {-d.y, d.x};
    Vector2 l = {base.x + n.x * head * 0.6f, base.y + n.y * head * 0.6f};
    Vector2 r = {base.x - n.x * head * 0.6f, base.y - n.y * head * 0.6f};
    DrawTriangle(tip, r, l, col);
    DrawTriangle(tip, l, r, col);
}

static bool arrow_for_move(const char *uci, board_pos *src, board_pos *dest) {
    piece_type promo;
    return uci && uci[0] && uci_move_to_squares(uci, src, dest, &promo);
}

// a move the player is meant to read at a glance: both squares tinted, a
// thick arrow with a dark outline so it stands out on either square colour
static void draw_move_highlight(board_pos src, board_pos dest, float ts,
                                Color col) {
    DrawRectangle((int)(src.col * ts), (int)(src.row * ts), (int)ts, (int)ts,
                  Fade(col, 0.35f));
    DrawRectangleLinesEx((Rectangle){dest.col * ts, dest.row * ts, ts, ts},
                         4.0f, col);
    draw_arrow(src, dest, ts, (Color){0, 0, 0, 160}, 1.3f);
    draw_arrow(src, dest, ts, col, 1.0f);
}

// SAN of the first move of a UCI line from the live position ("Nf3")
static void first_move_san(const char *pv, char *out, size_t cap) {
    char numbered[COACH_LINE_MAX];
    uci_line_to_san_current(pv, 1, numbered, sizeof(numbered));
    // strip the "3." / "3..." prefix
    const char *sp = strchr(numbered, ' ');
    snprintf(out, cap, "%s", sp ? sp + 1 : numbered);
}

void draw_coach_overlay(float scale, const coach *c, const game_state *state) {
    if (!coach_active(c))
        return;
    float ts = 16.0f * scale;
    int font = ui_font(ts * 0.25f);
    board_pos src, dest;

    if (c->show_threat && c->threat.valid && coach_is_human_turn(c, state) &&
        arrow_for_move(c->threat.move_uci, &src, &dest)) {
        draw_arrow(src, dest, ts, (Color){0, 0, 0, 120}, 1.3f);
        draw_arrow(src, dest, ts, Fade(RED, 0.7f), 1.0f);
    }

    if (c->show_candidates && c->hint_frozen && coach_is_human_turn(c, state)) {
        for (int i = COACH_CANDIDATES - 1; i >= 0; i--) {
            if (!c->hint_valid[i])
                continue;
            if (!arrow_for_move(c->hint_lines[i].first_move, &src, &dest))
                continue;
            float alpha = i == 0 ? 0.85f : (i == 1 ? 0.6f : 0.45f);
            draw_arrow(src, dest, ts, (Color){0, 0, 0, 140}, 1.3f);
            draw_arrow(src, dest, ts, Fade(SKYBLUE, alpha), 1.0f);
            int cp = uci_score_cp_for_white(&c->hint_lines[i],
                                            c->hint_black_to_move);
            char txt[16];
            uci_format_score(c->human_color == White ? cp : -cp, txt, sizeof(txt));
            Vector2 p = square_center(dest, ts);
            int w = MeasureText(txt, font);
            DrawRectangle((int)p.x - w / 2 - 3, (int)p.y - font / 2 - 2, w + 6,
                          font + 4, (Color){0, 0, 0, 170});
            DrawText(txt, (int)p.x - w / 2, (int)p.y - font / 2, font, WHITE);
        }
    } else if (c->show_hint && coach_is_human_turn(c, state) &&
               arrow_for_move(coach_hint_move(c), &src, &dest)) {
        draw_move_highlight(src, dest, ts, LIME);
    }
}

// win probability from centipawns, the usual logistic curve
static float win_prob(int cp) {
    return 1.0f / (1.0f + powf(10.0f, -cp / 400.0f));
}

static void draw_eval_bar(int x, int y, int w, int h, int cp_white,
                          int font) {
    float p = win_prob(cp_white);
    int white_h = (int)(h * p);
    DrawRectangle(x, y, w, h, (Color){0x22, 0x1c, 0x2e, 0xff});
    DrawRectangle(x, y + h - white_h, w, white_h, RAYWHITE);
    DrawRectangleLines(x, y, w, h, GRAY);
    // midline marks the equal position
    DrawLine(x, y + h / 2, x + w, y + h / 2, (Color){255, 0, 0, 120});
    char txt[16];
    uci_format_score(cp_white, txt, sizeof(txt));
    int tw = MeasureText(txt, font);
    DrawText(txt, x + (w - tw) / 2, y + h + 4, font, LIGHTGRAY);
}

static int draw_wrapped(const char *text, int x, int y, int max_w, int font,
                        Color col) {
    // greedy word wrap, returns the y after the last line
    char line[256] = "";
    const char *p = text;
    while (*p) {
        const char *end = strchr(p, ' ');
        size_t wl = end ? (size_t)(end - p) : strlen(p);
        char word[64];
        snprintf(word, sizeof(word), "%.*s", (int)wl, p);
        char trial[256];
        snprintf(trial, sizeof(trial), "%s%s%s", line, line[0] ? " " : "", word);
        if (line[0] && MeasureText(trial, font) > max_w) {
            DrawText(line, x, y, font, col);
            y += font + 4;
            snprintf(line, sizeof(line), "%s", word);
        } else {
            snprintf(line, sizeof(line), "%s", trial);
        }
        p += wl;
        while (*p == ' ') p++;
    }
    if (line[0]) {
        DrawText(line, x, y, font, col);
        y += font + 4;
    }
    return y;
}

int draw_coach_panel(int x0, int y0, int w, int h, const coach *c,
                     const game_state *state) {
    int font = ui_font(h * 0.028f);
    if (font < 14) font = 14;
    int small = font - 3;
    int y = y0;

    if (!coach_active(c)) {
        DrawText("Coach: off", x0, y, font, GRAY);
        y += font + 4;
        y = draw_wrapped(c->last_error, x0, y, w, small, GRAY);
        return y + 8;
    }

    // eval bar down the left edge of the coach block
    int bar_w = 30;
    int bar_h = (int)(h * 0.36f);
    draw_eval_bar(x0, y, bar_w, bar_h, c->eval_cp_white, small);
    int tx = x0 + bar_w + 14;
    int tw = w - bar_w - 14;

    DrawText(TextFormat("Coach  |  %s", c->engine_name), tx, y, font, WHITE);
    y += font + 4;
    DrawText(TextFormat("Engine plays %s at Elo %d",
                        c->human_color == White ? "Black" : "White",
                        c->engine_elo),
             tx, y, small, LIGHTGRAY);
    y += small + 8;

    // status line
    const char *status = "";
    switch (c->phase) {
    case COACH_BOOT:        status = "starting engine..."; break;
    case COACH_THREAT:      status = "looking for threats..."; break;
    case COACH_ANALYZE:     status = "your move  (H hint, C lines, T threat)"; break;
    case COACH_EVAL_BEFORE:
    case COACH_EVAL_AFTER:  status = "grading your move..."; break;
    case COACH_PLAY:        status = "engine is thinking..."; break;
    case COACH_STOPPING:    status = "..."; break;
    default: break;
    }
    if (status[0]) {
        DrawText(status, tx, y, small, SKYBLUE);
        y += small + 8;
    }

    // feedback on the last human move
    if (c->feedback.valid) {
        const coach_feedback *f = &c->feedback;
        char head[64];
        snprintf(head, sizeof(head), "%s  -  %s", f->played_san,
                 uci_grade_name(f->grade));
        DrawText(head, tx, y, font, grade_color(f->grade));
        y += font + 4;
        char before[16], after[16];
        uci_format_score(f->eval_before_cp, before, sizeof(before));
        uci_format_score(f->eval_after_cp, after, sizeof(after));
        DrawText(TextFormat("eval %s -> %s  (lost %d cp)", before, after,
                            f->cp_loss),
                 tx, y, small, LIGHTGRAY);
        y += small + 4;
        if (f->best_san[0]) {
            y = draw_wrapped(TextFormat("Better was %s: %s", f->best_san,
                                        f->best_line),
                             tx, y, tw, small, WHITE);
        }
        y += 6;
    }

    // what the opponent threatens right now
    if (c->show_threat && c->threat.valid && coach_is_human_turn(c, state)) {
        char sc[16];
        uci_format_score(c->threat.cp_for_human, sc, sizeof(sc));
        // only worth flagging when giving them the move would hurt
        Color col = c->threat.cp_for_human < -150 ? RED
                    : c->threat.cp_for_human < -50 ? ORANGE
                                                    : LIGHTGRAY;
        y = draw_wrapped(TextFormat("Threat: if you passed, %s (%s)",
                                    c->threat.san, sc),
                         tx, y, tw, small, col);
        y += 6;
    }

    // hint / candidate lines, from the frozen snapshot
    bool want_lines = (c->show_hint || c->show_candidates) &&
                      coach_is_human_turn(c, state) && !state->game_over;
    if (want_lines && !c->hint_frozen) {
        DrawText("Hint: analysing...", tx, y, font, LIME);
        y += font + 6;
    } else if (c->show_candidates && c->hint_frozen) {
        DrawText("Candidate moves", tx, y, font, SKYBLUE);
        y += font + 4;
        for (int i = 0; i < COACH_CANDIDATES; i++) {
            if (!c->hint_valid[i])
                continue;
            char mv[SAN_MAX + 8], line[COACH_LINE_MAX], sc[16];
            first_move_san(c->hint_lines[i].pv, mv, sizeof(mv));
            uci_line_to_san_current(c->hint_lines[i].pv, 4, line, sizeof(line));
            int cp = uci_score_cp_for_white(&c->hint_lines[i],
                                            c->hint_black_to_move);
            uci_format_score(c->human_color == White ? cp : -cp, sc, sizeof(sc));
            // the move itself big, the eval next to it, the line underneath
            DrawText(TextFormat("%d.  %s", i + 1, mv), tx, y, font,
                     i == 0 ? WHITE : LIGHTGRAY);
            int mw = MeasureText(TextFormat("%d.  %s", i + 1, mv), font);
            DrawText(sc, tx + mw + 12, y + (font - small), small, GRAY);
            y += font + 2;
            y = draw_wrapped(line, tx + 24, y, tw - 24, small, GRAY);
            y += 4;
        }
        y += 4;
    } else if (c->show_hint && c->hint_frozen && coach_hint_move(c)) {
        char mv[SAN_MAX + 8], line[COACH_LINE_MAX];
        first_move_san(c->hint_lines[0].pv, mv, sizeof(mv));
        uci_line_to_san_current(c->hint_lines[0].pv, 4, line, sizeof(line));
        DrawText(TextFormat("Hint: %s", mv), tx, y, font + 6, LIME);
        y += font + 10;
        y = draw_wrapped(line, tx + 24, y, tw - 24, small, GRAY);
        y += 6;
    }

    // game summary
    if (state->game_over && c->moves_graded > 0) {
        y += 4;
        DrawText("Your accuracy this game", tx, y, font, GOLD);
        y += font + 4;
        for (int g = 0; g < GRADE_COUNT; g++) {
            DrawText(TextFormat("%-12s %d", uci_grade_name((move_grade)g),
                                c->grade_counts[g]),
                     tx, y, small, grade_color((move_grade)g));
            y += small + 2;
        }
        DrawText(TextFormat("avg loss %d cp/move, %d hints",
                            c->total_cp_loss / c->moves_graded, c->hints_used),
                 tx, y, small, LIGHTGRAY);
        y += small + 6;
    }

    int bottom = y0 + bar_h + small + 12;
    return y > bottom ? y : bottom;
}
