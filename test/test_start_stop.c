/* test_start_stop.c --- start / stop 全重置语义 / EXPIRED 重开件 */
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

void test_start_valid(void) {
    printf("Test: start moves STOPPED to RUNNING... ");
    reset();
    uint32_t id = mk(1000u, ELIB_TIMER_MODE_IMMEDIATE, 0u);
    assert(elib_timer_start(&ctx, id) == ELIB_TIMER_OK);
    assert(timers[id].bit_flags.state == ELIB_TIMER_STATE_RUNNING);
    assert(timers[id].remaining_ms == 1000u);
    printf("PASSED\n");
}

void test_start_twice(void) {
    printf("Test: double start rejected... ");
    reset();
    uint32_t id = mk(1000u, ELIB_TIMER_MODE_IMMEDIATE, 0u);
    (void)elib_timer_start(&ctx, id);
    assert(elib_timer_start(&ctx, id) == ELIB_TIMER_ERR_ALREADY_RUNNING);
    printf("PASSED\n");
}

void test_stop_running(void) {
    printf("Test: stop running timer resets remaining and loop count... ");
    reset();
    uint32_t id = mk(1000u, ELIB_TIMER_MODE_IMMEDIATE, 3u);
    (void)elib_timer_start(&ctx, id);
    assert(elib_timer_manager(&ctx, 1000u) == ELIB_TIMER_OK);
    assert(cb_count == 1u);
    assert(timers[id].loop_remaining == 2u);

    assert(elib_timer_stop(&ctx, id) == ELIB_TIMER_OK);
    assert(timers[id].bit_flags.state == ELIB_TIMER_STATE_STOPPED);
    assert(timers[id].remaining_ms == 1000u);

    (void)elib_timer_start(&ctx, id);
    assert(timers[id].loop_remaining == 3u);
    printf("PASSED\n");
}

void test_stop_stopped(void) {
    printf("Test: stop on STOPPED rejected... ");
    reset();
    uint32_t id = mk(1000u, ELIB_TIMER_MODE_IMMEDIATE, 0u);
    assert(elib_timer_stop(&ctx, id) == ELIB_TIMER_ERR_NOT_RUNNING);
    printf("PASSED\n");
}

void test_stop_then_start_restarts(void) {
    printf("Test: stop mid-cycle then start restarts from full period... ");
    reset();
    uint32_t id = mk(1000u, ELIB_TIMER_MODE_IMMEDIATE, 0u);
    (void)elib_timer_start(&ctx, id);
    (void)elib_timer_manager(&ctx, 400u);
    assert(timers[id].remaining_ms == 600u);

    (void)elib_timer_stop(&ctx, id);
    assert(timers[id].remaining_ms == 1000u);
    (void)elib_timer_start(&ctx, id);
    assert(timers[id].remaining_ms == 1000u);

    assert(elib_timer_manager(&ctx, 1000u) == ELIB_TIMER_OK);
    assert(cb_count == 1u);
    printf("PASSED\n");
}

void test_start_from_expired(void) {
    printf("Test: start re-arms EXPIRED one-shot timer... ");
    reset();
    uint32_t id = mk(1000u, ELIB_TIMER_MODE_IMMEDIATE, 1u);
    (void)elib_timer_start(&ctx, id);
    assert(elib_timer_manager(&ctx, 1000u) == ELIB_TIMER_OK);
    assert(timers[id].bit_flags.state == ELIB_TIMER_STATE_EXPIRED);

    assert(elib_timer_start(&ctx, id) == ELIB_TIMER_OK);
    assert(timers[id].bit_flags.state == ELIB_TIMER_STATE_RUNNING);
    assert(timers[id].loop_remaining == 1u);
    assert(timers[id].remaining_ms == 1000u);
    assert(elib_timer_manager(&ctx, 1000u) == ELIB_TIMER_OK);
    assert(cb_count == 2u);
    printf("PASSED\n");
}

void test_start_from_paused(void) {
    printf("Test: start on PAUSED re-arms with full period... ");
    reset();
    uint32_t id = mk(1000u, ELIB_TIMER_MODE_IMMEDIATE, 0u);
    (void)elib_timer_start(&ctx, id);
    (void)elib_timer_manager(&ctx, 300u);
    (void)elib_timer_pause(&ctx, id);

    assert(elib_timer_start(&ctx, id) == ELIB_TIMER_OK);
    assert(timers[id].bit_flags.state == ELIB_TIMER_STATE_RUNNING);
    assert(timers[id].remaining_ms == 1000u);
    printf("PASSED\n");
}

void test_start_invalid(void) {
    printf("Test: start rejects bad ids / unused slot... ");
    reset();
    uint32_t id = mk(1000u, ELIB_TIMER_MODE_IMMEDIATE, 0u);

    assert(elib_timer_start(&ctx, 99u) == ELIB_TIMER_ERR_NOT_FOUND);
    assert(elib_timer_start(&ctx, 0xFFFFFFFFu) == ELIB_TIMER_ERR_NOT_FOUND);

    (void)elib_timer_delete(&ctx, id);
    assert(elib_timer_start(&ctx, id) == ELIB_TIMER_ERR_NOT_FOUND);
    printf("PASSED\n");
}

int main(void) {
    printf("=== test_start_stop ===\n");
    test_start_valid();
    test_start_twice();
    test_stop_running();
    test_stop_stopped();
    test_stop_then_start_restarts();
    test_start_from_expired();
    test_start_from_paused();
    test_start_invalid();
    printf("=== test_start_stop: all PASSED ===\n");
    return 0;
}
