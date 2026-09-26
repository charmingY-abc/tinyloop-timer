#ifndef TINYLOOP_TIMER_CORE_H
#define TINYLOOP_TIMER_CORE_H

#include <stdint.h>

#define TL_CHIME_MS 900U
#define TL_MUSIC_MS 12500U

typedef enum {
    TL_IDLE, TL_FOCUS, TL_FOCUS_PAUSED, TL_CHIME,
    TL_MUSIC, TL_REST, TL_REST_PAUSED, TL_WAIT
} TLPhase;

enum {
    TL_EVENT_NONE = 0, TL_EVENT_CHIME = 1, TL_EVENT_COMPLETE = 2,
    TL_EVENT_MUSIC = 4, TL_EVENT_REST_START = 8, TL_EVENT_REST_END = 16
};

typedef struct {
    int focus_seconds;
    int rounds_per_block;
    int rest_seconds;
} TLConfig;

typedef struct {
    TLConfig config;
    TLPhase phase;
    uint64_t deadline_ms;
    uint64_t remaining_ms;
    uint32_t current_focus_ms;
    uint32_t sound_serial;
    uint32_t music_serial;
    int completed_in_block;
    int pending_early;
    uint32_t pending_actual_ms;
    uint32_t pending_planned_ms;
    int prior_block_count;
} TLEngine;

void tl_init(TLEngine *e, TLConfig cfg, int completed_in_block,
             uint32_t sound_serial, uint32_t music_serial);
int tl_config_valid(TLConfig cfg);
int tl_start(TLEngine *e, uint64_t now_ms);
int tl_pause(TLEngine *e, uint64_t now_ms);
unsigned tl_finish(TLEngine *e, uint64_t now_ms, int early);
unsigned tl_tick(TLEngine *e, uint64_t now_ms);
uint64_t tl_remaining(const TLEngine *e, uint64_t now_ms);
void tl_undo(TLEngine *e, uint64_t now_ms, int restored_block_count);

/* Six different notes per completion are unnecessary: the three notes within
   a chime are distinct, and all 336 ordered triples are visited before reuse. */
void tl_chime_notes(uint32_t serial, int notes[3]);

#endif
