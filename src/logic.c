#include "logic.h"
#include "moves.h"
#include <string.h>

static unsigned default_rng(void* ctx) {
    unsigned* s = (unsigned*)ctx;
    unsigned x;
    if (!s) return 0;
    x = *s;
    if (!x) x = 0xA5A5u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *s = x;
    return x;
}

static unsigned rng(TrainEngine* e) {
    static unsigned seed = 1;
    if (e->rng) return e->rng(e->rng_ctx);
    return default_rng(&seed);
}

void train_init(TrainEngine* e) {
    int i;
    memset(e, 0, sizeof(*e));
    e->prev_block = -1;
    e->prev_hit = -1;
    e->wakeup_action = ACT_NONE;
    e->block_action = ACT_NONE;
    e->hit_action = ACT_NONE;
    e->wakeup_move = 0;
    e->block_move = 0;
    e->hit_move = 0;
    e->play_slot = -1;
    e->motion_len = 0;
    e->motion_pos = 0;
    e->record_slot = 0;
    for (i = 0; i < TRAIN_SLOTS; ++i) {
        e->slots[i].name[0] = '1' + (char)i;
        e->slots[i].name[1] = 0;
        e->slots[i].weight = 1;
    }
}

const char* train_action_name(int action) {
    switch (action) {
    case ACT_NONE: return "nada";
    case ACT_A: return "A";
    case ACT_B: return "B";
    case ACT_C: return "C";
    case ACT_D: return "D";
    case ACT_AB: return "A+B";
    case ACT_THROW: return "agarre";
    case ACT_BLOCK: return "bloqueo";
    case ACT_SLOT: return "grabacion";
    case ACT_RANDOM: return "azar";
    case ACT_MOVE: return "movimiento";
    default: return "desconocida";
    }
}

int train_action_needs_direction(int action) {
    return action == ACT_THROW || action == ACT_BLOCK;
}

static unsigned button_word(const TrainEngine* e, int action) {
    switch (action) {
    case ACT_A: return e->btn_a;
    case ACT_B: return e->btn_b;
    case ACT_C: return e->btn_c;
    case ACT_D: return e->btn_d;
    case ACT_AB: return e->btn_a | e->btn_b;
    case ACT_THROW: return e->dir_forward | e->btn_c;
    case ACT_BLOCK: return e->dir_back | (e->guard_crouch ? e->dir_down : 0);
    default: return 0;
    }
}

static int pick_weighted(TrainEngine* e) {
    int i, total = 0, mark, acc = 0;
    for (i = 0; i < TRAIN_SLOTS; ++i) {
        if (e->slots[i].weight > 0 && e->slots[i].count > 0) total += e->slots[i].weight;
    }
    if (total <= 0) return -1;
    mark = (int)(rng(e) % (unsigned)total);
    for (i = 0; i < TRAIN_SLOTS; ++i) {
        if (e->slots[i].weight <= 0 || e->slots[i].count <= 0) continue;
        acc += e->slots[i].weight;
        if (mark < acc) return i;
    }
    return -1;
}

static void arm(TrainEngine* e, int trig, int action, int slot, int delay) {
    int extra = 0;
    if (e->random_span > 0) extra = (int)(rng(e) % (unsigned)(e->random_span + 1));
    e->armed_trig = trig;
    e->armed_action = action;
    e->armed_slot = slot;
    if (trig == TRIG_WAKEUP) e->armed_move = e->wakeup_move;
    else if (trig == TRIG_BLOCK) e->armed_move = e->block_move;
    else if (trig == TRIG_HIT) e->armed_move = e->hit_move;
    e->wait = delay + extra;
    if (e->wait < 0) e->wait = 0;
    e->play_slot = -1;
    e->play_pos = 0;
}

static int rising_clear(int prev, int now) {
    return prev > 0 && now == 0;
}

static void consider(TrainEngine* e, const TrainView* v) {
    if (!v->known) return;
    if (e->was_knocked && !v->knocked) e->wake_latched = 1;
    if (v->blockstun >= 0 && rising_clear(e->prev_block, v->blockstun)) e->block_latched = 1;
    if (v->hitstun >= 0 && rising_clear(e->prev_hit, v->hitstun) && !v->knocked) e->hit_latched = 1;
    if (e->armed_trig || e->play_slot >= 0 || e->synth_left > 0 || !v->actionable) return;
    if (e->wake_latched && e->wakeup_action != ACT_NONE) {
        e->wake_latched = 0;
        arm(e, TRIG_WAKEUP, e->wakeup_action, e->wakeup_action == ACT_SLOT ? e->record_slot : e->wakeup_slot, e->wakeup_delay);
    } else if (e->block_latched && e->block_action != ACT_NONE) {
        e->block_latched = 0;
        arm(e, TRIG_BLOCK, e->block_action, e->block_action == ACT_SLOT ? e->record_slot : e->block_slot, e->block_delay);
    } else if (e->hit_latched && e->hit_action != ACT_NONE) {
        e->hit_latched = 0;
        arm(e, TRIG_HIT, e->hit_action, e->hit_action == ACT_SLOT ? e->record_slot : e->hit_slot, e->hit_delay);
    }
}

static int begin_playback(TrainEngine* e, int action, int slot, TrainOut* out) {
    int chosen = slot;
    unsigned word;
    if (action == ACT_NONE) return 0;
    if (action == ACT_RANDOM) chosen = pick_weighted(e);
    if (action == ACT_SLOT || action == ACT_RANDOM) {
        if (chosen < 0 || chosen >= TRAIN_SLOTS || e->slots[chosen].count <= 0) {
            out->unavailable = 1;
            return 0;
        }
        e->play_slot = chosen;
        e->play_pos = 0;
        return 1;
    }
    if ((action == ACT_BLOCK && !e->dir_back) || (action == ACT_THROW && !e->dir_forward)) {
        out->unavailable = 1;
        return 0;
    }
    if (action == ACT_MOVE) {
        if (train_load_move(e, e->armed_move) <= 0) {
            out->unavailable = 1;
            return 0;
        }
        e->motion_pos = 0;
        return 1;
    }
    word = button_word(e, action);
    if (!word) {
        out->unavailable = 1;
        return 0;
    }
    e->synth_word = word;
    e->synth_left = 2;
    return 1;
}

int train_load_move(TrainEngine* e, int move_index) {
    const MoveDef* mv;
    int n;
    if (!e) return 0;
    mv = roster_move(e->char_id, move_index);
    if (!mv) return 0;
    n = move_compile(mv, e->dir_back, e->dir_forward, e->dir_down, e->dir_up,
        e->btn_a, e->btn_b, e->btn_c, e->btn_d, e->motion, TRAIN_MOTION_MAX);
    if (n <= 0) return 0;
    e->motion_len = n;
    e->motion_pos = 0;
    return n;
}

int train_fire_move(TrainEngine* e, int move_index, TrainOut* out) {
    if (out) memset(out, 0, sizeof(*out));
    e->armed_move = move_index;
    if (train_load_move(e, move_index) <= 0) {
        if (out) out->unavailable = 1;
        return 0;
    }
    e->play_slot = -1;
    e->synth_left = 0;
    e->motion_pos = 0;
    return 1;
}

void train_tick(TrainEngine* e, const TrainView* view, unsigned live_word, TrainOut* out) {
    memset(out, 0, sizeof(*out));
    if (e->recording) {
        TrainSlot* s = &e->slots[e->record_slot];
        if (s->count < TRAIN_MAX_FRAMES) s->frames[s->count++] = live_word;
        else e->recording = 0;
    }
    if (!view->known) {
        e->was_knocked = 0;
        e->prev_block = -1;
        e->prev_hit = -1;
        e->armed_trig = TRIG_NONE;
        e->wake_latched = e->block_latched = e->hit_latched = 0;
        if (e->play_slot >= 0) {
            TrainSlot* s = &e->slots[e->play_slot];
            if (e->play_pos < s->count) {
                out->suppress = 1;
                out->word = s->frames[e->play_pos++];
            } else e->play_slot = -1;
        } else if (e->synth_left > 0) {
            out->suppress = 1;
            out->word = e->synth_word;
            e->synth_left--;
        } else if (e->motion_pos < e->motion_len) {
            out->suppress = 1;
            out->word = e->motion[e->motion_pos++];
        }
        return;
    }
    consider(e, view);
    e->was_knocked = view->knocked;
    if (view->blockstun >= 0) e->prev_block = view->blockstun;
    if (view->hitstun >= 0) e->prev_hit = view->hitstun;
    if (view->blockstun > 0) e->saw_block = 1;

    if (e->armed_trig && view->actionable) {
        if (e->wait > 0) {
            e->wait--;
        } else {
            int trig = e->armed_trig;
            int action = e->armed_action;
            int slot = e->armed_slot;
            e->armed_trig = TRIG_NONE;
            if (begin_playback(e, action, slot, out)) out->event = trig;
        }
    }

    if (e->play_slot >= 0) {
        TrainSlot* s = &e->slots[e->play_slot];
        if (e->play_pos >= s->count) e->play_slot = -1;
        else {
            out->suppress = 1;
            out->word = s->frames[e->play_pos++];
            return;
        }
    }
    if (e->synth_left > 0) {
        out->suppress = 1;
        out->word = e->synth_word;
        e->synth_left--;
        return;
    }
    if (e->motion_pos < e->motion_len) {
        out->suppress = 1;
        out->word = e->motion[e->motion_pos++];
        return;
    }

    if (e->guard != GUARD_OFF && e->dir_back && (view->actionable || view->blockstun > 0)) {
        int hold = 0;
        if (e->guard == GUARD_ALL || e->guard == GUARD_HIGH) hold = 1;
        if (e->guard == GUARD_LOW) hold = 1;
        if (e->guard == GUARD_AFTER_FIRST) hold = e->saw_block;
        if (e->guard == GUARD_RANDOM) hold = (rng(e) & 1u) != 0;
        if (hold) {
            out->suppress = 1;
            out->word = e->dir_back | ((e->guard == GUARD_LOW || e->guard_crouch) ? e->dir_down : 0);
        }
    }
}

int train_start_record(TrainEngine* e, int slot) {
    if (slot < 0 || slot >= TRAIN_SLOTS) return 0;
    e->record_slot = slot;
    e->slots[slot].count = 0;
    e->recording = 1;
    return 1;
}

void train_stop_record(TrainEngine* e) {
    e->recording = 0;
}
