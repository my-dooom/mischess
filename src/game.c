#include "game.h"
#include "fen.h"
#include "notation.h"
#include "raylib.h"
#include <string.h>

game_state game = {0};

void init_game_state(game_state *state) {
    state->turn = false;
    state->current_selection = NULL_POS;
    state->possible_moves.pos = NULL;
    state->possible_moves.count = 0;
    state->possible_moves.capacity = 0;
    state->en_passant_square = NULL_POS;
    state->can_castle_short[White] = true;
    state->can_castle_short[Black] = true;
    state->can_castle_long[White] = true;
    state->can_castle_long[Black] = true;
    state->move_count = 0;
    state->halfmove_clock = 0;
    state->game_over = false;
    state->result = RESULT_NONE;
    state->is_in_check[White] = false;
    state->is_in_check[Black] = false;
    state->promotion_pending = false;
    state->promotion_src = NULL_POS;
    state->promotion_dest = NULL_POS;
    state->history = NULL;
    state->history_count = 0;
    state->history_capacity = 0;
}

void free_game_state(game_state *state) {
    free(state->possible_moves.pos);
    free(state->history);
    state->possible_moves.pos = NULL;
    state->history = NULL;
}

static inline bool in_bounds(int r, int c) {
    return r >= 0 && r < 8 && c >= 0 && c < 8;
}

static const int ROOK_DIRS[4][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};
static const int BISHOP_DIRS[4][2] = {{-1, 1}, {1, 1}, {1, -1}, {-1, -1}};
static const int ALL_DIRS[8][2] = {{-1, 1}, {1, 1}, {1, -1}, {-1, -1},
                                   {-1, 0}, {1, 0}, {0, -1}, {0, 1}};
static const int KNIGHT_JUMPS[8][2] = {{-2, -1}, {-2, 1}, {-1, -2}, {-1, 2},
                                       {1, -2},  {1, 2},  {2, -1},  {2, 1}};

//------------------------------------------------------------------------------
// Pseudo-legal move generation (does not consider king safety)
//------------------------------------------------------------------------------

static void generate_pawn_moves(piece board[8][8], board_pos pos,
                                possible_moves *moves) {
    piece p = board[pos.row][pos.col];
    int dir = (p.color == White) ? -1 : 1;
    int next_row = pos.row + dir;

    if (in_bounds(next_row, pos.col) &&
        board[next_row][pos.col].type == EMPTY) {
        da_append(*moves, ((Vector2){pos.col, next_row}));
        if (p.has_moved == false) {
            int double_row = pos.row + 2 * dir;
            if (in_bounds(double_row, pos.col) &&
                board[double_row][pos.col].type == EMPTY) {
                da_append(*moves, ((Vector2){pos.col, double_row}));
            }
        }
    }

    int capture_cols[] = {pos.col - 1, pos.col + 1};
    for (int i = 0; i < 2; i++) {
        int c = capture_cols[i];
        if (!in_bounds(next_row, c))
            continue;
        // regular diagonal capture
        if (board[next_row][c].type != EMPTY &&
            board[next_row][c].color != p.color) {
            da_append(*moves, ((Vector2){c, next_row}));
        }
        // en passant capture
        if (next_row == game.en_passant_square.row &&
            c == game.en_passant_square.col) {
            da_append(*moves, ((Vector2){c, next_row}));
        }
    }
}

static void generate_sliding_moves(piece board[8][8], board_pos pos,
                                   const int (*dirs)[2], int dir_count,
                                   possible_moves *moves) {
    piece p = board[pos.row][pos.col];
    for (int d = 0; d < dir_count; d++) {
        int dr = dirs[d][0], dc = dirs[d][1];
        int r = pos.row + dr, c = pos.col + dc;
        while (in_bounds(r, c)) {
            if (board[r][c].type == EMPTY) {
                da_append(*moves, ((Vector2){c, r}));
            } else {
                if (board[r][c].color != p.color)
                    da_append(*moves, ((Vector2){c, r})); // capture
                break; // blocked either way
            }
            r += dr;
            c += dc;
        }
    }
}

static void generate_king_moves(piece board[8][8], board_pos pos,
                                possible_moves *moves) {
    piece p = board[pos.row][pos.col];
    color opp = opposite(p.color);
    for (int d = 0; d < 8; d++) {
        int r = pos.row + ALL_DIRS[d][0], c = pos.col + ALL_DIRS[d][1];
        if (in_bounds(r, c) &&
            (board[r][c].type == EMPTY || board[r][c].color != p.color) &&
            !board[r][c].attacked_by[opp]) {
            da_append(*moves, ((Vector2){c, r}));
        }
    }
    // Castling
    if (!p.has_moved) {
        int row = pos.row;
        // Short castle: king must not be in check, pass through, or land on
        // an attacked square (cols 4, 5, 6)
        if (game.can_castle_short[p.color] && board[row][7].type == ROOK &&
            board[row][7].color == p.color && !board[row][7].has_moved &&
            board[row][5].type == EMPTY && board[row][6].type == EMPTY &&
            !board[row][4].attacked_by[opp] &&
            !board[row][5].attacked_by[opp] &&
            !board[row][6].attacked_by[opp]) {
            da_append(*moves, ((Vector2){6, row}));
        }
        // Long castle: king must not be in check, pass through, or land on
        // an attacked square (cols 4, 3, 2); b1/b8 only needs to be empty
        if (game.can_castle_long[p.color] && board[row][0].type == ROOK &&
            board[row][0].color == p.color && !board[row][0].has_moved &&
            board[row][1].type == EMPTY && board[row][2].type == EMPTY &&
            board[row][3].type == EMPTY && !board[row][4].attacked_by[opp] &&
            !board[row][3].attacked_by[opp] &&
            !board[row][2].attacked_by[opp]) {
            da_append(*moves, ((Vector2){2, row}));
        }
    }
}

static void generate_knight_moves(piece board[8][8], board_pos pos,
                                  possible_moves *moves) {
    piece p = board[pos.row][pos.col];
    for (int i = 0; i < 8; i++) {
        int r = pos.row + KNIGHT_JUMPS[i][0];
        int c = pos.col + KNIGHT_JUMPS[i][1];
        if (in_bounds(r, c) &&
            (board[r][c].type == EMPTY || board[r][c].color != p.color)) {
            da_append(*moves, ((Vector2){c, r}));
        }
    }
}

void check_possible_moves(piece board[8][8], board_pos pos,
                          possible_moves *moves) {
    switch (board[pos.row][pos.col].type) {
    case PAWN:
        generate_pawn_moves(board, pos, moves);
        break;
    case ROOK:
        generate_sliding_moves(board, pos, ROOK_DIRS, 4, moves);
        break;
    case BISHOP:
        generate_sliding_moves(board, pos, BISHOP_DIRS, 4, moves);
        break;
    case QUEEN:
        generate_sliding_moves(board, pos, ALL_DIRS, 8, moves);
        break;
    case KING:
        generate_king_moves(board, pos, moves);
        break;
    case KNIGHT:
        generate_knight_moves(board, pos, moves);
        break;
    default:
        break;
    }
}

//------------------------------------------------------------------------------
// Attack generation: the squares a piece *controls*, which differs from the
// squares it can move to. Pawns attack diagonally regardless of occupancy and
// never attack the square in front of them, and every piece "attacks" squares
// occupied by its own side (defends them), which matters when the enemy king
// considers a capture.
//------------------------------------------------------------------------------

static void mark_attacks(piece board[8][8], board_pos pos) {
    piece p = board[pos.row][pos.col];
    color me = p.color;
    switch (p.type) {
    case PAWN: {
        int r = pos.row + (me == White ? -1 : 1);
        if (in_bounds(r, pos.col - 1))
            board[r][pos.col - 1].attacked_by[me] = true;
        if (in_bounds(r, pos.col + 1))
            board[r][pos.col + 1].attacked_by[me] = true;
        break;
    }
    case KNIGHT:
        for (int i = 0; i < 8; i++) {
            int r = pos.row + KNIGHT_JUMPS[i][0];
            int c = pos.col + KNIGHT_JUMPS[i][1];
            if (in_bounds(r, c))
                board[r][c].attacked_by[me] = true;
        }
        break;
    case KING:
        for (int d = 0; d < 8; d++) {
            int r = pos.row + ALL_DIRS[d][0], c = pos.col + ALL_DIRS[d][1];
            if (in_bounds(r, c))
                board[r][c].attacked_by[me] = true;
        }
        break;
    case ROOK:
    case BISHOP:
    case QUEEN: {
        const int(*dirs)[2] = p.type == ROOK     ? ROOK_DIRS
                              : p.type == BISHOP ? BISHOP_DIRS
                                                 : ALL_DIRS;
        int dir_count = p.type == QUEEN ? 8 : 4;
        for (int d = 0; d < dir_count; d++) {
            int r = pos.row + dirs[d][0], c = pos.col + dirs[d][1];
            while (in_bounds(r, c)) {
                board[r][c].attacked_by[me] = true;
                if (board[r][c].type != EMPTY)
                    break;
                r += dirs[d][0];
                c += dirs[d][1];
            }
        }
        break;
    }
    default:
        break;
    }
}

void update_capture_matrices(piece board[8][8]) {
    for (int row = 0; row < 8; row++)
        for (int col = 0; col < 8; col++) {
            board[row][col].attacked_by[White] = false;
            board[row][col].attacked_by[Black] = false;
        }
    for (int row = 0; row < 8; row++)
        for (int col = 0; col < 8; col++)
            if (board[row][col].type != EMPTY)
                mark_attacks(board, (board_pos){row, col});
}

//------------------------------------------------------------------------------
// Legal move generation
//------------------------------------------------------------------------------

static board_pos find_king_pos(piece board[8][8], color king_color) {
    for (int row = 0; row < 8; row++) {
        for (int col = 0; col < 8; col++) {
            if (board[row][col].type == KING &&
                board[row][col].color == king_color) {
                return (board_pos){row, col};
            }
        }
    }
    return NULL_POS;
}

// moves a piece on the board only: handles the rook hop for castling and the
// captured pawn for en passant, but no game state bookkeeping
static void apply_board_move(piece board[8][8], board_pos src,
                             board_pos dest) {
    piece moving_piece = board[src.row][src.col];
    if (moving_piece.type == KING && abs(dest.col - src.col) == 2) {
        if (dest.col == 6)
            short_castle(board, moving_piece.color);
        else
            long_castle(board, moving_piece.color);
        return;
    }
    move_piece(board, src, dest);
}

void generate_legal_moves(piece board[8][8], board_pos pos,
                          possible_moves *moves) {
    possible_moves pseudo = {0};
    color mover = board[pos.row][pos.col].color;
    color opp = opposite(mover);

    check_possible_moves(board, pos, &pseudo);
    moves->count = 0;

    for (size_t i = 0; i < pseudo.count; i++) {
        board_pos dest = {(int)pseudo.pos[i].y, (int)pseudo.pos[i].x};
        piece sim_board[8][8];
        memcpy(sim_board, board, sizeof(sim_board));
        apply_board_move(sim_board, pos, dest);
        update_capture_matrices(sim_board);

        board_pos king_pos = find_king_pos(sim_board, mover);
        if (king_pos.row < 0)
            continue;
        if (!sim_board[king_pos.row][king_pos.col].attacked_by[opp]) {
            da_append(*moves, ((Vector2){dest.col, dest.row}));
        }
    }

    free(pseudo.pos);
}

bool has_any_legal_move(piece board[8][8], color side) {
    possible_moves moves = {0};
    bool found = false;
    for (int row = 0; row < 8 && !found; row++) {
        for (int col = 0; col < 8 && !found; col++) {
            if (board[row][col].type == EMPTY ||
                board[row][col].color != side)
                continue;
            generate_legal_moves(board, (board_pos){row, col}, &moves);
            found = moves.count > 0;
        }
    }
    free(moves.pos);
    return found;
}

int move_piece(piece board[8][8], board_pos src, board_pos dest) {
    // En passant: pawn moves diagonally to an empty square
    if (board[src.row][src.col].type == PAWN && src.col != dest.col &&
        board[dest.row][dest.col].type == EMPTY) {
        board[src.row][dest.col].type = EMPTY;
    }
    board[dest.row][dest.col] = board[src.row][src.col];
    board[src.row][src.col].type = EMPTY;
    board[dest.row][dest.col].has_moved = true;
    return 1;
}

void compute_check_status(piece board[8][8], game_state *state) {
    for (int c = 0; c < 2; c++) {
        board_pos kp = find_king_pos(board, (color)c);
        if (kp.row < 0) {
            state->is_in_check[c] = false;
            continue;
        }
        state->is_in_check[c] =
            board[kp.row][kp.col].attacked_by[opposite((color)c)];
    }
}

int long_castle(piece board[8][8], color player_color) {
    int row = (player_color == White) ? 7 : 0;
    if (board[row][1].type != EMPTY || board[row][2].type != EMPTY ||
        board[row][3].type != EMPTY) {
        return 0;
    }
    board[row][2] = board[row][4];
    board[row][2].has_moved = true;
    board[row][4].type = EMPTY;
    board[row][3] = board[row][0];
    board[row][3].has_moved = true;
    board[row][0].type = EMPTY;
    return 1;
}

int short_castle(piece board[8][8], color player_color) {
    int row = (player_color == White) ? 7 : 0;
    if (board[row][5].type != EMPTY || board[row][6].type != EMPTY) {
        return 0;
    }
    board[row][6] = board[row][4];
    board[row][6].has_moved = true;
    board[row][4].type = EMPTY;
    board[row][5] = board[row][7];
    board[row][5].has_moved = true;
    board[row][7].type = EMPTY;
    return 1;
}

//------------------------------------------------------------------------------
// Full move application, history and game-over detection
//------------------------------------------------------------------------------

bool is_promotion_move(piece board[8][8], board_pos src, board_pos dest) {
    return board[src.row][src.col].type == PAWN &&
           (dest.row == 0 || dest.row == 7);
}

static bool is_legal(piece board[8][8], board_pos src, board_pos dest) {
    possible_moves moves = {0};
    generate_legal_moves(board, src, &moves);
    bool ok = false;
    for (size_t i = 0; i < moves.count; i++) {
        if ((int)moves.pos[i].y == dest.row && (int)moves.pos[i].x == dest.col) {
            ok = true;
            break;
        }
    }
    free(moves.pos);
    return ok;
}

static void revoke_rook_rights(game_state *state, piece rook, board_pos at) {
    if (rook.type != ROOK)
        return;
    int home_row = rook.color == White ? 7 : 0;
    if (at.row != home_row)
        return;
    if (at.col == 0)
        state->can_castle_long[rook.color] = false;
    if (at.col == 7)
        state->can_castle_short[rook.color] = false;
}

// move generation reads castling rights and the en passant square from the
// global; keep it in step when the caller drives a different state object
static void sync_global_rights(const game_state *state) {
    if (state == &game)
        return;
    game.en_passant_square = state->en_passant_square;
    memcpy(game.can_castle_short, state->can_castle_short,
           sizeof(game.can_castle_short));
    memcpy(game.can_castle_long, state->can_castle_long,
           sizeof(game.can_castle_long));
}

static void push_history(game_state *state, piece board[8][8],
                         board_pos src, board_pos dest, const char *san) {
    if (state->history_count == state->history_capacity) {
        state->history_capacity =
            state->history_capacity ? state->history_capacity * 2 : 64;
        state->history = realloc(state->history, state->history_capacity *
                                                     sizeof(*state->history));
    }
    ply_record *rec = &state->history[state->history_count++];
    memcpy(rec->board, board, sizeof(rec->board));
    rec->turn = state->turn;
    rec->en_passant_square = state->en_passant_square;
    memcpy(rec->can_castle_short, state->can_castle_short,
           sizeof(rec->can_castle_short));
    memcpy(rec->can_castle_long, state->can_castle_long,
           sizeof(rec->can_castle_long));
    rec->move_count = state->move_count;
    rec->halfmove_clock = state->halfmove_clock;
    rec->src = src;
    rec->dest = dest;
    snprintf(rec->san, SAN_MAX, "%s", san);
}

// K vs K, K+B vs K, K+N vs K, and K+B vs K+B with bishops on the same color
static bool insufficient_material(piece board[8][8]) {
    int minors[2] = {0, 0};
    int bishop_square_color[2] = {-1, -1};
    for (int r = 0; r < 8; r++) {
        for (int c = 0; c < 8; c++) {
            piece p = board[r][c];
            switch (p.type) {
            case EMPTY:
            case KING:
                break;
            case PAWN:
            case ROOK:
            case QUEEN:
                return false;
            case KNIGHT:
                minors[p.color]++;
                break;
            case BISHOP:
                minors[p.color]++;
                bishop_square_color[p.color] = (r + c) % 2;
                break;
            }
        }
    }
    if (minors[White] + minors[Black] <= 1)
        return true;
    if (minors[White] == 1 && minors[Black] == 1 &&
        bishop_square_color[White] >= 0 && bishop_square_color[Black] >= 0 &&
        bishop_square_color[White] == bishop_square_color[Black])
        return true;
    return false;
}

// counts how many times the current position has occurred, including now
static int repetition_count(piece board[8][8], const game_state *state) {
    char current[LONGEST_FEN];
    position_key(board, state, current, sizeof(current));
    int count = 1;
    // only positions since the last irreversible move can repeat
    size_t start = state->history_count > state->halfmove_clock
                       ? state->history_count - state->halfmove_clock
                       : 0;
    for (size_t i = start; i < state->history_count; i++) {
        ply_record *rec = &state->history[i];
        game_state snapshot = *state;
        snapshot.turn = rec->turn;
        snapshot.en_passant_square = rec->en_passant_square;
        memcpy(snapshot.can_castle_short, rec->can_castle_short,
               sizeof(snapshot.can_castle_short));
        memcpy(snapshot.can_castle_long, rec->can_castle_long,
               sizeof(snapshot.can_castle_long));
        char key[LONGEST_FEN];
        position_key(rec->board, &snapshot, key, sizeof(key));
        if (strcmp(key, current) == 0)
            count++;
    }
    return count;
}

static void detect_game_over(piece board[8][8], game_state *state) {
    color side = turn_to_color(state->turn);
    state->result = RESULT_NONE;
    if (!has_any_legal_move(board, side)) {
        state->result =
            state->is_in_check[side] ? RESULT_CHECKMATE : RESULT_STALEMATE;
    } else if (insufficient_material(board)) {
        state->result = RESULT_INSUFFICIENT_MATERIAL;
    } else if (state->halfmove_clock >= 100) {
        state->result = RESULT_FIFTY_MOVES;
    } else if (repetition_count(board, state) >= 3) {
        state->result = RESULT_REPETITION;
    }
    state->game_over = state->result != RESULT_NONE;
}

int make_move(piece board[8][8], game_state *state, board_pos src,
              board_pos dest, piece_type promotion) {
    if (state->game_over || !in_bounds(src.row, src.col) ||
        !in_bounds(dest.row, dest.col))
        return 0;
    piece moving_piece = board[src.row][src.col];
    color mover = turn_to_color(state->turn);
    if (moving_piece.type == EMPTY || moving_piece.color != mover)
        return 0;
    sync_global_rights(state);
    if (!is_legal(board, src, dest))
        return 0;

    bool is_promotion = is_promotion_move(board, src, dest);
    if (promotion == EMPTY)
        promotion = QUEEN;

    char san[SAN_MAX];
    move_to_san(board, src, dest, promotion, san);
    push_history(state, board, src, dest, san);

    bool is_castle = moving_piece.type == KING && abs(dest.col - src.col) == 2;
    bool is_en_passant = moving_piece.type == PAWN && src.col != dest.col &&
                         board[dest.row][dest.col].type == EMPTY;
    board_pos captured_pos =
        is_en_passant ? (board_pos){src.row, dest.col} : dest;
    piece captured_piece = board[captured_pos.row][captured_pos.col];
    bool is_capture = !is_castle && captured_piece.type != EMPTY;

    apply_board_move(board, src, dest);
    if (is_promotion)
        board[dest.row][dest.col].type = promotion;

    // castling rights
    if (moving_piece.type == KING) {
        state->can_castle_short[mover] = false;
        state->can_castle_long[mover] = false;
    }
    revoke_rook_rights(state, moving_piece, src);
    if (is_capture)
        revoke_rook_rights(state, captured_piece, captured_pos);

    // clocks
    if (moving_piece.type == PAWN || is_capture)
        state->halfmove_clock = 0;
    else
        state->halfmove_clock++;
    state->move_count++;

    // en passant target: set on a double pawn push, cleared otherwise
    state->en_passant_square = NULL_POS;
    if (moving_piece.type == PAWN && abs(dest.row - src.row) == 2)
        state->en_passant_square =
            (board_pos){(src.row + dest.row) / 2, dest.col};

    state->turn = !state->turn;
    state->current_selection = NULL_POS;
    state->possible_moves.count = 0;
    sync_global_rights(state);

    update_capture_matrices(board);
    compute_check_status(board, state);
    detect_game_over(board, state);

    // check / mate suffix on the recorded SAN
    ply_record *rec = &state->history[state->history_count - 1];
    size_t len = strlen(rec->san);
    if (state->result == RESULT_CHECKMATE) {
        rec->san[len] = '#';
        rec->san[len + 1] = '\0';
    } else if (state->is_in_check[turn_to_color(state->turn)]) {
        rec->san[len] = '+';
        rec->san[len + 1] = '\0';
    }
    return 1;
}

int undo_move(piece board[8][8], game_state *state) {
    if (state->history_count == 0)
        return 0;
    ply_record *rec = &state->history[--state->history_count];
    memcpy(board, rec->board, sizeof(rec->board));
    state->turn = rec->turn;
    state->en_passant_square = rec->en_passant_square;
    memcpy(state->can_castle_short, rec->can_castle_short,
           sizeof(state->can_castle_short));
    memcpy(state->can_castle_long, rec->can_castle_long,
           sizeof(state->can_castle_long));
    state->move_count = rec->move_count;
    state->halfmove_clock = rec->halfmove_clock;
    state->game_over = false;
    state->result = RESULT_NONE;
    state->promotion_pending = false;
    state->current_selection = NULL_POS;
    state->possible_moves.count = 0;
    sync_global_rights(state);
    update_capture_matrices(board);
    compute_check_status(board, state);
    return 1;
}

const char *result_to_string(const game_state *state) {
    switch (state->result) {
    case RESULT_CHECKMATE:
        return state->turn ? "Checkmate! White wins" : "Checkmate! Black wins";
    case RESULT_STALEMATE:
        return "Stalemate! Draw";
    case RESULT_FIFTY_MOVES:
        return "Draw by fifty-move rule";
    case RESULT_REPETITION:
        return "Draw by threefold repetition";
    case RESULT_INSUFFICIENT_MATERIAL:
        return "Draw by insufficient material";
    default:
        return "";
    }
}
