#pragma once

#ifdef __KERNEL__
    #include <lethe/kht_erl.h>
    #include <sys/debug.h>
    #include <sys/mutex.h>

    typedef kmutex_t lethe_mutex_t;
    #define lethe_mutex_init(m)    mutex_init((m), NULL, MUTEX_DEFAULT, NULL)
    #define lethe_mutex_destroy(m) mutex_destroy(m)
    #define lethe_mutex_enter(m)   mutex_enter(m)
    #define lethe_mutex_exit(m)    mutex_exit(m)
#else
    #include <lethe/kht_erl.h>
    #include <pthread.h>

    typedef pthread_mutex_t lethe_mutex_t;
    #define lethe_mutex_init(m)    pthread_mutex_init((m), NULL)
    #define lethe_mutex_destroy(m) pthread_mutex_destroy(m)
    #define lethe_mutex_enter(m)   pthread_mutex_lock(m)
    #define lethe_mutex_exit(m)    pthread_mutex_unlock(m)
#endif

// A heap-allocated ERL with a stable address and its own content lock.
// Containers store ErlBox pointers: node splits move the pointer, never
// the box, so the embedded mutex is safe and outstanding references
// survive container churn (under the structure lock's protection).
struct ErlBox {
    struct Erl erl;
    lethe_mutex_t lock;
};

struct ErlBox *erlbox_new(struct Erl erl);

void erlbox_drop(struct ErlBox *self);
