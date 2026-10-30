/* test_set_loop_count.c --- 循环次数运行中改参件 */
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

void test_set_loop_midflight_resets(void) {
    printf("Test: set_loop_count resets loop counter immediately... ");
    reset();
    uint32_t id = mk(1000u, 3u);
    (void)elib_timer_start(&ctx, id);
    assert(elib_timer_manager(&ctx, 1000u) == ELIB_TIMER_OK);
    assert(cb_count == 1u);
    assert(timers[id].loop_remaining == 2u);

    assert(elib_timer_set_loop_count(&ctx, id, 3u) == ELIB_TIMER_OK);
    assert(timers[id].loop_remaining == 3u);

    assert(elib_timer_manager(&ctx, 1000u) == ELIB_TIMER_OK);
    assert(elib_timer_manager(&ctx, 1000u) == ELIB_TIMER_OK);
    assert(elib_timer_manager(&ctx, 1000u) == ELIB_TIMER_OK);
    assert(cb_count == 4u);
    assert(timers[id].bit_flags.state == ELIB_TIMER_STATE_EXPIRED);
    printf("PASSED\n");
}

void test_infinite_to_finite(void) {
    printf("Test: set_loop_count converts infinite to finite... ");
    reset();
    uint32_t id = mk(1000u, 0u);
    (void)elib_timer_start(&ctx, id);
    assert(elib_timer_manager(&ctx, 1000u) == ELIB_TIMER_OK);
    assert(cb_count == 1u);
    assert(timers[id].bit_flags.state == ELIB_TIMER_STATE_RUNNING);

    assert(elib_timer_set_loop_count(&ctx, id, 2u) == ELIB_TIMER_OK);
    assert(elib_timer_manager(&ctx, 1000u) == ELIB_TIMER_OK);
    assert(cb_count == 2u);
    assert(elib_timer_manager(&ctx, 1000u) == ELIB_TIMER_OK);
    assert(cb_count == 3u);
    assert(timers[id].bit_flags.state == ELIB_TIMER_STATE_EXPIRED);
    printf("PASSED\n");
}

void test_finite_to_infinite(void) {
    printf("Test: set_loop_count converts finite to infinite... ");
    reset();
    uint32_t id = mk(1000u, 2u);
    (void)elib_timer_start(&ctx, id);
    assert(elib_timer_manager(&ctx, 1000u) == ELIB_TIMER_OK);
    assert(cb_count == 1u);
    assert(timers[id].loop_remaining == 1u);

    assert(elib_timer_set_loop_count(&ctx, id, 0u) == ELIB_TIMER_OK);

    assert(elib_timer_manager(&ctx, 1000u) == ELIB_TIMER_OK);
    assert(cb_count == 2u);
    assert(timers[id].bit_flags.state == ELIB_TIMER_STATE_RUNNING);
    assert(elib_timer_manager(&ctx, 1000u) == ELIB_TIMER_OK);
    assert(cb_count == 3u);
    assert(timers[id].bit_flags.state == ELIB_TIMER_STATE_RUNNING);
    printf("PASSED\n");
}

void test_set_loop_invalid(void) {
    printf("Test: set_loop_count rejects unused / bad id... ");
    reset();
    uint32_t id = mk(1000u, 1u);
    (void)elib_timer_delete(&ctx, id);
    assert(elib_timer_set_loop_count(&ctx, id, 1u) == ELIB_TIMER_ERR_NOT_FOUND);
    assert(elib_timer_set_loop_count(&ctx, 99u, 1u) == ELIB_TIMER_ERR_NOT_FOUND);
    printf("PASSED\n");
}

void test_loop_count_1_is_one_shot(void) {
    printf("Test: loop_count == 1 is exactly one-shot... ");
    reset();
    uint32_t id = mk(1000u, 1u);
    (void)elib_timer_start(&ctx, id);
    (void)elib_timer_manager(&ctx, 500u);
    (void)elib_timer_manager(&ctx, 500u);
    assert(cb_count == 1u);
    assert(timers[id].bit_flags.state == ELIB_TIMER_STATE_EXPIRED);
    assert(timers[id].remaining_ms == 0u);

    (void)elib_timer_manager(&ctx, 1000u);
    assert(cb_count == 1u);
    printf("PASSED\n");
}

int main(void) {
    printf("=== test_set_loop_count ===\n");
    test_set_loop_midflight_resets();
    test_infinite_to_finite();
    test_finite_to_infinite();
    test_set_loop_invalid();
    test_loop_count_1_is_one_shot();
    printf("=== test_set_loop_count: all PASSED ===\n");
    return 0;
}
