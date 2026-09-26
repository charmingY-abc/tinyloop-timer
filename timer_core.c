#include "timer_core.h"
#include <string.h>

int tl_config_valid(TLConfig cfg) {
    return cfg.focus_seconds >= 5 && cfg.focus_seconds <= 5999 &&
           cfg.rounds_per_block >= 1 && cfg.rounds_per_block <= 999 &&
           cfg.rest_seconds >= 60 && cfg.rest_seconds <= 5999;
}

void tl_init(TLEngine *e, TLConfig cfg, int completed_in_block,
             uint32_t sound_serial, uint32_t music_serial) {
    memset(e, 0, sizeof(*e));
    e->config = cfg;
    e->phase = TL_IDLE;
    e->completed_in_block = completed_in_block;
    e->sound_serial = sound_serial;
    e->music_serial = music_serial;
    e->current_focus_ms = (uint32_t)cfg.focus_seconds * 1000U;
    e->remaining_ms = (uint64_t)cfg.focus_seconds * 1000;
}

uint64_t tl_remaining(const TLEngine *e, uint64_t now_ms) {
    if (e->phase == TL_FOCUS || e->phase == TL_REST ||
        e->phase == TL_CHIME || e->phase == TL_MUSIC)
        return now_ms >= e->deadline_ms ? 0 : e->deadline_ms - now_ms;
    return e->remaining_ms;
}

int tl_start(TLEngine *e, uint64_t now_ms) {
    if (e->phase == TL_IDLE || e->phase == TL_WAIT) {
        e->current_focus_ms = (uint32_t)e->config.focus_seconds * 1000U;
        e->remaining_ms = e->current_focus_ms;
        e->phase = TL_FOCUS;
    } else if (e->phase == TL_FOCUS_PAUSED) {
        e->phase = TL_FOCUS;
    } else if (e->phase == TL_REST_PAUSED) {
        e->phase = TL_REST;
    } else return 0;
    e->deadline_ms = now_ms + e->remaining_ms;
    return 1;
}

int tl_pause(TLEngine *e, uint64_t now_ms) {
    if (e->phase == TL_FOCUS) {
        e->remaining_ms = tl_remaining(e, now_ms);
        e->phase = TL_FOCUS_PAUSED;
        return 1;
    }
    if (e->phase == TL_REST) {
        e->remaining_ms = tl_remaining(e, now_ms);
        e->phase = TL_REST_PAUSED;
        return 1;
    }
    return 0;
}

unsigned tl_finish(TLEngine *e, uint64_t now_ms, int early) {
    uint64_t left;
    if (e->phase != TL_FOCUS) return TL_EVENT_NONE;
    left = tl_remaining(e, now_ms);
    e->pending_early = early != 0 && now_ms < e->deadline_ms;
    e->pending_planned_ms = e->current_focus_ms;
    e->pending_actual_ms = e->pending_early ? (uint32_t)(e->pending_planned_ms - left)
                                           : e->pending_planned_ms;
    e->phase = TL_CHIME;
    e->deadline_ms = now_ms + TL_CHIME_MS;
    e->sound_serial++;
    return TL_EVENT_CHIME;
}

unsigned tl_tick(TLEngine *e, uint64_t now_ms) {
    if (e->phase == TL_FOCUS && now_ms >= e->deadline_ms)
        return tl_finish(e, now_ms, 0);
    if (e->phase == TL_CHIME && now_ms >= e->deadline_ms) {
        e->prior_block_count = e->completed_in_block;
        e->completed_in_block++;
        if (e->completed_in_block >= e->config.rounds_per_block) {
            e->completed_in_block = 0;
            e->phase = TL_MUSIC;
            e->deadline_ms = now_ms + TL_MUSIC_MS;
            e->music_serial++;
            return TL_EVENT_COMPLETE | TL_EVENT_MUSIC;
        }
        e->phase = TL_FOCUS;
        e->current_focus_ms = (uint32_t)e->config.focus_seconds * 1000U;
        e->remaining_ms = e->current_focus_ms;
        e->deadline_ms = now_ms + e->remaining_ms;
        return TL_EVENT_COMPLETE;
    }
    if (e->phase == TL_MUSIC && now_ms >= e->deadline_ms) {
        e->phase = TL_REST;
        e->remaining_ms = (uint64_t)e->config.rest_seconds * 1000;
        e->deadline_ms = now_ms + e->remaining_ms;
        return TL_EVENT_REST_START;
    }
    if (e->phase == TL_REST && now_ms >= e->deadline_ms) {
        e->phase = TL_WAIT;
        e->remaining_ms = 0;
        return TL_EVENT_REST_END;
    }
    return TL_EVENT_NONE;
}

void tl_undo(TLEngine *e, uint64_t now_ms, int restored_block_count) {
    e->completed_in_block = restored_block_count;
    if (e->phase == TL_MUSIC || e->phase == TL_REST ||
        e->phase == TL_REST_PAUSED || e->phase == TL_WAIT) {
        e->phase = TL_FOCUS;
        e->current_focus_ms = (uint32_t)e->config.focus_seconds * 1000U;
        e->remaining_ms = e->current_focus_ms;
        e->deadline_ms = now_ms + e->remaining_ms;
    }
}

void tl_chime_notes(uint32_t serial, int notes[3]) {
    unsigned rank = ((serial % 336U) * 101U + 37U) % 336U;
    unsigned a = rank / 42U, b, c, j, k;
    rank %= 42U;
    b = rank / 6U;
    c = rank % 6U;
    notes[0] = (int)a;
    for (j = 0, k = 0; j < 8; ++j) {
        if (j == a) continue;
        if (k++ == b) { notes[1] = (int)j; break; }
    }
    for (j = 0, k = 0; j < 8; ++j) {
        if (j == a || j == (unsigned)notes[1]) continue;
        if (k++ == c) { notes[2] = (int)j; break; }
    }
}
