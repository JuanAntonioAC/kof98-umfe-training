#ifndef KOF98_TRAIN_MOVES_H
#define KOF98_TRAIN_MOVES_H

enum {
    CAT_NORMAL = 0,
    CAT_COMMAND,
    CAT_SPECIAL,
    CAT_SUPER,
    CAT_DEFENSE
};

enum { MOVE_MAX_FRAMES = 96 };

typedef struct MoveDef {
    const char* name;
    const char* cmd;
    int cat;
    int meter;     /* 0 none, 1 DM, 2 SDM */
    int wakeup_ok; /* 1: usable as a typical reversal */
    int charge;    /* frames to hold the bracketed dir, 0 = from cmd */
} MoveDef;

int roster_count(void);
const char* roster_name(int char_id);
int roster_move_count(int char_id);
const MoveDef* roster_move(int char_id, int index);
int roster_first_of_cat(int char_id, int cat);
int roster_filter_count(int char_id, int cat);
const MoveDef* roster_filter_at(int char_id, int cat, int n);

int move_compile(const MoveDef* mv, unsigned back, unsigned forward, unsigned down, unsigned up,
    unsigned a, unsigned b, unsigned c, unsigned d, unsigned* out, int max);

#endif
