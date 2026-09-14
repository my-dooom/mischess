#include "board.h"
#include "coach.h"
#include "coach_render.h"
#include "fen.h"
#include "game.h"
#include "logger.h"
#include "raylib.h"
#include "render.h"
#include "strategist.h"
#include "atlas_png.h"

//------------------------------------------------------------------------------------
// Program main entry point
//------------------------------------------------------------------------------------

static const int tile_size = 16;
// board scale, recomputed every frame from the window size
static float scale = 5.0f;

// the smallest side panel that still fits the coach text and move list
#define PANEL_MIN_W 380
#define PANEL_GAP 60

// Picks the largest board that leaves room for the status lines below it
// and the side panel next to it. Status block is ~3 font lines of
// 0.35 * tile height, which is what the 16.8 accounts for.
static float fit_scale(int w, int h) {
    float by_h = (h - 14.0f) / (128.0f + 16.8f * ui_text_scale);
    float by_w = (w - PANEL_MIN_W - PANEL_GAP - 16.0f) / 128.0f;
    float s = by_h < by_w ? by_h : by_w;
    return s < 1.5f ? 1.5f : s;
}

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
    coach_on_human_move(&the_coach, board, state, src, dest, promotion);
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
    coach_new_game(&the_coach, board, state);
}

// in coach mode a take-back rewinds to the previous human turn
static void take_back(game_state *state) {
    if (!undo_move(board, state))
        return;
    if (coach_active(&the_coach) && !coach_is_human_turn(&the_coach, state))
        undo_move(board, state);
    update_full_fen(board, state);
    TraceLog(LOG_INFO, "Move undone");
    coach_on_position_changed(&the_coach, board, state);
}

// argv[2], then MISCHESS_MODEL, then the first *.gguf in models/ next to
// the executable; NULL when nothing is found
static const char *find_model(const char *explicit_path) {
    static char path[600];
    if (explicit_path && explicit_path[0] && FileExists(explicit_path))
        return explicit_path;
    const char *env = getenv("MISCHESS_MODEL");
    if (env && env[0] && FileExists(env))
        return env;
    snprintf(path, sizeof(path), "%smodels", GetApplicationDirectory());
    if (!DirectoryExists(path))
        return NULL;
    FilePathList files = LoadDirectoryFilesEx(path, ".gguf", false);
    const char *found = NULL;
    // a chess-trained model wins over a general one when both are present
    for (unsigned i = 0; i < files.count; i++) {
        if (!found || TextFindIndex(TextToLower(GetFileName(files.paths[i])),
                                    "chessgpt") >= 0) {
            snprintf(path, sizeof(path), "%s", files.paths[i]);
            found = path;
        }
    }
    UnloadDirectoryFiles(files);
    return found;
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

int main(int argc, char **argv) {
    Vector2 mouse_position = {0, 0};
    int target_row = -1, target_col = -1;

    SetTraceLogCallback(LogColored);

    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
    InitWindow(1280, 790, "mischess");
    SetWindowMinSize(760, 480);

    Texture tex_pattern;
    tile tiles[2];
    initialize_render(atlas_png, (int)atlas_png_len, &tex_pattern, tiles);

    initialize_board(board);
    init_game_state(&game);
    update_full_fen(board, &game);
    update_capture_matrices(board); // seed attacked_by cache before first move

    coach_init(&the_coach, argc > 1 ? argv[1] : NULL);
    strategist_init(find_model(argc > 2 ? argv[2] : NULL));

    SetTargetFPS(60);
    while (!WindowShouldClose()) {
        // text size: + / - (also on the keypad), 0 resets
        if (IsKeyPressed(KEY_EQUAL) || IsKeyPressed(KEY_KP_ADD))
            ui_text_scale = ui_text_scale < 2.5f ? ui_text_scale + 0.1f : 2.5f;
        if (IsKeyPressed(KEY_MINUS) || IsKeyPressed(KEY_KP_SUBTRACT))
            ui_text_scale = ui_text_scale > 0.7f ? ui_text_scale - 0.1f : 0.7f;
        if (IsKeyPressed(KEY_ZERO) || IsKeyPressed(KEY_KP_0))
            ui_text_scale = 1.0f;

        const int screenWidth = GetScreenWidth();
        const int screenHeight = GetScreenHeight();
        scale = fit_scale(screenWidth, screenHeight);
        mouse_position = GetMousePosition();

        // engine I/O first so its reply lands before input is read
        coach_update(&the_coach, board, &game);
        board_pos eng_src, eng_dest;
        if (coach_take_engine_move(&the_coach, &eng_src, &eng_dest)) {
            start_move_animation(&current_anim,
                                 board[eng_dest.row][eng_dest.col], eng_src,
                                 eng_dest);
            update_full_fen(board, &game);
            print_fen();
            if (game.game_over)
                TraceLog(LOG_WARNING, "%s", result_to_string(&game));
        }
        bool human_may_move = !coach_active(&the_coach) ||
                              coach_is_human_turn(&the_coach, &game);

        if (game.promotion_pending) {
            handle_promotion_input(&game, mouse_position);
        } else {
            convert_mouse_position_to_board_coordinates(
                mouse_position, tile_size * scale, &target_row, &target_col);
            if (!game.game_over && human_may_move)
                handle_input(target_row, target_col, &game);

            // R resets, U takes back the last move (also after game over)
            if (IsKeyPressed(KEY_R))
                reset_game(&game);
            if (IsKeyPressed(KEY_U) && !current_anim.active)
                take_back(&game);

            // coach toggles
            if (IsKeyPressed(KEY_H)) {
                the_coach.show_hint = !the_coach.show_hint;
                if (the_coach.show_hint) {
                    the_coach.hints_used++;
                    coach_request_hints(&the_coach);
                }
            }
            if (IsKeyPressed(KEY_C)) {
                the_coach.show_candidates = !the_coach.show_candidates;
                if (the_coach.show_candidates)
                    coach_request_hints(&the_coach);
            }
            if (IsKeyPressed(KEY_T))
                the_coach.show_threat = !the_coach.show_threat;
            if (IsKeyPressed(KEY_P))
                the_coach.show_plan = !the_coach.show_plan;
            if (IsKeyPressed(KEY_M))
                ui_show_moves = !ui_show_moves;
            if (IsKeyPressed(KEY_S) && !current_anim.active)
                coach_switch_sides(&the_coach, board, &game);
        }

        BeginDrawing();
        ClearBackground(UI_BG);
        update_animation(&current_anim);
        draw_board_frame(scale);
        draw_chessboard(tiles, &tex_pattern, scale);
        draw_last_move_highlight(scale, &game);
        draw_check_highlight(scale, &game);
        draw_pieces(&tex_pattern, scale);
        draw_animation(&tex_pattern, &current_anim, scale);
        draw_board_labels(tile_size, scale);
        draw_selection_highlight(scale, &game.current_selection);
        draw_possible_moves(&game.possible_moves, scale);
        draw_coach_overlay(scale, &the_coach, &game);
        draw_ui(tile_size, scale, &game);
        {
            int px = (int)(tile_size * scale * 8) + PANEL_GAP;
            int pw = screenWidth - px - 16;
            DrawRectangleRounded((Rectangle){(float)px - 14, 8, (float)pw,
                                             (float)screenHeight - 16},
                                 0.04f, 8, UI_PANEL);
            int legend_h = ui_font(screenHeight * 0.028f) * 2 + 12;
            int y = draw_coach_panel(px, 18, pw - 16, screenHeight, &the_coach,
                                     &game);
            draw_move_list(px, y, pw - 16, screenHeight - legend_h - 8, &game);
            draw_key_legend(px, screenHeight - 10, pw - 16, screenHeight);
        }
        draw_promotion_picker(&tex_pattern, scale, &game);
        EndDrawing();
    }

    strategist_shutdown();
    coach_shutdown(&the_coach);
    free_game_state(&game);
    UnloadTexture(tex_pattern);
    CloseWindow();
    return 0;
}
