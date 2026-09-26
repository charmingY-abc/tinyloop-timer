#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0601
#include <windows.h>
#include <shellapi.h>
#include <mmsystem.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>
#include "timer_core.h"
#include "audio.h"
#include "win_storage.h"

#define APP_CLASS L"TinyLoopNativeTimer"
#define WM_TRAY (WM_APP + 1)
#define WM_SHOW_APP (WM_APP + 2)
#define ID_TIMER 101
#define ID_STATUS 102
#define ID_START 103
#define ID_COMPLETE 104
#define ID_FOCUS_EDIT 105
#define ID_ROUNDS_EDIT 106
#define ID_REST_EDIT 107
#define ID_APPLY 108
#define ID_UNDO 109
#define ID_FOOTER 110
#define ID_TRAY_SHOW 201
#define ID_TRAY_PAUSE 202
#define ID_TRAY_UNDO 203
#define ID_TRAY_HOTKEY 204
#define ID_TRAY_EXIT 205

typedef struct {
    HWND window, timer, status, start, complete, focus_edit, rounds_edit;
    HWND rest_edit, apply, undo, footer;
    HFONT font_big, font_normal, font_small;
    HBRUSH bg;
    TLHistory history;
    TLEngine engine;
    TLAudio audio;
    HINSTANCE instance;
    HANDLE instance_mutex;
    int dpi, hotkey_vk, hotkey_registered, hotkey_error;
    int music_suspended;
    int complete_enabled, start_enabled, undo_enabled;
    wchar_t last_time[48], last_status[160], last_start[48], last_footer[160];
} App;

static App app;
static int scale(int n) { return MulDiv(n, app.dpi, 96); }
static uint64_t tick_now(void) { return GetTickCount64(); }
static void set_changed(HWND w, wchar_t *cache, size_t size, const wchar_t *value) {
    if (w && wcscmp(cache, value) != 0) {
        wcsncpy(cache, value, size - 1); cache[size - 1] = 0;
        SetWindowTextW(w, cache);
    }
}
static void set_enabled(HWND w, int *cache, int enabled) {
    if (w && *cache != enabled) { EnableWindow(w,enabled); *cache = enabled; }
}
static HFONT make_font(int size, int weight) {
    return CreateFontW(-scale(size), 0, 0, 0, weight, FALSE, FALSE, FALSE,
                       DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                       CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
}
static HWND control(const wchar_t *kind, const wchar_t *text, DWORD style,
                    int x, int y, int w, int h, int id, HFONT font) {
    HWND out = CreateWindowExW(0, kind, text, WS_CHILD | WS_VISIBLE | style,
                               scale(x), scale(y), scale(w), scale(h), app.window,
                               (HMENU)(INT_PTR)id, app.instance, NULL);
    if (out && font) SendMessageW(out, WM_SETFONT, (WPARAM)font, TRUE);
    return out;
}
static void time_text(int seconds, wchar_t *out, size_t n) {
    _snwprintf(out, n, L"%02d:%02d", seconds / 60, seconds % 60);
    out[n - 1] = 0;
}
static int parse_time(HWND edit, int min_seconds, int *seconds) {
    wchar_t text[24] = {0}, tail = 0;
    int m = 0, s = 0;
    if (GetWindowTextLengthW(edit) >= 23) return 0;
    GetWindowTextW(edit, text, 24);
    if (swscanf(text, L"%d:%d%c", &m, &s, &tail) != 2 ||
        m < 0 || s < 0 || s > 59 || m > 99 || m * 60 + s < min_seconds ||
        m * 60 + s > 5999) return 0;
    *seconds = m * 60 + s;
    return 1;
}
static int parse_rounds(HWND edit, int *value) {
    wchar_t text[24], *end;
    long n;
    if (GetWindowTextLengthW(edit) >= 23) return 0;
    GetWindowTextW(edit, text, 24);
    n = wcstol(text, &end, 10);
    if (end == text || *end != 0 || n < 1 || n > 999) return 0;
    *value = (int)n;
    return 1;
}
static void play_wave(void) {
    /* audio.wav remains allocated and unchanged until the next sound stops it. */
    if (app.audio.wav && app.audio.length)
        PlaySoundA((LPCSTR)app.audio.wav, NULL, SND_MEMORY | SND_ASYNC | SND_NODEFAULT);
}
static void stop_wave(void) { PlaySoundA(NULL, NULL, 0); }
static void hotkey_sync(void) {
    int needed = app.engine.phase == TL_FOCUS;
    if (!needed && app.hotkey_registered) {
        UnregisterHotKey(app.window, 1);
        app.hotkey_registered = 0;
    }
    if (needed && !app.hotkey_registered && !app.hotkey_error) {
        if (RegisterHotKey(app.window, 1, MOD_NOREPEAT, app.hotkey_vk))
            app.hotkey_registered = 1;
        else app.hotkey_error = 1;
    }
}
static void redraw(void) {
    wchar_t display[48], details[160], button[48], foot[160];
    uint64_t left = tl_remaining(&app.engine, tick_now());
    unsigned seconds = (unsigned)((left + 999U) / 1000U);
    int next_round = app.engine.completed_in_block + 1;
    if (next_round > app.engine.config.rounds_per_block)
        next_round = app.engine.config.rounds_per_block;
    if (app.engine.phase == TL_MUSIC) wcscpy(display, L"♪ 轻音乐");
    else if (app.engine.phase == TL_CHIME) wcscpy(display, L"完成");
    else if (app.engine.phase == TL_WAIT) wcscpy(display, L"休息结束");
    else time_text((int)seconds, display, 48);
    if (app.engine.phase == TL_REST || app.engine.phase == TL_REST_PAUSED)
        _snwprintf(details, 160, L"休息中 · 今日 %u 次 · 总计 %u 次",
                   tl_store_today(&app.history), (unsigned)app.history.count);
    else if (app.engine.phase == TL_MUSIC)
        _snwprintf(details, 160, L"第 %u 段轻音乐 · 随后休息",
                   ((app.engine.music_serial - 1U) % 4U) + 1U);
    else if (app.engine.phase == TL_WAIT)
        _snwprintf(details, 160, L"本组完成 · 今日 %u 次 · 点击开始下一组",
                   tl_store_today(&app.history));
    else _snwprintf(details, 160, L"第 %d / %d 轮 · 今日 %u 次 · 总计 %u 次",
                    next_round, app.engine.config.rounds_per_block,
                    tl_store_today(&app.history), (unsigned)app.history.count);
    if (app.engine.phase == TL_FOCUS || app.engine.phase == TL_REST) wcscpy(button,L"暂停");
    else if (app.engine.phase == TL_FOCUS_PAUSED || app.engine.phase == TL_REST_PAUSED) wcscpy(button,L"继续");
    else if (app.engine.phase == TL_CHIME || app.engine.phase == TL_MUSIC) wcscpy(button,L"播放中");
    else wcscpy(button,L"开始");
    if (app.hotkey_error)
        _snwprintf(foot, 160, L"%ls 被占用 · 托盘菜单可切换 F8 / F9",
                   app.hotkey_vk == VK_F8 ? L"F8" : L"F9");
    else _snwprintf(foot, 160, L"空格或 %ls 提前完成 · 关闭窗口后留在托盘",
                    app.hotkey_vk == VK_F8 ? L"F8" : L"F9");
    display[47] = 0; details[159] = 0; foot[159] = 0;
    set_changed(app.timer,app.last_time,48,display);
    set_changed(app.status,app.last_status,160,details);
    set_changed(app.start,app.last_start,48,button);
    set_changed(app.footer,app.last_footer,160,foot);
    set_enabled(app.complete,&app.complete_enabled,app.engine.phase == TL_FOCUS);
    set_enabled(app.start,&app.start_enabled,app.engine.phase != TL_CHIME && app.engine.phase != TL_MUSIC);
    set_enabled(app.undo,&app.undo_enabled,app.history.count > 0);
    hotkey_sync();
}
static void save_session(void) {
    tl_store_save_session(&app.history, &app.engine, tick_now());
}
static void complete_early(void) {
    unsigned event = tl_finish(&app.engine, tick_now(), 1);
    if (event & TL_EVENT_CHIME) {
        stop_wave();
        tl_audio_chime(&app.audio, app.engine.sound_serial - 1U);
        play_wave();
        save_session();
    }
    redraw();
}
static void progress_engine(void) {
    if (app.music_suspended) return;
    unsigned event = tl_tick(&app.engine,tick_now());
    if (event & TL_EVENT_CHIME) {
        stop_wave();
        tl_audio_chime(&app.audio,app.engine.sound_serial - 1U);
        play_wave();
    }
    if (event & TL_EVENT_COMPLETE) {
        if (!tl_store_add(&app.history,&app.engine)) {
            stop_wave();
            app.engine.phase = TL_IDLE;
            app.engine.completed_in_block = app.history.block_count;
            MessageBoxW(app.window,L"无法保存本轮记录。计时已停止，请检查本机存储空间与权限。",
                        L"保存失败",MB_OK | MB_ICONERROR);
        } else if (event & TL_EVENT_MUSIC) {
            stop_wave();
            tl_audio_music(&app.audio,app.engine.music_serial - 1U);
            play_wave();
        }
        save_session();
    }
    if (event & TL_EVENT_REST_START) save_session();
    if (event & TL_EVENT_REST_END) {
        stop_wave();
        tl_audio_rest_end(&app.audio); play_wave(); save_session();
    }
    redraw();
}
static void do_start_pause(void) {
    uint64_t now = tick_now();
    if (app.engine.phase == TL_FOCUS || app.engine.phase == TL_REST)
        tl_pause(&app.engine,now);
    else tl_start(&app.engine,now);
    save_session(); redraw();
}
static void do_undo(void) {
    if (!app.history.count) return;
    if (!tl_store_undo(&app.history)) {
        MessageBoxW(app.window,L"无法写入撤销记录。",L"保存失败",MB_OK | MB_ICONERROR);
        return;
    }
    if (app.engine.phase == TL_MUSIC || app.engine.phase == TL_REST ||
        app.engine.phase == TL_REST_PAUSED || app.engine.phase == TL_WAIT) stop_wave();
    tl_undo(&app.engine,tick_now(),app.history.block_count);
    save_session(); redraw();
}
static void apply_settings(void) {
    TLConfig c;
    if (!parse_time(app.focus_edit,5,&c.focus_seconds) ||
        !parse_rounds(app.rounds_edit,&c.rounds_per_block) ||
        !parse_time(app.rest_edit,60,&c.rest_seconds)) {
        MessageBoxW(app.window,L"单轮和休息请输入 分:秒（例如 03:00、15:00）；大循环请输入 1–999。",
                    L"设置格式",MB_OK | MB_ICONINFORMATION);
        return;
    }
    if (!tl_store_save_config(&app.history,c)) {
        MessageBoxW(app.window,L"设置无法写入本机。",L"保存失败",MB_OK | MB_ICONERROR);
        return;
    }
    app.engine.config = c;
    /* Active rounds and breaks keep their current duration. */
    if (app.engine.phase == TL_IDLE) {
        app.engine.current_focus_ms = (uint32_t)c.focus_seconds * 1000U;
        app.engine.remaining_ms = app.engine.current_focus_ms;
    }
    save_session(); redraw();
}
static void show_app(void) {
    ShowWindow(app.window,SW_SHOWNORMAL);
    SetForegroundWindow(app.window);
}
static void tray_menu(void) {
    HMENU menu = CreatePopupMenu();
    POINT p;
    if (!menu) return;
    AppendMenuW(menu,MF_STRING,ID_TRAY_SHOW,L"显示计时器");
    AppendMenuW(menu,MF_STRING,ID_TRAY_PAUSE,L"暂停 / 继续");
    AppendMenuW(menu,MF_STRING,ID_TRAY_UNDO,L"撤销上一轮");
    AppendMenuW(menu,MF_STRING,ID_TRAY_HOTKEY,L"切换单键 F8 / F9");
    AppendMenuW(menu,MF_SEPARATOR,0,NULL);
    AppendMenuW(menu,MF_STRING,ID_TRAY_EXIT,L"退出");
    GetCursorPos(&p); SetForegroundWindow(app.window);
    TrackPopupMenu(menu,TPM_RIGHTBUTTON,p.x,p.y,0,app.window,NULL);
    PostMessageW(app.window,WM_NULL,0,0);
    DestroyMenu(menu);
}
static void switch_hotkey(void) {
    if (app.hotkey_registered) { UnregisterHotKey(app.window,1); app.hotkey_registered = 0; }
    app.hotkey_vk = app.hotkey_vk == VK_F8 ? VK_F9 : VK_F8;
    app.hotkey_error = 0;
    {
        wchar_t key[8];
        _snwprintf(key,8,L"%d",app.hotkey_vk);
        WritePrivateProfileStringW(L"Timer",L"Hotkey",key,app.history.ini_path);
    }
    redraw();
}
static void create_ui(HWND hwnd) {
    wchar_t focus[24], rest[24], rounds[24];
    app.window = hwnd;
    app.font_big = make_font(55,FW_SEMIBOLD);
    app.font_normal = make_font(16,FW_NORMAL);
    app.font_small = make_font(12,FW_NORMAL);
    app.bg = CreateSolidBrush(RGB(250,250,250));
    app.timer = control(L"STATIC",L"03:00",SS_CENTER,30,22,360,75,ID_TIMER,app.font_big);
    app.status = control(L"STATIC",L"",SS_CENTER,20,110,380,28,ID_STATUS,app.font_normal);
    app.start = control(L"BUTTON",L"开始",BS_PUSHBUTTON | WS_TABSTOP,46,151,155,43,ID_START,app.font_normal);
    app.complete = control(L"BUTTON",L"提前完成",BS_PUSHBUTTON | WS_TABSTOP,219,151,155,43,ID_COMPLETE,app.font_normal);
    control(L"STATIC",L"单轮",SS_LEFT,31,222,46,25,0,app.font_small);
    control(L"STATIC",L"轮数",SS_LEFT,158,222,46,25,0,app.font_small);
    control(L"STATIC",L"休息",SS_LEFT,274,222,46,25,0,app.font_small);
    time_text(app.engine.config.focus_seconds,focus,24);
    time_text(app.engine.config.rest_seconds,rest,24);
    _snwprintf(rounds,24,L"%d",app.engine.config.rounds_per_block);
    app.focus_edit = control(L"EDIT",focus,WS_BORDER | ES_CENTER | WS_TABSTOP,
                             72,217,75,29,ID_FOCUS_EDIT,app.font_small);
    app.rounds_edit = control(L"EDIT",rounds,WS_BORDER | ES_CENTER | WS_TABSTOP,
                              201,217,58,29,ID_ROUNDS_EDIT,app.font_small);
    app.rest_edit = control(L"EDIT",rest,WS_BORDER | ES_CENTER | WS_TABSTOP,
                            315,217,75,29,ID_REST_EDIT,app.font_small);
    app.apply = control(L"BUTTON",L"应用设置",BS_PUSHBUTTON | WS_TABSTOP,
                        46,263,155,35,ID_APPLY,app.font_small);
    app.undo = control(L"BUTTON",L"撤销上次",BS_PUSHBUTTON | WS_TABSTOP,
                       219,263,155,35,ID_UNDO,app.font_small);
    app.footer = control(L"STATIC",L"",SS_CENTER,15,311,390,22,ID_FOOTER,app.font_small);
    SetTimer(hwnd,1,100,NULL);
    {
        NOTIFYICONDATAW tray;
        ZeroMemory(&tray,sizeof(tray));
        tray.cbSize = sizeof(tray); tray.hWnd = hwnd; tray.uID = 1;
        tray.uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE;
        tray.uCallbackMessage = WM_TRAY;
        tray.hIcon = LoadIconW(NULL,IDI_APPLICATION);
        wcscpy(tray.szTip,L"轮次计时 · 右键打开菜单");
        Shell_NotifyIconW(NIM_ADD,&tray);
    }
    redraw();
}
static LRESULT CALLBACK window_proc(HWND hwnd, UINT msg, WPARAM w, LPARAM l) {
    switch (msg) {
    case WM_CREATE: create_ui(hwnd); return 0;
    case WM_TIMER: if (w == 1) progress_engine(); return 0;
    case WM_HOTKEY: if (w == 1) complete_early(); return 0;
    case WM_SHOW_APP: show_app(); return 0;
    case WM_TRAY:
        if (l == WM_LBUTTONDBLCLK) show_app();
        else if (l == WM_RBUTTONUP || l == WM_CONTEXTMENU) tray_menu();
        return 0;
    case WM_COMMAND:
        switch (LOWORD(w)) {
        case ID_START: case ID_TRAY_PAUSE: do_start_pause(); return 0;
        case ID_COMPLETE: complete_early(); return 0;
        case ID_APPLY: apply_settings(); return 0;
        case ID_UNDO: case ID_TRAY_UNDO: do_undo(); return 0;
        case ID_TRAY_SHOW: show_app(); return 0;
        case ID_TRAY_HOTKEY: switch_hotkey(); return 0;
        case ID_TRAY_EXIT: DestroyWindow(hwnd); return 0;
        }
        break;
    case WM_POWERBROADCAST:
        if (w == PBT_APMSUSPEND) {
            if (app.engine.phase == TL_CHIME || app.engine.phase == TL_MUSIC) {
                stop_wave(); app.music_suspended = 1;
            } else { tl_pause(&app.engine,tick_now()); save_session(); }
            redraw(); return TRUE;
        }
        if (w == PBT_APMRESUMEAUTOMATIC && app.music_suspended) {
            app.music_suspended = 0;
            if (app.engine.phase == TL_CHIME) {
                app.engine.deadline_ms = tick_now() + TL_CHIME_MS;
                stop_wave();
                tl_audio_chime(&app.audio,app.engine.sound_serial - 1U); play_wave();
            } else if (app.engine.phase == TL_MUSIC) {
                app.engine.deadline_ms = tick_now() + TL_MUSIC_MS;
                stop_wave();
                tl_audio_music(&app.audio,app.engine.music_serial - 1U); play_wave();
            }
            return TRUE;
        }
        break;
    case WM_CLOSE: ShowWindow(hwnd,SW_HIDE); return 0;
    case WM_CTLCOLORSTATIC:
        SetBkMode((HDC)w,TRANSPARENT);
        return (LRESULT)app.bg;
    case WM_ERASEBKGND: {
        RECT r; GetClientRect(hwnd,&r); FillRect((HDC)w,&r,app.bg); return 1;
    }
    case WM_DESTROY: {
        NOTIFYICONDATAW tray;
        save_session();
        ZeroMemory(&tray,sizeof(tray)); tray.cbSize = sizeof(tray);
        tray.hWnd = hwnd; tray.uID = 1;
        Shell_NotifyIconW(NIM_DELETE,&tray);
        if (app.hotkey_registered) UnregisterHotKey(hwnd,1);
        KillTimer(hwnd,1); stop_wave();
        tl_audio_free(&app.audio); tl_store_free(&app.history);
        DeleteObject(app.font_big); DeleteObject(app.font_normal);
        DeleteObject(app.font_small); DeleteObject(app.bg);
        PostQuitMessage(0); return 0;
    }
    }
    return DefWindowProcW(hwnd,msg,w,l);
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE unused, PWSTR cmd, int show) {
    WNDCLASSEXW wc;
    MSG msg;
    RECT rect = {0,0,420,351};
    HWND other;
    HDC dc;
    (void)unused; (void)cmd; (void)show;
    ZeroMemory(&app,sizeof(app));
    app.complete_enabled = app.start_enabled = app.undo_enabled = -1;
    app.instance = instance;
    app.instance_mutex = CreateMutexW(NULL,FALSE,L"Local\\TinyLoopTimer_Instance");
    if (app.instance_mutex && GetLastError() == ERROR_ALREADY_EXISTS) {
        other = FindWindowW(APP_CLASS,NULL);
        if (other) PostMessageW(other,WM_SHOW_APP,0,0);
        CloseHandle(app.instance_mutex);
        return 0;
    }
    if (!tl_store_init(&app.history)) {
        MessageBoxW(NULL,L"无法打开本机记录目录。",L"轮次计时",MB_OK | MB_ICONERROR);
        return 1;
    }
    tl_init(&app.engine,tl_store_config(&app.history),app.history.block_count,
            app.history.next_id - 1U,app.history.music_count);
    tl_store_restore_session(&app.history,&app.engine);
    if (!tl_audio_init(&app.audio)) {
        MessageBoxW(NULL,L"内存不足，无法初始化声音。",L"轮次计时",MB_OK | MB_ICONERROR);
        tl_store_free(&app.history); return 1;
    }
    app.hotkey_vk = GetPrivateProfileIntW(L"Timer",L"Hotkey",VK_F8,app.history.ini_path);
    if (app.hotkey_vk != VK_F8 && app.hotkey_vk != VK_F9) app.hotkey_vk = VK_F8;
    dc = GetDC(NULL); app.dpi = GetDeviceCaps(dc,LOGPIXELSX); ReleaseDC(NULL,dc);
    if (app.dpi < 72 || app.dpi > 384) app.dpi = 96;
    ZeroMemory(&wc,sizeof(wc)); wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = window_proc; wc.hInstance = instance;
    wc.hCursor = LoadCursorW(NULL,IDC_ARROW);
    wc.hIcon = LoadIconW(NULL,IDI_APPLICATION);
    wc.lpszClassName = APP_CLASS;
    if (!RegisterClassExW(&wc)) return 1;
    rect.right = scale(rect.right); rect.bottom = scale(rect.bottom);
    AdjustWindowRectEx(&rect,WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                       FALSE,0);
    app.window = CreateWindowExW(0,APP_CLASS,L"轮次计时",WS_OVERLAPPED | WS_CAPTION |
                WS_SYSMENU | WS_MINIMIZEBOX,CW_USEDEFAULT,CW_USEDEFAULT,
                rect.right - rect.left,rect.bottom - rect.top,NULL,NULL,instance,NULL);
    if (!app.window) return 1;
    ShowWindow(app.window,SW_SHOWNORMAL);
    UpdateWindow(app.window);
    while (GetMessageW(&msg,NULL,0,0) > 0) {
        HWND focused = GetFocus();
        int edit_focused = focused == app.focus_edit || focused == app.rounds_edit ||
                           focused == app.rest_edit;
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_SPACE &&
            !(msg.lParam & (1UL << 30)) && !edit_focused &&
            app.engine.phase == TL_FOCUS && GetForegroundWindow() == app.window) {
            complete_early(); continue;
        }
        if (IsDialogMessageW(app.window,&msg)) continue;
        TranslateMessage(&msg); DispatchMessageW(&msg);
    }
    if (app.instance_mutex) CloseHandle(app.instance_mutex);
    return (int)msg.wParam;
}
