/* test_delete.c --- 删除 / ID 边界 / 槽位复用净身件 */
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

static uint32_t mk(uint32_t period, uint32_t loops, uint32_t idx) {
    uint32_t id;
    assert(elib_timer_create(&ctx, period, cb, idx ? NULL : (void *)&cb_count,
                             ELIB_TIMER_MODE_IMMEDIATE, loops, &id)
           == ELIB_TIMER_OK);
    (void)idx;
    return id;
}

void test_delete_ok(void) {
    printf("Test: delete frees slot and decrements active count... ");
    reset();
    uint32_t id = mk(1000u, 0u, 0u);
    assert(ctx.active_count == 1u);
    assert(elib_timer_delete(&ctx, id) == ELIB_TIMER_OK);
    assert(ctx.active_count == 0u);
    assert(timers[id].bit_flags.state == ELIB_TIMER_STATE_UNUSED);
    printf("PASSED\n");
}

void test_delete_twice(void) {
    printf("Test: double delete rejected... ");
    reset();
    uint32_t id = mk(1000u, 0u, 0u);
    (void)elib_timer_delete(&ctx, id);
    assert(elib_timer_delete(&ctx, id) == ELIB_TIMER_ERR_NOT_FOUND);
    printf("PASSED\n");
}

void test_delete_bad_id(void) {
    printf("Test: delete rejects out-of-range ids... ");
    reset();
    assert(elib_timer_delete(&ctx, MAX_TIMERS) == ELIB_TIMER_ERR_NOT_FOUND);
    assert(elib_timer_delete(&ctx, 99u) == ELIB_TIMER_ERR_NOT_FOUND);
    assert(elib_timer_delete(&ctx, 0xFFFFFFFFu) == ELIB_TIMER_ERR_NOT_FOUND);
    printf("PASSED\n");
}

void test_delete_running_ok(void) {
    printf("Test: deleting a RUNNING timer is allowed... ");
    reset();
    uint32_t id = mk(1000u, 0u, 0u);
    (void)elib_timer_start(&ctx, id);
    assert(elib_timer_delete(&ctx, id) == ELIB_TIMER_OK);
    assert(ctx.active_count == 0u);
    (void)elib_timer_manager(&ctx, 1000u);
    assert(cb_count == 0u);                      /* 已删定时器不再驱动 */
    printf("PASSED\n");
}

void test_delete_preserves_others(void) {
    printf("Test: deleting one timer leaves others intact... ");
    reset();
    for (uint32_t i = 0u; i < 3u; i++) {
        (void)mk(1000u, 0u, 0u);
    }
    (void)elib_timer_delete(&ctx, 1u);
    assert(timers[0].bit_flags.state == ELIB_TIMER_STATE_STOPPED);
    assert(timers[2].bit_flags.state == ELIB_TIMER_STATE_STOPPED);
    assert(ctx.active_count == 2u);
    printf("PASSED\n");
}

void test_reuse_state_clean(void) {
    printf("Test: reused slot carries no stale state... ");
    reset();
    uint32_t id = mk(500u, 3u, 0u);
    (void)elib_timer_start(&ctx, id);
    (void)elib_timer_manager(&ctx, 200u);        /* remaining 300，loop 3 */
    (void)elib_timer_delete(&ctx, id);

    uint32_t id2 = mk(1000u, 0u, 0u);
    assert(id2 == id);
    assert(timers[id2].period_ms == 1000u);
    assert(timers[id2].remaining_ms == 1000u);
    assert(timers[id2].loop_count == 0u && timers[id2].loop_remaining == 0u);
    assert(timers[id2].bit_flags.pending_execution == 0u);
    printf("PASSED\n");
}

int main(void) {
    printf("=== test_delete ===\n");
    test_delete_ok();
    test_delete_twice();
    test_delete_bad_id();
    test_delete_running_ok();
    test_delete_preserves_others();
    test_reuse_state_clean();
    printf("=== test_delete: all PASSED ===\n");
    return 0;
}
