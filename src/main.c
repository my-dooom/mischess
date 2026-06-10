
#include "board.h"
#include "fen.h"
#include "game.h"
#include "logger.h"
#include "raylib.h"
#include "render.h"

//------------------------------------------------------------------------------------
// Program main entry point
//------------------------------------------------------------------------------------

void print_available_captures(int target_row, int target_col,
                              possible_moves *piece_moves) {
    const char *piece_names[] = {"Empty", "Pawn",  "Knight", "Bishop",
                                 "Rook",  "Queen", "King"};
    const char *col_names[] = {"A", "B", "C", "D", "E", "F", "G", "H"};
    printf("Captures available from %s%d:\n", col_names[target_col],
           8 - target_row);
    for (size_t i = 0; i < piece_moves->count; i++) {
        int mr = (int)piece_moves->pos[i].y;
        int mc = (int)piece_moves->pos[i].x;
        if (board[mr][mc].type != EMPTY) {
            printf("  can capture %s at %s%d\n",
                   piece_names[board[mr][mc].type], col_names[mc], 8 - mr);
        }
    }
}
bool check_stalemate(color player_color) {
    // Check if the player has any legal moves left
    for (int row = 0; row < 8; row++) {
        for (int col = 0; col < 8; col++) {
            if (board[row][col].color == player_color) {
                possible_moves moves = {0};
                check_possible_moves(board, (board_pos){row, col}, &moves);
                if (moves.count > 0) {
                    free(moves.pos);
                    return false; // Found a legal move, not stalemate
                }
                free(moves.pos);
            }
        }
    }
    return true; // No legal moves found, stalemate
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
    TraceLog(LOG_DEBUG, "Selected piece: %d of color %d",
             board[sel->row][sel->col].type, board[sel->row][sel->col].color);
    print_available_captures(target_row, target_col, moves);
    return true;
}
static void clear_selection(board_pos *sel, possible_moves *moves) {
    *sel = NULL_POS;
    moves->count = 0;
}
static void update_castling_rights_after_move(game_state *state,
                                              piece moving_piece,
                                              color moving_color,
                                              board_pos src,
                                              bool is_capture,
                                              piece captured_piece,
                                              board_pos captured_pos) {
    if (moving_piece.type == KING) {
        state->can_castle_short[moving_color] = false;
        state->can_castle_long[moving_color] = false;
    } else if (moving_piece.type == ROOK) {
        if (moving_color == White && src.row == 7 && src.col == 0)
            state->can_castle_long[White] = false;
        if (moving_color == White && src.row == 7 && src.col == 7)
            state->can_castle_short[White] = false;
        if (moving_color == Black && src.row == 0 && src.col == 0)
            state->can_castle_long[Black] = false;
        if (moving_color == Black && src.row == 0 && src.col == 7)
            state->can_castle_short[Black] = false;
    }
    if (is_capture && captured_piece.type == ROOK) {
        if (captured_piece.color == White && captured_pos.row == 7 &&
            captured_pos.col == 0)
            state->can_castle_long[White] = false;
        if (captured_piece.color == White && captured_pos.row == 7 &&
            captured_pos.col == 7)
            state->can_castle_short[White] = false;
        if (captured_piece.color == Black && captured_pos.row == 0 &&
            captured_pos.col == 0)
            state->can_castle_long[Black] = false;
        if (captured_piece.color == Black && captured_pos.row == 0 &&
            captured_pos.col == 7)
            state->can_castle_short[Black] = false;
    }
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

    piece moving_piece = board[sel->row][sel->col];

    if (moving_piece.type == KING && abs(target_col - sel->col) == 2) {
        if (target_col == 6) {
            // castling validity is fully checked in generate_king_moves so we
            // only need to execute the rook and king moves here
            short_castle(board, moving_piece.color);
            start_move_animation(&current_anim, moving_piece, *sel,
                                 (board_pos){target_row, target_col});
            TraceLog(LOG_INFO, "Short castling performed");
        } else if (target_col == 2) {
            long_castle(board, moving_piece.color);
            start_move_animation(&current_anim, moving_piece, *sel,
                                 (board_pos){target_row, target_col});
            TraceLog(LOG_INFO, "Long castling performed");
        }
        state->can_castle_short[moving_color] = false;
        state->can_castle_long[moving_color] = false;
        update_fen_table(board);
        update_capture_matrices(board);
        state->halfmove_clock++;
        state->move_count++;
    } else {
        bool is_en_passant = moving_piece.type == PAWN &&
                             sel->col != target_col &&
                             board[target_row][target_col].type == EMPTY;
        board_pos captured_pos = is_en_passant
                                     ? (board_pos){sel->row, target_col}
                                     : (board_pos){target_row, target_col};
        piece captured_piece = board[captured_pos.row][captured_pos.col];
        bool is_capture = captured_piece.type != EMPTY;
        move_piece(board, *sel, (board_pos){target_row, target_col});
        // auto-promote to queen when a pawn reaches the back rank
        if (moving_piece.type == PAWN &&
            (target_row == 0 || target_row == 7)) {
            board[target_row][target_col].type = QUEEN;
            TraceLog(LOG_INFO, "Pawn promoted to Queen at %c%d",
                     'A' + target_col, 8 - target_row);
        }
        start_move_animation(&current_anim, moving_piece, *sel,
                             (board_pos){target_row, target_col});
        TraceLog(LOG_DEBUG, "Moved piece to: %d, %d", target_row, target_col);
        update_fen_table(board);
        possible_moves piece_moves = {0};
        check_possible_moves(board, (board_pos){target_row, target_col},
                             &piece_moves);

        print_available_captures(target_row, target_col, &piece_moves);

        free(piece_moves.pos);
        fflush(stdout);
        print_fen();
        update_castling_rights_after_move(state, moving_piece, moving_color,
                                          *sel, is_capture, captured_piece,
                                          captured_pos);
        if (moving_piece.type == PAWN || is_capture)
            state->halfmove_clock = 0;
        else
            state->halfmove_clock++;
        state->move_count++;
    }
    // Update en passant square: set if double pawn push, clear otherwise
    state->en_passant_square = NULL_POS;
    if (moving_piece.type == PAWN && abs(target_row - sel->row) == 2) {
        state->en_passant_square =
            (board_pos){(sel->row + target_row) / 2, target_col};
    }
    state->turn = !state->turn;
    update_capture_matrices(board);
    compute_check_status(board, state);
    color next = turn_to_color(state->turn);
    if (state->is_in_check[next]) {
        TraceLog(LOG_WARNING, "%s king is in check",
                 next == White ? "White" : "Black");
    }
    clear_selection(sel, moves);
}

void convert_mouse_position_to_board_coordinates(Vector2 mouse_position,
                                                 float tile_size, int *row,
                                                 int *col) {
    if (mouse_position.x < 0 || mouse_position.y < 0) {
        *row = -1;
        *col = -1;
        return;
    }
    if (mouse_position.x > tile_size * 8 || mouse_position.y > tile_size * 8) {
        *row = -1;
        *col = -1;
        return;
    }
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {

        *col = (int)(mouse_position.x / tile_size);
        *row = (int)(mouse_position.y / tile_size);
        //     const char *col_names[] = {"A", "B", "C", "D", "E", "F", "G",
        //     "H"}; TraceLog(LOG_INFO, "Clicked on board coordinates: %s%d",
        //              col_names[*col], *row + 1);
    }
}

int main(void) {
    // Initialization
    //--------------------------------------------------------------------------------------
    //

    const int tile_size = 16;
    const float scale = 5.0f;
    const int margins = 150;
    const int screenWidth = 1500; // hardcoded for now, should be calculated
                                  // based on tile size and scale
    const int screenHeight = tile_size * 8 * scale + margins;
    Vector2 mouse_position = {0, 0};
    int target_row = -1, target_col = -1;

    SetTraceLogCallback(LogColored);

    InitWindow(screenWidth, screenHeight,
               "raylib [core] example - basic window");

    //--------------------------------------------------------------------------------------
    // IcNIT RENDERING
    //--------------------------------------------------------------------------------------

    Texture tex_pattern;
    tile tiles[2];
    initialize_render("assets/atlas.png", &tex_pattern, tiles);

    initialize_board(board);
    init_game_state(&game);
    update_fen_table(board);
    update_capture_matrices(board); // seed attacked_by cache before first move

    SetTargetFPS(60); // Set our game to run at 60 frames-per-second
    // Main game loop
    while (!WindowShouldClose()) // Detect window close button or ESC key
    {
        mouse_position = GetMousePosition();
        target_row = -1;
        target_col = -1;
        convert_mouse_position_to_board_coordinates(
            mouse_position, tile_size * scale, &target_row, &target_col);
        handle_input(target_row, target_col, &game);

        if (check_stalemate(turn_to_color(game.turn))) {
            TraceLog(LOG_INFO, "Stalemate detected for color %d",
                     turn_to_color(game.turn));
            TraceLog(LOG_INFO, "Game over! DRAW");
            return -1;
        }
        BeginDrawing();
        ClearBackground((Color){0x40, 0x33, 0x53, 0xFF});
        update_animation(&current_anim);
        draw_chessboard(tiles, &tex_pattern, scale);
        draw_pieces(&tex_pattern, scale);
        draw_animation(&tex_pattern, &current_anim, scale);
        draw_board_labels(tile_size, scale);
        draw_selection_highlight(scale, &game.current_selection);
        draw_possible_moves(&game.possible_moves, scale);
        EndDrawing();
    }

    // De-Initialization
    //--------------------------------------------------------------------------------------
    CloseWindow(); // Close window and OpenGL context
    //--------------------------------------------------------------------------------------

    return 0;
}
