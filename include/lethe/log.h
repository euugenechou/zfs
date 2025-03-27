#pragma once

#if defined(__KERNEL__) && defined(DEBUG)
    #include <linux/printk.h>
    #include <linux/string.h>

    #define __BASENAME__ (strrchr(__FILE__, '/') ? strrchr(__FILE__, '/') + 1 : NULL)

    #define lethe_log(fn, level, ...)                                                              \
        do {                                                                                       \
            fn("lethe: [" level ":%s:%d]\t%s(): ", __BASENAME__, __LINE__, __func__);              \
            pr_cont(__VA_ARGS__);                                                                  \
        } while (0)                                                                                \

    #define lethe_info(...) lethe_log(pr_info, "info", __VA_ARGS__)
    #define lethe_warn(...) lethe_log(pr_warn, "warn", __VA_ARGS__)
    #define lethe_error(...) lethe_log(pr_error, "error", __VA_ARGS__)
    #define lethe_cont(...) pr_cont(__VA_ARGS__)
#else
    #define lethe_log(...) ((void)0)
    #define lethe_info(...) ((void)0)
    #define lethe_warn(...) ((void)0)
    #define lethe_error(...) ((void)0)
    #define lethe_cont(...) ((void)0)
#endif

#define lethe_rw_enter(lock, type) \
    do { \
        lethe_info("locking %p from thread %p\n", (void *)lock, (void *)current); \
        rw_enter(lock, type); \
    } while (0)

#define lethe_rw_exit(lock) \
    do { \
        lethe_info("unlocking %p from thread %p\n", (void *)lock, (void *)current); \
        rw_exit(lock); \
    } while (0)
