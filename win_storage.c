#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <io.h>
#include "win_storage.h"

static int path_join(wchar_t *out, size_t size, const wchar_t *base, const wchar_t *leaf) {
    int n = _snwprintf(out, size, L"%ls\\%ls", base, leaf);
    if (n < 0 || (size_t)n >= size) { out[0] = 0; return 0; }
    return 1;
}
static int push(TLHistory *h, TLRecord r) {
    if (h->count == h->capacity) {
        size_t cap = h->capacity ? h->capacity * 2 : 64;
        TLRecord *p;
        if (cap < h->capacity || cap > SIZE_MAX / sizeof(*p)) return 0;
        p = (TLRecord *)realloc(h->records, cap * sizeof(*p));
        if (!p) return 0;
        h->records = p; h->capacity = cap;
    }
    h->records[h->count++] = r;
    return 1;
}
static int date_key(time_t when) {
    struct tm local;
    localtime_s(&local, &when);
    return local.tm_year * 366 + local.tm_yday;
}
static void add_memory(TLHistory *h, TLRecord r) {
    r.prior_block = h->block_count;
    h->block_count++;
    if (h->block_count >= r.goal) { h->block_count = 0; h->music_count++; }
    push(h, r);
}
static void undo_memory(TLHistory *h, unsigned id) {
    TLRecord *last;
    if (!h->count) return;
    last = &h->records[h->count - 1];
    if (last->id != id) return;
    if (last->prior_block + 1 >= last->goal && h->music_count) h->music_count--;
    h->block_count = last->prior_block;
    h->count--;
}
int tl_store_init(TLHistory *h) {
    wchar_t base[900], dir[960];
    FILE *f;
    char line[256];
    memset(h, 0, sizeof(*h));
    h->next_id = 1;
    if (SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA, NULL, SHGFP_TYPE_CURRENT, base) != S_OK ||
        !path_join(dir, sizeof(dir)/sizeof(dir[0]), base, L"TinyLoop")) return 0;
    if (!CreateDirectoryW(dir, NULL) && GetLastError() != ERROR_ALREADY_EXISTS) return 0;
    if (!path_join(h->log_path, 1024, dir, L"events.log") ||
        !path_join(h->ini_path, 1024, dir, L"settings.ini")) return 0;
    f = _wfopen(h->log_path, L"rb");
    if (!f) return 1;
    while (fgets(line, sizeof(line), f)) {
        TLRecord r = {0};
        long long ts = 0;
        if (!strchr(line, '\n') && !feof(f)) {
            int ch;
            while ((ch = fgetc(f)) != '\n' && ch != EOF) {}
            continue;
        }
        if (line[0] == 'C') {
            if (sscanf(line, "C,%u,%lld,%d,%u,%u,%d", &r.id, &ts, &r.early,
                       &r.planned_ms, &r.actual_ms, &r.goal) != 6) continue;
            if (r.id == 0 || r.id == UINT32_MAX || ts < 946684800LL ||
                ts > 4102444800LL || r.goal < 1 || r.goal > 999 || r.early < 0 ||
                r.early > 1 || r.planned_ms < 5000 || r.planned_ms > 5999000 ||
                r.actual_ms > r.planned_ms || r.id < h->next_id) continue;
            r.timestamp = (time_t)ts;
            h->next_id = r.id + 1;
            if (!push(h, r)) { fclose(f); return 0; }
            h->records[h->count - 1].prior_block = h->block_count;
            h->block_count++;
            if (h->block_count >= r.goal) { h->block_count = 0; h->music_count++; }
        } else if (line[0] == 'U') {
            unsigned id;
            if (sscanf(line, "U,%u", &id) == 1) undo_memory(h, id);
        }
    }
    fclose(f);
    tl_store_today(h);
    return 1;
}
void tl_store_free(TLHistory *h) { free(h->records); h->records = NULL; h->count = h->capacity = 0; }
TLConfig tl_store_config(TLHistory *h) {
    TLConfig c;
    c.focus_seconds = GetPrivateProfileIntW(L"Timer", L"FocusSeconds", 180, h->ini_path);
    c.rounds_per_block = GetPrivateProfileIntW(L"Timer", L"Rounds", 30, h->ini_path);
    c.rest_seconds = GetPrivateProfileIntW(L"Timer", L"RestSeconds", 900, h->ini_path);
    if (!tl_config_valid(c)) c = (TLConfig){180, 30, 900};
    return c;
}
static int write_int(const wchar_t *path, const wchar_t *section,
                     const wchar_t *key, int value) {
    wchar_t buf[32];
    _snwprintf(buf, 32, L"%d", value);
    return WritePrivateProfileStringW(section, key, buf, path) != 0;
}
int tl_store_save_config(TLHistory *h, TLConfig c) {
    return tl_config_valid(c) &&
           write_int(h->ini_path, L"Timer", L"FocusSeconds", c.focus_seconds) &&
           write_int(h->ini_path, L"Timer", L"Rounds", c.rounds_per_block) &&
           write_int(h->ini_path, L"Timer", L"RestSeconds", c.rest_seconds);
}
static int append_line(TLHistory *h, const char *line) {
    FILE *f = _wfopen(h->log_path, L"ab");
    size_t length = strlen(line);
    int ok;
    if (!f) return 0;
    ok = fwrite(line, 1, length, f) == length && fflush(f) == 0 && _commit(_fileno(f)) == 0;
    if (fclose(f) != 0) ok = 0;
    return ok;
}
int tl_store_add(TLHistory *h, const TLEngine *e) {
    TLRecord r;
    char line[256];
    if (h->next_id == UINT32_MAX) return 0;
    r.id = h->next_id;
    r.timestamp = time(NULL);
    r.early = e->pending_early;
    r.planned_ms = e->pending_planned_ms;
    r.actual_ms = e->pending_actual_ms;
    r.goal = e->config.rounds_per_block;
    if (h->count == h->capacity) {
        size_t cap = h->capacity ? h->capacity * 2 : 64;
        TLRecord *p;
        if (cap < h->capacity || cap > SIZE_MAX / sizeof(*p)) return 0;
        p = (TLRecord *)realloc(h->records, cap * sizeof(*p));
        if (!p) return 0;
        h->records = p; h->capacity = cap;
    }
    _snprintf(line, sizeof(line), "C,%u,%lld,%d,%u,%u,%d\n", r.id,
              (long long)r.timestamp, r.early, r.planned_ms, r.actual_ms, r.goal);
    if (!append_line(h, line)) return 0;
    add_memory(h, r);
    h->next_id++;
    if (date_key(r.timestamp) == h->today_key) h->today_count++;
    return 1;
}
int tl_store_undo(TLHistory *h) {
    char line[64];
    unsigned id;
    if (!h->count) return 0;
    id = h->records[h->count - 1].id;
    _snprintf(line, sizeof(line), "U,%u\n", id);
    if (!append_line(h, line)) return 0;
    if (date_key(h->records[h->count - 1].timestamp) == h->today_key &&
        h->today_count) h->today_count--;
    undo_memory(h, id);
    return 1;
}
unsigned tl_store_today(TLHistory *h) {
    size_t i;
    time_t now = time(NULL);
    int today = date_key(now);
    if (today == h->today_key) return h->today_count;
    h->today_key = today;
    h->today_count = 0;
    for (i = 0; i < h->count; ++i) {
        if (date_key(h->records[i].timestamp) == today) h->today_count++;
    }
    return h->today_count;
}
void tl_store_save_session(TLHistory *h, const TLEngine *e, uint64_t now_ms) {
    TLPhase phase = e->phase;
    uint64_t remaining = tl_remaining(e, now_ms);
    if (phase == TL_CHIME) phase = TL_IDLE;
    if (phase == TL_MUSIC) { phase = TL_REST_PAUSED; remaining = (uint64_t)e->config.rest_seconds * 1000; }
    if (phase == TL_FOCUS) phase = TL_FOCUS_PAUSED;
    if (phase == TL_REST) phase = TL_REST_PAUSED;
    write_int(h->ini_path, L"Session", L"Phase", phase);
    write_int(h->ini_path, L"Session", L"RemainingMs", (int)remaining);
    write_int(h->ini_path, L"Session", L"FocusMs", (int)e->current_focus_ms);
}
void tl_store_restore_session(TLHistory *h, TLEngine *e) {
    int phase = GetPrivateProfileIntW(L"Session", L"Phase", TL_IDLE, h->ini_path);
    int remaining = GetPrivateProfileIntW(L"Session", L"RemainingMs", 0, h->ini_path);
    int focus = GetPrivateProfileIntW(L"Session", L"FocusMs", e->config.focus_seconds * 1000, h->ini_path);
    if (phase == TL_FOCUS_PAUSED && focus >= 5000 && focus <= 5999000 &&
        remaining >= 0 && remaining <= focus) {
        e->phase = TL_FOCUS_PAUSED;
        e->current_focus_ms = (uint32_t)focus;
        e->remaining_ms = (uint64_t)remaining;
    } else if (phase == TL_REST_PAUSED && remaining >= 0 && remaining <= 5999000) {
        e->phase = TL_REST_PAUSED;
        e->remaining_ms = (uint64_t)remaining;
    } else if (phase == TL_WAIT) {
        e->phase = TL_WAIT;
        e->remaining_ms = 0;
    }
}
