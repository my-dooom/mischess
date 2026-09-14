#include "strategist.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

//------------------------------------------------------------------------------
// tiny thread / mutex layer (Win32 or pthreads)
//------------------------------------------------------------------------------
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
typedef HANDLE thread_t;
typedef CRITICAL_SECTION mutex_t;
static void mutex_init(mutex_t *m) { InitializeCriticalSection(m); }
static void mutex_lock(mutex_t *m) { EnterCriticalSection(m); }
static void mutex_unlock(mutex_t *m) { LeaveCriticalSection(m); }
static void mutex_destroy(mutex_t *m) { DeleteCriticalSection(m); }
static void sleep_ms(int ms) { Sleep((DWORD)ms); }
static DWORD WINAPI thread_trampoline(LPVOID arg);
static bool thread_start(thread_t *t, void *arg) {
    *t = CreateThread(NULL, 0, thread_trampoline, arg, 0, NULL);
    return *t != NULL;
}
static void thread_join(thread_t t) {
    WaitForSingleObject(t, INFINITE);
    CloseHandle(t);
}
#else
#include <pthread.h>
#include <unistd.h>
typedef pthread_t thread_t;
typedef pthread_mutex_t mutex_t;
static void mutex_init(mutex_t *m) { pthread_mutex_init(m, NULL); }
static void mutex_lock(mutex_t *m) { pthread_mutex_lock(m); }
static void mutex_unlock(mutex_t *m) { pthread_mutex_unlock(m); }
static void mutex_destroy(mutex_t *m) { pthread_mutex_destroy(m); }
static void sleep_ms(int ms) { usleep((useconds_t)ms * 1000); }
static void *thread_trampoline(void *arg);
static bool thread_start(thread_t *t, void *arg) {
    return pthread_create(t, NULL, thread_trampoline, arg) == 0;
}
static void thread_join(thread_t t) { pthread_join(t, NULL); }
#endif

// raylib.h cannot be included next to windows.h (both define Rectangle,
// CloseWindow, ...), so only its logger is declared here
void TraceLog(int logLevel, const char *text, ...);
enum { LOG_INFO = 3, LOG_WARNING = 4, LOG_ERROR = 5 };

//------------------------------------------------------------------------------
// shared state
//------------------------------------------------------------------------------

struct strategist {
    mutex_t lock;
    thread_t thread;
    bool thread_running;

    // written by the game thread, read by the worker
    char model_path[512];
    char prompt[STRATEGIST_PROMPT_MAX];
    unsigned request_id;   // bumped on every ask; the worker abandons old ones
    bool quit;

    // written by the worker, read by the game thread
    strategist_state state;
    char model_name[128];
    char error[256];
    char answer[STRATEGIST_ANSWER_MAX];
    bool has_answer;
    unsigned answer_id;
};

static strategist instance;
strategist *the_strategist = &instance;

//------------------------------------------------------------------------------
// llama.cpp side (only when built with it)
//------------------------------------------------------------------------------
#ifdef MISCHESS_HAVE_LLM
#include "llama.h"

#define MAX_NEW_TOKENS 160
#define N_CTX 2048

static void llama_quiet_log(enum ggml_log_level level, const char *text,
                            void *user) {
    (void)user;
    if (level >= GGML_LOG_LEVEL_ERROR)
        TraceLog(LOG_ERROR, "llama: %s", text);
}

// system prompt is the same for every question
static const char *SYSTEM_PROMPT =
    "You are a chess coach talking to a club player. Using the position and "
    "the engine information given, explain in two or three short sentences "
    "what the opponent is planning: which pieces or squares they aim at, and "
    "what the player should watch out for. Be concrete, mention squares and "
    "pieces, no move lists, no greetings.";

// formats system + user through the model's own chat template; falls back
// to a plain layout when the template is unknown to llama.cpp
static int build_chat(const struct llama_model *model, const char *user,
                      char *out, int cap) {
    struct llama_chat_message msgs[2] = {{"system", SYSTEM_PROMPT},
                                         {"user", user}};
    const char *tmpl = llama_model_chat_template(model, NULL);
    int n = llama_chat_apply_template(tmpl, msgs, 2, true, out, cap);
    if (n < 0 || n >= cap)
        n = snprintf(out, (size_t)cap, "%s\n\n%s\n\nAnswer:", SYSTEM_PROMPT, user);
    return n;
}

static void worker_generate(strategist *s, struct llama_model *model,
                            struct llama_context *ctx,
                            struct llama_sampler *smpl, const char *prompt,
                            unsigned id) {
    const struct llama_vocab *vocab = llama_model_get_vocab(model);
    static char chat[STRATEGIST_PROMPT_MAX * 2];
    int chat_len = build_chat(model, prompt, chat, sizeof(chat));

    static llama_token tokens[N_CTX];
    int n_tokens = llama_tokenize(vocab, chat, chat_len, tokens, N_CTX, true, true);
    if (n_tokens <= 0 || n_tokens >= N_CTX - MAX_NEW_TOKENS) {
        mutex_lock(&s->lock);
        snprintf(s->error, sizeof(s->error), "prompt too long for the model context");
        mutex_unlock(&s->lock);
        return;
    }

    llama_memory_clear(llama_get_memory(ctx), true);
    llama_sampler_reset(smpl);
    struct llama_batch batch = llama_batch_get_one(tokens, n_tokens);
    if (llama_decode(ctx, batch) != 0) {
        mutex_lock(&s->lock);
        snprintf(s->error, sizeof(s->error), "llama_decode failed on the prompt");
        mutex_unlock(&s->lock);
        return;
    }

    size_t len = 0;
    for (int i = 0; i < MAX_NEW_TOKENS; i++) {
        llama_token tok = llama_sampler_sample(smpl, ctx, -1);
        if (llama_vocab_is_eog(vocab, tok))
            break;
        char piece[64];
        int pn = llama_token_to_piece(vocab, tok, piece, sizeof(piece), 0, true);
        if (pn < 0)
            break;

        mutex_lock(&s->lock);
        bool stale = s->request_id != id || s->quit;
        if (!stale && len + (size_t)pn < sizeof(s->answer) - 1) {
            memcpy(s->answer + len, piece, (size_t)pn);
            len += (size_t)pn;
            s->answer[len] = '\0';
            s->has_answer = true;
        }
        mutex_unlock(&s->lock);
        if (stale)
            return;

        batch = llama_batch_get_one(&tok, 1);
        if (llama_decode(ctx, batch) != 0)
            break;
    }
}

static void worker_main(strategist *s) {
    llama_log_set(llama_quiet_log, NULL);
    llama_backend_init();

    struct llama_model_params mp = llama_model_default_params();
    struct llama_model *model = llama_model_load_from_file(s->model_path, mp);
    if (!model) {
        mutex_lock(&s->lock);
        snprintf(s->error, sizeof(s->error), "could not load %s", s->model_path);
        s->state = STRATEGIST_OFF;
        mutex_unlock(&s->lock);
        return;
    }
    struct llama_context_params cp = llama_context_default_params();
    cp.n_ctx = N_CTX;
    cp.n_batch = N_CTX;
    cp.no_perf = true;
    struct llama_context *ctx = llama_init_from_model(model, cp);
    if (!ctx) {
        llama_model_free(model);
        mutex_lock(&s->lock);
        snprintf(s->error, sizeof(s->error), "could not create llama context");
        s->state = STRATEGIST_OFF;
        mutex_unlock(&s->lock);
        return;
    }
    struct llama_sampler_chain_params sp = llama_sampler_chain_default_params();
    sp.no_perf = true;
    struct llama_sampler *smpl = llama_sampler_chain_init(sp);
    llama_sampler_chain_add(smpl, llama_sampler_init_top_k(40));
    llama_sampler_chain_add(smpl, llama_sampler_init_top_p(0.9f, 1));
    llama_sampler_chain_add(smpl, llama_sampler_init_temp(0.6f));
    llama_sampler_chain_add(smpl, llama_sampler_init_dist(LLAMA_DEFAULT_SEED));

    char desc[128];
    llama_model_desc(model, desc, sizeof(desc));
    mutex_lock(&s->lock);
    snprintf(s->model_name, sizeof(s->model_name), "%s", desc);
    s->state = STRATEGIST_READY;
    mutex_unlock(&s->lock);
    TraceLog(LOG_INFO, "Strategist: loaded %s", desc);

    unsigned done_id = 0;
    while (true) {
        mutex_lock(&s->lock);
        bool quit = s->quit;
        unsigned id = s->request_id;
        char prompt[STRATEGIST_PROMPT_MAX];
        bool work = !quit && id != done_id;
        if (work) {
            memcpy(prompt, s->prompt, sizeof(prompt));
            s->answer[0] = '\0';
            s->has_answer = false;
            s->answer_id = id;
            s->state = STRATEGIST_WRITING;
        }
        mutex_unlock(&s->lock);
        if (quit)
            break;
        if (!work) {
            sleep_ms(30);
            continue;
        }
        worker_generate(s, model, ctx, smpl, prompt, id);
        done_id = id;
        mutex_lock(&s->lock);
        if (s->request_id == id)
            s->state = STRATEGIST_READY;
        mutex_unlock(&s->lock);
    }

    llama_sampler_free(smpl);
    llama_free(ctx);
    llama_model_free(model);
    llama_backend_free();
}
#else
static void worker_main(strategist *s) { (void)s; }
#endif

#ifdef _WIN32
static DWORD WINAPI thread_trampoline(LPVOID arg) {
    worker_main((strategist *)arg);
    return 0;
}
#else
static void *thread_trampoline(void *arg) {
    worker_main((strategist *)arg);
    return NULL;
}
#endif

//------------------------------------------------------------------------------
// public API (game thread)
//------------------------------------------------------------------------------

void strategist_init(const char *model_path) {
    strategist *s = the_strategist;
    memset(s, 0, sizeof(*s));
    mutex_init(&s->lock);
    s->state = STRATEGIST_OFF;
#ifndef MISCHESS_HAVE_LLM
    (void)model_path;
    snprintf(s->error, sizeof(s->error), "built without llama.cpp");
    return;
#else
    if (!model_path || !model_path[0]) {
        snprintf(s->error, sizeof(s->error),
                 "no model: put a .gguf file in models/ or set MISCHESS_MODEL");
        TraceLog(LOG_WARNING, "Strategist: %s", s->error);
        return;
    }
    snprintf(s->model_path, sizeof(s->model_path), "%s", model_path);
    s->state = STRATEGIST_LOADING;
    if (!thread_start(&s->thread, s)) {
        s->state = STRATEGIST_OFF;
        snprintf(s->error, sizeof(s->error), "could not start worker thread");
        return;
    }
    s->thread_running = true;
    TraceLog(LOG_INFO, "Strategist: loading %s", s->model_path);
#endif
}

void strategist_shutdown(void) {
    strategist *s = the_strategist;
    if (s->thread_running) {
        mutex_lock(&s->lock);
        s->quit = true;
        mutex_unlock(&s->lock);
        thread_join(s->thread);
        s->thread_running = false;
    }
    mutex_destroy(&s->lock);
}

strategist_state strategist_get_state(void) {
    strategist *s = the_strategist;
    mutex_lock(&s->lock);
    strategist_state st = s->state;
    mutex_unlock(&s->lock);
    return st;
}

const char *strategist_model_name(void) { return the_strategist->model_name; }
const char *strategist_last_error(void) { return the_strategist->error; }

void strategist_ask(const char *user_prompt) {
    strategist *s = the_strategist;
    mutex_lock(&s->lock);
    if (s->state != STRATEGIST_OFF) {
        snprintf(s->prompt, sizeof(s->prompt), "%s", user_prompt);
        s->request_id++;
        s->answer[0] = '\0';
        s->has_answer = false;
    }
    mutex_unlock(&s->lock);
}

bool strategist_answer(char *out, size_t cap) {
    strategist *s = the_strategist;
    mutex_lock(&s->lock);
    bool has = s->has_answer;
    if (has)
        snprintf(out, cap, "%s", s->answer);
    mutex_unlock(&s->lock);
    return has;
}

void strategist_clear_answer(void) {
    strategist *s = the_strategist;
    mutex_lock(&s->lock);
    s->answer[0] = '\0';
    s->has_answer = false;
    s->request_id++; // abandons anything in flight
    mutex_unlock(&s->lock);
}
