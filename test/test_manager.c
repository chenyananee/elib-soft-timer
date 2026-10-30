/* test_manager.c --- manager 驱动参数 / 多定时器批量 tick 件 */
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "elib_timer.h"

#define MAX_TIMERS 4
static elib_timer_ctx_t ctx;
static elib_timer_instance_t timers[MAX_TIMERS];

static uint32_t per[MAX_TIMERS];
static void cb_per(void *p) { ((uint32_t *)p)[0]++; }

static void reset(void) {
    memset(&ctx, 0, sizeof(ctx));
    memset(timers, 0, sizeof(timers));
    memset(per, 0, sizeof(per));
    assert(elib_timer_init(&ctx, timers, MAX_TIMERS) == ELIB_TIMER_OK);
}

static void mki(uint32_t period, uint32_t loops, uint32_t slot) {
    uint32_t id;
    assert(elib_timer_create(&ctx, period, cb_per, &per[slot],
                             ELIB_TIMER_MODE_IMMEDIATE, loops, &id)
           == ELIB_TIMER_OK);
    assert(id == slot);
}

void test_zero_elapsed_noop(void) {
    printf("Test: manager(0) is a clean no-op... ");
    reset();
    uint32_t id;
    assert(elib_timer_create(&ctx, 1000u, cb_per, &per[0],
                             ELIB_TIMER_MODE_IMMEDIATE, 0u, &id) == ELIB_TIMER_OK);
    (void)elib_timer_start(&ctx, id);
    assert(elib_timer_manager(&ctx, 0u) == ELIB_TIMER_OK);
    assert(per[0] == 0u);
    assert(timers[id].remaining_ms == 1000u);
    printf("PASSED\n");
}

void test_manager_invalid(void) {
    printf("Test: manager rejects null ctx / uninitialized... ");
    assert(elib_timer_manager(NULL, 1u) == ELIB_TIMER_ERR_INVALID_PARAM);

    reset();
    elib_timer_deinit(&ctx);
    assert(elib_timer_manager(&ctx, 1u) == ELIB_TIMER_ERR_NOT_INITIALIZED);
    printf("PASSED\n");
}

void test_equal_elapsed_fires(void) {
    printf("Test: remaining == ms_elapsed is exactly an expiry... ");
    reset();
    uint32_t id;
    assert(elib_timer_create(&ctx, 1000u, cb_per, &per[0],
                             ELIB_TIMER_MODE_IMMEDIATE, 0u, &id) == ELIB_TIMER_OK);
    (void)elib_timer_start(&ctx, id);
    (void)elib_timer_manager(&ctx, 1000u);
    assert(per[0] == 1u);
    assert(timers[id].remaining_ms == 1000u);
    printf("PASSED\n");
}

void test_partial_accumulation(void) {
    printf("Test: partial ticks accumulate to a single expiry... ");
    reset();
    uint32_t id;
    assert(elib_timer_create(&ctx, 1000u, cb_per, &per[0],
                             ELIB_TIMER_MODE_IMMEDIATE, 0u, &id) == ELIB_TIMER_OK);
    (void)elib_timer_start(&ctx, id);
    (void)elib_timer_manager(&ctx, 400u);
    (void)elib_timer_manager(&ctx, 400u);
    assert(per[0] == 0u);
    assert(timers[id].remaining_ms == 200u);
    (void)elib_timer_manager(&ctx, 200u);
    assert(per[0] == 1u);
    printf("PASSED\n");
}

void test_only_running_ticked(void) {
    printf("Test: manager ticks RUNNING state only... ");
    reset();
    uint32_t r, p, d, x;
    assert(elib_timer_create(&ctx, 1000u, cb_per, &per[0],
                             ELIB_TIMER_MODE_IMMEDIATE, 0u, &r) == ELIB_TIMER_OK);
    assert(elib_timer_create(&ctx, 1000u, cb_per, &per[1],
                             ELIB_TIMER_MODE_IMMEDIATE, 0u, &p) == ELIB_TIMER_OK);
    assert(elib_timer_create(&ctx, 1000u, cb_per, &per[2],
                             ELIB_TIMER_MODE_DELAYED, 0u, &d) == ELIB_TIMER_OK);
    assert(elib_timer_create(&ctx, 1000u, cb_per, &per[3],
                             ELIB_TIMER_MODE_IMMEDIATE, 1u, &x) == ELIB_TIMER_OK);
    (void)elib_timer_start(&ctx, r);
    (void)elib_timer_start(&ctx, p);
    (void)elib_timer_start(&ctx, d);

    /* 首遍：r/p 正常复装，d 置 pending；随后 p 暂停 */
    (void)elib_timer_manager(&ctx, 1000u);
    assert(per[0] == 1u && per[1] == 1u && per[2] == 0u);
    (void)elib_timer_process_pending(&ctx);
    assert(per[2] == 1u);
    (void)elib_timer_pause(&ctx, p);

    /* x 一次性启动并跑到 EXPIRED */
    (void)elib_timer_start(&ctx, x);
    (void)elib_timer_manager(&ctx, 1000u);
    assert(timers[x].bit_flags.state == ELIB_TIMER_STATE_EXPIRED);
    assert(per[3] == 1u);

    /* 第二遍：仅 r 驱动；PAUSED 冻结；EXPIRED 不动 */
    (void)elib_timer_manager(&ctx, 1000u);
    assert(per[0] == 3u);                  /* RUNNING 被驱动（首遍 + 中间遍 + 本遍） */
    assert(per[1] == 1u);                  /* PAUSED 不驱动（计数不变） */
    assert(per[3] == 1u);                  /* EXPIRED 不驱动 */
    uint32_t rem;
    assert(elib_timer_get_remaining(&ctx, p, &rem) == ELIB_TIMER_OK);
    assert(rem == 1000u);                  /* PAUSED 冻结在 1000 */
    printf("PASSED\n");
}

void test_all_slots_ticked(void) {
    printf("Test: all RUNNING slots tick in one manager pass... ");
    reset();
    for (uint32_t i = 0u; i < MAX_TIMERS; i++) {
        mki(1000u, 0u, i);
        (void)elib_timer_start(&ctx, i);
    }
    (void)elib_timer_manager(&ctx, 1000u);
    for (uint32_t i = 0u; i < MAX_TIMERS; i++) {
        assert(per[i] == 1u);
    }
    printf("PASSED\n");
}

int main(void) {
    printf("=== test_manager ===\n");
    test_zero_elapsed_noop();
    test_manager_invalid();
    test_equal_elapsed_fires();
    test_partial_accumulation();
    test_only_running_ticked();
    test_all_slots_ticked();
    printf("=== test_manager: all PASSED ===\n");
    return 0;
}
