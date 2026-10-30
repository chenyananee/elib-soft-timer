/* test_pause.c --- pause 冻结 / resume 续跑 / 状态门禁件 */
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

void test_pause_resume_valid(void) {
    printf("Test: pause RUNNING -> PAUSED, resume -> RUNNING... ");
    reset();
    uint32_t id = mk(1000u, ELIB_TIMER_MODE_IMMEDIATE, 0u);
    (void)elib_timer_start(&ctx, id);
    assert(elib_timer_pause(&ctx, id) == ELIB_TIMER_OK);
    assert(timers[id].bit_flags.state == ELIB_TIMER_STATE_PAUSED);
    assert(elib_timer_resume(&ctx, id) == ELIB_TIMER_OK);
    assert(timers[id].bit_flags.state == ELIB_TIMER_STATE_RUNNING);
    printf("PASSED\n");
}

void test_pause_freezes_remaining(void) {
    printf("Test: paused timer is not ticked by manager... ");
    reset();
    uint32_t id = mk(1000u, ELIB_TIMER_MODE_IMMEDIATE, 0u);
    (void)elib_timer_start(&ctx, id);
    (void)elib_timer_manager(&ctx, 300u);
    assert(timers[id].remaining_ms == 700u);

    (void)elib_timer_pause(&ctx, id);
    (void)elib_timer_manager(&ctx, 500u);
    assert(timers[id].remaining_ms == 700u);
    assert(timers[id].bit_flags.state == ELIB_TIMER_STATE_PAUSED);

    (void)elib_timer_resume(&ctx, id);
    assert(elib_timer_manager(&ctx, 700u) == ELIB_TIMER_OK);
    assert(cb_count == 1u);
    printf("PASSED\n");
}

void test_pause_stopped(void) {
    printf("Test: pause on STOPPED rejected... ");
    reset();
    uint32_t id = mk(1000u, ELIB_TIMER_MODE_IMMEDIATE, 0u);
    assert(elib_timer_pause(&ctx, id) == ELIB_TIMER_ERR_NOT_RUNNING);
    printf("PASSED\n");
}

void test_pause_paused(void) {
    printf("Test: double pause rejected... ");
    reset();
    uint32_t id = mk(1000u, ELIB_TIMER_MODE_IMMEDIATE, 0u);
    (void)elib_timer_start(&ctx, id);
    (void)elib_timer_pause(&ctx, id);
    assert(elib_timer_pause(&ctx, id) == ELIB_TIMER_ERR_NOT_RUNNING);
    printf("PASSED\n");
}

void test_resume_without_pause(void) {
    printf("Test: resume on non-PAUSED rejected... ");
    reset();
    uint32_t id = mk(1000u, ELIB_TIMER_MODE_IMMEDIATE, 0u);
    assert(elib_timer_resume(&ctx, id) == ELIB_TIMER_ERR_NOT_RUNNING);

    (void)elib_timer_start(&ctx, id);
    assert(elib_timer_resume(&ctx, id) == ELIB_TIMER_ERR_NOT_RUNNING);
    printf("PASSED\n");
}

void test_pause_bad_id(void) {
    printf("Test: pause/resume reject bad ids... ");
    reset();
    assert(elib_timer_pause(&ctx, 99u) == ELIB_TIMER_ERR_NOT_FOUND);
    assert(elib_timer_resume(&ctx, 99u) == ELIB_TIMER_ERR_NOT_FOUND);
    printf("PASSED\n");
}

int main(void) {
    printf("=== test_pause ===\n");
    test_pause_resume_valid();
    test_pause_freezes_remaining();
    test_pause_stopped();
    test_pause_paused();
    test_resume_without_pause();
    test_pause_bad_id();
    printf("=== test_pause: all PASSED ===\n");
    return 0;
}
