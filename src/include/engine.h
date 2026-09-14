#ifndef ENGINE_H
#define ENGINE_H

#include <stdbool.h>
#include <stddef.h>

// A child UCI engine process with non-blocking line I/O, so the game loop can
// poll it every frame without threads.

#ifdef _WIN32
typedef void *engine_handle; // HANDLE without pulling windows.h in here
#else
typedef int engine_handle;
#endif

#define ENGINE_BUF 8192

typedef struct {
    bool running;
    engine_handle process;
    engine_handle stdin_write;
    engine_handle stdout_read;
    char buf[ENGINE_BUF];
    size_t buf_len;
} engine_proc;

// Starts `path` (searched on PATH when it has no directory part).
bool engine_spawn(engine_proc *e, const char *path);
// Writes one line (newline appended). Returns false if the engine is gone.
bool engine_send(engine_proc *e, const char *line);
// Copies the next complete line, without its newline, into `line`.
// Returns false when no full line is available right now.
bool engine_poll_line(engine_proc *e, char *line, size_t cap);
void engine_close(engine_proc *e);

#endif // ENGINE_H
