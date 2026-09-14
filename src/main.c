#include "board.h"
#include "fen.h"
#include "game.h"
#include "logger.h"
#include "raylib.h"
#include "render.h"

//------------------------------------------------------------------------------------
// Program main entry point
//------------------------------------------------------------------------------------

static const int tile_size = 16;
static const float scale = 5.0f;

static bool is_move_in_list(int target_row, int target_col,
                            const possible_moves *moves) {
    for (size_t i = 0; i < moves->count; i++) {
        if ((int)moves->pos[i].y == target_row &&
            (int)moves->pos[i].x == target_col) {
            return true;
        }
    }
    return false;
}

static bool select_piece_if_owned(int target_row, int target_col,
                                  color moving_color, board_pos *sel,
                                  possible_moves *moves) {
    if (board[target_row][target_col].color != moving_color ||
        board[target_row][target_col].type == EMPTY) {
        return false;
    }
    *sel = (board_pos){target_row, target_col};
    moves->count = 0;
    generate_legal_moves(board, *sel, moves);
    return true;
}

static void clear_selection(board_pos *sel, possible_moves *moves) {
    *sel = NULL_POS;
    moves->count = 0;
}

// applies a move chosen in the UI and kicks off the animation
static void commit_move(game_state *state, board_pos src, board_pos dest,
                        piece_type promotion) {
    piece moving_piece = board[src.row][src.col];
    if (!make_move(board, state, src, dest, promotion)) {
        TraceLog(LOG_WARNING, "Rejected illegal move");
        return;
    }
    start_move_animation(&current_anim, moving_piece, src, dest);
    update_full_fen(board, state);
    const ply_record *last = &state->history[state->history_count - 1];
    TraceLog(LOG_INFO, "%zu%s %s", last->move_count / 2 + 1,
             last->turn ? "..." : ".", last->san);
    print_fen();
    fflush(stdout);
    color next = turn_to_color(state->turn);
    if (state->game_over)
        TraceLog(LOG_WARNING, "%s", result_to_string(state));
    else if (state->is_in_check[next])
        TraceLog(LOG_WARNING, "%s king is in check",
                 next == White ? "White" : "Black");
}

// while the promotion picker is open every click either picks a piece or
// cancels the move
static void handle_promotion_input(game_state *state, Vector2 mouse) {
    piece_type choice = EMPTY;
    if (IsKeyPressed(KEY_Q)) choice = QUEEN;
    if (IsKeyPressed(KEY_R)) choice = ROOK;
    if (IsKeyPressed(KEY_B)) choice = BISHOP;
    if (IsKeyPressed(KEY_N)) choice = KNIGHT;

    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        int picked = promotion_picker_hit(scale, state, mouse);
        if (picked >= 0)
            choice = promotion_choices[picked];
        else {
            state->promotion_pending = false;
            return;
        }
    }
    if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) {
        state->promotion_pending = false;
        return;
    }
    if (choice == EMPTY)
        return;
    state->promotion_pending = false;
    commit_move(state, state->promotion_src, state->promotion_dest, choice);
}

static void handle_input(int target_row, int target_col, game_state *state) {
    if (target_row < 0 || target_col < 0 || current_anim.active)
        return;
    board_pos *sel = &state->current_selection;
    possible_moves *moves = &state->possible_moves;
    color moving_color = turn_to_color(state->turn);
    bool has_selection =
        sel->row >= 0 && board[sel->row][sel->col].type != EMPTY &&
        board[sel->row][sel->col].color == moving_color;
    if (!has_selection) {
        select_piece_if_owned(target_row, target_col, moving_color, sel, moves);
        return;
    }

    if (target_row == sel->row && target_col == sel->col) {
        clear_selection(sel, moves);
        return;
    }

    if (!is_move_in_list(target_row, target_col, moves)) {
        if (!select_piece_if_owned(target_row, target_col, moving_color, sel,
                                   moves)) {
            clear_selection(sel, moves);
        }
        return;
    }

    board_pos src = *sel;
    board_pos dest = {target_row, target_col};
    if (is_promotion_move(board, src, dest)) {
        // hold the move until the player picks a piece
        state->promotion_pending = true;
        state->promotion_src = src;
        state->promotion_dest = dest;
        clear_selection(sel, moves);
        return;
    }
    commit_move(state, src, dest, EMPTY);
}

static void reset_game(game_state *state) {
    free_game_state(state);
    initialize_board(board);
    init_game_state(state);
    update_full_fen(board, state);
    update_capture_matrices(board);
    current_anim.active = false;
    TraceLog(LOG_INFO, "Game reset");
}

static void convert_mouse_position_to_board_coordinates(Vector2 mouse_position,
                                                        float tile_px, int *row,
                                                        int *col) {
    *row = -1;
    *col = -1;
    if (mouse_position.x < 0 || mouse_position.y < 0)
        return;
    if (mouse_position.x >= tile_px * 8 || mouse_position.y >= tile_px * 8)
        return;
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        *col = (int)(mouse_position.x / tile_px);
        *row = (int)(mouse_position.y / tile_px);
    }
}

int main(void) {
    const int margins = 150;
    const int screenWidth = 1100;
    const int screenHeight = tile_size * 8 * scale + margins;
    Vector2 mouse_position = {0, 0};
    int target_row = -1, target_col = -1;

    SetTraceLogCallback(LogColored);

    InitWindow(screenWidth, screenHeight, "mischess");

    Texture tex_pattern;
    tile tiles[2];
    initialize_render("assets/atlas.png", &tex_pattern, tiles);

    initialize_board(board);
    init_game_state(&game);
    update_full_fen(board, &game);
    update_capture_matrices(board); // seed attacked_by cache before first move

    SetTargetFPS(60);
    while (!WindowShouldClose()) {
        mouse_position = GetMousePosition();

        if (game.promotion_pending) {
            handle_promotion_input(&game, mouse_position);
        } else {
            convert_mouse_position_to_board_coordinates(
                mouse_position, tile_size * scale, &target_row, &target_col);
            if (!game.game_over)
                handle_input(target_row, target_col, &game);

            // R resets, U takes back the last move (also after game over)
            if (IsKeyPressed(KEY_R))
                reset_game(&game);
            if (IsKeyPressed(KEY_U) && !current_anim.active) {
                if (undo_move(board, &game)) {
                    update_full_fen(board, &game);
                    TraceLog(LOG_INFO, "Move undone");
                }
            }
        }

        BeginDrawing();
        ClearBackground((Color){0x40, 0x33, 0x53, 0xFF});
        update_animation(&current_anim);
        draw_chessboard(tiles, &tex_pattern, scale);
        draw_last_move_highlight(scale, &game);
        draw_check_highlight(scale, &game);
        draw_pieces(&tex_pattern, scale);
        draw_animation(&tex_pattern, &current_anim, scale);
        draw_board_labels(tile_size, scale);
        draw_selection_highlight(scale, &game.current_selection);
        draw_possible_moves(&game.possible_moves, scale);
        draw_ui(tile_size, scale, &game);
        draw_move_list(tile_size, scale, screenWidth, screenHeight, &game);
        draw_promotion_picker(&tex_pattern, scale, &game);
        EndDrawing();
    }

    free_game_state(&game);
    UnloadTexture(tex_pattern);
    CloseWindow();
    return 0;
}
