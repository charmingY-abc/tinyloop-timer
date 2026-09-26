#include "../timer_core.h"
#include "../audio.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static void test_cycle_and_rest(void) {
    TLConfig c = {5, 3, 60};
    TLEngine e;
    unsigned x;
    tl_init(&e, c, 0, 0, 0);
    assert(tl_start(&e, 1000));
    assert(tl_tick(&e, 5999) == 0);
    assert(tl_tick(&e, 6000) == TL_EVENT_CHIME);
    assert(e.phase == TL_CHIME && e.sound_serial == 1);
    assert(tl_tick(&e, 6900) == TL_EVENT_COMPLETE);
    assert(e.phase == TL_FOCUS && e.completed_in_block == 1);
    assert(tl_finish(&e, 7000, 1) == TL_EVENT_CHIME);
    assert(e.pending_early && e.pending_actual_ms == 100);
    assert(tl_tick(&e, 7900) == TL_EVENT_COMPLETE);
    assert(e.completed_in_block == 2);
    assert(tl_pause(&e, 8000));
    assert(e.phase == TL_FOCUS_PAUSED && tl_remaining(&e, 99000) == 4900);
    assert(tl_start(&e, 99000));
    assert(tl_tick(&e, 103899) == 0);
    assert(tl_tick(&e, 103900) == TL_EVENT_CHIME);
    x = tl_tick(&e, 104800);
    assert((x & (TL_EVENT_COMPLETE | TL_EVENT_MUSIC)) ==
           (TL_EVENT_COMPLETE | TL_EVENT_MUSIC));
    assert(e.phase == TL_MUSIC && e.completed_in_block == 0 && e.music_serial == 1);
    assert(tl_tick(&e, 117300) == TL_EVENT_REST_START);
    assert(e.phase == TL_REST);
    assert(tl_tick(&e, 177300) == TL_EVENT_REST_END && e.phase == TL_WAIT);
    assert(tl_start(&e, 180000) && e.phase == TL_FOCUS);
}

static void test_config_and_undo(void) {
    TLConfig c = {5,1,60};
    TLEngine e;
    tl_init(&e,c,0,0,0);
    assert(tl_start(&e,0));
    e.config.focus_seconds = 6;
    assert(tl_finish(&e,1000,1));
    assert(e.pending_planned_ms == 5000);
    assert(tl_tick(&e,1900) == (TL_EVENT_COMPLETE | TL_EVENT_MUSIC));
    tl_undo(&e,1900,0);
    assert(e.phase == TL_FOCUS && e.completed_in_block == 0);
    assert(tl_remaining(&e,1900) == 6000);
    assert(tl_finish(&e,7900,1) == TL_EVENT_CHIME);
    assert(!e.pending_early && e.pending_actual_ms == 6000);
    assert(!tl_config_valid((TLConfig){0,30,900}));
    assert(!tl_config_valid((TLConfig){180,0,900}));
    assert(tl_config_valid((TLConfig){180,30,900}));
}

static void test_sound_space(void) {
    unsigned i;
    unsigned char seen[8][8][8] = {{{0}}};
    TLAudio audio;
    int notes[3];
    uint64_t hashes[4] = {0,0,0,0};
    assert(tl_audio_init(&audio));
    for (i = 0; i < 336; ++i) {
        tl_chime_notes(i,notes);
        assert(notes[0] != notes[1] && notes[1] != notes[2] && notes[0] != notes[2]);
        assert(!seen[notes[0]][notes[1]][notes[2]]);
        seen[notes[0]][notes[1]][notes[2]] = 1;
        assert(tl_audio_chime(&audio,i));
        assert(!memcmp(audio.wav,"RIFF",4));
    }
    for (i = 0; i < 4; ++i) {
        size_t j;
        assert(tl_audio_music(&audio,i));
        hashes[i] = 1469598103934665603ULL;
        for (j = 44; j < audio.length; ++j)
            hashes[i] = (hashes[i] ^ audio.wav[j]) * 1099511628211ULL;
        for (j = 0; j < i; ++j) assert(hashes[j] != hashes[i]);
    }
    assert(tl_audio_rest_end(&audio));
    tl_audio_free(&audio);
}
static void test_thirty_rounds(void) {
    TLConfig c = {180,30,900};
    TLEngine e;
    unsigned i;
    uint64_t t = 0;
    tl_init(&e,c,0,0,0);
    assert(tl_start(&e,t));
    for (i = 0; i < 30; ++i) {
        t += 180000;
        assert(tl_tick(&e,t) == TL_EVENT_CHIME);
        t += TL_CHIME_MS;
        if (i < 29) assert(tl_tick(&e,t) == TL_EVENT_COMPLETE);
        else assert(tl_tick(&e,t) == (TL_EVENT_COMPLETE | TL_EVENT_MUSIC));
    }
    assert(e.phase == TL_MUSIC && e.completed_in_block == 0);
    t += TL_MUSIC_MS;
    assert(tl_tick(&e,t) == TL_EVENT_REST_START);
    assert(tl_remaining(&e,t) == 900000);
}

int main(void) {
    test_cycle_and_rest(); test_config_and_undo(); test_sound_space(); test_thirty_rounds();
    puts("core: pass (30 rounds, rest, pause, undo, 336 distinct chimes, 4 themes)");
    return 0;
}
