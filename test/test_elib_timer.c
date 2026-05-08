/* test_elib_timer.c - Software Timer Unit Tests */
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "../include/elib_timer.h"

/* Test configuration */
#define MAX_TIMERS 8
static elib_timer_ctx_t test_ctx;
static elib_timer_instance_t test_timers[MAX_TIMERS];

/* Callback counters */
static int callback_count[3] = {0, 0, 0};

/* Test callback functions */
static void callback_timer0(void *user_data) {
    (void)user_data;
    callback_count[0]++;
}

static void callback_timer1(void *user_data) {
    (void)user_data;
    callback_count[1]++;
}

static void callback_timer2(void *user_data) {
    (void)user_data;
    callback_count[2]++;
}

/* Reset test state */
static void reset_test(void) {
    memset(&test_ctx, 0, sizeof(test_ctx));
    memset(test_timers, 0, sizeof(test_timers));
    memset(callback_count, 0, sizeof(callback_count));
    elib_timer_init(&test_ctx, test_timers, MAX_TIMERS);
}

/* Test: init with valid parameters */
static void test_init_valid(void) {
    printf("Test: init with valid parameters... ");
    reset_test();

    assert(test_ctx.initialized == 1);
    assert(test_ctx.max_timers == MAX_TIMERS);
    assert(test_ctx.active_count == 0);

    printf("PASSED\n");
}

/* Test: init with null ctx */
static void test_init_null_ctx(void) {
    printf("Test: init with null ctx... ");
    reset_test();

    elib_timer_err_t err = elib_timer_init(NULL, test_timers, MAX_TIMERS);
    assert(err == ELIB_TIMER_ERR_INVALID_PARAM);

    printf("PASSED\n");
}

/* Test: init with null timers */
static void test_init_null_timers(void) {
    printf("Test: init with null timers... ");
    reset_test();

    elib_timer_err_t err = elib_timer_init(&test_ctx, NULL, MAX_TIMERS);
    assert(err == ELIB_TIMER_ERR_INVALID_PARAM);

    printf("PASSED\n");
}

/* Test: init with zero max_timers */
static void test_init_zero_max(void) {
    printf("Test: init with zero max_timers... ");
    reset_test();

    elib_timer_err_t err = elib_timer_init(&test_ctx, test_timers, 0);
    assert(err == ELIB_TIMER_ERR_INVALID_PARAM);

    printf("PASSED\n");
}

/* Test: create timer with valid parameters */
static void test_create_valid(void) {
    printf("Test: create timer with valid parameters... ");
    reset_test();

    uint32_t timer_id;
    elib_timer_err_t err = elib_timer_create(&test_ctx, 1000, callback_timer0,
                                              NULL, ELIB_TIMER_MODE_IMMEDIATE,
                                              0, &timer_id);

    assert(err == ELIB_TIMER_OK);
    assert(timer_id == 0);
    assert(test_ctx.active_count == 1);
    assert(test_timers[timer_id].state == ELIB_TIMER_STATE_STOPPED);
    assert(test_timers[timer_id].period_ms == 1000);

    printf("PASSED\n");
}

/* Test: create timer with null callback */
static void test_create_null_callback(void) {
    printf("Test: create timer with null callback... ");
    reset_test();

    uint32_t timer_id;
    elib_timer_err_t err = elib_timer_create(&test_ctx, 1000, NULL,
                                              NULL, ELIB_TIMER_MODE_IMMEDIATE,
                                              0, &timer_id);

    assert(err == ELIB_TIMER_ERR_INVALID_PARAM);

    printf("PASSED\n");
}

/* Test: create timer with zero period */
static void test_create_zero_period(void) {
    printf("Test: create timer with zero period... ");
    reset_test();

    uint32_t timer_id;
    elib_timer_err_t err = elib_timer_create(&test_ctx, 0, callback_timer0,
                                              NULL, ELIB_TIMER_MODE_IMMEDIATE,
                                              0, &timer_id);

    assert(err == ELIB_TIMER_ERR_INVALID_PARAM);

    printf("PASSED\n");
}

/* Test: create timer with pool full */
static void test_create_pool_full(void) {
    printf("Test: create timer with pool full... ");
    reset_test();

    /* Create maximum number of timers */
    for (uint32_t i = 0; i < MAX_TIMERS; i++) {
        uint32_t timer_id;
        elib_timer_err_t err = elib_timer_create(&test_ctx, 1000, callback_timer0,
                                                  NULL, ELIB_TIMER_MODE_IMMEDIATE,
                                                  0, &timer_id);
        assert(err == ELIB_TIMER_OK);
    }

    /* Try to create one more */
    uint32_t timer_id;
    elib_timer_err_t err = elib_timer_create(&test_ctx, 1000, callback_timer0,
                                              NULL, ELIB_TIMER_MODE_IMMEDIATE,
                                              0, &timer_id);
    assert(err == ELIB_TIMER_ERR_FULL);

    printf("PASSED\n");
}

/* Test: start and stop timer */
static void test_start_stop(void) {
    printf("Test: start and stop timer... ");
    reset_test();

    uint32_t timer_id;
    elib_timer_create(&test_ctx, 1000, callback_timer0, NULL,
                      ELIB_TIMER_MODE_IMMEDIATE, 0, &timer_id);

    /* Start timer */
    elib_timer_err_t err = elib_timer_start(&test_ctx, timer_id);
    assert(err == ELIB_TIMER_OK);
    assert(test_timers[timer_id].state == ELIB_TIMER_STATE_RUNNING);

    /* Stop timer */
    err = elib_timer_stop(&test_ctx, timer_id);
    assert(err == ELIB_TIMER_OK);
    assert(test_timers[timer_id].state == ELIB_TIMER_STATE_STOPPED);

    printf("PASSED\n");
}

/* Test: start already running timer */
static void test_start_already_running(void) {
    printf("Test: start already running timer... ");
    reset_test();

    uint32_t timer_id;
    elib_timer_create(&test_ctx, 1000, callback_timer0, NULL,
                      ELIB_TIMER_MODE_IMMEDIATE, 0, &timer_id);
    elib_timer_start(&test_ctx, timer_id);

    elib_timer_err_t err = elib_timer_start(&test_ctx, timer_id);
    assert(err == ELIB_TIMER_ERR_ALREADY_RUNNING);

    printf("PASSED\n");
}

/* Test: stop stopped timer */
static void test_stop_stopped_timer(void) {
    printf("Test: stop stopped timer... ");
    reset_test();

    uint32_t timer_id;
    elib_timer_create(&test_ctx, 1000, callback_timer0, NULL,
                      ELIB_TIMER_MODE_IMMEDIATE, 0, &timer_id);

    elib_timer_err_t err = elib_timer_stop(&test_ctx, timer_id);
    assert(err == ELIB_TIMER_ERR_NOT_RUNNING);

    printf("PASSED\n");
}

/* Test: pause and resume timer */
static void test_pause_resume(void) {
    printf("Test: pause and resume timer... ");
    reset_test();

    uint32_t timer_id;
    elib_timer_create(&test_ctx, 1000, callback_timer0, NULL,
                      ELIB_TIMER_MODE_IMMEDIATE, 0, &timer_id);
    elib_timer_start(&test_ctx, timer_id);

    /* Pause timer */
    elib_timer_err_t err = elib_timer_pause(&test_ctx, timer_id);
    assert(err == ELIB_TIMER_OK);
    assert(test_timers[timer_id].state == ELIB_TIMER_STATE_PAUSED);

    /* Resume timer */
    err = elib_timer_resume(&test_ctx, timer_id);
    assert(err == ELIB_TIMER_OK);
    assert(test_timers[timer_id].state == ELIB_TIMER_STATE_RUNNING);

    printf("PASSED\n");
}

/* Test: pause stopped timer fails */
static void test_pause_not_running(void) {
    printf("Test: pause stopped timer fails... ");
    reset_test();

    uint32_t timer_id;
    elib_timer_create(&test_ctx, 1000, callback_timer0, NULL,
                      ELIB_TIMER_MODE_IMMEDIATE, 0, &timer_id);

    elib_timer_err_t err = elib_timer_pause(&test_ctx, timer_id);
    assert(err == ELIB_TIMER_ERR_NOT_RUNNING);

    printf("PASSED\n");
}

/* Test: set period at runtime */
static void test_set_period(void) {
    printf("Test: set period at runtime... ");
    reset_test();

    uint32_t timer_id;
    elib_timer_create(&test_ctx, 1000, callback_timer0, NULL,
                      ELIB_TIMER_MODE_IMMEDIATE, 0, &timer_id);
    elib_timer_start(&test_ctx, timer_id);

    /* Advance time by 500ms (50% of period) */
    elib_timer_manager(&test_ctx, 500);
    assert(test_timers[timer_id].remaining_ms == 500);

    /* Change period to 2000ms - remaining should be proportionally adjusted */
    elib_timer_set_period(&test_ctx, timer_id, 2000);
    assert(test_timers[timer_id].period_ms == 2000);
    /* 500/1000 = 50%, 50% of 2000 = 1000 */
    assert(test_timers[timer_id].remaining_ms == 1000);

    printf("PASSED\n");
}

/* Test: set period with zero */
static void test_set_period_zero(void) {
    printf("Test: set period with zero fails... ");
    reset_test();

    uint32_t timer_id;
    elib_timer_create(&test_ctx, 1000, callback_timer0, NULL,
                      ELIB_TIMER_MODE_IMMEDIATE, 0, &timer_id);

    elib_timer_err_t err = elib_timer_set_period(&test_ctx, timer_id, 0);
    assert(err == ELIB_TIMER_ERR_INVALID_PARAM);

    printf("PASSED\n");
}

/* Test: timer expiration with immediate mode */
static void test_timer_expiration_immediate(void) {
    printf("Test: timer expiration immediate mode... ");
    reset_test();

    uint32_t timer_id;
    elib_timer_create(&test_ctx, 1000, callback_timer0, NULL,
                      ELIB_TIMER_MODE_IMMEDIATE, 0, &timer_id);
    elib_timer_start(&test_ctx, timer_id);

    /* Advance time to trigger expiration */
    elib_timer_manager(&test_ctx, 1000);

    assert(callback_count[0] == 1);
    assert(test_timers[timer_id].state == ELIB_TIMER_STATE_RUNNING);
    assert(test_timers[timer_id].remaining_ms == 1000);

    printf("PASSED\n");
}

/* Test: timer expiration with delayed mode */
static void test_timer_expiration_delayed(void) {
    printf("Test: timer expiration delayed mode... ");
    reset_test();

    uint32_t timer_id;
    elib_timer_create(&test_ctx, 1000, callback_timer1, NULL,
                      ELIB_TIMER_MODE_DELAYED, 0, &timer_id);
    elib_timer_start(&test_ctx, timer_id);

    /* Advance time to trigger expiration - callback should NOT execute yet */
    elib_timer_manager(&test_ctx, 1000);

    assert(callback_count[1] == 0);
    assert(test_timers[timer_id].pending_execution == true);

    /* Process pending callbacks */
    elib_timer_process_pending(&test_ctx);

    assert(callback_count[1] == 1);
    assert(test_timers[timer_id].pending_execution == false);

    printf("PASSED\n");
}

/* Test: one-shot timer (loop_count = 1) */
static void test_one_shot_timer(void) {
    printf("Test: one-shot timer... ");
    reset_test();

    uint32_t timer_id;
    elib_timer_create(&test_ctx, 1000, callback_timer2, NULL,
                      ELIB_TIMER_MODE_IMMEDIATE, 1, &timer_id);
    elib_timer_start(&test_ctx, timer_id);

    /* First expiration */
    elib_timer_manager(&test_ctx, 1000);

    assert(callback_count[2] == 1);
    assert(test_timers[timer_id].state == ELIB_TIMER_STATE_EXPIRED);

    /* Second expiration should not trigger callback */
    elib_timer_manager(&test_ctx, 1000);

    assert(callback_count[2] == 1);

    printf("PASSED\n");
}

/* Test: delete timer */
static void test_delete_timer(void) {
    printf("Test: delete timer... ");
    reset_test();

    uint32_t timer_id;
    elib_timer_create(&test_ctx, 1000, callback_timer0, NULL,
                      ELIB_TIMER_MODE_IMMEDIATE, 0, &timer_id);

    assert(test_ctx.active_count == 1);

    elib_timer_err_t err = elib_timer_delete(&test_ctx, timer_id);
    assert(err == ELIB_TIMER_OK);
    assert(test_ctx.active_count == 0);
    assert(test_timers[timer_id].state == ELIB_TIMER_STATE_UNUSED);

    printf("PASSED\n");
}

/* Test: get remaining time */
static void test_get_remaining(void) {
    printf("Test: get remaining time... ");
    reset_test();

    uint32_t timer_id;
    elib_timer_create(&test_ctx, 1000, callback_timer0, NULL,
                      ELIB_TIMER_MODE_IMMEDIATE, 0, &timer_id);
    elib_timer_start(&test_ctx, timer_id);

    /* Advance time by 300ms */
    elib_timer_manager(&test_ctx, 300);

    uint32_t remaining;
    elib_timer_err_t err = elib_timer_get_remaining(&test_ctx, timer_id, &remaining);

    assert(err == ELIB_TIMER_OK);
    assert(remaining == 700);

    printf("PASSED\n");
}

/* Test: uninitialized context rejects operations */
static void test_uninitialized_context(void) {
    printf("Test: uninitialized context rejects operations... ");
    reset_test();

    elib_timer_deinit(&test_ctx);

    uint32_t timer_id;
    assert(elib_timer_create(&test_ctx, 1000, callback_timer0, NULL,
                             ELIB_TIMER_MODE_IMMEDIATE, 0, &timer_id)
           == ELIB_TIMER_ERR_NOT_INITIALIZED);

    printf("PASSED\n");
}

int main(void) {
    printf("=== elib-soft-timer tests ===\n\n");

    /* Initialization tests */
    test_init_valid();
    test_init_null_ctx();
    test_init_null_timers();
    test_init_zero_max();

    /* Create tests */
    test_create_valid();
    test_create_null_callback();
    test_create_zero_period();
    test_create_pool_full();

    /* Start/Stop tests */
    test_start_stop();
    test_start_already_running();
    test_stop_stopped_timer();

    /* Pause/Resume tests */
    test_pause_resume();
    test_pause_not_running();

    /* Dynamic configuration tests */
    test_set_period();
    test_set_period_zero();

    /* Timer operation tests */
    test_timer_expiration_immediate();
    test_timer_expiration_delayed();
    test_one_shot_timer();

    /* Delete and query tests */
    test_delete_timer();
    test_get_remaining();

    /* Error handling tests */
    test_uninitialized_context();

    printf("\n=== All tests passed ===\n");
    return 0;
}