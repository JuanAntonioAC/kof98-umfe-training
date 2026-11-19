#include "padnav.h"

enum { CH_IDLE, CH_PENDING, CH_LATCH };

#define SYS_KEYS (MENU_START | MENU_D)

static int iabs(int v) {
    return v < 0 ? -v : v;
}

unsigned short menu_buttons(unsigned short buttons, short lx, short ly) {
    unsigned short out = (unsigned short)(buttons & (MENU_UP | MENU_DOWN | MENU_LEFT | MENU_RIGHT | MENU_START | MENU_BACK | MENU_D));
    if (iabs(lx) > iabs(ly)) {
        if (lx > MENU_STICK) out |= MENU_RIGHT;
        else if (lx < -MENU_STICK) out |= MENU_LEFT;
    } else {
        if (ly > MENU_STICK) out |= MENU_UP;
        else if (ly < -MENU_STICK) out |= MENU_DOWN;
    }
    return out;
}

void menu_pad_reset(MenuPad* m, unsigned short held) {
    m->prev = held;
    m->chord = (held & SYS_KEYS) ? CH_LATCH : CH_IDLE;
    m->chord_at = 0;
    m->chord_first = 0;
    m->repeat_at = 0;
    m->repeat_dir = 0;
}

unsigned menu_pad_feed(MenuPad* m, unsigned short down, unsigned long long now) {
    unsigned short pressed = (unsigned short)(down & ~m->prev);
    unsigned short vdir = (unsigned short)(down & (MENU_UP | MENU_DOWN));
    unsigned act = 0;
    act |= pressed & (MENU_UP | MENU_DOWN | MENU_LEFT | MENU_RIGHT);
    if (vdir == MENU_UP || vdir == MENU_DOWN) {
        if (pressed & vdir) {
            m->repeat_dir = vdir;
            m->repeat_at = now + MENU_REPEAT_DELAY_MS;
        } else if (m->repeat_dir == vdir && now >= m->repeat_at) {
            act |= vdir;
            m->repeat_at = now + MENU_REPEAT_MS;
        }
    } else {
        m->repeat_dir = 0;
    }
    if (pressed & MENU_BACK) act |= MA_BACK;
    switch (m->chord) {
    case CH_IDLE:
        if ((pressed & SYS_KEYS) == SYS_KEYS) {
            act |= MA_RANDOM;
            m->chord = CH_LATCH;
        } else if (pressed & SYS_KEYS) {
            m->chord = CH_PENDING;
            m->chord_first = (unsigned short)(pressed & SYS_KEYS);
            m->chord_at = now;
        }
        break;
    case CH_PENDING:
        if ((down & SYS_KEYS) == SYS_KEYS || (pressed & SYS_KEYS & ~m->chord_first)) {
            act |= MA_RANDOM;
            m->chord = CH_LATCH;
        } else if (now - m->chord_at >= MENU_CHORD_MS || !(down & m->chord_first)) {
            act |= MA_PICK;
            m->chord = CH_LATCH;
        }
        break;
    default:
        if (!(down & SYS_KEYS)) m->chord = CH_IDLE;
        break;
    }
    if (m->chord == CH_LATCH && !(down & SYS_KEYS)) m->chord = CH_IDLE;
    m->prev = down;
    return act;
}

void menu_begin(MenuInput* in, int source) {
    int i;
    if (in->source != source) {
        for (i = 0; i < MENU_MAX_PADS; ++i) in->pads[i].used = 0;
        in->source = source;
    }
    for (i = 0; i < MENU_MAX_PADS; ++i) in->pads[i].seen = 0;
}

unsigned menu_feed(MenuInput* in, int id, unsigned short down, unsigned long long now) {
    int i, free_slot = -1;
    MenuPad* m;
    for (i = 0; i < MENU_MAX_PADS; ++i) {
        if (in->pads[i].used && in->pads[i].id == id) {
            in->pads[i].seen = 1;
            return menu_pad_feed(&in->pads[i], down, now);
        }
        if (!in->pads[i].used && free_slot < 0) free_slot = i;
    }
    if (free_slot < 0) return 0;
    m = &in->pads[free_slot];
    m->used = 1;
    m->id = id;
    m->seen = 1;
    menu_pad_reset(m, down);
    return 0;
}

void menu_end(MenuInput* in) {
    int i;
    for (i = 0; i < MENU_MAX_PADS; ++i)
        if (in->pads[i].used && !in->pads[i].seen) in->pads[i].used = 0;
}

int menu_any_held(const MenuInput* in) {
    int i;
    for (i = 0; i < MENU_MAX_PADS; ++i)
        if (in->pads[i].used && in->pads[i].prev) return 1;
    return 0;
}

int menu_step(int index, int count, int delta) {
    int i;
    if (count <= 0) return 0;
    if (index < 0 || index >= count) index = 0;
    i = index + delta;
    if (i < 0) i = count - 1;
    if (i >= count) i = 0;
    return i;
}

int menu_page(int index, int count, int rows, int dir) {
    int pages, page, row, i;
    if (count <= 0 || rows <= 0) return 0;
    if (index < 0 || index >= count) index = 0;
    pages = (count + rows - 1) / rows;
    page = index / rows;
    row = index % rows;
    page = ((page + dir) % pages + pages) % pages;
    i = page * rows + row;
    if (i >= count) i = count - 1;
    return i;
}

unsigned menu_resolve(unsigned a) {
    if (a & MA_RANDOM) return MA_RANDOM;
    if (a & MA_PICK) return MA_PICK;
    if (a & MA_BACK) return MA_BACK;
    if (a & (MA_UP | MA_DOWN)) {
        if ((a & (MA_UP | MA_DOWN)) == (MA_UP | MA_DOWN)) return 0;
        return a & (MA_UP | MA_DOWN);
    }
    if ((a & (MA_LEFT | MA_RIGHT)) == (MA_LEFT | MA_RIGHT)) return 0;
    return a & (MA_LEFT | MA_RIGHT);
}
