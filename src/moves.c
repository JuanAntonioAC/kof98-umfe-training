#include "moves.h"
#include <string.h>

static const MoveDef k_normals[] = {
    { "Standing A", "A", CAT_NORMAL, 0, 1, 0 },
    { "Standing B", "B", CAT_NORMAL, 0, 1, 0 },
    { "Standing C", "C", CAT_NORMAL, 0, 1, 0 },
    { "Standing D", "D", CAT_NORMAL, 0, 1, 0 },
    { "Crouch A", "2A", CAT_NORMAL, 0, 1, 0 },
    { "Crouch B", "2B", CAT_NORMAL, 0, 1, 0 },
    { "Crouch C", "2C", CAT_NORMAL, 0, 1, 0 },
    { "Crouch D", "2D", CAT_NORMAL, 0, 1, 0 },
    { "Jump A", "8A", CAT_NORMAL, 0, 0, 0 },
    { "Jump B", "8B", CAT_NORMAL, 0, 0, 0 },
    { "Jump C", "8C", CAT_NORMAL, 0, 0, 0 },
    { "Jump D", "8D", CAT_NORMAL, 0, 0, 0 },
    { "Blowback CD", "CD", CAT_NORMAL, 0, 1, 0 },
    { "Throw C", "6C", CAT_COMMAND, 0, 0, 0 },
    { "Throw D", "6D", CAT_COMMAND, 0, 0, 0 },
    { "Evasion A+B", "AB", CAT_DEFENSE, 0, 1, 0 },
    { "Guard back", "4", CAT_DEFENSE, 0, 1, 0 },
    { "Guard crouch", "1", CAT_DEFENSE, 0, 1, 0 },
};

#include "roster.inc"

int roster_count(void) { return ROSTER_N; }

const char* roster_name(int char_id) {
    if (char_id < 0 || char_id >= ROSTER_N) return "Unknown";
    return g_names[char_id];
}

int roster_move_count(int char_id) {
    int extra = 0;
    if (char_id >= 0 && char_id < ROSTER_N) extra = g_sp_n[char_id];
    return (int)(sizeof(k_normals) / sizeof(k_normals[0])) + extra;
}

const MoveDef* roster_move(int char_id, int index) {
    int base = (int)(sizeof(k_normals) / sizeof(k_normals[0]));
    if (index < 0) return 0;
    if (index < base) return &k_normals[index];
    index -= base;
    if (char_id < 0 || char_id >= ROSTER_N) return 0;
    if (index >= g_sp_n[char_id]) return 0;
    return &g_sp[char_id][index];
}

int roster_first_of_cat(int char_id, int cat) {
    int i, n = roster_move_count(char_id);
    for (i = 0; i < n; ++i) {
        const MoveDef* m = roster_move(char_id, i);
        if (m && m->cat == cat) return i;
    }
    return 0;
}

int roster_filter_count(int char_id, int cat) {
    int i, n, c = 0;
    if (char_id < 0 || char_id >= ROSTER_N) return 0;
    n = g_sp_n[char_id];
    for (i = 0; i < n; ++i) {
        const MoveDef* m = &g_sp[char_id][i];
        if (cat < 0 || m->cat == cat) c++;
    }
    return c;
}

const MoveDef* roster_filter_at(int char_id, int cat, int n) {
    int i, seen = 0, total;
    if (char_id < 0 || char_id >= ROSTER_N) return 0;
    total = g_sp_n[char_id];
    for (i = 0; i < total; ++i) {
        const MoveDef* m = &g_sp[char_id][i];
        if (cat >= 0 && m->cat != cat) continue;
        if (seen == n) return m;
        seen++;
    }
    return 0;
}

static unsigned btn_of(char ch, unsigned a, unsigned b, unsigned c, unsigned d) {
    if (ch == 'A' || ch == 'a') return a;
    if (ch == 'B' || ch == 'b') return b;
    if (ch == 'C' || ch == 'c') return c;
    if (ch == 'D' || ch == 'd') return d;
    return 0;
}

static unsigned numpad(int d, unsigned back, unsigned forward, unsigned down, unsigned up) {
    switch (d) {
    case 1: return back | down;
    case 2: return down;
    case 3: return forward | down;
    case 4: return back;
    case 5: return 0;
    case 6: return forward;
    case 7: return back | up;
    case 8: return up;
    case 9: return forward | up;
    default: return 0;
    }
}

static int push(unsigned* out, int n, int max, unsigned word, int frames) {
    int i;
    for (i = 0; i < frames && n < max; ++i) out[n++] = word;
    return n;
}

static int compile_token(const char* s, int len, unsigned back, unsigned forward, unsigned down, unsigned up,
    unsigned a, unsigned b, unsigned c, unsigned d, unsigned* out, int n, int max, int charge) {
    int i, hold = 2;
    unsigned buttons = 0;
    if (len <= 0) return n;
    for (i = 0; i < len; ++i) {
        char ch = s[i];
        if ((ch == 'A' || ch == 'B' || ch == 'C' || ch == 'D' ||
             ch == 'a' || ch == 'b' || ch == 'c' || ch == 'd'))
            buttons |= btn_of(ch, a, b, c, d);
    }
    for (i = 0; i < len; ++i) {
        int digit;
        unsigned dir;
        if (s[i] < '1' || s[i] > '9') continue;
        digit = s[i] - '0';
        dir = numpad(digit, back, forward, down, up);
        if (i == 0 && charge > 0) n = push(out, n, max, dir, charge);
        else n = push(out, n, max, dir, hold);
    }
    if (buttons) {
        unsigned last = n > 0 ? out[n - 1] : 0;
        if (n > 0) out[n - 1] = last | buttons;
        else n = push(out, n, max, buttons, 2);
        n = push(out, n, max, buttons, 1);
    }
    return n;
}

int move_compile(const MoveDef* mv, unsigned back, unsigned forward, unsigned down, unsigned up,
    unsigned a, unsigned b, unsigned c, unsigned d, unsigned* out, int max) {
    const char* p;
    char tok[32];
    int t = 0, n = 0, charge = 0;
    if (!mv || !mv->cmd || !out || max <= 0) return 0;
    if (!back || !forward || !down) return -1;
    if (!up) up = (unsigned)(0x0Fu & ~(back | forward | down));
    p = mv->cmd;
    charge = mv->charge;
    while (*p) {
        if (*p == ' ' || *p == ',') { ++p; continue; }
        if (*p == '>') {
            n = push(out, n, max, 0, 10);
            ++p;
            t = 0;
            continue;
        }
        if (*p == '[') {
            ++p;
            t = 0;
            while (*p && *p != ']' && t < 30) tok[t++] = *p++;
            tok[t] = 0;
            if (*p == ']') ++p;
            if (charge <= 0) charge = 40;
            n = compile_token(tok, t, back, forward, down, up, a, b, c, d, out, n, max, charge);
            t = 0;
            charge = 0;
            continue;
        }
        tok[t++] = *p++;
        if (t >= 31) t = 31;
        if (!*p || *p == '>' || *p == '[') {
            n = compile_token(tok, t, back, forward, down, up, a, b, c, d, out, n, max, 0);
            t = 0;
        }
    }
    if (t) n = compile_token(tok, t, back, forward, down, up, a, b, c, d, out, n, max, 0);
    return n;
}
