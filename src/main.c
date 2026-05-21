
#include "board.h"
#include "fen.h"
#include "game.h"
#include "logger.h"
#include "raylib.h"
#include "render.h"

#define null_pos (board_pos){-1, -1}
//------------------------------------------------------------------------------------
// Program main entry point
//------------------------------------------------------------------------------------

void print_available_captures(int target_row, int target_col, possible_moves *piece_moves) {
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

static void handle_input(int target_row, int target_col, board_pos *sel,
                         possible_moves *moves, color *moving_color) {
    if (target_row < 0 || target_col < 0 || current_anim.active)
        return;
    // selection phase: if no piece selected,
    // or clicked on empty square,
    // or clicked on opponent's piece -> select new piece
    if (sel->row < 0 || board[sel->row][sel->col].type == EMPTY ||
        board[sel->row][sel->col].color != *moving_color) {
        if (board[target_row][target_col].color != *moving_color)
            return;
        *sel = (board_pos){target_row, target_col};
        moves->count = 0;
        check_possible_moves(board, *sel, moves);
        TraceLog(LOG_DEBUG, "Selected piece: %d of color %d",
                 board[sel->row][sel->col].type,
                 board[sel->row][sel->col].color);
        print_available_captures(target_row, target_col, moves);
        return;
    }

    if (target_row == sel->row && target_col == sel->col) {
        *sel = null_pos;
        moves->count = 0;
        return;
    }

    bool valid_move = false;
    for (size_t i = 0; i < moves->count; i++) {
        if ((int)moves->pos[i].y == target_row &&
            (int)moves->pos[i].x == target_col) {
            valid_move = true;
            break;
        }
    }

    if (!valid_move) {
        // cannot move to target square,
        // but if it has a piece of the
        // same color, select it
        if (board[target_row][target_col].color != *moving_color ||
            board[target_row][target_col].type == EMPTY) {
            *sel = null_pos;
            moves->count = 0;
            return;
        }
        *sel = (board_pos){target_row, target_col};
        moves->count = 0;
        check_possible_moves(board, *sel, moves);
        return;
    }

    piece moving_piece = board[sel->row][sel->col];
    if (moving_piece.type == KING && abs(target_col - sel->col) == 2) {
        if (target_col == 6) {
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
    } else {
        move_piece(board, *sel, (board_pos){target_row, target_col});
        start_move_animation(&current_anim, moving_piece, *sel,
                             (board_pos){target_row, target_col});
        TraceLog(LOG_DEBUG, "Moved piece to: %d, %d", target_row, target_col);
        update_fen_table(board);
        update_capture_matrices(board);
        possible_moves piece_moves = {0};
        check_possible_moves(board, (board_pos){target_row, target_col},
        &piece_moves);
        
        print_available_captures(target_row, target_col, &piece_moves);

        free(piece_moves.pos);
        fflush(stdout);
        print_fen();
    }
    // Update en passant square: set if double pawn push, clear otherwise
    en_passant_square = null_pos;
    if (moving_piece.type == PAWN && abs(target_row - sel->row) == 2) {
        en_passant_square =
            (board_pos){(sel->row + target_row) / 2, target_col};
    }
    *moving_color = (*moving_color == White) ? Black : White;
    *sel = null_pos;
    moves->count = 0;
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
    board_pos selected = {-1, -1};
    int target_row = -1, target_col = -1;
    possible_moves moves = {0};
    color moving_color = White;

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
    update_fen_table(board);

    SetTargetFPS(60); // Set our game to run at 60 frames-per-second
    // Main game loop
    while (!WindowShouldClose()) // Detect window close button or ESC key
    {
        mouse_position = GetMousePosition();
        target_row = -1;
        target_col = -1;
        convert_mouse_position_to_board_coordinates(
            mouse_position, tile_size * scale, &target_row, &target_col);
        handle_input(target_row, target_col, &selected, &moves, &moving_color);

        BeginDrawing();
        ClearBackground((Color){0x40, 0x33, 0x53, 0xFF});
        update_animation(&current_anim);
        draw_chessboard(tiles, &tex_pattern, scale);
        draw_pieces(&tex_pattern, scale);
        draw_animation(&tex_pattern, &current_anim, scale);
        draw_board_labels(tile_size, scale);
        draw_selection_highlight(scale, &selected);
        draw_possible_moves(&moves, scale);
        EndDrawing();
    }

    // De-Initialization
    //--------------------------------------------------------------------------------------
    CloseWindow(); // Close window and OpenGL context
    //--------------------------------------------------------------------------------------

    return 0;
}
