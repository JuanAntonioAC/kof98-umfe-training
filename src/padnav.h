#pragma once

/* Controller input for the music menu: edge detection, stick-as-dpad,
   Start / D / Start+D chord and Select (back). Pure logic, no Windows calls. */

#define MENU_UP 0x0001u
#define MENU_DOWN 0x0002u
#define MENU_LEFT 0x0004u
#define MENU_RIGHT 0x0008u
#define MENU_START 0x0010u
#define MENU_BACK 0x0020u
#define MENU_D 0x2000u

#define MA_UP 0x01u
#define MA_DOWN 0x02u
#define MA_LEFT 0x04u
#define MA_RIGHT 0x08u
#define MA_PICK 0x10u
#define MA_RANDOM 0x20u
#define MA_BACK 0x40u

#define MENU_CHORD_MS 90
#define MENU_REPEAT_DELAY_MS 420
#define MENU_REPEAT_MS 90
#define MENU_STICK 16000
#define MENU_MAX_PADS 8

typedef struct MenuPad {
    int used;
    int id;
    int seen;
    unsigned short prev;
    int chord;
    unsigned long long chord_at;
    unsigned short chord_first;
    unsigned long long repeat_at;
    unsigned short repeat_dir;
} MenuPad;

typedef struct MenuInput {
    MenuPad pads[MENU_MAX_PADS];
    int source;
} MenuInput;

unsigned short menu_buttons(unsigned short buttons, short lx, short ly);
void menu_pad_reset(MenuPad* m, unsigned short held);
unsigned menu_pad_feed(MenuPad* m, unsigned short down, unsigned long long now);
void menu_begin(MenuInput* in, int source);
unsigned menu_feed(MenuInput* in, int id, unsigned short down, unsigned long long now);
void menu_end(MenuInput* in);
int menu_any_held(const MenuInput* in);
unsigned menu_resolve(unsigned actions);
int menu_step(int index, int count, int delta);
int menu_page(int index, int count, int rows, int dir);
