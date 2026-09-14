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

Stockfish is built from source as part of the normal build and bundled next to the executable, so the game starts in coach mode out of the box (see *Building* for requirements and how to point it at a different engine). Without an engine it is a plain two-player board.

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

Requires CMake 3.16+ and a C compiler. raylib and Stockfish are vendored as git submodules.

```sh
git submodule update --init
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/mischess
```

A `Makefile` wraps the same steps (GNU make; on Windows use the MSYS2 one):

```sh
make            # game + tests + bundled Stockfish
make run        # build and start
make test       # run the test suite
make release    # optimised build in build-release/
make package    # release zip via CPack
make no-engine  # configure without the Stockfish build
make GEN=Ninja  # pick a CMake generator; also BUILD=, CONFIG=, ARCH=, JOBS=
```

### Stockfish from source

The build compiles [Stockfish](https://github.com/official-stockfish/Stockfish) from the `stockfish/` submodule with its own Makefile and bundles the result as `build/engines/stockfish[.exe]`, which the coach picks up automatically. That step needs a C++17 compiler, GNU make and a POSIX shell (the Makefile also downloads the NNUE network files, so the first build needs network access):

| Platform | What to have |
|---|---|
| Windows | MSYS2 with `mingw-w64-x86_64-gcc` and `make` (`pacman -S make`); CMake finds `C:\msys64\usr\bin\make.exe` on its own. CMake 3.25+ |
| Linux | `g++`, `make`, `curl` or `wget` |
| macOS | Xcode command line tools |

Options:

```sh
# skip the engine build entirely (coach is off unless you provide an engine)
cmake -B build -DMISCHESS_BUILD_STOCKFISH=OFF
# pick the instruction set; "native" (default) is fastest for this machine,
# use a portable one for binaries you give to others
cmake -B build -DMISCHESS_STOCKFISH_ARCH=x86-64-avx2
```

A prebuilt engine works too: drop it into `build/engines/`, set `MISCHESS_ENGINE=/path/to/stockfish`, or pass the path as the first argument.

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
| `cmake/Stockfish.cmake` | builds the Stockfish submodule and bundles the binary |
