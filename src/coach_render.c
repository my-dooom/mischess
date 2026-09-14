#include "coach.h"
#include "coach_render.h"
#include "notation.h"
#include "render.h"
#include "strategist.h"
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

//------------------------------------------------------------------------------
// side panel: a column of cards, each with a coloured stripe and a title.
// Every section is drawn twice: a measuring pass sizes the card, the second
// pass paints it. That keeps the card backgrounds behind wrapped text
// without knowing the heights up front.
//------------------------------------------------------------------------------

typedef struct {
    int x, w;       // content column
    int font, small;
    bool draw;      // false = measure only
} panel_ctx;

static const Color CARD_BG = {0x2f, 0x29, 0x42, 0xff};
static const Color TEXT_MAIN = {0xee, 0xea, 0xf6, 0xff};
static const Color TEXT_MUTED = {0xa3, 0x9c, 0xb8, 0xff};
static const Color ACCENT_PLAN = {0xc9, 0x9d, 0xf5, 0xff};
static const Color ACCENT_HINT = {0x8b, 0xe0, 0x8a, 0xff};
static const Color ACCENT_LINES = {0x84, 0xc5, 0xf5, 0xff};
static const Color ACCENT_THREAT = {0xf0, 0x7a, 0x6e, 0xff};
static const Color ACCENT_GOLD = {0xf2, 0xc9, 0x6b, 0xff};

// "12." or "12..." move numbers in a SAN line
static bool is_move_number(const char *word) {
    const char *p = word;
    if (*p < '0' || *p > '9') return false;
    while (*p >= '0' && *p <= '9') p++;
    return strcmp(p, ".") == 0 || strcmp(p, "...") == 0;
}

// draws one line word by word; in SAN mode the move numbers are dimmed so
// the moves themselves stand out
static void draw_line(const char *line, int x, int y, int font, Color col,
                      bool san) {
    if (!san) {
        DrawText(line, x, y, font, col);
        return;
    }
    Color dim = Fade(col, 0.45f);
    int space = MeasureText(" ", font) + font / 4;
    char buf[256];
    snprintf(buf, sizeof(buf), "%s", line);
    char *cursor = buf;
    while (*cursor) {
        while (*cursor == ' ') { cursor++; x += space; }
        if (!*cursor) break;
        char *end = cursor;
        while (*end && *end != ' ') end++;
        char saved = *end;
        *end = 0;
        DrawText(cursor, x, y, font, is_move_number(cursor) ? dim : col);
        x += MeasureText(cursor, font);
        *end = saved;
        cursor = end;
    }
}

static int wrap_text_ex(const char *text, int x, int y, int max_w, int font,
                        Color col, bool draw, bool san) {
    // greedy word wrap, returns the y after the last line
    char line[256] = "";
    const char *p = text;
    while (*p) {
        // a newline in the text forces a break
        if (*p == '\n') {
            if (draw && line[0]) draw_line(line, x, y, font, col, san);
            if (line[0] || (p > text && p[-1] == '\n')) y += font + 4;
            line[0] = '\0';
            p++;
            continue;
        }
        size_t wl = strcspn(p, " \n");
        char word[64];
        snprintf(word, sizeof(word), "%.*s", (int)wl, p);
        char trial[256];
        snprintf(trial, sizeof(trial), "%s%s%s", line, line[0] ? " " : "", word);
        if (line[0] && MeasureText(trial, font) > max_w) {
            if (draw) draw_line(line, x, y, font, col, san);
            y += font + 4;
            snprintf(line, sizeof(line), "%s", word);
        } else {
            snprintf(line, sizeof(line), "%s", trial);
        }
        p += wl;
        while (*p == ' ') p++;
    }
    if (line[0]) {
        if (draw) draw_line(line, x, y, font, col, san);
        y += font + 4;
    }
    return y;
}

static int wrap_text(const char *text, int x, int y, int max_w, int font,
                     Color col, bool draw) {
    return wrap_text_ex(text, x, y, max_w, font, col, draw, false);
}

static int text(panel_ctx *p, int y, const char *txt, int font, Color col) {
    if (p->draw) DrawText(txt, p->x, y, font, col);
    return y + font + 4;
}

static int para(panel_ctx *p, int y, const char *txt, int indent, Color col) {
    return wrap_text(txt, p->x + indent, y, p->w - indent, p->small, col,
                     p->draw);
}

// a SAN line: moves in the main text colour, move numbers dimmed
static int san_para(panel_ctx *p, int y, const char *txt, int indent) {
    return wrap_text_ex(txt, p->x + indent, y, p->w - indent, p->small,
                        TEXT_MAIN, p->draw, true);
}

typedef int (*card_body)(panel_ctx *p, int y, const coach *c,
                         const game_state *state);

// a rounded card with a stripe on the left and a title; body drawn inside
static int card(panel_ctx *p, int y, const char *title, Color accent,
                card_body body, const coach *c, const game_state *state) {
    const int pad = 10, stripe = 4, gap = 10;
    panel_ctx inner = *p;
    inner.x = p->x + stripe + pad;
    inner.w = p->w - stripe - 2 * pad;

    // measure
    inner.draw = false;
    int cy = y + pad;
    if (title) cy = text(&inner, cy, title, p->small, accent);
    cy = body(&inner, cy, c, state);
    int h = cy - y + pad - 4;

    if (p->draw) {
        Rectangle r = {(float)p->x, (float)y, (float)p->w, (float)h};
        DrawRectangleRounded(r, 0.12f, 6, CARD_BG);
        DrawRectangleRounded((Rectangle){r.x, r.y, (float)stripe + 6, r.height},
                             0.5f, 6, accent);
        DrawRectangle(p->x + stripe, y, 6, h, CARD_BG);
        inner.draw = true;
        cy = y + pad;
        if (title) cy = text(&inner, cy, title, p->small, accent);
        body(&inner, cy, c, state);
    }
    return y + h + gap;
}

//------------------------------------------------------------------------------
// sections
//------------------------------------------------------------------------------

static int body_feedback(panel_ctx *p, int y, const coach *c,
                         const game_state *state) {
    (void)state;
    const coach_feedback *f = &c->feedback;
    Color gc = grade_color(f->grade);
    char head[64];
    snprintf(head, sizeof(head), "%s   %s", f->played_san, uci_grade_name(f->grade));
    y = text(p, y, head, p->font, gc);
    char before[16], after[16];
    uci_format_score(f->eval_before_cp, before, sizeof(before));
    uci_format_score(f->eval_after_cp, after, sizeof(after));
    y = text(p, y, TextFormat("%s  to  %s   (lost %d cp)", before, after, f->cp_loss),
             p->small, TEXT_MUTED);
    if (f->best_san[0]) {
        y += 2;
        y = text(p, y, TextFormat("Better: %s", f->best_san), p->font, TEXT_MAIN);
        y = san_para(p, y, f->best_line, 16);
    }
    return y;
}

static int body_threat(panel_ctx *p, int y, const coach *c,
                       const game_state *state) {
    (void)state;
    char sc[16];
    uci_format_score(c->threat.cp_for_human, sc, sizeof(sc));
    y = text(p, y, TextFormat("%s   %s", c->threat.san, sc), p->font, TEXT_MAIN);
    const char *why = c->threat.cp_for_human < -150 ? "serious: deal with it now"
                      : c->threat.cp_for_human < -50 ? "worth preventing"
                                                      : "not dangerous yet";
    return para(p, y, TextFormat("If you passed, the opponent plays this: %s.", why),
                0, TEXT_MUTED);
}

static int body_plan(panel_ctx *p, int y, const coach *c,
                     const game_state *state) {
    (void)c; (void)state;
    strategist_state st = strategist_get_state();
    char t[STRATEGIST_ANSWER_MAX];
    if (st == STRATEGIST_OFF)
        return para(p, y, strategist_last_error(), 0, TEXT_MUTED);
    if (st == STRATEGIST_LOADING)
        return text(p, y, "loading model...", p->small, TEXT_MUTED);
    if (strategist_answer(t, sizeof(t))) {
        // "White: ... Black: ..." reads better as two paragraphs
        char *black = strstr(t, "Black:");
        if (black && black != t) {
            char *cut = black;
            while (cut > t && (cut[-1] == ' ' || cut[-1] == '\n')) cut--;
            *cut = '\0';
            y = para(p, y, t, 0, TEXT_MAIN);
            y += 4;
            return para(p, y, black, 0, TEXT_MAIN);
        }
        return para(p, y, t, 0, TEXT_MAIN);
    }
    if (st == STRATEGIST_WRITING)
        return text(p, y, "thinking...", p->small, TEXT_MUTED);
    return text(p, y, "waiting for the opponent's next move", p->small, TEXT_MUTED);
}

static int body_hint(panel_ctx *p, int y, const coach *c,
                     const game_state *state) {
    (void)state;
    if (!c->hint_frozen)
        return text(p, y, "analysing...", p->small, TEXT_MUTED);
    char mv[SAN_MAX + 8], line[COACH_LINE_MAX];
    first_move_san(c->hint_lines[0].pv, mv, sizeof(mv));
    uci_line_to_san_current(c->hint_lines[0].pv, 4, line, sizeof(line));
    y = text(p, y, mv, p->font + 8, ACCENT_HINT);
    return san_para(p, y, line, 16);
}

static int body_candidates(panel_ctx *p, int y, const coach *c,
                           const game_state *state) {
    (void)state;
    if (!c->hint_frozen)
        return text(p, y, "analysing...", p->small, TEXT_MUTED);
    for (int i = 0; i < COACH_CANDIDATES; i++) {
        if (!c->hint_valid[i])
            continue;
        char mv[SAN_MAX + 8], line[COACH_LINE_MAX], sc[16];
        first_move_san(c->hint_lines[i].pv, mv, sizeof(mv));
        uci_line_to_san_current(c->hint_lines[i].pv, 4, line, sizeof(line));
        int cp = uci_score_cp_for_white(&c->hint_lines[i], c->hint_black_to_move);
        uci_format_score(c->human_color == White ? cp : -cp, sc, sizeof(sc));
        const char *head = TextFormat("%d.  %s", i + 1, mv);
        if (p->draw) {
            DrawText(head, p->x, y, p->font, i == 0 ? TEXT_MAIN : TEXT_MUTED);
            DrawText(sc, p->x + MeasureText(head, p->font) + 12,
                     y + (p->font - p->small), p->small, TEXT_MUTED);
        }
        y += p->font + 2;
        y = san_para(p, y, line, 24);
        y += 2;
    }
    return y;
}

static int body_summary(panel_ctx *p, int y, const coach *c,
                        const game_state *state) {
    (void)state;
    for (int g = 0; g < GRADE_COUNT; g++) {
        const char *row = TextFormat("%-12s %d", uci_grade_name((move_grade)g),
                                     c->grade_counts[g]);
        y = text(p, y, row, p->small, grade_color((move_grade)g));
    }
    return text(p, y, TextFormat("avg loss %d cp/move, %d hints",
                                 c->total_cp_loss / c->moves_graded, c->hints_used),
                p->small, TEXT_MUTED);
}

// a horizontal eval bar: white share from the left, score printed on top
static void draw_eval_bar(int x, int y, int w, int h, int cp_white, int font) {
    float p = win_prob(cp_white);
    int white_w = (int)(w * p);
    Rectangle r = {(float)x, (float)y, (float)w, (float)h};
    DrawRectangleRounded(r, 0.5f, 6, (Color){0x1a, 0x16, 0x26, 0xff});
    if (white_w > 0)
        DrawRectangleRounded((Rectangle){r.x, r.y, (float)white_w, r.height},
                             0.5f, 6, RAYWHITE);
    DrawRectangle(x + w / 2 - 1, y, 2, h, (Color){0xf0, 0x7a, 0x6e, 0xa0});
    char txt[16];
    uci_format_score(cp_white, txt, sizeof(txt));
    int tw = MeasureText(txt, font);
    // print on whichever side has room, in the contrasting colour
    bool on_white = p >= 0.5f;
    int tx = on_white ? x + 8 : x + w - tw - 8;
    DrawText(txt, tx, y + (h - font) / 2, font, on_white ? BLACK : RAYWHITE);
}

int draw_coach_panel(int x0, int y0, int w, int h, const coach *c,
                     const game_state *state) {
    panel_ctx p = {x0, w, ui_font(h * 0.028f), 0, true};
    if (p.font < 14) p.font = 14;
    p.small = p.font - 3;
    int y = y0;

    // header: title, engine, strength
    DrawText("COACH", x0, y, p.font + 6, TEXT_MAIN);
    if (coach_active(c)) {
        const char *sub = TextFormat("%s   Elo %d, plays %s", c->engine_name,
                                     c->engine_elo,
                                     c->human_color == White ? "Black" : "White");
        DrawText(sub, x0 + MeasureText("COACH", p.font + 6) + 14,
                 y + (p.font + 6 - p.small), p.small, TEXT_MUTED);
    }
    y += p.font + 14;

    if (!coach_active(c)) {
        y = wrap_text(c->last_error, x0, y, w, p.small, TEXT_MUTED, true);
        return y + 8;
    }

    // status with a coloured dot
    const char *status = "";
    Color dot = TEXT_MUTED;
    switch (c->phase) {
    case COACH_BOOT:        status = "starting engine"; break;
    case COACH_THREAT:      status = "looking for threats"; dot = ACCENT_THREAT; break;
    case COACH_ANALYZE:     status = "your move"; dot = ACCENT_HINT; break;
    case COACH_EVAL_BEFORE:
    case COACH_EVAL_AFTER:  status = "grading your move"; dot = ACCENT_GOLD; break;
    case COACH_PLAY:        status = "engine is thinking"; dot = ACCENT_LINES; break;
    case COACH_IDLE:        status = state->game_over ? "game over" : ""; break;
    default: break;
    }
    if (status[0]) {
        DrawCircle(x0 + 6, y + p.small / 2 + 1, 5, dot);
        DrawText(status, x0 + 18, y, p.small, TEXT_MAIN);
        y += p.small + 10;
    }

    draw_eval_bar(x0, y, w, p.small + 8, c->eval_cp_white, p.small);
    y += p.small + 8 + 14;

    bool human_turn = coach_is_human_turn(c, state) && !state->game_over;

    if (c->feedback.valid)
        y = card(&p, y, "YOUR LAST MOVE", grade_color(c->feedback.grade),
                 body_feedback, c, state);

    if (c->show_threat && c->threat.valid && human_turn)
        y = card(&p, y, "THREAT", ACCENT_THREAT, body_threat, c, state);

    if (c->show_plan)
        y = card(&p, y, strategist_get_state() == STRATEGIST_WRITING
                            ? "PLANS  (writing...)"
                            : "PLANS",
                 ACCENT_PLAN, body_plan, c, state);

    if (c->show_candidates && human_turn)
        y = card(&p, y, "CANDIDATE MOVES", ACCENT_LINES, body_candidates, c, state);
    else if (c->show_hint && human_turn)
        y = card(&p, y, "HINT", ACCENT_HINT, body_hint, c, state);

    if (state->game_over && c->moves_graded > 0)
        y = card(&p, y, "YOUR ACCURACY THIS GAME", ACCENT_GOLD, body_summary, c,
                 state);

    return y;
}

// key legend, pinned to the bottom of the panel
void draw_key_legend(int x0, int y_bottom, int w, int h) {
    int small = ui_font(h * 0.028f) - 3;
    if (small < 11) small = 11;
    const char *keys = "H hint   C lines   T threat   P plan   M moves   U undo   R new   S sides   +/- text";
    int height = wrap_text(keys, x0, 0, w, small, TEXT_MUTED, false);
    wrap_text(keys, x0, y_bottom - height, w, small, TEXT_MUTED, true);
}
