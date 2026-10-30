/* test_get_remaining.c --- 剩余查询四态 / 参数校验件 */
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
                             ELIB_TIMER_MODE_IMMEDIATE, loops, &id)
           == ELIB_TIMER_OK);
    return id;
}

void test_remaining_states(void) {
    printf("Test: get_remaining across states... ");
    reset();
    uint32_t r = 0u;

    /* RUNNING：动态值 */
    uint32_t run = mk(1000u, 0u);
    (void)elib_timer_start(&ctx, run);
    (void)elib_timer_manager(&ctx, 300u);
    assert(elib_timer_get_remaining(&ctx, run, &r) == ELIB_TIMER_OK);
    assert(r == 700u);

    /* PAUSED：冻结值 */
    (void)elib_timer_pause(&ctx, run);
    (void)elib_timer_manager(&ctx, 400u);
    assert(elib_timer_get_remaining(&ctx, run, &r) == ELIB_TIMER_OK);
    assert(r == 700u);

    /* STOPPED：全重置后 == period */
    (void)elib_timer_stop(&ctx, run);
    assert(elib_timer_get_remaining(&ctx, run, &r) == ELIB_TIMER_OK);
    assert(r == 1000u);

    /* EXPIRED：0 */
    uint32_t exp = mk(1000u, 1u);
    (void)elib_timer_start(&ctx, exp);
    (void)elib_timer_manager(&ctx, 1000u);
    assert(elib_timer_get_remaining(&ctx, exp, &r) == ELIB_TIMER_OK);
    assert(r == 0u);
    printf("PASSED\n");
}

void test_remaining_fresh_create(void) {
    printf("Test: fresh timer remaining equals period... ");
    reset();
    uint32_t r = 0u;
    (void)mk(1000u, 0u);
    assert(elib_timer_get_remaining(&ctx, 0u, &r) == ELIB_TIMER_OK);
    assert(r == 1000u);
    printf("PASSED\n");
}

void test_remaining_invalid(void) {
    printf("Test: get_remaining rejects bad params... ");
    reset();
    uint32_t id = mk(1000u, 0u);
    uint32_t r;

    assert(elib_timer_get_remaining(&ctx, id, NULL)
           == ELIB_TIMER_ERR_INVALID_PARAM);
    assert(elib_timer_get_remaining(&ctx, 99u, &r)
           == ELIB_TIMER_ERR_NOT_FOUND);
    (void)elib_timer_delete(&ctx, id);
    assert(elib_timer_get_remaining(&ctx, id, &r)
           == ELIB_TIMER_ERR_NOT_FOUND);
    elib_timer_deinit(&ctx);
    assert(elib_timer_get_remaining(&ctx, id, &r)
           == ELIB_TIMER_ERR_NOT_INITIALIZED);
    printf("PASSED\n");
}

int main(void) {
    printf("=== test_get_remaining ===\n");
    test_remaining_states();
    test_remaining_fresh_create();
    test_remaining_invalid();
    printf("=== test_get_remaining: all PASSED ===\n");
    return 0;
}
