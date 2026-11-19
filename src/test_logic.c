#include "logic.h"
#include "moves.h"
#include <stdio.h>
#include <string.h>

static int g_fail;

static void expect(int cond, const char* msg) {
    if (!cond) {
        printf("FALLO: %s\n", msg);
        g_fail++;
    }
}

static unsigned rng_seq;
static unsigned rng(void* ctx) {
    (void)ctx;
    return rng_seq++;
}

static void setup(TrainEngine* e) {
    train_init(e);
    e->rng = rng;
    e->rng_ctx = &rng_seq;
    e->btn_a = 0x80;
    e->btn_b = 0x20;
    e->btn_c = 0x100;
    e->btn_d = 0x40;
    e->dir_back = 0x1;
    e->dir_forward = 0x2;
    e->dir_down = 0x8;
    e->wakeup_action = ACT_A;
    e->block_action = ACT_A;
    e->hit_action = ACT_B;
}

static TrainView view(int known, int knocked, int actionable, int block, int hit) {
    TrainView v;
    v.known = known;
    v.knocked = knocked;
    v.actionable = actionable;
    v.blockstun = block;
    v.hitstun = hit;
    return v;
}

static void test_wakeup_delays(void) {
    int delay, trial;
    for (delay = 0; delay <= 10; ++delay) {
        if (delay > 3 && delay != 5 && delay != 10) continue;
        for (trial = 0; trial < 20; ++trial) {
            TrainEngine e;
            TrainOut o;
            TrainView down, up;
            int frame, fired = -1;
            setup(&e);
            e.wakeup_delay = delay;
            down = view(1, 1, 0, -1, 10);
            up = view(1, 0, 1, -1, 0);
            train_tick(&e, &down, 0, &o);
            train_tick(&e, &up, 0, &o);
            if (delay == 0) {
                expect(o.event == TRIG_WAKEUP && o.suppress && o.word == 0x80, "wakeup delay 0");
                continue;
            }
            expect(o.event == 0, "wakeup no dispara antes del retraso");
            for (frame = 1; frame <= delay; ++frame) {
                train_tick(&e, &up, 0, &o);
                if (o.event == TRIG_WAKEUP) fired = frame;
            }
            expect(fired == delay, "wakeup en el frame de retraso");
        }
    }
}

static void test_block_not_during_stun(void) {
    TrainEngine e;
    TrainOut o;
    TrainView v;
    setup(&e);
    v = view(1, 0, 0, 4, -1);
    train_tick(&e, &v, 0, &o);
    expect(!o.event, "no responde dentro del blockstun");
    v = view(1, 0, 0, 1, -1);
    train_tick(&e, &v, 0, &o);
    v = view(1, 0, 1, 0, -1);
    train_tick(&e, &v, 0, &o);
    expect(o.event == TRIG_BLOCK && o.word == 0x80, "A en el primer frame legal tras el bloqueo");
}

static void test_unknown_state_is_silent(void) {
    TrainEngine e;
    TrainOut o;
    TrainView v = view(0, 1, 1, 0, 0);
    setup(&e);
    train_tick(&e, &v, 0x80, &o);
    expect(!o.suppress && !o.event, "sin estado no hay inyeccion");
}

static void test_recording_and_weights(void) {
    TrainEngine e;
    TrainOut o;
    TrainView down, up;
    int i, counts[2];
    setup(&e);
    e.slots[0].weight = 1;
    e.slots[1].weight = 3;
    e.slots[0].count = 1;
    e.slots[0].frames[0] = 0x11;
    e.slots[1].count = 1;
    e.slots[1].frames[0] = 0x22;
    e.wakeup_action = ACT_RANDOM;
    counts[0] = counts[1] = 0;
    rng_seq = 0;
    for (i = 0; i < 40; ++i) {
        train_init(&e);
        e.rng = rng;
        e.rng_ctx = &rng_seq;
        e.btn_a = 0x80;
        e.slots[0].weight = 1;
        e.slots[1].weight = 3;
        e.slots[0].count = 1;
        e.slots[0].frames[0] = 0x11;
        e.slots[1].count = 1;
        e.slots[1].frames[0] = 0x22;
        e.wakeup_action = ACT_RANDOM;
        down = view(1, 1, 0, -1, -1);
        up = view(1, 0, 1, -1, -1);
        train_tick(&e, &down, 0, &o);
        train_tick(&e, &up, 0, &o);
        if (o.word == 0x11) counts[0]++;
        if (o.word == 0x22) counts[1]++;
    }
    expect(counts[0] + counts[1] == 40, "el azar siempre elige un slot con peso");
    expect(counts[1] > counts[0], "el peso 3 sale mas que el peso 1");
}

static void test_playback_keeps_frames(void) {
    TrainEngine e;
    TrainOut o;
    TrainView v = view(0, 0, 0, -1, -1);
    int i;
    setup(&e);
    train_start_record(&e, 2);
    for (i = 0; i < 6; ++i) train_tick(&e, &v, 0x100u + (unsigned)i, &o);
    train_stop_record(&e);
    expect(e.slots[2].count == 6, "la grabacion guarda un frame por tick");
    e.play_slot = 2;
    e.play_pos = 0;
    for (i = 0; i < 6; ++i) {
        train_tick(&e, &v, 0, &o);
        expect(o.suppress && o.word == 0x100u + (unsigned)i, "la reproduccion conserva el orden");
    }
    train_tick(&e, &v, 0, &o);
    expect(!o.suppress && e.play_slot < 0, "la grabacion termina y no se repite sola");
}

static void test_hit_waits_for_recovery(void) {
    TrainEngine e;
    TrainOut o;
    TrainView stun, free_v, down, up;
    setup(&e);
    stun = view(1, 0, 0, -1, 3);
    train_tick(&e, &stun, 0, &o);
    expect(!o.event, "no ataca dentro del hitstun");
    free_v = view(1, 0, 1, -1, 0);
    train_tick(&e, &free_v, 0, &o);
    expect(o.event == TRIG_HIT && o.word == 0x20, "B en el primer frame tras el hitstun");
    setup(&e);
    down = view(1, 1, 0, -1, 0);
    up = view(1, 0, 1, -1, 0);
    train_tick(&e, &stun, 0, &o);
    train_tick(&e, &down, 0, &o);
    train_tick(&e, &up, 0, &o);
    expect(o.event == TRIG_WAKEUP, "un derribo no se trata como hit reversal");
}

static void test_direction_refused(void) {
    TrainEngine e;
    TrainOut o;
    TrainView down, up;
    setup(&e);
    e.dir_forward = 0;
    e.wakeup_action = ACT_THROW;
    down = view(1, 1, 0, -1, -1);
    up = view(1, 0, 1, -1, -1);
    train_tick(&e, &down, 0, &o);
    train_tick(&e, &up, 0, &o);
    expect(o.unavailable && !o.suppress, "agarre sin direccion calibrada no se inyecta");
}

static int find_move(int char_id, const char* name) {
    int i, n = roster_move_count(char_id);
    for (i = 0; i < n; ++i) {
        const MoveDef* m = roster_move(char_id, i);
        if (m && strstr(m->name, name)) return i;
    }
    return -1;
}

static void test_roster_and_motion(void) {
    TrainEngine e;
    TrainOut o;
    unsigned frames[96];
    const MoveDef* kyo;
    const MoveDef* iori;
    int n, i, kyo_dp, iori_yami, takuma_koou, has_kyo_only;
    expect(roster_count() >= 51, "roster UMFE completo");
    expect(strcmp(roster_name(0), "Kyo") == 0, "id 0 es Kyo");
    expect(strstr(roster_name(27), "Iori") != 0, "id 27 es Iori");
    expect(strstr(roster_name(31), "Takuma") != 0, "id 31 es Takuma");
    kyo_dp = find_move(0, "Oniyaki A");
    iori_yami = find_move(27, "Yami Barai A");
    takuma_koou = find_move(31, "Ko'ou Ken A");
    expect(kyo_dp >= 0 && iori_yami >= 0 && takuma_koou >= 0, "especiales de Kyo Iori Takuma");
    has_kyo_only = find_move(27, "Aragami") >= 0;
    expect(!has_kyo_only, "Iori no lista Aragami de Kyo");
    kyo = roster_move(0, kyo_dp);
    n = move_compile(kyo, 0x1, 0x2, 0x8, 0x4, 0x80, 0x20, 0x100, 0x40, frames, 96);
    expect(n > 4, "Oniyaki A genera una secuencia");
    expect((frames[n - 2] & 0x80) != 0 || (frames[n - 1] & 0x80) != 0, "Oniyaki A termina en boton A");
    expect((frames[0] & 0x2) != 0, "DP empieza hacia adelante");
    iori = roster_move(27, iori_yami);
    n = move_compile(iori, 0x1, 0x2, 0x8, 0x4, 0x80, 0x20, 0x100, 0x40, frames, 96);
    expect(n > 3, "Yami Barai genera QCF");
    setup(&e);
    e.char_id = 0;
    e.wakeup_action = ACT_MOVE;
    e.wakeup_move = kyo_dp;
    e.wakeup_delay = 0;
    {
        TrainView down = view(1, 1, 0, -1, -1);
        TrainView up = view(1, 0, 1, -1, -1);
        train_tick(&e, &down, 0, &o);
        train_tick(&e, &up, 0, &o);
        expect(o.event == TRIG_WAKEUP && o.suppress, "Kyo Oniyaki al levantarse con estado conocido");
        expect((o.word & 0x2) != 0, "primer frame del DP es adelante");
        for (i = 0; i < 20 && e.motion_pos < e.motion_len; ++i)
            train_tick(&e, &up, 0, &o);
        expect(e.motion_pos >= e.motion_len, "la secuencia del movimiento termina");
    }
    setup(&e);
    e.char_id = 0;
    expect(train_fire_move(&e, kyo_dp, &o) == 1, "test now carga Oniyaki sin estado");
    {
        TrainView idle = view(0, 0, 0, -1, -1);
        train_tick(&e, &idle, 0, &o);
    }
    expect(o.suppress && (o.word & 0x2), "test now inyecta el comando aunque known=0");
    n = move_compile(kyo, 0, 0, 0, 0, 0x80, 0x20, 0x100, 0x40, frames, 96);
    expect(n < 0, "sin calibrar direcciones no se inventa el comando");
}

int main(void) {
    test_wakeup_delays();
    test_block_not_during_stun();
    test_unknown_state_is_silent();
    test_recording_and_weights();
    test_playback_keeps_frames();
    test_hit_waits_for_recovery();
    test_direction_refused();
    test_roster_and_motion();
    if (g_fail) {
        printf("%d pruebas fallaron\n", g_fail);
        return 1;
    }
    printf("logica ok\n");
    return 0;
}
