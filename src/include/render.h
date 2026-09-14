#ifndef RENDER_H
#define RENDER_H

#include "board.h"
#include "raylib.h"

#include "game.h"
typedef struct {
    Rectangle rec_pos_from_texture;
} tile;

typedef struct {
    bool active;
    piece animating_piece;
    board_pos src;
    board_pos dest;
    float progress; // 0.0 to 1.0
    float duration; // seconds
} move_animation;

extern move_animation current_anim;

// multiplier on every UI font size, changed with + / - at runtime
extern float ui_text_scale;

// font size for a nominal pixel height, never below the readable minimum
static inline int ui_font(float px) {
    int f = (int)(px * ui_text_scale);
    return f < 10 ? 10 : f;
}

extern Rectangle selected_tile_rect;

extern Rectangle piece_rects[2][7]; // [color][piece_type]

// order the promotion picker offers pieces in
#define PROMOTION_CHOICE_COUNT 4
extern const piece_type promotion_choices[PROMOTION_CHOICE_COUNT];

void draw_selection_highlight(float scale, board_pos *selection);
void draw_last_move_highlight(float scale, const game_state *state);
void draw_check_highlight(float scale, const game_state *state);

void draw_possible_moves(possible_moves *moves, float scale);

void draw_pieces(Texture *tex_pattern, float scale);

// loads the sprite atlas from the PNG bytes compiled into the executable
void initialize_render(const unsigned char *png, int png_len,
                       Texture *tex_pattern, tile *tiles);
void draw_chessboard(tile *tiles, Texture *tex_pattern, float scale);
void draw_board_labels(float tile_size, float scale);
void draw_ui(float tile_size, float scale, const game_state *state);
void draw_move_list(float tile_size, float scale, int screen_w, int screen_h,
                    int y0, const game_state *state);

// promotion picker: drawn over the board while state->promotion_pending;
// promotion_picker_hit returns the index into promotion_choices under the
// mouse, or -1
void draw_promotion_picker(Texture *tex_pattern, float scale,
                           const game_state *state);
int promotion_picker_hit(float scale, const game_state *state, Vector2 mouse);

void start_move_animation(move_animation *anim, piece p, board_pos src,
                          board_pos dest);
void update_animation(move_animation *anim);
void draw_animation(Texture *tex_pattern, move_animation *anim, float scale);

#define SIZEOF(A) sizeof(A) / sizeof(A[0])

#endif // RENDER_H
