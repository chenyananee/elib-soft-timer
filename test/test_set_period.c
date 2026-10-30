/* test_set_period.c --- 周期修改 / 比例缩放 / 三态差异件 */
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

static uint32_t mk(uint32_t period, elib_timer_exec_mode_t mode, uint32_t loops) {
    uint32_t id;
    assert(elib_timer_create(&ctx, period, cb, NULL, mode, loops, &id)
           == ELIB_TIMER_OK);
    return id;
}

void test_set_period_running_rescale(void) {
    printf("Test: set_period rescales RUNNING proportionally... ");
    reset();
    uint32_t id = mk(1000u, ELIB_TIMER_MODE_IMMEDIATE, 0u);
    (void)elib_timer_start(&ctx, id);
    (void)elib_timer_manager(&ctx, 500u);
    assert(timers[id].remaining_ms == 500u);

    assert(elib_timer_set_period(&ctx, id, 2000u) == ELIB_TIMER_OK);
    assert(timers[id].period_ms == 2000u);
    assert(timers[id].remaining_ms == 1000u); /* 500 * 2000 / 1000 */

    assert(elib_timer_manager(&ctx, 1000u) == ELIB_TIMER_OK);
    assert(cb_count == 1u);
    assert(timers[id].remaining_ms == 2000u);
    printf("PASSED\n");
}

void test_set_period_paused_rescale(void) {
    printf("Test: set_period rescales PAUSED proportionally... ");
    reset();
    uint32_t id = mk(1000u, ELIB_TIMER_MODE_IMMEDIATE, 0u);
    (void)elib_timer_start(&ctx, id);
    (void)elib_timer_manager(&ctx, 500u);
    (void)elib_timer_pause(&ctx, id);

    assert(elib_timer_set_period(&ctx, id, 2000u) == ELIB_TIMER_OK);
    assert(timers[id].remaining_ms == 1000u); /* 500 * 2000 / 1000，冻结值同步缩放 */

    (void)elib_timer_resume(&ctx, id);
    assert(elib_timer_manager(&ctx, 1000u) == ELIB_TIMER_OK);
    assert(cb_count == 1u);
    printf("PASSED\n");
}

void test_set_period_stopped_untouched(void) {
    printf("Test: set_period leaves STOPPED remaining unscaled... ");
    reset();
    uint32_t id = mk(2000u, ELIB_TIMER_MODE_IMMEDIATE, 0u);
    (void)elib_timer_start(&ctx, id);
    (void)elib_timer_manager(&ctx, 500u);
    (void)elib_timer_stop(&ctx, id);           /* 全重置：remaining == 2000 */

    assert(elib_timer_set_period(&ctx, id, 400u) == ELIB_TIMER_OK);
    assert(timers[id].period_ms == 400u);
    assert(timers[id].remaining_ms == 2000u);  /* 不缩放 */

    (void)elib_timer_start(&ctx, id);
    assert(timers[id].remaining_ms == 400u);   /* start 重置为新周期 */
    printf("PASSED\n");
}

void test_set_period_expired_untouched(void) {
    printf("Test: set_period leaves EXPIRED remaining 0... ");
    reset();
    uint32_t id = mk(1000u, ELIB_TIMER_MODE_IMMEDIATE, 1u);
    (void)elib_timer_start(&ctx, id);
    (void)elib_timer_manager(&ctx, 1000u);
    assert(timers[id].bit_flags.state == ELIB_TIMER_STATE_EXPIRED);
    assert(timers[id].remaining_ms == 0u);

    (void)elib_timer_set_period(&ctx, id, 500u);
    assert(timers[id].remaining_ms == 0u);
    printf("PASSED\n");
}

void test_set_period_clamp_to_1(void) {
    printf("Test: scaled remaining clamps to at least 1 ms... ");
    reset();
    uint32_t id = mk(1000u, ELIB_TIMER_MODE_IMMEDIATE, 0u);
    (void)elib_timer_start(&ctx, id);
    (void)elib_timer_manager(&ctx, 1u);        /* remaining == 999 */
    assert(elib_timer_set_period(&ctx, id, 1u) == ELIB_TIMER_OK);
    assert(timers[id].remaining_ms == 1u);     /* 999*1/1000 = 0 -> 钳 1 */

    assert(elib_timer_manager(&ctx, 1u) == ELIB_TIMER_OK);
    assert(cb_count == 1u);
    printf("PASSED\n");
}

void test_set_period_invalid(void) {
    printf("Test: set_period rejects zero / unused / bad id... ");
    reset();
    uint32_t id = mk(1000u, ELIB_TIMER_MODE_IMMEDIATE, 0u);

    assert(elib_timer_set_period(&ctx, id, 0u) == ELIB_TIMER_ERR_INVALID_PARAM);
    (void)elib_timer_delete(&ctx, id);
    assert(elib_timer_set_period(&ctx, id, 500u) == ELIB_TIMER_ERR_NOT_FOUND);
    assert(elib_timer_set_period(&ctx, 99u, 500u) == ELIB_TIMER_ERR_NOT_FOUND);
    printf("PASSED\n");
}

int main(void) {
    printf("=== test_set_period ===\n");
    test_set_period_running_rescale();
    test_set_period_paused_rescale();
    test_set_period_stopped_untouched();
    test_set_period_expired_untouched();
    test_set_period_clamp_to_1();
    test_set_period_invalid();
    printf("=== test_set_period: all PASSED ===\n");
    return 0;
}
