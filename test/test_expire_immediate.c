/* test_expire_immediate.c --- 立即模式到期 / 重入契约 / 丢拍语义件 */
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "elib_timer.h"

#define MAX_TIMERS 8
static elib_timer_ctx_t ctx;
static elib_timer_instance_t timers[MAX_TIMERS];

static uint32_t cb_count;
static uint32_t self_id;      /* 重入回调使用的目标 ID */

static void cb(void *p) { (void)p; cb_count++; }

static void cb_self_delete(void *p) {
    (void)p;
    cb_count++;
    assert(elib_timer_delete(&ctx, self_id) == ELIB_TIMER_OK);
}

static void cb_self_stop(void *p) {
    (void)p;
    cb_count++;
    assert(elib_timer_stop(&ctx, self_id) == ELIB_TIMER_OK);
}

static void cb_self_set_period(void *p) {
    (void)p;
    cb_count++;
    assert(elib_timer_set_period(&ctx, self_id, 300u) == ELIB_TIMER_OK);
}

static void reset(void) {
    memset(&ctx, 0, sizeof(ctx));
    memset(timers, 0, sizeof(timers));
    cb_count = 0u;
    self_id = 0u;
    assert(elib_timer_init(&ctx, timers, MAX_TIMERS) == ELIB_TIMER_OK);
}

static uint32_t mk(uint32_t period, uint32_t loops) {
    uint32_t id;
    assert(elib_timer_create(&ctx, period, cb, NULL,
                             ELIB_TIMER_MODE_IMMEDIATE, loops, &id)
           == ELIB_TIMER_OK);
    return id;
}

void test_expire_edge_and_repeat(void) {
    printf("Test: immediate fires exactly at zero, repeats infinitely... ");
    reset();
    uint32_t id = mk(1000u, 0u);
    (void)elib_timer_start(&ctx, id);
    (void)elib_timer_manager(&ctx, 999u);
    assert(cb_count == 0u);
    assert(timers[id].remaining_ms == 1u);

    assert(elib_timer_manager(&ctx, 1u) == ELIB_TIMER_OK);
    assert(cb_count == 1u);
    assert(timers[id].remaining_ms == 1000u);

    (void)elib_timer_manager(&ctx, 1000u);
    (void)elib_timer_manager(&ctx, 1000u);
    assert(cb_count == 3u);
    printf("PASSED\n");
}

void test_one_shot_and_loop_n(void) {
    printf("Test: one-shot expires once; loop N fires exactly N times... ");
    reset();
    uint32_t id = mk(1000u, 1u);
    (void)elib_timer_start(&ctx, id);
    (void)elib_timer_manager(&ctx, 1000u);
    assert(cb_count == 1u);
    assert(timers[id].bit_flags.state == ELIB_TIMER_STATE_EXPIRED);
    assert(timers[id].remaining_ms == 0u);
    (void)elib_timer_manager(&ctx, 1000u);
    assert(cb_count == 1u);

    (void)elib_timer_delete(&ctx, id);
    id = mk(1000u, 3u);
    (void)elib_timer_start(&ctx, id);
    for (uint32_t i = 0u; i < 4u; i++) {
        (void)elib_timer_manager(&ctx, 1000u);
    }
    assert(cb_count == 4u);                      /* 1 + 3 */
    assert(timers[id].bit_flags.state == ELIB_TIMER_STATE_EXPIRED);
    printf("PASSED\n");
}

void test_lost_tick_single_fire(void) {
    printf("Test: ms_elapsed >> period fires once per call (lost-tick)... ");
    reset();
    uint32_t id = mk(500u, 0u);
    (void)elib_timer_start(&ctx, id);
    assert(elib_timer_manager(&ctx, 2000u) == ELIB_TIMER_OK);
    assert(cb_count == 1u);
    assert(timers[id].remaining_ms == 500u);     /* 越界部分丢弃 */
    (void)elib_timer_manager(&ctx, 500u);
    assert(cb_count == 2u);
    printf("PASSED\n");
}

void test_callback_self_delete(void) {
    printf("Test: immediate callback may delete self... ");
    reset();
    self_id = 0u;
    uint32_t id;
    assert(elib_timer_create(&ctx, 1000u, cb_self_delete, NULL,
                             ELIB_TIMER_MODE_IMMEDIATE, 0u, &id) == ELIB_TIMER_OK);
    (void)elib_timer_start(&ctx, id);
    assert(elib_timer_manager(&ctx, 1000u) == ELIB_TIMER_OK);
    assert(cb_count == 1u);
    assert(timers[id].bit_flags.state == ELIB_TIMER_STATE_UNUSED);
    assert(ctx.active_count == 0u);              /* 回调后未装填、未复活 */
    printf("PASSED\n");
}

void test_callback_self_stop(void) {
    printf("Test: immediate callback may stop self... ");
    reset();
    self_id = 0u;
    uint32_t id;
    assert(elib_timer_create(&ctx, 1000u, cb_self_stop, NULL,
                             ELIB_TIMER_MODE_IMMEDIATE, 0u, &id) == ELIB_TIMER_OK);
    (void)elib_timer_start(&ctx, id);
    assert(elib_timer_manager(&ctx, 1000u) == ELIB_TIMER_OK);
    assert(cb_count == 1u);
    assert(timers[id].bit_flags.state == ELIB_TIMER_STATE_STOPPED);
    assert(timers[id].remaining_ms == 1000u);    /* stop 全重置，未再装填 */
    assert(ctx.active_count == 1u);

    (void)elib_timer_manager(&ctx, 1000u);       /* STOPPED 不再 tick */
    assert(cb_count == 1u);

    (void)elib_timer_start(&ctx, id);
    assert(elib_timer_manager(&ctx, 1000u) == ELIB_TIMER_OK);
    assert(cb_count == 2u);
    printf("PASSED\n");
}

void test_callback_self_set_period(void) {
    printf("Test: immediate callback may set_period self... ");
    reset();
    self_id = 0u;
    uint32_t id;
    assert(elib_timer_create(&ctx, 1000u, cb_self_set_period, NULL,
                             ELIB_TIMER_MODE_IMMEDIATE, 0u, &id) == ELIB_TIMER_OK);
    (void)elib_timer_start(&ctx, id);
    assert(elib_timer_manager(&ctx, 1000u) == ELIB_TIMER_OK);
    assert(cb_count == 1u);
    assert(timers[id].period_ms == 300u);
    assert(timers[id].remaining_ms == 300u);     /* 回调后复查 RUNNING 再装填 */

    (void)elib_timer_manager(&ctx, 300u);
    assert(cb_count == 2u);
    printf("PASSED\n");
}

int main(void) {
    printf("=== test_expire_immediate ===\n");
    test_expire_edge_and_repeat();
    test_one_shot_and_loop_n();
    test_lost_tick_single_fire();
    test_callback_self_delete();
    test_callback_self_stop();
    test_callback_self_set_period();
    printf("=== test_expire_immediate: all PASSED ===\n");
    return 0;
}
