// Notes and to-dos, kept in the board's flash, changed by voice: hold the top
// button, speak, and the AI turns what it heard into tasks and notes.
#pragma once
#include <Arduino.h>
#include <vector>

struct NoteItem {
  bool task, done;
  String text;
  uint32_t added;  // unix seconds; 0 for items saved before this was kept
};

void notesBegin();                    // loads the saved list
std::vector<NoteItem> &notesItems();
void notesDemo(bool on);  // screenshots: show made-up items (never saved) instead of the real ones  // tasks and notes in the order they were added
void notesToggle(int index);          // tick or untick a task; saved at once
void notesSetTask(int index, bool task);  // a note becomes a task, or the other way; saved
void notesDelete(int index);          // saved at once

// Records from the microphone while keepGoing() is true (30 s at most).
// The caller frees *pcm. Returns the number of samples, 16 kHz mono; 0 and
// micError() says why when it couldn't record.
size_t micRecord(bool (*keepGoing)(), int16_t **pcm);
const char *micError();

// An OpenAI key is set (openai.api_key): voice notes, dictation and the AI
// sorting work. Without one, typed notes are sorted by how they start.
bool notesAiConfigured();

// The network part of a voice note, a typed note or dictation runs on its
// own task (its stack in PSRAM), so the screen, the buttons, the timer and
// the USB setup keep going while OpenAI or the relay answers. Every wait is
// bounded (connect 5 s, TLS handshake 8 s, an answer 25 s), and Cancel
// stops waiting at once (the answer, if one comes, is dropped).
enum NotesJob : uint8_t {
  JOB_NOTE,     // speech: to text, then to the AI with the list
  JOB_TYPED,    // typed words: to the AI with the list
  JOB_DICTATE,  // speech to text only (the keyboard's microphone)
  JOB_RECIPE_VOICE,  // Recipes: speech to text, then the AI writes a recipe (recipes.h)
  JOB_RECIPE_TYPED,  // Recipes: typed words to the AI
  JOB_GAME,          // a game (games.h): its words to the AI, with its own instructions and JSON schema
};
static inline bool notesJobIsRecipe(NotesJob k) { return k == JOB_RECIPE_VOICE || k == JOB_RECIPE_TYPED; }
void notesWorkerBegin();  // in setup(), once: the task that does the network part
// Starts one. `pcm` (from micRecord) is handed over: the job frees it.
// False, and `why` for the screen, when the last one is still finishing.
bool notesStart(NotesJob kind, int16_t *pcm, size_t samples, const String &typed, String &why);
// A game's request (Quiz, Adventure): the AI answers `input` under
// `instructions` as strict JSON (`schema`, called `name`), waiting `waitMs`
// at most. It comes back as a recipe does: `say` is why it failed ("" when
// it worked), `raw` the AI's text, for the game's own parser on the loop.
bool notesStartGame(const char *name, const String &instructions, const String &schema, const String &input, uint32_t waitMs, String &why);
NotesJob notesKind();  // the kind of the job on now (or the last one)
bool notesPending();  // a job is on and not cancelled: the screen says "Thinking..."
bool notesBusy();     // the network part is still running (perhaps a cancelled one)
// The loop calls this often: once the job is done it is applied here (the
// list is changed and saved on the loop, never on the job's task) and it
// returns true, with `say` for the screen and, for dictation, `heard`. A
// recipe job changes no list: `say` is why it failed ("" when it worked),
// `heard` what was asked, and `raw` the AI's text for recipeParse().
bool notesFinish(String &say, String &heard, NotesJob &kind, String *raw = nullptr);
void notesCancel();
uint32_t notesStackFree();

// Testing only (the serial "airelay" command): until the next restart, send
// the AI's requests to https://host:port, without checking its certificate
// and without the OpenAI key, so a fake server on a computer can answer. "" ends it.
void notesTestRelay(const String &host, int port);  // the job task's stack never used so far, in bytes
