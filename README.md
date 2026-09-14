# mischess

A complete chess game written in C99 on top of [raylib](https://github.com/raysan5/raylib), with a built-in **coach** that plays you with Stockfish and teaches you as you go.

## Features

- Full rules: legal move generation with pin and check handling, castling, en passant, promotion with piece choice
- Game end detection: checkmate, stalemate, fifty-move rule, threefold repetition, insufficient material
- Move list in standard algebraic notation (SAN), last-move and check highlights
- Undo and restart at any time
- Full FEN import/export (`load_fen`, `update_full_fen`)
- Coach mode with any UCI engine (tested with Stockfish 19), see below

## Coach mode

Drop a Stockfish binary into `engines/` next to the executable (or set `MISCHESS_ENGINE=/path/to/stockfish`, or pass the path as the first argument) and the game starts in coach mode. Without an engine it is a plain two-player board.

The trick that makes it feel instant: **while you think, the engine is already analysing your position at full strength** (MultiPV, infinite search). The moment you move it already knows the best move and its evaluation, so it can grade what you played right away and the engine reply follows without a long pause.

- **Move grading** — every move you play is rated *Best / Good / Inaccuracy / Mistake / Blunder* by centipawn loss, with the evaluation before and after, the move the engine preferred, and its continuation in SAN
- **Threats** — a null-move search ("what if you passed?") shows what your opponent is threatening right now, as a red arrow and a line in the panel
- **Hint** (`H`) — the engine's best move as a green arrow, with its line
- **Candidate lines** (`C`) — the top three moves as arrows with evaluations
- **Live eval bar** — win probability from the current analysis
- **Adaptive strength** — the engine plays at a limited Elo (starting at 1400) that rises when your recent moves are accurate and drops when they are not, so the opponent stays at the edge of your level
- **Take-back** (`U`) rewinds to your previous turn, keeping the lesson on screen
- **Accuracy report** at the end of the game: counts of each grade, average loss, hints used
- `S` switches sides so the engine plays White

## Controls

| Input | Action |
|---|---|
| Left click | Select a piece / move it to a highlighted square |
| Q / R / B / N or click | Choose the promotion piece when the picker is shown (right click cancels) |
| U | Take back the last move (in coach mode: back to your previous turn) |
| R | Start a new game |
| H | Toggle hint arrow (coach) |
| C | Toggle the top three candidate lines (coach) |
| T | Toggle threat display (coach) |
| S | Switch sides with the engine (coach) |
| Esc | Quit |

## Building

Requires CMake 3.16+ and a C compiler. raylib is vendored as a git submodule.

```sh
git submodule update --init
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/mischess
```

Get Stockfish from <https://stockfishchess.org/download/> and place it at `build/engines/stockfish.exe` (Windows) or `build/engines/stockfish` (Linux/macOS).

## Tests

The rule engine and the UCI text layer have a raylib-free test target:

```sh
cmake --build build --target chess_tests
./build/chess_tests
```

## Layout

| File | Role |
|---|---|
| `src/game.c` | rules, move application, history, game-over detection |
| `src/notation.c` | SAN generation, repetition keys, UCI line to SAN |
| `src/fen.c` | FEN import/export |
| `src/uci.c` | UCI parsing and score helpers (pure, tested) |
| `src/engine.c` | engine child process with non-blocking pipes (Win32 / POSIX) |
| `src/coach.c` | the coach state machine driving the engine |
| `src/coach_render.c`, `src/render.c` | drawing |
| `src/main.c` | input and the game loop |
