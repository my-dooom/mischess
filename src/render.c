#include "render.h"
#include "game.h"
#include "raylib.h"

Rectangle piece_rects[2][7] = {
    // White pieces
    [White] =
        {
            [EMPTY] = {64, 192, 16, 16},
            [PAWN] = {96, 224, 16, 16},
            [KNIGHT] = {80, 224, 16, 16},
            [BISHOP] = {64, 224, 16, 16},
            [ROOK] = {48, 224, 16, 16},
            [QUEEN] = {32, 224, 16, 16},
            [KING] = {16, 224, 16, 16},
        },
    // Black pieces
    [Black] =
        {
            [EMPTY] = {64, 192, 16, 16},
            [PAWN] = {96, 208, 16, 16},
            [KNIGHT] = {80, 208, 16, 16},
            [BISHOP] = {64, 208, 16, 16},
            [ROOK] = {48, 208, 16, 16},
            [QUEEN] = {32, 208, 16, 16},
            [KING] = {16, 208, 16, 16},
        },

};

Rectangle selected_tile_rect = {48, 192, 16, 16};

move_animation current_anim = {0};

const piece_type promotion_choices[PROMOTION_CHOICE_COUNT] = {QUEEN, ROOK,
                                                              BISHOP, KNIGHT};

void start_move_animation(move_animation *anim, piece p, board_pos src,
                          board_pos dest) {
    anim->active = true;
    anim->animating_piece = p;
    anim->src = src;
    anim->dest = dest;
    anim->progress = 0.0f;
    anim->duration = 0.18f;
}

void update_animation(move_animation *anim) {
    if (!anim->active)
        return;
    anim->progress += GetFrameTime() / anim->duration;
    if (anim->progress >= 1.0f) {
        anim->progress = 1.0f;
        anim->active = false;
    }
}

void draw_animation(Texture *tex_pattern, move_animation *anim, float scale) {
    if (!anim->active)
        return;
    float ts = 16.0f * scale;
    float t = anim->progress;
    // ease-out quadratic
    float et = 1.0f - (1.0f - t) * (1.0f - t);
    float cur_x = anim->src.col * ts + (anim->dest.col - anim->src.col) * ts * et;
    float cur_y = anim->src.row * ts + (anim->dest.row - anim->src.row) * ts * et;
    Rectangle src_rect =
        piece_rects[anim->animating_piece.color][anim->animating_piece.type];
    Rectangle dest_rect = {cur_x, cur_y, ts, ts};
    DrawTexturePro(*tex_pattern, src_rect, dest_rect, (Vector2){0, 0}, 0.0f,
                   WHITE);
}

void draw_selection_highlight(float scale, board_pos *selection) {
    if (selection->row == -1 || selection->col == -1)
        return; // No selection

    float ts = 16.0f * scale;
    Rectangle dest = {selection->col * ts, selection->row * ts, ts, ts};
    DrawRectangleLinesEx(dest, 2.0f, YELLOW);
}

void draw_pieces(Texture *tex_pattern, float scale) {
    float ts = 16.0f * scale;
    for (int row = 0; row < 8; row++) {
        for (int col = 0; col < 8; col++) {
            // While animating, skip dest tile (drawn by draw_animation)
            if (current_anim.active && row == current_anim.dest.row &&
                col == current_anim.dest.col)
                continue;
            piece p = board[row][col];
            if (p.type == EMPTY)
                continue;

            Rectangle src = piece_rects[p.color][p.type];
            Rectangle dest = {col * ts, row * ts, ts, ts};
            DrawTexturePro(*tex_pattern, src, dest, (Vector2){0, 0}, 0.0f,
                           WHITE);
        }
    }
}

void draw_possible_moves(possible_moves *moves, float scale) {
    float ts = 16.0f * scale;
    for (size_t i = 0; i < moves->count; i++) {
        Vector2 pos = moves->pos[i];
        Rectangle dest = {pos.x * ts, pos.y * ts, ts, ts};
        Vector2 center = {(float)dest.x + dest.width / 2,
                          (float)dest.y + dest.height / 2};
        DrawCircleV(center, ts * 0.15f, Fade(GREEN, 0.5f));
    }
}

void initialize_render(const char *texture_path, Texture *tex_pattern,
                       tile *tiles) {
    /// Initializes the rendering system, including loading textures and setting
    /// up any necessary OpenGL state.
    // NOTE: Textures must be loaded after Window initialization (OpenGL context
    // is required)
    *tex_pattern = LoadTexture(texture_path);
    SetTextureFilter(*tex_pattern, TEXTURE_FILTER_POINT);
    tiles[0].rec_pos_from_texture = (Rectangle){32, 8, 16, 16};
    tiles[1].rec_pos_from_texture = (Rectangle){48, 8, 16, 16};
}

void draw_board_labels(float tile_size, float scale) {
    const char *col_names[] = {"A", "B", "C", "D", "E", "F", "G", "H"};
    float ts = tile_size * scale;
    int font_size = (int)(ts * 0.35f);
    for (int c = 0; c < 8; c++) {
        int x =
            (int)(c * ts + ts / 2) - MeasureText(col_names[c], font_size) / 2;
        DrawText(col_names[c], x, (int)(8 * ts) + 4, font_size, WHITE);
    }
    for (int r = 0; r < 8; r++) {
        int rank = 8 - r;
        const char *label = TextFormat("%d", rank);
        int y = (int)(r * ts + ts / 2) - font_size / 2;
        DrawText(label, (int)(8 * ts) + 4, y, font_size, WHITE);
    }
}

void draw_chessboard(tile *tiles, Texture *tex_pattern, float scale) {
    float tile_size = 16.0f * scale;
    for (int row = 0; row < 8; row++) {
        for (int col = 0; col < 8; col++) {
            int tile_index = (col + row) % 2; // 0 = White, 1 = Black
            Rectangle dest = {col * tile_size, row * tile_size, tile_size,
                              tile_size};
            DrawTexturePro(*tex_pattern, tiles[tile_index].rec_pos_from_texture,
                           dest, (Vector2){0, 0}, 0.0f, WHITE);
        }
    }
}

void draw_last_move_highlight(float scale, const game_state *state) {
    if (state->history_count == 0)
        return;
    const ply_record *last = &state->history[state->history_count - 1];
    float ts = 16.0f * scale;
    Color tint = Fade(GOLD, 0.35f);
    DrawRectangle((int)(last->src.col * ts), (int)(last->src.row * ts),
                  (int)ts, (int)ts, tint);
    DrawRectangle((int)(last->dest.col * ts), (int)(last->dest.row * ts),
                  (int)ts, (int)ts, tint);
}

void draw_check_highlight(float scale, const game_state *state) {
    color side = turn_to_color(state->turn);
    if (!state->is_in_check[side])
        return;
    float ts = 16.0f * scale;
    for (int r = 0; r < 8; r++)
        for (int c = 0; c < 8; c++)
            if (board[r][c].type == KING && board[r][c].color == side)
                DrawRectangle((int)(c * ts), (int)(r * ts), (int)ts, (int)ts,
                              Fade(RED, 0.45f));
}

void draw_ui(float tile_size, float scale, const game_state *state) {
    float board_px = tile_size * scale * 8;
    int font_size = (int)(tile_size * scale * 0.35f);
    // the column labels sit directly under the board, status goes below them
    int y = (int)board_px + font_size + 14;

    if (state->game_over) {
        const char *msg = result_to_string(state);
        int bw = MeasureText(msg, font_size + 4);
        int bx = ((int)board_px - bw) / 2;
        DrawRectangle(bx - 8, y - 4, bw + 16, font_size * 2 + 16,
                      (Color){0, 0, 0, 180});
        DrawText(msg, bx, y, font_size + 4, RED);
        DrawText("R: new game   U: take back", bx, y + font_size + 8,
                 font_size - 4, LIGHTGRAY);
        return;
    }

    const char *turn_msg = state->turn ? "Black to move" : "White to move";
    DrawText(turn_msg, 8, y, font_size, WHITE);
    DrawText("R: new game   U: take back", 8, y + font_size + 6, font_size - 6,
             LIGHTGRAY);

    color side = turn_to_color(state->turn);
    if (state->is_in_check[side]) {
        const char *chk = "CHECK";
        int cx = (int)board_px - MeasureText(chk, font_size + 2) - 8;
        DrawText(chk, cx, y, font_size + 2, ORANGE);
    }
}

// move list in the panel right of the board, one full move per row; when
// there are more rows than fit, the oldest scroll off the top
void draw_move_list(float tile_size, float scale, int screen_w, int screen_h,
                    const game_state *state) {
    float board_px = tile_size * scale * 8;
    int font_size = (int)(tile_size * scale * 0.3f);
    int row_h = font_size + 6;
    int x0 = (int)board_px + 60;
    int y0 = 16;
    int panel_w = screen_w - x0 - 16;

    DrawRectangle(x0 - 12, y0 - 8, panel_w, screen_h - y0, (Color){0, 0, 0, 90});
    DrawText("Moves", x0, y0, font_size + 4, WHITE);
    y0 += font_size + 16;

    size_t full_moves = (state->history_count + 1) / 2;
    int max_rows = (screen_h - y0 - 8) / row_h;
    if (max_rows < 1)
        return;
    size_t first = full_moves > (size_t)max_rows ? full_moves - max_rows : 0;

    int num_w = MeasureText("999.", font_size) + 6;
    int col_w = MeasureText("exd8=Q#", font_size) + 16;
    for (size_t m = first; m < full_moves; m++) {
        int y = y0 + (int)(m - first) * row_h;
        DrawText(TextFormat("%zu.", m + 1), x0, y, font_size, LIGHTGRAY);
        const ply_record *w = &state->history[m * 2];
        DrawText(w->san, x0 + num_w, y, font_size, WHITE);
        if (m * 2 + 1 < state->history_count) {
            const ply_record *b = &state->history[m * 2 + 1];
            DrawText(b->san, x0 + num_w + col_w, y, font_size, WHITE);
        }
    }
}

// picker box geometry: a row of the four choices centered on the board
static Rectangle promotion_picker_rect(float scale) {
    float ts = 16.0f * scale;
    float pad = ts * 0.25f;
    float w = PROMOTION_CHOICE_COUNT * ts + (PROMOTION_CHOICE_COUNT + 1) * pad;
    float h = ts + 2 * pad + ts * 0.5f;
    float board_px = ts * 8;
    return (Rectangle){(board_px - w) / 2, (board_px - h) / 2, w, h};
}

static Rectangle promotion_choice_rect(float scale, int i) {
    float ts = 16.0f * scale;
    float pad = ts * 0.25f;
    Rectangle box = promotion_picker_rect(scale);
    return (Rectangle){box.x + pad + i * (ts + pad), box.y + pad + ts * 0.5f,
                       ts, ts};
}

void draw_promotion_picker(Texture *tex_pattern, float scale,
                           const game_state *state) {
    if (!state->promotion_pending)
        return;
    float ts = 16.0f * scale;
    float board_px = ts * 8;
    DrawRectangle(0, 0, (int)board_px, (int)board_px, (Color){0, 0, 0, 140});

    Rectangle box = promotion_picker_rect(scale);
    DrawRectangleRec(box, (Color){0x40, 0x33, 0x53, 0xFF});
    DrawRectangleLinesEx(box, 2.0f, GOLD);

    int font_size = (int)(ts * 0.3f);
    const char *title = "Promote to  (Q / R / B / N)";
    DrawText(title, (int)(box.x + (box.width - MeasureText(title, font_size)) / 2),
             (int)(box.y + 6), font_size, WHITE);

    color side = turn_to_color(state->turn);
    Vector2 mouse = GetMousePosition();
    for (int i = 0; i < PROMOTION_CHOICE_COUNT; i++) {
        Rectangle dest = promotion_choice_rect(scale, i);
        if (CheckCollisionPointRec(mouse, dest))
            DrawRectangleRec(dest, Fade(GOLD, 0.35f));
        DrawTexturePro(*tex_pattern, piece_rects[side][promotion_choices[i]],
                       dest, (Vector2){0, 0}, 0.0f, WHITE);
    }
}

int promotion_picker_hit(float scale, const game_state *state, Vector2 mouse) {
    if (!state->promotion_pending)
        return -1;
    for (int i = 0; i < PROMOTION_CHOICE_COUNT; i++)
        if (CheckCollisionPointRec(mouse, promotion_choice_rect(scale, i)))
            return i;
    return -1;
}
