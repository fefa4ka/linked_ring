#include <lr.h> // include header for Linked Ring library
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h> // For usleep


#define log_print(type, message, ...)                                          \
    printf(type "\t" message "\n", ##__VA_ARGS__)
#define log_debug(type, message, ...)                                          \
    log_print(type, message " (%s:%d)\n", ##__VA_ARGS__, __FILE__, __LINE__)
#define log_verbose(message, ...) log_print("VERBOSE", message, ##__VA_ARGS__)
#define log_info(message, ...)    log_print("INFO", message, ##__VA_ARGS__)
#define log_ok(message, ...)      log_print("OK", message, ##__VA_ARGS__)
#define log_error(message, ...)                                                \
    log_print("\e[1m\e[31mERROR\e[39m\e[0m", message " (%s:%d)\n",             \
              ##__VA_ARGS__, __FILE__, __LINE__)

#define test_assert(test, message, ...)                                        \
    if (!(test)) {                                                             \
        log_error(message, ##__VA_ARGS__);                                     \
        return LR_ERROR_UNKNOWN;                                               \
    } else {                                                                   \
        log_ok(message, ##__VA_ARGS__);                                        \
    }

struct linked_ring buffer; // declare a buffer for the Linked Ring
pthread_mutex_t    pthread_mutex;
lr_owner_t         bare_metal_mutex;

/**
 * Implementation of lock using the linux pthread library
 */
lr_result_t pthread_lock(void *state)
{
    pthread_mutex_t *mutex = (pthread_mutex_t *)state;
    if (pthread_mutex_lock(mutex) == 0) {
        return LR_OK;
    }
    return LR_ERROR_LOCK;
}

/**
 * Implementation of unlock using the linux pthread library
 */
lr_result_t pthread_unlock(void *state)
{
    pthread_mutex_t *mutex = (pthread_mutex_t *)state;

    pthread_mutex_unlock(mutex);

    return 0;
}

/**
 * Implementation of lock using atomics
 */
lr_result_t bare_metal_lock(void *state)
{
    lr_owner_t *mutex = (lr_owner_t *)state;
    lr_owner_t expected = UINTPTR_MAX; // Unlocked state
    lr_owner_t desired = 0;            // Locked state
    
    // Try to atomically change from unlocked (UINTPTR_MAX) to locked (0)
    if (__atomic_compare_exchange_n(mutex, &expected, desired, false,
                                   __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)) {
        return LR_OK;
    }
    
    // If we couldn't acquire the lock immediately, return error
    return LR_ERROR_LOCK;
}

/**
 * Implementation of unlock using atomics
 */
lr_result_t bare_metal_unlock(void *state)
{
    lr_owner_t *mutex = (lr_owner_t *)state;
    lr_owner_t current = 0;
    lr_owner_t desired = UINTPTR_MAX;  // Unlocked state
    
    // Only unlock if we currently own the lock (value is 0)
    if (__atomic_compare_exchange_n(mutex, &current, desired, false,
                                   __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)) {
        return LR_OK;
    }
    
    // If we couldn't unlock (wasn't locked or locked by someone else)
    return LR_ERROR_UNLOCK;
}

// function to initialize the linked ring buffer
lr_result_t init_buffer(int buffer_size)
{
    // create an array of lr_cell for the buffer
    struct lr_cell *cells = calloc(buffer_size, sizeof(struct lr_cell));

    // initialize the buffer
    lr_result_t result = lr_init(&buffer, buffer_size, cells);
    if (result != LR_OK) {
        log_error("Failed to initialize buffer");
        return LR_ERROR_UNKNOWN;
    }

    return LR_OK;
}

void *put_get_data(void *data_in)
{
    unsigned int   owner = *((unsigned int *)data_in);
    lr_data_t      data  = lr_data(data_in);
    enum lr_result result;
    int retry_count = 0;
    const int max_retries = 5;
    
    // Try to put data with limited retries
    do {
        result = lr_put(&buffer, data, owner);
        if (result == LR_OK) {
            log_info("Thread %d: Put data %lu to owner %d", (int)pthread_self(), data, owner);
            break;
        } else if (result == LR_ERROR_LOCK) {
            // If we couldn't get the lock, sleep a bit and retry
            usleep(10000); // 10ms
        }
        retry_count++;
    } while (result != LR_ERROR_BUFFER_FULL && retry_count < max_retries);
    
    if (result != LR_OK) {
        log_error("Thread %d: Failed to put data after %d attempts, error: %d", 
                 (int)pthread_self(), retry_count, result);
        pthread_exit((void *)result);
    }
    
    // Reset retry counter for get operation
    retry_count = 0;
    lr_data_t read;
    
    // Try to get data with limited retries
    do {
        result = lr_get(&buffer, &read, owner);
        if (result == LR_OK) {
            log_info("Thread %d: Got data %lu from owner %d", (int)pthread_self(), read, owner);
            break;
        } else if (result == LR_ERROR_LOCK) {
            // If we couldn't get the lock, sleep a bit and retry
            usleep(10000); // 10ms
        }
        retry_count++;
    } while (retry_count < max_retries);
    
    if (result != LR_OK) {
        log_error("Thread %d: Error while getting data from buffer: %d", 
                 (int)pthread_self(), result);
    } else if (read != data) {
        log_error("Thread %d: Data does not match %lu != %lu", 
                 (int)pthread_self(), read, data);
        result = LR_ERROR_UNKNOWN;
    }
    
    pthread_exit((void *)result);
}

/** Invokes `num_threads` threads that each run `func`
 *  Returns LR_OK on success.
 *  If at least one of the threads reported an error, then this function returns
 * the error of the thread that reported an error and that was joined last.
 */
lr_result_t test_multiple_threads_with_pthread_mutex(unsigned int num_threads,
                                  void *(*func)(void *))
{
    // use less cells than threads to force contention
    unsigned int buffer_size = (num_threads > 1) ? num_threads : 1;
    test_assert(init_buffer(buffer_size) == LR_OK, "Buffer created");
    pthread_t    threads[num_threads];
    unsigned int owners[num_threads];

    log_info("Using pthread mutex");

    // test using pthread lock and unlock
    pthread_mutexattr_t pthread_attr;
    pthread_mutexattr_init(&pthread_attr);
    pthread_mutexattr_settype(&pthread_attr, PTHREAD_MUTEX_ERRORCHECK);
    pthread_mutex_init(&pthread_mutex, &pthread_attr);
    struct lr_mutex_attr attr;
    attr.lock   = pthread_lock;
    attr.unlock = pthread_unlock;
    attr.state  = (void *)&pthread_mutex;
    lr_set_mutex(&buffer, &attr);

    for (unsigned int i = 0; i < num_threads; i++) {
        owners[i] = i;
        int ret   = pthread_create(&threads[i], NULL, func, (void *)&owners[i]);
        test_assert(ret == 0, "Thread created");
    }

    enum lr_result result = LR_OK;
    for (unsigned int i = 0; i < num_threads; i++) {
        void *ret;
        pthread_join(threads[i], &ret);
        enum lr_result ret_as_result = (enum lr_result)ret;
        result = (ret_as_result == LR_OK) ? result : ret_as_result;
    }
    return result;
}


lr_result_t test_multiple_threads_with_bare_metal_mutex(unsigned int num_threads,
                                  void *(*func)(void *))
{
    // use less cells than threads to force contention
    unsigned int buffer_size = (num_threads > 1) ? num_threads : 1;
    test_assert(init_buffer(buffer_size) == LR_OK, "Buffer created");
    pthread_t    threads[num_threads];
    unsigned int owners[num_threads];

    struct lr_mutex_attr attr;
    enum lr_result result = LR_OK;

    log_info("Using atomics");

    // Initialize mutex to unlocked state
    bare_metal_mutex = UINTPTR_MAX;
    
    // Set up mutex functions
    attr.lock        = bare_metal_lock;
    attr.unlock      = bare_metal_unlock;
    attr.state       = (void *)&bare_metal_mutex;
    lr_set_mutex(&buffer, &attr);

    // Create threads
    for (unsigned int i = 0; i < num_threads; i++) {
        owners[i] = i;
        int ret = pthread_create(&threads[i], NULL, func, (void *)&owners[i]);
        test_assert(ret == 0, "Thread %u created", i);
    }

    // Wait for all threads to complete
    for (unsigned int i = 0; i < num_threads; i++) {
        void *ret;
        pthread_join(threads[i], &ret);
        enum lr_result ret_as_result = (enum lr_result)ret;
        if (ret_as_result != LR_OK) {
            log_error("Thread %u returned error: %d", i, ret_as_result);
            result = ret_as_result;
        }
    }

    return result;
}

int main(int argc, char **argv)
{
    // run the test function
    enum lr_result result = test_multiple_threads_with_pthread_mutex(2, put_get_data);
    
    if (result == LR_OK) {
        result = test_multiple_threads_with_bare_metal_mutex(2, put_get_data);
    }

    if (result == LR_OK) {
        log_ok("All tests passed");
    } else {
        log_error("Test failed");
    }
    return 0;
}
