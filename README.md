# mischess

A complete two-player chess game written in C99 on top of [raylib](https://github.com/raysan5/raylib).

## Features

- Full rules: legal move generation with pin and check handling, castling, en passant, promotion with piece choice
- Game end detection: checkmate, stalemate, fifty-move rule, threefold repetition, insufficient material
- Move list in standard algebraic notation (SAN), last-move and check highlights
- Undo and restart at any time
- Full FEN import/export (`load_fen`, `update_full_fen`)

## Controls

| Input | Action |
|---|---|
| Left click | Select a piece / move it to a highlighted square |
| Q / R / B / N or click | Choose the promotion piece when the picker is shown (right click cancels) |
| U | Take back the last move |
| R | Start a new game |
| Esc | Quit |

## Building

Requires CMake 3.16+ and a C compiler. raylib is vendored as a git submodule.

```sh
git submodule update --init
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/mischess
```

## Tests

The rule engine has a raylib-free test target:

```sh
cmake --build build --target chess_tests
./build/chess_tests
```

---

*Stockfish integration is a possible next step; the engine already exposes full FEN, which is what a UCI engine consumes.*
