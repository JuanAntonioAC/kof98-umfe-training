#include "menu.h"
#include "padnav.h"
#include "logic.h"
#include "log.h"
#include "moves.h"
#include <stdio.h>
#include <string.h>
#include <windows.h>

extern TrainEngine g_eng;
extern unsigned g_live_word;
extern volatile int g_practice;
extern int g_state_known;
extern int g_dummy_index;
extern int g_menu_index;
extern void game_save(void);
extern void game_capture_dir(int which);

unsigned g_confirm = 0x40;  /* D */
unsigned g_cancel = 0x100; /* C */

static HWND g_hwnd;
static volatile int g_open;
static volatile int g_pad_alive;
static char g_status[200];
static unsigned g_prev_word;
static unsigned g_prev_key;
static HANDLE g_ready;
static volatile long g_ui_started;
static DWORD g_ui_tid;
static int g_screen;
static int g_index;
static int g_scroll;
static int g_rev; /* 0 wake, 1 block, 2 hit */
static volatile int g_request_show;
static MenuInput g_pads;
static unsigned g_last_held;
static DWORD (WINAPI *g_xinput)(DWORD, void*);
static int (WINAPI *g_vs_pads)(void*, int);
static void (WINAPI *g_vs_menu)(int);
static unsigned long long g_vs_probe;

typedef struct XPad {
    unsigned short buttons;
    unsigned char lt, rt;
    short lx, ly, rx, ry;
} XPad;
typedef struct XState { DWORD packet; XPad pad; } XState;
typedef struct VsPad { int id; int side; XPad pad; } VsPad;

enum { SCR_ROOT = 0, SCR_REV, SCR_MOVES, SCR_DUMMY, SCR_REC };
enum { ROW_H = 28, ROW_N = 12, LIST_TOP = 124, WIN_W = 760, WIN_H = 640 };
#define WM_MENU_TOGGLE (WM_APP + 1)

#define XB_A 0x1000u
#define XB_B 0x2000u
#define XB_X 0x4000u
#define XB_Y 0x8000u
#define FIGHT_PICK (XB_A | XB_B | XB_X | XB_Y)

#define COL_BG 0x00913B07u
#define COL_BAR 0x00D4A848u
#define COL_HI 0x00781824u
#define COL_SEL 0x00FF7716u
#define COL_TEXT 0x00FFFFFFu
#define COL_DIM 0x00C4B090u
#define COL_GOLD 0x004AD5FFu
#define COL_CYAN 0x00FFE296u

static int clampi(int v, int lo, int hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static int* delay_ptr(void) {
    if (g_rev == 0) return &g_eng.wakeup_delay;
    if (g_rev == 1) return &g_eng.block_delay;
    return &g_eng.hit_delay;
}

static int* action_ptr(void) {
    if (g_rev == 0) return &g_eng.wakeup_action;
    if (g_rev == 1) return &g_eng.block_action;
    return &g_eng.hit_action;
}

static int* move_ptr(void) {
    if (g_rev == 0) return &g_eng.wakeup_move;
    if (g_rev == 1) return &g_eng.block_move;
    return &g_eng.hit_move;
}

static const char* rev_title(void) {
    if (g_rev == 0) return "WAKE-UP REVERSAL";
    if (g_rev == 1) return "BLOCK REVERSAL";
    return "HIT REVERSAL";
}

static int screen_count(void) {
    if (g_screen == SCR_ROOT) return 8;
    if (g_screen == SCR_REV) return 6;
    if (g_screen == SCR_MOVES) return roster_filter_count(g_eng.char_id, -1);
    if (g_screen == SCR_DUMMY) return 6;
    if (g_screen == SCR_REC) return 6;
    return 1;
}

static void keep_scroll(void) {
    int n = screen_count();
    if (n <= 0) n = 1;
    g_index = clampi(g_index, 0, n - 1);
    if (g_index < g_scroll) g_scroll = g_index;
    if (g_index >= g_scroll + ROW_N) g_scroll = g_index - ROW_N + 1;
    if (g_scroll < 0) g_scroll = 0;
}

static HWND find_game_window(void) {
    HWND hwnd = 0;
    DWORD pid = GetCurrentProcessId();
    while ((hwnd = FindWindowExW(0, hwnd, 0, 0)) != 0) {
        DWORD wpid = 0;
        if (hwnd == g_hwnd || !IsWindowVisible(hwnd)) continue;
        GetWindowThreadProcessId(hwnd, &wpid);
        if (wpid == pid && GetWindow(hwnd, GW_OWNER) == 0) return hwnd;
    }
    return 0;
}

static void draw_utf8(HDC dc, const char* text, RECT* area, UINT flags) {
    wchar_t wide[512];
    if (!text) return;
    if (MultiByteToWideChar(CP_UTF8, 0, text, -1, wide, 512) <= 0) return;
    DrawTextW(dc, wide, -1, area, flags | DT_NOPREFIX | DT_END_ELLIPSIS);
}

static void fill(HDC dc, RECT* r, unsigned color) {
    HBRUSH b = CreateSolidBrush(color);
    FillRect(dc, r, b);
    DeleteObject(b);
}

static void row_text(HDC dc, int y, int selected, const char* left, const char* right) {
    RECT line, rc;
    GetClientRect(g_hwnd, &rc);
    line.left = 48; line.top = y; line.right = rc.right - 48; line.bottom = y + ROW_H;
    if (selected) fill(dc, &line, COL_HI);
    SetTextColor(dc, selected ? COL_GOLD : COL_TEXT);
    line.left += 12;
    draw_utf8(dc, left, &line, DT_VCENTER | DT_SINGLELINE | DT_LEFT);
    if (right && right[0]) {
        SetTextColor(dc, selected ? COL_CYAN : COL_DIM);
        draw_utf8(dc, right, &line, DT_VCENTER | DT_SINGLELINE | DT_RIGHT);
    }
}

static void item_root(int i, char* l, char* r, int cap) {
    r[0] = 0;
    switch (i) {
    case 0: snprintf(l, cap, "Wake-up Reversal"); break;
    case 1: snprintf(l, cap, "Block Reversal"); break;
    case 2: snprintf(l, cap, "Hit Reversal"); break;
    case 3: snprintf(l, cap, "Dummy Settings"); break;
    case 4: snprintf(l, cap, "Recording"); break;
    case 5: snprintf(l, cap, "Calibrate directions"); break;
    case 6: snprintf(l, cap, "Save"); break;
    default: snprintf(l, cap, "Close"); break;
    }
}

static void item_rev(int i, char* l, char* r, int cap) {
    const MoveDef* mv = roster_move(g_eng.char_id, *move_ptr());
    if (mv && mv->cat == CAT_NORMAL) mv = roster_filter_at(g_eng.char_id, -1, 0);
    switch (i) {
    case 0:
        snprintf(l, cap, "Enabled");
        snprintf(r, cap, "%s", *action_ptr() == ACT_NONE ? "OFF" : "ON");
        break;
    case 1:
        snprintf(l, cap, "Move");
        snprintf(r, cap, "%s", mv ? mv->name : "(none)");
        break;
    case 2:
        snprintf(l, cap, "Delay");
        snprintf(r, cap, "%d frames", *delay_ptr());
        break;
    case 3:
        snprintf(l, cap, "Random extra");
        snprintf(r, cap, "0..%d", g_eng.random_span);
        break;
    case 4:
        snprintf(l, cap, "Test move now");
        r[0] = 0;
        break;
    default:
        snprintf(l, cap, "Back");
        r[0] = 0;
        break;
    }
}

static void item_dummy(int i, char* l, char* r, int cap) {
    const char* cn = "D";
    const char* ca = "C";
    if (g_confirm == 0x80) cn = "A";
    else if (g_confirm == 0x20) cn = "B";
    else if (g_confirm == 0x100) cn = "C";
    if (g_cancel == 0x80) ca = "A";
    else if (g_cancel == 0x20) ca = "B";
    else if (g_cancel == 0x40) ca = "D";
    switch (i) {
    case 0:
        snprintf(l, cap, "Dummy character");
        snprintf(r, cap, "%s", roster_name(g_eng.char_id));
        break;
    case 1:
        snprintf(l, cap, "Dummy input index");
        snprintf(r, cap, "%d", g_dummy_index);
        break;
    case 2:
        snprintf(l, cap, "Guard");
        {
            static const char* gname[] = { "off", "all", "high", "low", "after first", "random" };
            snprintf(r, cap, "%s", gname[g_eng.guard >= 0 && g_eng.guard <= 5 ? g_eng.guard : 0]);
        }
        break;
    case 3:
        snprintf(l, cap, "Confirm button");
        snprintf(r, cap, "%s", cn);
        break;
    case 4:
        snprintf(l, cap, "Cancel button");
        snprintf(r, cap, "%s", ca);
        break;
    default:
        snprintf(l, cap, "Back");
        r[0] = 0;
        break;
    }
}

static void item_rec(int i, char* l, char* r, int cap) {
    switch (i) {
    case 0:
        snprintf(l, cap, "Slot");
        snprintf(r, cap, "%d  (%d frames)", g_eng.record_slot, g_eng.slots[g_eng.record_slot].count);
        break;
    case 1:
        snprintf(l, cap, "Weight");
        snprintf(r, cap, "%d", g_eng.slots[g_eng.record_slot].weight);
        break;
    case 2:
        snprintf(l, cap, "%s", g_eng.recording ? "Stop recording" : "Start recording");
        r[0] = 0;
        break;
    case 3:
        snprintf(l, cap, "Play slot");
        r[0] = 0;
        break;
    case 4:
        snprintf(l, cap, "Clear slot");
        r[0] = 0;
        break;
    default:
        snprintf(l, cap, "Back");
        r[0] = 0;
        break;
    }
}

static void paint(HWND hwnd) {
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(hwnd, &ps);
    RECT rc, row;
    HDC mem;
    HBITMAP bmp;
    HGDIOBJ old, old_font;
    HFONT font, title_font;
    int i, y, n;
    char left[160], right[160], head[160];
    GetClientRect(hwnd, &rc);
    mem = CreateCompatibleDC(dc);
    bmp = CreateCompatibleBitmap(dc, rc.right, rc.bottom);
    old = SelectObject(mem, bmp);
    font = CreateFontW(20, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    title_font = CreateFontW(34, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    old_font = SelectObject(mem, title_font);
    SetBkMode(mem, TRANSPARENT);
    fill(mem, &rc, COL_BG);
    row.left = 24; row.top = 18; row.right = rc.right - 24; row.bottom = 28;
    fill(mem, &row, COL_BAR);
    SetTextColor(mem, COL_CYAN);
    row.top = 40; row.bottom = 84;
    DrawTextA(mem, "ADVANCED TRAINING", -1, &row, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(mem, font);
    SetTextColor(mem, COL_DIM);
    row.top = 84; row.bottom = 114;
    if (g_screen == SCR_ROOT) snprintf(head, sizeof(head), "KOF '98 UMFE   dummy %s   %s", roster_name(g_eng.char_id), g_practice ? "PRACTICE" : "NOT PRACTICE");
    else if (g_screen == SCR_REV) snprintf(head, sizeof(head), "%s   %s", rev_title(), roster_name(g_eng.char_id));
    else if (g_screen == SCR_MOVES) snprintf(head, sizeof(head), "MOVES   %s", roster_name(g_eng.char_id));
    else if (g_screen == SCR_DUMMY) snprintf(head, sizeof(head), "DUMMY SETTINGS");
    else snprintf(head, sizeof(head), "RECORDING");
    draw_utf8(mem, head, &row, DT_CENTER | DT_SINGLELINE);
    n = screen_count();
    y = LIST_TOP;
    for (i = 0; i < ROW_N; ++i) {
        int idx = g_scroll + i;
        if (idx >= n) break;
        left[0] = right[0] = 0;
        if (g_screen == SCR_ROOT) item_root(idx, left, right, 160);
        else if (g_screen == SCR_REV) item_rev(idx, left, right, 160);
        else if (g_screen == SCR_DUMMY) item_dummy(idx, left, right, 160);
        else if (g_screen == SCR_REC) item_rec(idx, left, right, 160);
        else {
            const MoveDef* mv = roster_filter_at(g_eng.char_id, -1, idx);
            if (mv) {
                snprintf(left, sizeof(left), "%s", mv->name);
                snprintf(right, sizeof(right), "%s%s", mv->cmd, mv->meter == 2 ? "  SDM" : mv->meter == 1 ? "  DM" : "");
            }
        }
        row_text(mem, y, idx == g_index, left, right);
        y += ROW_H;
    }
    SetTextColor(mem, COL_GOLD);
    row.left = 40; row.right = rc.right - 40;
    row.top = rc.bottom - 118; row.bottom = rc.bottom - 96;
    draw_utf8(mem, g_status, &row, DT_CENTER | DT_SINGLELINE);
    SetTextColor(mem, COL_CYAN);
    row.top = rc.bottom - 94; row.bottom = rc.bottom - 58;
    draw_utf8(mem,
        "Reversals auto: sin knockdown/blockstun verificado no se inventa el frame. Test move now si inyecta el comando.",
        &row, DT_CENTER | DT_WORDBREAK);
    SetTextColor(mem, COL_DIM);
    row.top = rc.bottom - 56; row.bottom = rc.bottom - 34;
    draw_utf8(mem, "A+B+C+D abre/cierra    Stick: mover    A: elegir    SELECT: atras    Close cierra", &row, DT_CENTER | DT_SINGLELINE);
    row.top = rc.bottom - 34; row.bottom = rc.bottom - 12;
    draw_utf8(mem, "Solo movimientos de este personaje. Test move now lo ejecuta el dummy.", &row, DT_CENTER | DT_SINGLELINE);
    BitBlt(dc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
    SelectObject(mem, old_font);
    SelectObject(mem, old);
    DeleteObject(font);
    DeleteObject(title_font);
    DeleteObject(bmp);
    DeleteDC(mem);
    EndPaint(hwnd, &ps);
}

static void go(int screen) {
    g_screen = screen;
    g_index = 0;
    g_scroll = 0;
}

static unsigned cycle_btn(unsigned cur, int dir) {
    static const unsigned b[4] = { 0x80, 0x20, 0x100, 0x40 };
    int i;
    for (i = 0; i < 4; ++i) if (b[i] == cur) break;
    if (i >= 4) i = 3;
    i = (i + (dir > 0 ? 1 : 3)) % 4;
    return b[i];
}

static void change_value(int dir) {
    if (g_screen == SCR_REV) {
        if (g_index == 0) *action_ptr() = *action_ptr() == ACT_NONE ? ACT_MOVE : ACT_NONE;
        else if (g_index == 1) {
            int n = roster_filter_count(g_eng.char_id, -1);
            int cur = 0, i;
            const MoveDef* now = roster_move(g_eng.char_id, *move_ptr());
            for (i = 0; i < n; ++i) if (roster_filter_at(g_eng.char_id, -1, i) == now) { cur = i; break; }
            if (n) {
                const MoveDef* mv;
                cur = (cur + dir + n) % n;
                mv = roster_filter_at(g_eng.char_id, -1, cur);
                for (i = 0; i < roster_move_count(g_eng.char_id); ++i)
                    if (roster_move(g_eng.char_id, i) == mv) { *move_ptr() = i; break; }
            }
        } else if (g_index == 2) *delay_ptr() = clampi(*delay_ptr() + dir, 0, 60);
        else if (g_index == 3) g_eng.random_span = clampi(g_eng.random_span + dir, 0, 30);
    } else if (g_screen == SCR_DUMMY) {
        if (g_index == 0) g_eng.char_id = (g_eng.char_id + dir + roster_count()) % roster_count();
        else if (g_index == 1) g_dummy_index = clampi(g_dummy_index + dir, 1, 8);
        else if (g_index == 2) g_eng.guard = (g_eng.guard + dir + 6) % 6;
        else if (g_index == 3) g_confirm = cycle_btn(g_confirm, dir);
        else if (g_index == 4) g_cancel = cycle_btn(g_cancel, dir);
    } else if (g_screen == SCR_REC) {
        if (g_index == 0) g_eng.record_slot = (g_eng.record_slot + dir + TRAIN_SLOTS) % TRAIN_SLOTS;
        else if (g_index == 1) g_eng.slots[g_eng.record_slot].weight = clampi(g_eng.slots[g_eng.record_slot].weight + dir, 0, 20);
    } else if (g_screen == SCR_MOVES) {
        int n = roster_filter_count(g_eng.char_id, -1);
        if (n) g_index = (g_index + dir + n) % n;
    }
}

static void hide_window(void);
static void show_window(void);

static void pick(void) {
    if (g_screen == SCR_ROOT) {
        if (g_index <= 2) { g_rev = g_index; go(SCR_REV); return; }
        if (g_index == 3) { go(SCR_DUMMY); return; }
        if (g_index == 4) { go(SCR_REC); return; }
        if (g_index == 5) {
            game_capture_dir(0);
            snprintf(g_status, sizeof(g_status), "Cierra, mantén atras, luego adelante y abajo.");
            hide_window();
            return;
        }
        if (g_index == 6) { game_save(); snprintf(g_status, sizeof(g_status), "Guardado."); return; }
        hide_window();
        return;
    }
    if (g_screen == SCR_REV) {
        if (g_index == 0) { *action_ptr() = *action_ptr() == ACT_NONE ? ACT_MOVE : ACT_NONE; return; }
        if (g_index == 1) { go(SCR_MOVES); return; }
        if (g_index == 2) { *delay_ptr() = clampi(*delay_ptr() + 1, 0, 60); return; }
        if (g_index == 3) { g_eng.random_span = clampi(g_eng.random_span + 1, 0, 30); return; }
        if (g_index == 4) {
            TrainOut o;
            *action_ptr() = ACT_MOVE;
            hide_window();
            if (!train_fire_move(&g_eng, *move_ptr(), &o))
                snprintf(g_status, sizeof(g_status), "No se pudo inyectar el comando.");
            else
                snprintf(g_status, sizeof(g_status), "Dummy ejecuta %s", roster_move(g_eng.char_id, *move_ptr()) ? roster_move(g_eng.char_id, *move_ptr())->name : "move");
            return;
        }
        if (g_index == 5) { go(SCR_ROOT); return; }
        return;
    }
    if (g_screen == SCR_MOVES) {
        const MoveDef* mv = roster_filter_at(g_eng.char_id, -1, g_index);
        TrainOut o;
        int i, n = roster_move_count(g_eng.char_id);
        for (i = 0; i < n; ++i) if (roster_move(g_eng.char_id, i) == mv) { *move_ptr() = i; break; }
        *action_ptr() = ACT_MOVE;
        hide_window();
        if (train_fire_move(&g_eng, *move_ptr(), &o) && mv)
            snprintf(g_status, sizeof(g_status), "Dummy: %s", mv->name);
        return;
    }
    if (g_screen == SCR_DUMMY) {
        if (g_index == 5) go(SCR_ROOT);
        else change_value(1);
        return;
    }
    if (g_screen == SCR_REC) {
        if (g_index == 0 || g_index == 1) change_value(1);
        else if (g_index == 2) {
            if (g_eng.recording) train_stop_record(&g_eng);
            else train_start_record(&g_eng, g_eng.record_slot);
        } else if (g_index == 3) {
            g_eng.play_slot = g_eng.record_slot;
            g_eng.play_pos = 0;
            snprintf(g_status, sizeof(g_status), "Reproduciendo slot. Cierra el menu.");
            menu_toggle();
        } else if (g_index == 4) {
            g_eng.slots[g_eng.record_slot].count = 0;
            snprintf(g_status, sizeof(g_status), "Slot vaciado.");
        } else if (g_index == 5) go(SCR_ROOT);
    }
}

static void back(void) {
    if (g_screen == SCR_ROOT) hide_window();
    else if (g_screen == SCR_MOVES) go(SCR_REV);
    else go(SCR_ROOT);
}

static void apply(unsigned a) {
    int n = screen_count();
    if (!a || !g_open) return;
    if (a & MA_RANDOM) { hide_window(); return; }
    if (a & MA_BACK) { back(); }
    else if (a & MA_PICK) {
        if ((g_last_held & MENU_START) && !(g_last_held & FIGHT_PICK)) { /* START solo no elige */ }
        else pick();
    }
    else if (a & MA_UP) g_index = (g_index + n - 1) % (n ? n : 1);
    else if (a & MA_DOWN) g_index = (g_index + 1) % (n ? n : 1);
    else if (a & MA_LEFT) change_value(-1);
    else if (a & MA_RIGHT) change_value(1);
    keep_scroll();
    if (g_hwnd) InvalidateRect(g_hwnd, 0, TRUE);
}

static unsigned dir_from_nibble(unsigned n) {
    unsigned a = 0, b = g_eng.dir_back, f = g_eng.dir_forward, d = g_eng.dir_down, u = g_eng.dir_up;
    if (b && f && d) {
        if (!u) u = (unsigned)(0x0Fu & ~(b | f | d));
        if ((n & d) && !(n & u)) a |= MA_DOWN;
        if ((n & u) && !(n & d)) a |= MA_UP;
        if ((n & b) && !(n & f)) a |= MA_LEFT;
        if ((n & f) && !(n & b)) a |= MA_RIGHT;
        return a;
    }
    if (n & 1) a |= MA_UP;
    if (n & 2) a |= MA_DOWN;
    if (n & 4) a |= MA_LEFT;
    if (n & 8) a |= MA_RIGHT;
    return a;
}

static unsigned keyboard_poll(void) {
    static const int keys[6] = { 0x26, 0x28, 0x25, 0x27, VK_PRIOR, VK_NEXT };
    static const unsigned acts[6] = { MA_UP, MA_DOWN, MA_LEFT, MA_RIGHT, MA_LEFT, MA_RIGHT };
    unsigned now = 0, pressed, out = 0;
    int i;
    HWND fg = GetForegroundWindow();
    DWORD pid = 0;
    if (fg) GetWindowThreadProcessId(fg, &pid);
    if (pid == GetCurrentProcessId() || fg == g_hwnd) {
        for (i = 0; i < 6; ++i)
            if (GetAsyncKeyState(keys[i]) & 0x8000) now |= 1u << i;
        if (GetAsyncKeyState(VK_RETURN) & 0x8000) now |= 1u << 6;
        if (GetAsyncKeyState(VK_ESCAPE) & 0x8000) now |= 1u << 7;
    }
    pressed = now & ~g_prev_key;
    g_prev_key = now;
    for (i = 0; i < 6; ++i)
        if (pressed & (1u << i)) out |= acts[i];
    if (pressed & (1u << 6)) out |= MA_PICK;
    if (pressed & (1u << 7)) out |= MA_BACK;
    return out;
}

static void vs_bind(void) {
    HMODULE m;
    unsigned long long now = GetTickCount64();
    if (g_vs_pads) return;
    if (g_vs_probe && now - g_vs_probe < 1000) return;
    g_vs_probe = now;
    m = GetModuleHandleW(L"KofVsSelector.dll");
    if (!m) return;
    g_vs_pads = (int (WINAPI*)(void*, int))GetProcAddress(m, "KofVs_MusicPads");
    g_vs_menu = (void (WINAPI*)(int))GetProcAddress(m, "KofVs_MusicMenu");
    if (g_vs_pads) log_line("mismos mandos que SELECT MUSIC");
}

static int read_pads(VsPad* out, int max, int* source) {
    int i, n = 0;
    vs_bind();
    if (g_vs_pads) {
        n = g_vs_pads(out, max);
        if (n >= 0) { *source = 1; return n; }
        n = 0;
    }
    *source = 0;
    for (i = 0; i < 4 && n < max; ++i) {
        XState st;
        if (!g_xinput || g_xinput((DWORD)i, &st) != 0) continue;
        out[n].id = i;
        out[n].side = -1;
        out[n].pad = st.pad;
        ++n;
    }
    return n;
}

static unsigned held_buttons(const VsPad* pads, int n) {
    unsigned h = 0;
    int i;
    for (i = 0; i < n; ++i) h |= pads[i].pad.buttons;
    return h;
}

static int color_picker_up(void) {
    HWND w = 0;
    while ((w = FindWindowExW(0, w, L"Kof98ColorSelect", 0)) != 0)
        if (IsWindowVisible(w)) return 1;
    return 0;
}

static void hide_window(void) {
    g_open = 0;
    g_request_show = 0;
    if (g_vs_menu) g_vs_menu(0);
    if (g_hwnd) ShowWindow(g_hwnd, SW_HIDE);
    log_line("Menu oculto.");
}

static void show_window(void) {
    RECT rc;
    HWND game;
    g_request_show = 0;
    g_open = 1;
    g_screen = SCR_ROOT;
    g_index = 0;
    g_scroll = 0;
    g_prev_word = 0;
    g_prev_key = 0xFFFFFFFFu;
    memset(&g_pads, 0, sizeof(g_pads));
    g_pads.source = -1;
    if (g_vs_menu) g_vs_menu(1);
    game = find_game_window();
    if (game) SetWindowLongPtrW(g_hwnd, GWLP_HWNDPARENT, (LONG_PTR)game);
    if (game && GetWindowRect(game, &rc)) {
        int x = rc.left + ((rc.right - rc.left) - WIN_W) / 2;
        int y = rc.top + ((rc.bottom - rc.top) - WIN_H) / 2;
        SetWindowPos(g_hwnd, HWND_TOPMOST, x, y, WIN_W, WIN_H, SWP_NOACTIVATE | SWP_SHOWWINDOW);
        log_fmt("Menu visible (musica) %d,%d %dx%d", x, y, rc.right - rc.left, rc.bottom - rc.top);
    } else {
        ShowWindow(g_hwnd, SW_SHOWNOACTIVATE);
        log_line("Menu visible sin ventana de juego.");
    }
    InvalidateRect(g_hwnd, 0, 1);
}

static void poll_pad(void) {
    VsPad pads[8];
    int n, i, source = 0;
    unsigned acts = 0, held, pressed;
    unsigned long long now;
    static unsigned prev_held;
    static int last_n = -1, last_src = -1;
    static int chord_pad;
    now = GetTickCount64();
    n = read_pads(pads, 8, &source);
    held = held_buttons(pads, n);
    pressed = held & ~prev_held;
    prev_held = held;
    g_last_held = held;
    g_pad_alive = n > 0;
    if (n != last_n || source != last_src) {
        log_fmt("pads n=%d source=%s held=%X", n, source ? "selector" : "xinput", held);
        last_n = n;
        last_src = source;
    }
    if (!g_practice) {
        if (g_open) hide_window();
        return;
    }
    if ((held & FIGHT_PICK) == FIGHT_PICK) {
        if (!chord_pad) {
            chord_pad = 1;
            if (g_hwnd) PostMessageW(g_hwnd, WM_MENU_TOGGLE, 0, 0);
            log_line("A+B+C+D pad.");
        }
        return;
    }
    chord_pad = 0;
    if (!g_open) {
        if (color_picker_up()) return;
        return;
    }
    if (g_vs_menu) g_vs_menu(1);
    menu_begin(&g_pads, source);
    for (i = 0; i < n; ++i)
        acts |= menu_feed(&g_pads, pads[i].id, menu_buttons(pads[i].pad.buttons, pads[i].pad.lx, pads[i].pad.ly), now);
    menu_end(&g_pads);
    if (pressed & FIGHT_PICK) acts |= MA_PICK;
    {
        unsigned dirs = acts & (MA_UP | MA_DOWN | MA_LEFT | MA_RIGHT);
        unsigned nibble = dir_from_nibble(g_live_word & 0x0Fu);
        static unsigned prev_nibble;
        if (!dirs) acts |= nibble & ~prev_nibble;
        prev_nibble = nibble;
    }
    acts |= keyboard_poll();
    apply(menu_resolve(acts));
}

static LRESULT CALLBACK wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_PAINT) { paint(hwnd); return 0; }
    if (msg == WM_TIMER) {
        poll_pad();
        return 0;
    }
    if (msg == WM_HOTKEY) {
        if (g_open) hide_window();
        else show_window();
        return 0;
    }
    if (msg == WM_MENU_TOGGLE) {
        static unsigned long long last;
        unsigned long long t = GetTickCount64();
        if (t - last < 400ull) return 0;
        last = t;
        if (g_open) hide_window();
        else show_window();
        log_fmt("menu %s", g_open ? "abierto" : "cerrado");
        return 0;
    }
    if (msg == WM_CLOSE) { hide_window(); return 0; }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static DWORD WINAPI ui_thread(void* unused) {
    WNDCLASSEXW wc;
    HINSTANCE inst;
    wchar_t sys[MAX_PATH];
    HMODULE xmod;
    MSG msg;
    (void)unused;
    inst = GetModuleHandleW(L"Kof98Training.dll");
    if (!inst) inst = GetModuleHandleW(0);
    xmod = GetModuleHandleW(L"xinput1_4.dll");
    if (!xmod) {
        GetSystemDirectoryW(sys, MAX_PATH);
        wcscat_s(sys, MAX_PATH, L"\\xinput1_4.dll");
        xmod = LoadLibraryW(sys);
    }
    if (xmod) g_xinput = (DWORD (WINAPI*)(DWORD, void*))GetProcAddress(xmod, "XInputGetState");
    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = wndproc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(0, (LPCWSTR)(ULONG_PTR)32512);
    wc.lpszClassName = L"Kof98TrainingMenu";
    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        log_fmt("No se registro la clase del menu. error %lu", GetLastError());
        if (g_ready) SetEvent(g_ready);
        return 0;
    }
    g_hwnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, wc.lpszClassName,
        L"ADVANCED TRAINING", WS_POPUP | WS_BORDER, 80, 80, WIN_W, WIN_H, 0, 0, inst, 0);
    if (!g_hwnd) {
        log_fmt("No se creo la ventana del menu. error %lu", GetLastError());
        if (g_ready) SetEvent(g_ready);
        return 0;
    }
    SetTimer(g_hwnd, 1, 15, 0);
    snprintf(g_status, sizeof(g_status), "A+B+C+D abre. Stick mueve. A elige. Close cierra.");
    log_line("Ventana del menu creada como SELECT MUSIC.");
    if (g_ready) SetEvent(g_ready);
    while (GetMessageW(&msg, 0, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return 0;
}

void menu_pump(void) { }

void menu_init(void* unused) {
    (void)unused;
    if (InterlockedCompareExchange(&g_ui_started, 1, 0) != 0) return;
    g_ready = CreateEventW(0, TRUE, FALSE, 0);
    CreateThread(0, 0, ui_thread, 0, 0, &g_ui_tid);
}

void menu_toggle(void) {
    menu_init(0);
    if (!g_hwnd && g_ready) WaitForSingleObject(g_ready, 1500);
    if (g_hwnd) PostMessageW(g_hwnd, WM_MENU_TOGGLE, 0, 0);
}

void menu_request_show(void) {
    menu_init(0);
    g_request_show = 1;
}

int menu_open(void) { return g_open; }

void menu_note(const char* text) {
    if (!text) return;
    snprintf(g_status, sizeof(g_status), "%s", text);
    if (g_hwnd) InvalidateRect(g_hwnd, 0, TRUE);
}

void menu_on_word(unsigned word) {
    unsigned down, acts = 0, n, prev_dir;
    if (!g_open) return;
    if ((word & 0x1E0) == 0x1E0) { g_prev_word = word; return; }
    down = word & ~g_prev_word;
    n = dir_from_nibble(word & 0x0Fu);
    prev_dir = g_prev_word & 0x0Fu;
    g_prev_word = word;
    if (!g_pad_alive) acts |= n & ~dir_from_nibble(prev_dir);
    if (down & 0x1E0u) acts |= MA_PICK;
    apply(acts);
}
