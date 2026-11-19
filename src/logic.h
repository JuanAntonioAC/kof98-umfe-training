#ifndef KOF98_TRAIN_LOGIC_H
#define KOF98_TRAIN_LOGIC_H

/* Game-independent training rules. Button masks are not applied here. */

enum {
    ACT_NONE = 0,
    ACT_A,
    ACT_B,
    ACT_C,
    ACT_D,
    ACT_AB,       /* A+B: forward roll / dodge, no direction required */
    ACT_THROW,    /* needs a horizontal direction */
    ACT_BLOCK,    /* needs back, or down+back when crouched */
    ACT_SLOT,
    ACT_RANDOM,
    ACT_MOVE,
    ACT_COUNT
};

enum {
    GUARD_OFF = 0,
    GUARD_ALL,
    GUARD_HIGH,
    GUARD_LOW,
    GUARD_AFTER_FIRST,
    GUARD_RANDOM
};

enum {
    TRIG_NONE = 0,
    TRIG_WAKEUP,
    TRIG_BLOCK,
    TRIG_HIT
};

#define TRAIN_SLOTS 8
#define TRAIN_MAX_FRAMES 720
#define TRAIN_MOTION_MAX 96

typedef struct TrainSlot {
    char name[24];
    int count;
    int weight; /* 0 excludes the slot from random */
    unsigned frames[TRAIN_MAX_FRAMES];
} TrainSlot;

typedef struct TrainView {
    int known;       /* 0: character state is not available */
    int knocked;     /* 1 while the dummy is down */
    int actionable;  /* 1 on a frame a new action is legal */
    int blockstun;   /* frames left, or -1 if unknown */
    int hitstun;     /* frames left, or -1 if unknown */
} TrainView;

typedef struct TrainOut {
    int suppress;          /* 1: replace the dummy word this frame */
    unsigned word;         /* full mapped word when suppress is 1 */
    int event;             /* TRIG_* that fired, else 0 */
    int unavailable;       /* 1: a requested action had no data */
    char note[80];
} TrainOut;

typedef struct TrainEngine {
    int wakeup_action;
    int wakeup_slot;
    int wakeup_delay;
    int wakeup_move;
    int block_action;
    int block_slot;
    int block_delay;
    int block_move;
    int hit_action;
    int hit_slot;
    int hit_delay;
    int hit_move;
    int char_id;
    int guard;
    int guard_crouch;
    unsigned dir_back;
    unsigned dir_forward;
    unsigned dir_down;
    unsigned dir_up;
    unsigned btn_a, btn_b, btn_c, btn_d;
    int random_span; /* extra frames added as rng % (span+1); 0 = exact delay */
    TrainSlot slots[TRAIN_SLOTS];
    int record_slot;
    int recording;
    int play_slot; /* >=0 recording, -1 idle */
    int play_pos;
    int synth_left;
    unsigned synth_word;
    int motion_len;
    int motion_pos;
    unsigned motion[TRAIN_MOTION_MAX];
    int armed_trig;
    int armed_action;
    int armed_slot;
    int armed_move;
    int wait;
    int was_knocked;
    int prev_block;
    int prev_hit;
    int saw_block;
    int wake_latched;
    int block_latched;
    int hit_latched;
    unsigned (*rng)(void*);
    void* rng_ctx;
} TrainEngine;

void train_init(TrainEngine* e);
void train_tick(TrainEngine* e, const TrainView* view, unsigned live_word, TrainOut* out);
int train_start_record(TrainEngine* e, int slot);
void train_stop_record(TrainEngine* e);
int train_action_needs_direction(int action);
const char* train_action_name(int action);
int train_load_move(TrainEngine* e, int move_index);
int train_fire_move(TrainEngine* e, int move_index, TrainOut* out);

#endif
