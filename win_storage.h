#ifndef TINYLOOP_WIN_STORAGE_H
#define TINYLOOP_WIN_STORAGE_H
#include "timer_core.h"
#include <stddef.h>
#include <stdint.h>
#include <time.h>
#include <wchar.h>

typedef struct {
    unsigned id;
    time_t timestamp;
    int early;
    uint32_t planned_ms;
    uint32_t actual_ms;
    int goal;
    int prior_block;
} TLRecord;

typedef struct {
    TLRecord *records;
    size_t count, capacity;
    unsigned next_id;
    int block_count;
    unsigned music_count;
    unsigned today_count;
    int today_key;
    wchar_t log_path[1024];
    wchar_t ini_path[1024];
} TLHistory;

int tl_store_init(TLHistory *h);
void tl_store_free(TLHistory *h);
TLConfig tl_store_config(TLHistory *h);
int tl_store_save_config(TLHistory *h, TLConfig c);
int tl_store_add(TLHistory *h, const TLEngine *e);
int tl_store_undo(TLHistory *h);
unsigned tl_store_today(TLHistory *h);
void tl_store_save_session(TLHistory *h, const TLEngine *e, uint64_t now_ms);
void tl_store_restore_session(TLHistory *h, TLEngine *e);

#endif
