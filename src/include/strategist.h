#ifndef STRATEGIST_H
#define STRATEGIST_H

#include <stdbool.h>
#include <stddef.h>

// A small local language model (any GGUF file, run through llama.cpp) that
// turns the engine's numbers into words: given the position, the recent
// moves and the line the engine intends to play, it describes in a few
// sentences what the opponent is planning.
//
// Generation runs on its own thread; the game loop only polls the answer,
// which arrives token by token so it can be shown as it is written.

#define STRATEGIST_PROMPT_MAX 4096
#define STRATEGIST_ANSWER_MAX 1024

typedef enum {
    STRATEGIST_OFF,     // no model found or built without llama.cpp
    STRATEGIST_LOADING, // model being read from disk
    STRATEGIST_READY,   // idle, answer (if any) is complete
    STRATEGIST_WRITING, // generating an answer
} strategist_state;

typedef struct strategist strategist;
extern strategist *the_strategist;

// Loads the GGUF at `model_path` on a background thread; NULL or "" leaves
// the strategist off with an explanatory message.
void strategist_init(const char *model_path);
void strategist_shutdown(void);

strategist_state strategist_get_state(void);
const char *strategist_model_name(void);
const char *strategist_last_error(void);

// Starts a new answer; any answer in progress is abandoned.
void strategist_ask(const char *user_prompt);

// Copies the current (possibly partial) answer into `out`. Returns false
// when there is no answer at all yet.
bool strategist_answer(char *out, size_t cap);
void strategist_clear_answer(void);

#endif // STRATEGIST_H
