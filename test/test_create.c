/* test_create.c --- create 参数校验 / 池满 / 槽位复用件 */
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

void test_create_first_id_0(void) {
    printf("Test: create first timer gets id 0... ");
    reset();
    uint32_t id = mk(1000u, ELIB_TIMER_MODE_IMMEDIATE, 0u);
    assert(id == 0u);
    assert(ctx.active_count == 1u);
    assert(timers[0].period_ms == 1000u);
    assert(timers[0].remaining_ms == 1000u);
    assert(timers[0].bit_flags.state == ELIB_TIMER_STATE_STOPPED);
    assert(timers[0].bit_flags.pending_execution == 0u);
    printf("PASSED\n");
}

void test_create_ids_sequential(void) {
    printf("Test: create assigns sequential ids... ");
    reset();
    assert(mk(100u, ELIB_TIMER_MODE_IMMEDIATE, 0u) == 0u);
    assert(mk(100u, ELIB_TIMER_MODE_IMMEDIATE, 0u) == 1u);
    assert(mk(100u, ELIB_TIMER_MODE_IMMEDIATE, 0u) == 2u);
    assert(ctx.active_count == 3u);
    printf("PASSED\n");
}

void test_create_null_outputs(void) {
    printf("Test: create rejects null callback / out id... ");
    reset();
    uint32_t id;
    assert(elib_timer_create(&ctx, 1000u, NULL, NULL,
                             ELIB_TIMER_MODE_IMMEDIATE, 0u, &id)
           == ELIB_TIMER_ERR_INVALID_PARAM);
    assert(elib_timer_create(&ctx, 1000u, cb, NULL,
                             ELIB_TIMER_MODE_IMMEDIATE, 0u, NULL)
           == ELIB_TIMER_ERR_INVALID_PARAM);
    assert(elib_timer_create(NULL, 1000u, cb, NULL,
                             ELIB_TIMER_MODE_IMMEDIATE, 0u, &id)
           == ELIB_TIMER_ERR_INVALID_PARAM);
    printf("PASSED\n");
}

void test_create_zero_period(void) {
    printf("Test: create rejects zero period... ");
    reset();
    uint32_t id;
    assert(elib_timer_create(&ctx, 0u, cb, NULL,
                             ELIB_TIMER_MODE_IMMEDIATE, 0u, &id)
           == ELIB_TIMER_ERR_INVALID_PARAM);
    printf("PASSED\n");
}

void test_create_invalid_exec_mode(void) {
    printf("Test: create rejects out-of-domain exec_mode... ");
    reset();
    uint32_t id;
    assert(elib_timer_create(&ctx, 1000u, cb, NULL,
                             (elib_timer_exec_mode_t)2, 0u, &id)
           == ELIB_TIMER_ERR_INVALID_PARAM);
    assert(elib_timer_create(&ctx, 1000u, cb, NULL,
                             (elib_timer_exec_mode_t)7, 0u, &id)
           == ELIB_TIMER_ERR_INVALID_PARAM);
    assert(elib_timer_create(&ctx, 1000u, cb, NULL,
                             (elib_timer_exec_mode_t)0xFFFFFFFFu, 0u, &id)
           == ELIB_TIMER_ERR_INVALID_PARAM);
    assert(ctx.active_count == 0u);
    printf("PASSED\n");
}

void test_create_pool_full(void) {
    printf("Test: create rejects when pool full... ");
    reset();
    for (uint32_t i = 0u; i < MAX_TIMERS; i++) {
        (void)mk(100u, ELIB_TIMER_MODE_IMMEDIATE, 0u);
    }
    uint32_t id;
    assert(elib_timer_create(&ctx, 100u, cb, NULL,
                             ELIB_TIMER_MODE_IMMEDIATE, 0u, &id)
           == ELIB_TIMER_ERR_FULL);
    assert(ctx.active_count == MAX_TIMERS);
    printf("PASSED\n");
}

void test_create_reuse_after_delete(void) {
    printf("Test: create reuses first free slot after delete... ");
    reset();
    for (uint32_t i = 0u; i < MAX_TIMERS; i++) {
        (void)mk(100u, ELIB_TIMER_MODE_IMMEDIATE, 0u);
    }
    assert(elib_timer_delete(&ctx, 3u) == ELIB_TIMER_OK);
    uint32_t id = mk(100u, ELIB_TIMER_MODE_IMMEDIATE, 0u);
    assert(id == 3u);
    assert(ctx.active_count == MAX_TIMERS);
    assert(timers[3].period_ms == 100u && timers[3].remaining_ms == 100u);
    printf("PASSED\n");
}

void test_create_delayed_ok(void) {
    printf("Test: create accepts DELAYED mode... ");
    reset();
    uint32_t id = mk(1000u, ELIB_TIMER_MODE_DELAYED, 0u);
    assert(timers[id].bit_flags.exec_mode == 1u);
    printf("PASSED\n");
}

int main(void) {
    printf("=== test_create ===\n");
    test_create_first_id_0();
    test_create_ids_sequential();
    test_create_null_outputs();
    test_create_zero_period();
    test_create_invalid_exec_mode();
    test_create_pool_full();
    test_create_reuse_after_delete();
    test_create_delayed_ok();
    printf("=== test_create: all PASSED ===\n");
    return 0;
}
