#include "bind.h"
#include "log.h"
#include "logic.h"
#include "menu.h"
#include "moves.h"
#include <stdio.h>
#include <string.h>
#include <windows.h>

#define MODE_RVA 0x5490C4u
#define MODE_SIG_RVA 0x2B15ECu
#define MAP_RVA 0x5B240u
#define FRAME_RVA 0x5AE30u
#define MAP_CALL_RVA 0x2B0B92u
#define MAP_STOLEN 15
#define FRAME_STOLEN 20
#define TS_EXPECT 0x62033053u
#define IMAGE_EXPECT 0x1B31000u
#define MODE_PRACTICE 6
#define P1_CHAR_RVA 0x19F1115u
#define P2_CHAR_RVA 0x19F0D6Eu

/* Factory keymap output masks. Same values as KofVsSelector layout.c KEY_LP/LK/HP/HK
   and the table at RVA 0x406910 in this exe (timestamp 0x62033053). */
#define BTN_B 0x00000020u
#define BTN_D 0x00000040u
#define BTN_A 0x00000080u
#define BTN_C 0x00000100u
#define BTN_ALL (BTN_A | BTN_B | BTN_C | BTN_D)

TrainEngine g_eng;
unsigned g_live_word;
extern unsigned g_confirm;
extern unsigned g_cancel;
volatile int g_practice;
int g_state_known;
int g_dummy_index = 2;
int g_menu_index = 1;

extern unsigned long long tramp_map;
extern unsigned long long tramp_frame;
extern void hook_map(void);
extern void hook_frame(void);

static unsigned char* g_base;
static HMODULE g_module;
static unsigned char g_map_saved[MAP_STOLEN];
static unsigned char g_frame_saved[FRAME_STOLEN];
static int g_hooked;
static unsigned g_frame;
static unsigned g_applied_frame[9];
static unsigned g_last_out[9];
static int g_last_replace[9];
static int g_seen_index[9];
static int g_force_left;
static unsigned g_force_word;
static int g_cap_arm;
static int g_cap_which;
static wchar_t g_dir[MAX_PATH];
static unsigned g_rng = 1;
static int g_chord_was;
static int g_chord_latched;
static int g_btn_logged[9];
static int was_practice;

static const unsigned char k_sig[10] = { 0xC7, 0x05, 0xCE, 0x7A, 0x29, 0x00, 0x02, 0x00, 0x00, 0x00 };
static const unsigned char k_map[MAP_STOLEN] = {
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x20
};
static const unsigned char k_frame[FRAME_STOLEN] = {
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18,
    0x57, 0x48, 0x83, 0xEC, 0x20
};
/* call 0x5B240 inside the loop that stores indices 1..8 at object+0x44. */
static const unsigned char k_map_call[5] = { 0xE8, 0xA9, 0xA6, 0xDA, 0xFF };

static unsigned rng_cb(void* ctx) {
    unsigned* s = (unsigned*)ctx;
    unsigned x = *s ? *s : 1u;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    *s = x;
    return x;
}

static int same(const void* a, const void* b, int n) {
    return memcmp(a, b, (size_t)n) == 0;
}

static int exe_ok(void) {
    IMAGE_DOS_HEADER* dos;
    IMAGE_NT_HEADERS64* nt;
    g_base = (unsigned char*)GetModuleHandleW(0);
    if (!g_base) return 0;
    dos = (IMAGE_DOS_HEADER*)g_base;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return 0;
    nt = (IMAGE_NT_HEADERS64*)(g_base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return 0;
    if (nt->FileHeader.TimeDateStamp != TS_EXPECT) {
        log_fmt("exe timestamp %08X, se esperaba %08X. No se engancha.", nt->FileHeader.TimeDateStamp, TS_EXPECT);
        return 0;
    }
    if (nt->OptionalHeader.SizeOfImage != IMAGE_EXPECT) {
        log_fmt("SizeOfImage %X, se esperaba %X. No se engancha.", nt->OptionalHeader.SizeOfImage, IMAGE_EXPECT);
        return 0;
    }
    if (!same(g_base + MODE_SIG_RVA, k_sig, 10)) {
        log_line("La firma del modo no coincide. No se engancha.");
        return 0;
    }
    if (!same(g_base + FRAME_RVA, k_frame, FRAME_STOLEN)) {
        log_line("El prologo del reloj de frames no coincide. No se engancha.");
        return 0;
    }
    return 1;
}

static void* make_tramp(void* site, int stolen) {
    unsigned char* t = (unsigned char*)VirtualAlloc(0, (SIZE_T)stolen + 16, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!t) return 0;
    memcpy(t, site, (size_t)stolen);
    t[stolen + 0] = 0xFF;
    t[stolen + 1] = 0x25;
    *(unsigned*)(t + stolen + 2) = 0;
    *(void**)(t + stolen + 6) = (unsigned char*)site + stolen;
    return t;
}

static int patch_site(void* site, void* hook, int stolen, unsigned long long* tramp_slot) {
    DWORD old = 0;
    unsigned char j[32];
    void* tramp = make_tramp(site, stolen);
    if (!tramp) return 0;
    *tramp_slot = (unsigned long long)tramp;
    memset(j, 0x90, sizeof(j));
    j[0] = 0xFF;
    j[1] = 0x25;
    *(unsigned*)(j + 2) = 0;
    *(void**)(j + 6) = hook;
    if (!VirtualProtect(site, (SIZE_T)stolen, PAGE_EXECUTE_READWRITE, &old)) return 0;
    memcpy(site, j, (size_t)stolen);
    VirtualProtect(site, (SIZE_T)stolen, old, &old);
    FlushInstructionCache(GetCurrentProcess(), site, (SIZE_T)stolen);
    return 1;
}

static void* alloc_near(unsigned char* site) {
    int i;
    for (i = 0; i < 48; ++i) {
        unsigned char* hint = site + 0x1900000 + (size_t)i * 0x10000;
        void* p = VirtualAlloc(hint, 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
        if (p) return p;
    }
    return 0;
}

/* Runs instead of the 5-byte call. Calls the current map function (KoM's jump
   included) and then lets the training code replace eax. */
static int hook_map_call(void) {
    unsigned char* cave;
    unsigned char* site = g_base + MAP_CALL_RVA;
    unsigned char* code;
    int rel, d_idx, d_load, d_reload;
    DWORD old = 0;
    unsigned long long fn;
    if (!same(site, k_map_call, 5)) {
        log_line("La llamada de indices 1..8 no coincide. No se engancha la entrada.");
        return 0;
    }
    cave = (unsigned char*)alloc_near(site);
    if (!cave) {
        log_line("No hubo memoria cercana para el enganche de entrada.");
        return 0;
    }
    code = cave;
    code[0] = 0x89; code[1] = 0x0D;
    code[6] = 0x48; code[7] = 0x83; code[8] = 0xEC; code[9] = 0x28;
    code[10] = 0x48; code[11] = 0x8B; code[12] = 0x05;
    code[17] = 0xFF; code[18] = 0xD0;
    code[19] = 0x8B; code[20] = 0x0D;
    code[25] = 0x89; code[26] = 0xC2;
    code[27] = 0x48; code[28] = 0xB8;
    code[37] = 0xFF; code[38] = 0xD0;
    code[39] = 0x48; code[40] = 0x83; code[41] = 0xC4; code[42] = 0x28;
    code[43] = 0xC3;
    d_idx = 44 - 6;
    d_load = 48 - 17;
    d_reload = 44 - 25;
    memcpy(code + 2, &d_idx, 4);
    memcpy(code + 13, &d_load, 4);
    memcpy(code + 21, &d_reload, 4);
    *(unsigned*)(cave + 44) = 0;
    *(unsigned long long*)(cave + 48) = (unsigned long long)(g_base + MAP_RVA);
    fn = (unsigned long long)(void*)game_on_mapped_input;
    memcpy(code + 29, &fn, 8);
    rel = (int)(cave - (site + 5));
    if (site + 5 + rel != cave) {
        log_line("El enganche de entrada no cabe en un call relativo.");
        return 0;
    }
    if (!VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &old)) return 0;
    site[0] = 0xE8;
    memcpy(site + 1, &rel, 4);
    VirtualProtect(site, 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), site, 5);
    log_line("Entrada enganchada despues del salto ya instalado por otro modulo. Ese salto no se modifico.");
    return 1;
}

static int mode_now(void) {
    return *(volatile int*)(g_base + MODE_RVA);
}

static void apply_default_dirs(void) {
    g_eng.dir_up = 0x1u;
    g_eng.dir_down = 0x2u;
    if (g_dummy_index == 1) {
        g_eng.dir_back = 0x4u;
        g_eng.dir_forward = 0x8u;
    } else {
        g_eng.dir_back = 0x8u;
        g_eng.dir_forward = 0x4u;
    }
}

static int read_side_char(int player) {
    unsigned char id;
    if (!g_base || (player != 0 && player != 1)) return -1;
    id = g_base[(player == 0 ? P1_CHAR_RVA : P2_CHAR_RVA) + 4u];
    if (id > 61) return -1;
    return (int)id;
}

static void sync_dummy_char(void) {
    int player = g_dummy_index == 1 ? 0 : 1;
    int id = read_side_char(player);
    if (id < 0) return;
    if (id != g_eng.char_id) {
        g_eng.char_id = id;
        g_eng.wakeup_move = roster_first_of_cat(id, CAT_SPECIAL);
        g_eng.block_move = g_eng.wakeup_move;
        g_eng.hit_move = g_eng.wakeup_move;
        log_fmt("Dummy %s (id %d) lado %s", roster_name(id), id, player ? "P2" : "P1");
    }
}

static void ini_path(wchar_t* out) {
    wcscpy_s(out, MAX_PATH, g_dir);
    wcscat_s(out, MAX_PATH, L"kof98training.ini");
}

void game_save(void) {
    wchar_t path[MAX_PATH];
    FILE* f;
    int i;
    ini_path(path);
    f = _wfopen(path, L"w");
    if (!f) { log_line("No se pudo guardar kof98training.ini"); return; }
    fprintf(f, "dummy_index=%d\nmenu_index=%d\nchar_id=%d\n", g_dummy_index, g_menu_index, g_eng.char_id);
    fprintf(f, "wakeup_action=%d\nwakeup_delay=%d\nwakeup_slot=%d\nwakeup_move=%d\n", g_eng.wakeup_action, g_eng.wakeup_delay, g_eng.wakeup_slot, g_eng.wakeup_move);
    fprintf(f, "block_action=%d\nblock_delay=%d\nblock_slot=%d\nblock_move=%d\n", g_eng.block_action, g_eng.block_delay, g_eng.block_slot, g_eng.block_move);
    fprintf(f, "hit_action=%d\nhit_delay=%d\nhit_slot=%d\nhit_move=%d\n", g_eng.hit_action, g_eng.hit_delay, g_eng.hit_slot, g_eng.hit_move);
    fprintf(f, "guard=%d\nrandom_span=%d\n", g_eng.guard, g_eng.random_span);
    fprintf(f, "dir_back=%u\ndir_forward=%u\ndir_down=%u\ndir_up=%u\n", g_eng.dir_back, g_eng.dir_forward, g_eng.dir_down, g_eng.dir_up);
    fprintf(f, "confirm=%u\ncancel=%u\n", g_confirm, g_cancel);
    for (i = 0; i < TRAIN_SLOTS; ++i) {
        int n, k;
        fprintf(f, "slot%d_weight=%d\nslot%d_count=%d\n", i, g_eng.slots[i].weight, i, g_eng.slots[i].count);
        fprintf(f, "slot%d_frames=", i);
        for (n = 0; n < g_eng.slots[i].count; ++n) {
            fprintf(f, "%s%X", n ? "," : "", g_eng.slots[i].frames[n]);
        }
        fprintf(f, "\n");
        (void)k;
    }
    fclose(f);
    log_line("Configuracion guardada.");
    menu_note("Guardado en kof98training.ini");
}

static void load_frames(TrainSlot* s, const char* list) {
    const char* p = list;
    s->count = 0;
    while (*p && s->count < TRAIN_MAX_FRAMES) {
        unsigned v = 0;
        if (*p == ',') ++p;
        if (!*p) break;
        sscanf(p, "%x", &v);
        s->frames[s->count++] = v;
        while (*p && *p != ',') ++p;
    }
}

static void game_load(void) {
    wchar_t path[MAX_PATH];
    FILE* f;
    char line[4096];
    ini_path(path);
    f = _wfopen(path, L"r");
    if (!f) return;
    while (fgets(line, sizeof(line), f)) {
        int slot = -1, val = 0;
        char frames[4000];
        if (sscanf(line, "dummy_index=%d", &val) == 1) g_dummy_index = val;
        else if (sscanf(line, "menu_index=%d", &val) == 1) g_menu_index = val;
        else if (sscanf(line, "char_id=%d", &val) == 1) g_eng.char_id = val;
        else if (sscanf(line, "wakeup_action=%d", &val) == 1) g_eng.wakeup_action = val;
        else if (sscanf(line, "wakeup_delay=%d", &val) == 1) g_eng.wakeup_delay = val;
        else if (sscanf(line, "wakeup_slot=%d", &val) == 1) g_eng.wakeup_slot = val;
        else if (sscanf(line, "wakeup_move=%d", &val) == 1) g_eng.wakeup_move = val;
        else if (sscanf(line, "block_action=%d", &val) == 1) g_eng.block_action = val;
        else if (sscanf(line, "block_delay=%d", &val) == 1) g_eng.block_delay = val;
        else if (sscanf(line, "block_slot=%d", &val) == 1) g_eng.block_slot = val;
        else if (sscanf(line, "block_move=%d", &val) == 1) g_eng.block_move = val;
        else if (sscanf(line, "hit_action=%d", &val) == 1) g_eng.hit_action = val;
        else if (sscanf(line, "hit_delay=%d", &val) == 1) g_eng.hit_delay = val;
        else if (sscanf(line, "hit_slot=%d", &val) == 1) g_eng.hit_slot = val;
        else if (sscanf(line, "hit_move=%d", &val) == 1) g_eng.hit_move = val;
        else if (sscanf(line, "guard=%d", &val) == 1) g_eng.guard = val;
        else if (sscanf(line, "random_span=%d", &val) == 1) g_eng.random_span = val;
        else if (sscanf(line, "dir_back=%d", &val) == 1) g_eng.dir_back = (unsigned)val;
        else if (sscanf(line, "dir_forward=%d", &val) == 1) g_eng.dir_forward = (unsigned)val;
        else if (sscanf(line, "dir_down=%d", &val) == 1) g_eng.dir_down = (unsigned)val;
        else if (sscanf(line, "dir_up=%d", &val) == 1) g_eng.dir_up = (unsigned)val;
        else if (sscanf(line, "confirm=%d", &val) == 1) g_confirm = (unsigned)val;
        else if (sscanf(line, "cancel=%d", &val) == 1) g_cancel = (unsigned)val;
        else if (sscanf(line, "slot%d_weight=%d", &slot, &val) == 2 && slot >= 0 && slot < TRAIN_SLOTS)
            g_eng.slots[slot].weight = val;
        frames[0] = 0;
        if (sscanf(line, "slot%d_frames=%3999s", &slot, frames) == 2 && slot >= 0 && slot < TRAIN_SLOTS)
            load_frames(&g_eng.slots[slot], frames);
    }
    fclose(f);
    log_line("Configuracion cargada.");
}

void game_force(unsigned word) {
    g_force_word = word;
    g_force_left = 2;
    log_fmt("Prueba de boton, mascara %X, 2 frames, solo practica.", word);
}

void game_capture_dir(int which) {
    (void)which;
    g_cap_arm = 1;
    g_cap_which = 0;
    menu_note("Cierra el menu, mantén atras y espera el log. Luego adelante y abajo.");
    log_line("Calibracion de direccion armada. El nibble bajo del dummy se guarda tal cual.");
}

static void take_dir(unsigned buttons) {
    unsigned n = buttons & 0x0Fu;
    const char* name;
    if (!g_cap_arm || !n) return;
    if (g_cap_which == 0) { g_eng.dir_back = n; name = "atras"; }
    else if (g_cap_which == 1) { g_eng.dir_forward = n; name = "adelante"; }
    else { g_eng.dir_down = n; name = "abajo"; g_cap_arm = 0; }
    log_fmt("Direccion %s = nibble %X (medido, no supuesto).", name, n);
    g_cap_which++;
    if (g_cap_which > 2) g_cap_arm = 0;
}

void game_on_frame(void) {
    int mode;
    g_frame++;
    if (!g_chord_latched) g_chord_was = 0;
    g_chord_latched = 0;
    menu_pump();
    mode = mode_now();
    g_practice = mode == MODE_PRACTICE;
    if (g_practice) {
        if (!g_eng.dir_back || !g_eng.dir_forward || !g_eng.dir_down) apply_default_dirs();
        sync_dummy_char();
    }
    if (g_practice && !was_practice)
        log_line("PRACTICE: A+B+C+D abre y cierra el menu.");
    was_practice = g_practice;
    if ((g_frame % 300u) == 1u) log_fmt("frame %u modo %d (%s)", g_frame, mode, g_practice ? "practice" : "otro");
}

unsigned game_on_mapped_input(unsigned index, unsigned buttons) {
    TrainView v;
    TrainOut o;
    int chord;
    if (index < 9 && !g_seen_index[index]) {
        g_seen_index[index] = 1;
        log_fmt("Indice de entrada %u visto. dummy=%d menu=%d", index, g_dummy_index, g_menu_index);
    }
    if (index == (unsigned)g_menu_index) g_live_word = buttons;
    if (index < 9 && !g_btn_logged[index] && buttons) {
        g_btn_logged[index] = 1;
        log_fmt("Indice %u palabra %X botones %X dir %X", index, buttons, buttons & BTN_ALL, buttons & 0xFu);
    }
    {
        static unsigned acc[9];
        unsigned attacks = buttons & BTN_ALL;
        if (index < 9) {
            if (!attacks) acc[index] = 0;
            else acc[index] |= attacks;
            chord = acc[index] == BTN_ALL;
            if (chord) acc[index] = 0;
        } else chord = attacks == BTN_ALL;
    }
    if (chord && g_practice) {
        g_chord_latched = 1;
        if (!g_chord_was) {
            if (index >= 1 && index <= 8) {
                g_menu_index = (int)index;
                g_dummy_index = g_menu_index == 1 ? 2 : 1;
            }
            menu_toggle();
            g_chord_was = 1;
            log_fmt("A+B+C+D en indice %u. menu %s dummy=%d", index, menu_open() ? "abierto" : "cerrado", g_dummy_index);
        }
    }
    if (index == (unsigned)g_menu_index && menu_open()) menu_on_word(buttons);
    if (menu_open() && g_eng.motion_pos >= g_eng.motion_len && g_eng.play_slot < 0 && g_eng.synth_left <= 0)
        return 0;
    if (!g_practice) {
        g_eng.recording = 0;
        g_eng.play_slot = -1;
        g_eng.synth_left = 0;
        g_eng.armed_trig = TRIG_NONE;
        return buttons;
    }
    if (index != (unsigned)g_dummy_index) return buttons;
    take_dir(buttons);
    if (g_force_left > 0) {
        g_force_left--;
        return g_force_word;
    }
    /* Stun, knockdown and actionable are not identified in this exe yet.
       known stays 0 so reversals cannot invent a frame. Recording still stores the live word. */
    memset(&v, 0, sizeof(v));
    v.blockstun = -1;
    v.hitstun = -1;
    v.known = g_state_known;
    if (index < 9 && g_applied_frame[index] == g_frame)
        return g_last_replace[index] ? g_last_out[index] : buttons;
    if (index < 9) g_applied_frame[index] = g_frame;
    train_tick(&g_eng, &v, buttons, &o);
    if (o.unavailable) log_line("Accion pedida sin dato verificado. No se inyecta.");
    if (o.event) log_fmt("Evento %d en frame %u", o.event, g_frame);
    if (index < 9) {
        g_last_replace[index] = o.suppress;
        g_last_out[index] = o.word;
    }
    if (!o.suppress) return buttons;
    return o.word;
}

static void unpatch(void* site, const unsigned char* saved, int n) {
    DWORD old = 0;
    if (!VirtualProtect(site, (SIZE_T)n, PAGE_EXECUTE_READWRITE, &old)) return;
    memcpy(site, saved, (size_t)n);
    VirtualProtect(site, (SIZE_T)n, old, &old);
    FlushInstructionCache(GetCurrentProcess(), site, (SIZE_T)n);
}

static DWORD WINAPI install_thread(void* unused) {
    wchar_t logpath[MAX_PATH];
    (void)unused;
    Sleep(400);
    if (!GetModuleFileNameW(g_module ? g_module : (HMODULE)GetModuleHandleW(L"Kof98Training.dll"), g_dir, MAX_PATH)) return 0;
    {
        wchar_t* slash = wcsrchr(g_dir, L'\\');
        if (slash) slash[1] = 0;
    }
    wcscpy_s(logpath, MAX_PATH, g_dir);
    wcscat_s(logpath, MAX_PATH, L"kof98training.log");
    log_open(logpath);
    log_line("--- Kof98Training ---");
    train_init(&g_eng);
    g_eng.btn_a = BTN_A;
    g_eng.btn_b = BTN_B;
    g_eng.btn_c = BTN_C;
    g_eng.btn_d = BTN_D;
    g_eng.rng = rng_cb;
    g_eng.rng_ctx = &g_rng;
    g_eng.wakeup_action = ACT_NONE;
    g_eng.block_action = ACT_NONE;
    game_load();
    if (!g_eng.dir_back || !g_eng.dir_forward || !g_eng.dir_down) apply_default_dirs();
    if (!exe_ok()) {
        log_line("El proceso no es el UMFE x64 esperado. El juego sigue igual.");
        return 0;
    }
    memcpy(g_map_saved, g_base + MAP_RVA, MAP_STOLEN);
    memcpy(g_frame_saved, g_base + FRAME_RVA, FRAME_STOLEN);
    if (!patch_site(g_base + FRAME_RVA, (void*)hook_frame, FRAME_STOLEN, &tramp_frame)) {
        log_line("No se pudo enganchar el frame.");
        return 0;
    }
    if (same(g_base + MAP_RVA, k_map, MAP_STOLEN)) {
        if (!patch_site(g_base + MAP_RVA, (void*)hook_map, MAP_STOLEN, &tramp_map)) {
            unpatch(g_base + FRAME_RVA, g_frame_saved, FRAME_STOLEN);
            log_line("No se pudo enganchar el mapeo. Se retiro el enganche de frame.");
            return 0;
        }
        log_line("Entrada enganchada en el prologo original.");
    } else if (!hook_map_call()) {
        unpatch(g_base + FRAME_RVA, g_frame_saved, FRAME_STOLEN);
        log_fmt("Prologo de mapeo en memoria: %02X %02X %02X %02X %02X. No se pisa.",
            g_base[MAP_RVA], g_base[MAP_RVA + 1], g_base[MAP_RVA + 2], g_base[MAP_RVA + 3], g_base[MAP_RVA + 4]);
        return 0;
    }
    g_hooked = 1;
    g_state_known = 0;
    log_line("Enganche instalado. Solo actua con modo 6 (PRACTICE). Reversals en espera: falta el estado del personaje.");
    menu_init(0);
    log_line("En PRACTICE: A+B+C+D abre y cierra el menu. Stick mueve. A elige.");
    return 0;
}

void game_set_module(void* module) {
    g_module = (HMODULE)module;
}

void game_install(void) {
    if (g_hooked) return;
    CreateThread(0, 0, install_thread, 0, 0, 0);
}
