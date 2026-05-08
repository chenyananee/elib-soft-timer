/* elib_timer_err.h - Software Timer Error Codes */
#ifndef ELIB_TIMER_ERR_H
#define ELIB_TIMER_ERR_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    ELIB_TIMER_OK = 0,
    ELIB_TIMER_ERR_INVALID_PARAM,      /* Invalid parameter */
    ELIB_TIMER_ERR_NOT_INITIALIZED,    /* Not initialized */
    ELIB_TIMER_ERR_NOT_FOUND,          /* Timer not found */
    ELIB_TIMER_ERR_FULL,               /* Timer pool full */
    ELIB_TIMER_ERR_ALREADY_RUNNING,    /* Timer already running */
    ELIB_TIMER_ERR_NOT_RUNNING,        /* Timer not running */
    ELIB_TIMER_ERR_EXECUTION_FAILED,   /* Callback execution failed */
} elib_timer_err_t;

#ifdef __cplusplus
}
#endif

#endif /* ELIB_TIMER_ERR_H */