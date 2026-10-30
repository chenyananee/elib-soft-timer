/* test_init.c --- init / deinit / re-init 边界件 */
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "elib_timer.h"

#define MAX_TIMERS 8
static elib_timer_ctx_t ctx;
static elib_timer_instance_t timers[MAX_TIMERS];

static uint32_t cb_count;
static void cb(void *p) { (void)p; cb_count++; }

static uint32_t mk(int line) {
    uint32_t id;
    elib_timer_err_t err = elib_timer_create(&ctx, 1000u, cb, NULL,
                                             ELIB_TIMER_MODE_IMMEDIATE, 0u,
                                             &id);
    assert(err == ELIB_TIMER_OK);
    (void)line;
    return id;
}

static void reset(void) {
    memset(&ctx, 0, sizeof(ctx));
    memset(timers, 0, sizeof(timers));
    cb_count = 0u;
    assert(elib_timer_init(&ctx, timers, MAX_TIMERS) == ELIB_TIMER_OK);
}

void test_init_valid(void) {
    printf("Test: init with valid parameters... ");
    reset();
    assert(ctx.bit_flags.initialized == 1u);
    assert(ctx.max_timers == MAX_TIMERS);
    assert(ctx.active_count == 0u);
    printf("PASSED\n");
}

void test_init_null_ctx(void) {
    printf("Test: init with null ctx... ");
    assert(elib_timer_init(NULL, timers, MAX_TIMERS)
           == ELIB_TIMER_ERR_INVALID_PARAM);
    printf("PASSED\n");
}

void test_init_null_timers(void) {
    printf("Test: init with null timers... ");
    assert(elib_timer_init(&ctx, NULL, MAX_TIMERS)
           == ELIB_TIMER_ERR_INVALID_PARAM);
    printf("PASSED\n");
}

void test_init_zero_max(void) {
    printf("Test: init with zero max_timers... ");
    assert(elib_timer_init(&ctx, timers, 0u)
           == ELIB_TIMER_ERR_INVALID_PARAM);
    printf("PASSED\n");
}

void test_deinit_clears(void) {
    printf("Test: deinit clears initialized flag... ");
    reset();
    (void)mk(__LINE__);
    elib_timer_deinit(&ctx);
    assert(ctx.bit_flags.initialized == 0u);
    printf("PASSED\n");
}

void test_ops_reject_after_deinit(void) {
    printf("Test: all APIs reject after deinit... ");
    reset();
    uint32_t id = mk(__LINE__);
    elib_timer_deinit(&ctx);

    uint32_t out;
    assert(elib_timer_create(&ctx, 100u, cb, NULL,
                             ELIB_TIMER_MODE_IMMEDIATE, 0u, &out)
           == ELIB_TIMER_ERR_NOT_INITIALIZED);
    assert(elib_timer_start(&ctx, id) == ELIB_TIMER_ERR_NOT_INITIALIZED);
    assert(elib_timer_stop(&ctx, id) == ELIB_TIMER_ERR_NOT_INITIALIZED);
    assert(elib_timer_pause(&ctx, id) == ELIB_TIMER_ERR_NOT_INITIALIZED);
    assert(elib_timer_resume(&ctx, id) == ELIB_TIMER_ERR_NOT_INITIALIZED);
    assert(elib_timer_set_period(&ctx, id, 99u) == ELIB_TIMER_ERR_NOT_INITIALIZED);
    assert(elib_timer_set_loop_count(&ctx, id, 1u) == ELIB_TIMER_ERR_NOT_INITIALIZED);
    assert(elib_timer_get_remaining(&ctx, id, &out) == ELIB_TIMER_ERR_NOT_INITIALIZED);
    assert(elib_timer_delete(&ctx, id) == ELIB_TIMER_ERR_NOT_INITIALIZED);
    assert(elib_timer_manager(&ctx, 1000u) == ELIB_TIMER_ERR_NOT_INITIALIZED);
    assert(elib_timer_process_pending(&ctx) == ELIB_TIMER_ERR_NOT_INITIALIZED);
    assert(cb_count == 0u);
    printf("PASSED\n");
}

void test_reinit_over_dirty(void) {
    printf("Test: re-init over dirty context... ");
    reset();
    (void)mk(__LINE__);
    (void)mk(__LINE__);
    assert(ctx.active_count == 2u);

    elib_timer_deinit(&ctx);
    assert(elib_timer_init(&ctx, timers, MAX_TIMERS) == ELIB_TIMER_OK);

    assert(ctx.bit_flags.initialized == 1u);
    assert(ctx.active_count == 0u);
    assert(timers[0].bit_flags.state == ELIB_TIMER_STATE_UNUSED);
    assert(timers[1].bit_flags.state == ELIB_TIMER_STATE_UNUSED);
    printf("PASSED\n");
}

int main(void) {
    printf("=== test_init ===\n");
    test_init_valid();
    test_init_null_ctx();
    test_init_null_timers();
    test_init_zero_max();
    test_deinit_clears();
    test_ops_reject_after_deinit();
    test_reinit_over_dirty();
    (void)mk;
    printf("=== test_init: all PASSED ===\n");
    return 0;
}
