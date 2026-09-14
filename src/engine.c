#include "engine.h"
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

// all engines we spawn live in one job that is torn down with this process,
// so a crash or a hard kill never leaves a Stockfish running in the background
static HANDLE engine_job = NULL;

static HANDLE get_engine_job(void) {
    if (engine_job)
        return engine_job;
    engine_job = CreateJobObjectA(NULL, NULL);
    if (!engine_job)
        return NULL;
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION info;
    memset(&info, 0, sizeof(info));
    info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    SetInformationJobObject(engine_job, JobObjectExtendedLimitInformation,
                            &info, sizeof(info));
    return engine_job;
}

bool engine_spawn(engine_proc *e, const char *path) {
    memset(e, 0, sizeof(*e));

    SECURITY_ATTRIBUTES sa = {sizeof(sa), NULL, TRUE};
    HANDLE child_stdin_r = NULL, child_stdin_w = NULL;
    HANDLE child_stdout_r = NULL, child_stdout_w = NULL;
    if (!CreatePipe(&child_stdin_r, &child_stdin_w, &sa, 0))
        return false;
    if (!CreatePipe(&child_stdout_r, &child_stdout_w, &sa, 0)) {
        CloseHandle(child_stdin_r);
        CloseHandle(child_stdin_w);
        return false;
    }
    // our ends must not leak into the child
    SetHandleInformation(child_stdin_w, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(child_stdout_r, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA si;
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = child_stdin_r;
    si.hStdOutput = child_stdout_w;
    si.hStdError = child_stdout_w;

    // CreateProcess may modify the command line buffer
    char cmd[MAX_PATH + 2];
    snprintf(cmd, sizeof(cmd), "\"%s\"", path);

    PROCESS_INFORMATION pi;
    BOOL ok = CreateProcessA(NULL, cmd, NULL, NULL, TRUE, CREATE_NO_WINDOW,
                             NULL, NULL, &si, &pi);
    CloseHandle(child_stdin_r);
    CloseHandle(child_stdout_w);
    if (!ok) {
        CloseHandle(child_stdin_w);
        CloseHandle(child_stdout_r);
        return false;
    }
    CloseHandle(pi.hThread);
    HANDLE job = get_engine_job();
    if (job)
        AssignProcessToJobObject(job, pi.hProcess);
    e->process = pi.hProcess;
    e->stdin_write = child_stdin_w;
    e->stdout_read = child_stdout_r;
    e->running = true;
    return true;
}

bool engine_send(engine_proc *e, const char *line) {
    if (!e->running)
        return false;
    char buf[1024];
    int n = snprintf(buf, sizeof(buf), "%s\n", line);
    DWORD written = 0;
    if (!WriteFile((HANDLE)e->stdin_write, buf, (DWORD)n, &written, NULL)) {
        e->running = false;
        return false;
    }
    return true;
}

// pulls whatever the pipe has right now into the buffer without blocking
static void fill_buffer(engine_proc *e) {
    if (!e->running || e->buf_len >= sizeof(e->buf) - 1)
        return;
    DWORD avail = 0;
    if (!PeekNamedPipe((HANDLE)e->stdout_read, NULL, 0, NULL, &avail, NULL)) {
        // broken pipe: the engine died
        e->running = false;
        return;
    }
    if (avail == 0)
        return;
    DWORD want = (DWORD)(sizeof(e->buf) - 1 - e->buf_len);
    if (want > avail)
        want = avail;
    DWORD got = 0;
    if (!ReadFile((HANDLE)e->stdout_read, e->buf + e->buf_len, want, &got,
                  NULL)) {
        e->running = false;
        return;
    }
    e->buf_len += got;
}

void engine_close(engine_proc *e) {
    if (e->stdin_write) {
        engine_send(e, "quit");
        CloseHandle((HANDLE)e->stdin_write);
    }
    if (e->stdout_read)
        CloseHandle((HANDLE)e->stdout_read);
    if (e->process) {
        // give it a moment to exit cleanly, then make sure it is gone
        if (WaitForSingleObject((HANDLE)e->process, 500) != WAIT_OBJECT_0)
            TerminateProcess((HANDLE)e->process, 0);
        CloseHandle((HANDLE)e->process);
    }
    memset(e, 0, sizeof(*e));
}

#else // POSIX

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

bool engine_spawn(engine_proc *e, const char *path) {
    memset(e, 0, sizeof(*e));
    int in_pipe[2], out_pipe[2];
    if (pipe(in_pipe) != 0)
        return false;
    if (pipe(out_pipe) != 0) {
        close(in_pipe[0]);
        close(in_pipe[1]);
        return false;
    }
    pid_t pid = fork();
    if (pid < 0)
        return false;
    if (pid == 0) {
        dup2(in_pipe[0], STDIN_FILENO);
        dup2(out_pipe[1], STDOUT_FILENO);
        dup2(out_pipe[1], STDERR_FILENO);
        close(in_pipe[0]);
        close(in_pipe[1]);
        close(out_pipe[0]);
        close(out_pipe[1]);
        execlp(path, path, (char *)NULL);
        _exit(127);
    }
    close(in_pipe[0]);
    close(out_pipe[1]);
    fcntl(out_pipe[0], F_SETFL, fcntl(out_pipe[0], F_GETFL) | O_NONBLOCK);
    e->process = (engine_handle)pid;
    e->stdin_write = in_pipe[1];
    e->stdout_read = out_pipe[0];
    e->running = true;
    return true;
}

bool engine_send(engine_proc *e, const char *line) {
    if (!e->running)
        return false;
    char buf[1024];
    int n = snprintf(buf, sizeof(buf), "%s\n", line);
    if (write(e->stdin_write, buf, (size_t)n) != n) {
        e->running = false;
        return false;
    }
    return true;
}

static void fill_buffer(engine_proc *e) {
    if (!e->running || e->buf_len >= sizeof(e->buf) - 1)
        return;
    ssize_t got = read(e->stdout_read, e->buf + e->buf_len,
                       sizeof(e->buf) - 1 - e->buf_len);
    if (got > 0)
        e->buf_len += (size_t)got;
    else if (got == 0 || (errno != EAGAIN && errno != EWOULDBLOCK))
        e->running = false;
}

void engine_close(engine_proc *e) {
    if (e->running)
        engine_send(e, "quit");
    if (e->stdin_write)
        close(e->stdin_write);
    if (e->stdout_read)
        close(e->stdout_read);
    if (e->process) {
        int status;
        if (waitpid((pid_t)e->process, &status, WNOHANG) == 0) {
            usleep(200 * 1000);
            if (waitpid((pid_t)e->process, &status, WNOHANG) == 0) {
                kill((pid_t)e->process, SIGKILL);
                waitpid((pid_t)e->process, &status, 0);
            }
        }
    }
    memset(e, 0, sizeof(*e));
}

#endif

bool engine_poll_line(engine_proc *e, char *line, size_t cap) {
    fill_buffer(e);
    char *nl = memchr(e->buf, '\n', e->buf_len);
    if (!nl) {
        // a line longer than the buffer would wedge us: drop it
        if (e->buf_len >= sizeof(e->buf) - 1)
            e->buf_len = 0;
        return false;
    }
    size_t len = (size_t)(nl - e->buf);
    size_t copy = len;
    if (copy && e->buf[copy - 1] == '\r')
        copy--;
    if (copy >= cap)
        copy = cap - 1;
    memcpy(line, e->buf, copy);
    line[copy] = '\0';
    size_t rest = e->buf_len - (len + 1);
    memmove(e->buf, nl + 1, rest);
    e->buf_len = rest;
    return true;
}
