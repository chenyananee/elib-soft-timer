[简体中文](README.md) | **English**

# elib-soft-timer

A lightweight software timer for resource-constrained embedded systems: zero dynamic memory, no global variables, pure C99, single umbrella-header API.

Current version: **2.0.0** (`ELIB_TIMER_VERSION`), single header `include/elib_timer.h`.

## Features

- **Zero dynamic memory / no globals**: all state lives in the user-supplied static `ctx + instance array`
- **Single umbrella-header API**: all public types and functions live in `elib_timer.h`; no sub-headers
- **1ms time base**: driven by elapsed milliseconds fed in from outside
- **Two execution modes**:
  - `IMMEDIATE`: the expiry callback runs directly in the caller's context of `elib_timer_manager()` (suited to a tick ISR)
  - `DELAYED`: expiry only sets a pending flag; the main loop drains and executes it via `elib_timer_process_pending()`
- **Full lifecycle**: create / start / stop / pause / resume / delete
- **Dynamic configuration**: change the period at runtime (remaining time is scaled proportionally) and the loop count
- **Optional critical-section hooks**: safe cross-context use between ISR and main loop

## Quick Start

### 1. Statically Define the Context and Instance Pool

```c
#include "elib_timer.h"   /* add -I<repo>/include at build time */

#define MAX_TIMERS 4

static elib_timer_ctx_t g_timer_ctx;
static elib_timer_instance_t g_timers[MAX_TIMERS];

static void app_init(void)
{
    elib_timer_init(&g_timer_ctx, g_timers, MAX_TIMERS);
}
```

### 2. Create and Start a Periodic Timer

```c
static void led_toggle_cb(void *user_data)
{
    (void)user_data;
    toggle_led();
}

static uint32_t g_led_timer_id;

static void app_start(void)
{
    elib_timer_create(&g_timer_ctx, 500u, led_toggle_cb, NULL,
                      ELIB_TIMER_MODE_IMMEDIATE, ELIB_TIMER_LOOP_INFINITE,
                      &g_led_timer_id);
    elib_timer_start(&g_timer_ctx, g_led_timer_id);
}
```

### 3. Drive the Clock (tick ISR) and Drain Delayed Callbacks (Main Loop)

```c
/* 1ms tick interrupt */
void SysTick_Handler(void)
{
    elib_timer_manager(&g_timer_ctx, 1u);
}

/* main loop */
for (;;) {
    elib_timer_process_pending(&g_timer_ctx);
    /* ... other tasks ... */
}
```

## API Overview

| Group | Function | Description |
|------|------|------|
| Lifetime | `elib_timer_init(ctx, timers, max_timers)` | Initialize (memset wipes prior state on reinit; invalid configurations are rejected) |
| | `elib_timer_deinit(ctx)` | Deinitialize (NULL-safe) |
| Instance | `elib_timer_create(ctx, period_ms, callback, user_data, exec_mode, loop_count, out_id)` | Create; ID = index of the first UNUSED slot |
| | `elib_timer_delete(ctx, timer_id)` | Delete; clears pending and returns the slot to UNUSED |
| State | `elib_timer_start(ctx, timer_id)` | Start/restart; remaining is reset to period |
| | `elib_timer_stop(ctx, timer_id)` | Stop; **full reset** (remaining returns to period) |
| | `elib_timer_pause(ctx, timer_id)` | Pause; remaining is frozen |
| | `elib_timer_resume(ctx, timer_id)` | Resume; continues from the frozen value |
| Configuration | `elib_timer_set_period(ctx, timer_id, period_ms)` | Change the period while running (proportionally scaled in RUNNING/PAUSED) |
| | `elib_timer_set_loop_count(ctx, timer_id, loop_count)` | Change the loop count while running (recounted immediately) |
| Query | `elib_timer_get_remaining(ctx, timer_id, &remaining_ms)` | Remaining milliseconds |
| Driver | `elib_timer_manager(ctx, ms_elapsed)` | Feed elapsed time from the tick ISR |
| | `elib_timer_process_pending(ctx)` | Main loop drains DELAYED callbacks (in slot-index order) |

Error codes: `ELIB_TIMER_OK` / `ERR_INVALID_PARAM` / `ERR_NOT_INITIALIZED` / `ERR_NOT_FOUND` / `ERR_FULL` / `ERR_ALREADY_RUNNING` / `ERR_NOT_RUNNING` (`ERR_EXECUTION_FAILED` is reserved).

## State Machine

```
UNUSED --create--> STOPPED --start--> RUNNING --pause--> PAUSED
                     ^                  |  ^               |
                     |                  |  +---resume------+
                     |                  |
                     |                  +--expiry and loop exhausted--> EXPIRED
                     |                                        |
                     +---------------- stop -----------------+
                                                              |
                                    EXPIRED --start--> RUNNING (restart)
```

- `delete`: any non-UNUSED state → UNUSED
- `stop`: RUNNING / PAUSED / EXPIRED → STOPPED (remaining is fully reset to period)
- `start`: STOPPED / PAUSED / EXPIRED → RUNNING (remaining reset to period, loop counter reset)

## Semantic Contract

### Time Driving and Missed Ticks

`elib_timer_manager(ctx, ms_elapsed)` is fed externally with the milliseconds elapsed since the previous call (typically a 1ms tick).

**Missed-tick semantics**: a single `manager` call produces at most one expiry decision. When `ms_elapsed` exceeds the remaining time, the excess beyond the boundary is discarded and the period restarts from a whole period—the observable effect is "one call, at most one expiry" drift. The tick granularity should therefore be far smaller than the smallest timer period.

### Period-Change Scaling Rules

`set_period` scales the remaining time proportionally for a running timer:

```
remaining' = remaining * new_period / old_period   (64-bit intermediate, rounded down, 0 clamped to 1)
```

| State | Behavior |
|------|------|
| RUNNING | scaled proportionally, continues counting down |
| PAUSED | frozen value is scaled proportionally; continues with the new value after resume |
| STOPPED | not scaled (uniformly reset to period on start) |
| EXPIRED | not scaled (remaining stays 0; reset only when start restarts it) |

### Callback Reentrancy Contract

The callback may **only operate on itself**:

- Allowed: `delete` / `stop` / `start` / `set_period` / `set_loop_count` / `get_remaining` on this timer
- Forbidden: `create`, deleting other timers, re-entering `manager` / `process_pending` (undefined behavior)

In `IMMEDIATE` mode, after the callback returns, manager re-checks `state == RUNNING` before loading the next remaining: a callback that deletes/stops itself is not revived/loaded again in this round.

### DELAYED Mode

On expiry only the `pending_execution` flag is set (state advances as usual). When the main loop calls `elib_timer_process_pending()`, callbacks run in slot-index order and the flags are cleared.

Deleting a timer while it is pending also clears the pending flag—a deleted timer never fires its callback.

## Concurrency Model (ISR + Main Loop)

`elib_timer_manager()` is meant to be called from the tick ISR; every other API from the main loop. Every shared-state mutation inside the library is wrapped in the critical-section hooks; define them in your compiler options as needed (they must be save/restore style and support nesting):

```c
/* Example: save/restore PRIMASK on Cortex-M (non-nesting) */
static uint32_t g_crit_save;

#define ELIB_TIMER_CRITICAL_ENTER()      \
    do {                                 \
        g_crit_save = __get_PRIMASK();   \
        __disable_irq();                 \
    } while (0)

#define ELIB_TIMER_CRITICAL_EXIT()       \
    do {                                 \
        __set_PRIMASK(g_crit_save);      \
    } while (0)
```

If you need nested calls, use a "save/restore stack + depth counter" implementation.

When undefined, both hooks are empty macros, declaring single-context use (e.g. manager and the API are only called from the main loop).

The callback runs **without** the critical section held.

## Building

Add `include/` to the header search path and compile the source file as an ordinary C99 object:

```bash
arm-none-eabi-gcc -std=c99 -Wall -Wextra -Werror -Iinclude \
    -c src/elib_timer_core.c -o elib_timer_core.o
```

The source file includes the umbrella header directly (`#include "elib_timer.h"`), depends on no relative paths, and ships no build system of its own.

## Testing

`test/` holds 11 independent test files, each with its own `main()`, which can be compiled and run separately (when the host has gcc):

```bash
for t in test/test_*.c; do
    gcc -std=c99 -Wall -Wextra -Werror -Iinclude "$t" src/elib_timer_core.c -o "${t%.c}.exe"
    "./${t%.c}.exe"
done
```

| File | Coverage |
|------|------|
| `test_init.c` | init/deinit, invalid configurations, reinitialization, all APIs rejected after deinit |
| `test_create.c` | parameter validation, ID order, pool full, invalid exec_mode, slot reuse |
| `test_start_stop.c` | start/stop, full reset, EXPIRED restart, double start |
| `test_pause.c` | pause freezes, resume continues, state gating |
| `test_set_period.c` | scaling differences across the four states, 0 clamped to 1, invalid parameters |
| `test_set_loop_count.c` | changing parameters while running, infinite <-> finite conversion, one-shot semantics |
| `test_expire_immediate.c` | expiry boundaries, looping N times, missed ticks, self-delete/self-stop/self-period-change inside the callback |
| `test_expire_delayed.c` | pending set/drain, pending+delete race, reuse without stale flags, mixed modes |
| `test_delete.c` | deletion, out-of-range ID, deletion while running, clean reuse |
| `test_get_remaining.c` | remaining values in the four states, parameter validation |
| `test_manager.c` | 0 milliseconds, equal boundary, batched accumulation, only RUNNING is driven, multi-slot batch tick |

## License

MIT License (see `LICENSE`).
