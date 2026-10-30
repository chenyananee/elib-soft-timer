/* test_expire_delayed.c --- 延迟模式 / pending+delete 竞态 / 混合模式件 */
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "elib_timer.h"

#define MAX_TIMERS 8
static elib_timer_ctx_t ctx;
static elib_timer_instance_t timers[MAX_TIMERS];

static uint32_t cb_count;
static void cb(void *p) { (void)p; cb_count++; }

static void reset(void) {
    memset(&ctx, 0, sizeof(ctx));
    memset(timers, 0, sizeof(timers));
    cb_count = 0u;
    assert(elib_timer_init(&ctx, timers, MAX_TIMERS) == ELIB_TIMER_OK);
}

static uint32_t mk(uint32_t period, uint32_t loops) {
    uint32_t id;
    assert(elib_timer_create(&ctx, period, cb, NULL,
                             ELIB_TIMER_MODE_DELAYED, loops, &id)
           == ELIB_TIMER_OK);
    return id;
}

void test_pending_set_not_fired_in_manager(void) {
    printf("Test: delayed expiry sets pending, does not fire... ");
    reset();
    uint32_t id = mk(1000u, 0u);
    (void)elib_timer_start(&ctx, id);
    (void)elib_timer_manager(&ctx, 1000u);
    assert(cb_count == 0u);
    assert(timers[id].bit_flags.pending_execution == 1u);
    assert(timers[id].bit_flags.state == ELIB_TIMER_STATE_RUNNING);
    assert(timers[id].remaining_ms == 1000u);    /* 无限循环已装填下一轮 */
    printf("PASSED\n");
}

void test_process_fires_once(void) {
    printf("Test: process_pending fires each pending exactly once... ");
    reset();
    uint32_t id = mk(1000u, 0u);
    (void)elib_timer_start(&ctx, id);
    (void)elib_timer_manager(&ctx, 1000u);
    assert(elib_timer_process_pending(&ctx) == ELIB_TIMER_OK);
    assert(cb_count == 1u);
    assert(timers[id].bit_flags.pending_execution == 0u);

    (void)elib_timer_process_pending(&ctx);
    assert(cb_count == 1u);                      /* 幂等 */
    printf("PASSED\n");
}

void test_one_shot_delayed(void) {
    printf("Test: one-shot delayed still fires exactly once... ");
    reset();
    uint32_t id = mk(1000u, 1u);
    (void)elib_timer_start(&ctx, id);
    (void)elib_timer_manager(&ctx, 1000u);
    assert(timers[id].bit_flags.state == ELIB_TIMER_STATE_EXPIRED);
    assert(timers[id].bit_flags.pending_execution == 1u);

    (void)elib_timer_process_pending(&ctx);
    assert(cb_count == 1u);
    assert(timers[id].bit_flags.pending_execution == 0u);
    assert(timers[id].remaining_ms == 0u);
    (void)elib_timer_process_pending(&ctx);
    assert(cb_count == 1u);
    printf("PASSED\n");
}

void test_pending_delete_race(void) {
    printf("Test: pending-then-delete does NOT resurrect callback (A1)... ");
    reset();
    uint32_t id = mk(1000u, 0u);
    (void)elib_timer_start(&ctx, id);
    (void)elib_timer_manager(&ctx, 1000u);
    assert(timers[id].bit_flags.pending_execution == 1u);

    (void)elib_timer_delete(&ctx, id);
    assert(timers[id].bit_flags.pending_execution == 0u);
    (void)elib_timer_process_pending(&ctx);
    assert(cb_count == 0u);                      /* 已删除不得引爆 */
    printf("PASSED\n");
}

void test_recreate_not_stale_pending(void) {
    printf("Test: recreated slot starts without stale pending... ");
    reset();
    uint32_t id = mk(1000u, 0u);
    (void)elib_timer_start(&ctx, id);
    (void)elib_timer_manager(&ctx, 1000u);
    (void)elib_timer_delete(&ctx, id);           /* pending 未处理即删除 */

    uint32_t id2 = mk(1000u, 0u);                /* 复用 0 号槽 */
    assert(id2 == id);
    assert(timers[id2].bit_flags.pending_execution == 0u);
    (void)elib_timer_process_pending(&ctx);
    assert(cb_count == 0u);

    (void)elib_timer_start(&ctx, id2);
    (void)elib_timer_manager(&ctx, 1000u);
    assert(timers[id2].bit_flags.pending_execution == 1u);
    (void)elib_timer_process_pending(&ctx);
    assert(cb_count == 1u);
    printf("PASSED\n");
}

void test_mixed_modes(void) {
    printf("Test: IMMEDIATE and DELAYED coexist... ");
    reset();
    uint32_t imm, del;
    assert(elib_timer_create(&ctx, 500u, cb, NULL,
                             ELIB_TIMER_MODE_IMMEDIATE, 0u, &imm) == ELIB_TIMER_OK);
    assert(elib_timer_create(&ctx, 500u, cb, NULL,
                             ELIB_TIMER_MODE_DELAYED, 0u, &del) == ELIB_TIMER_OK);
    (void)elib_timer_start(&ctx, imm);
    (void)elib_timer_start(&ctx, del);

    (void)elib_timer_manager(&ctx, 500u);
    assert(cb_count == 1u);                      /* 立即者已执行 */
    assert(timers[del].bit_flags.pending_execution == 1u);

    (void)elib_timer_process_pending(&ctx);
    assert(cb_count == 2u);                      /* 延迟者提取执行 */
    printf("PASSED\n");
}

int main(void) {
    printf("=== test_expire_delayed ===\n");
    test_pending_set_not_fired_in_manager();
    test_process_fires_once();
    test_one_shot_delayed();
    test_pending_delete_race();
    test_recreate_not_stale_pending();
    test_mixed_modes();
    printf("=== test_expire_delayed: all PASSED ===\n");
    return 0;
}
