#include <lr.h> // include header for Linked Ring library
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h> // For strcmp
#include <unistd.h> // For usleep
#include <time.h>   // For timing statistics


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

// Test operation types
typedef enum {
    OP_PUT_GET,       // Standard put followed by get
    OP_PUT_ONLY,      // Only put operations
    OP_GET_ONLY,      // Only get operations
    OP_MIXED,         // Random mix of puts and gets
    OP_PUSH_POP,      // Push followed by pop
    OP_RAPID_CYCLE,   // Rapid cycling between operations
    OP_DATA_INTEGRITY // Test with data integrity checks
} test_operation_t;

// Statistics structure to track thread performance
typedef struct {
    size_t total_operations;
    size_t successful_puts;
    size_t successful_gets;
    size_t successful_pushes;
    size_t successful_pops;
    size_t failed_puts;
    size_t failed_gets;
    size_t failed_pushes;
    size_t failed_pops;
    size_t lock_contentions;
    double avg_put_time_ms;
    double avg_get_time_ms;
    double avg_push_time_ms;
    double avg_pop_time_ms;
    double total_runtime_ms;
    double max_wait_time_ms;
    size_t buffer_full_count;
    size_t buffer_empty_count;
} thread_stats_t;

// Test configuration
typedef struct {
    test_operation_t operation;
    unsigned int num_threads;
    unsigned int buffer_size;
    unsigned int iterations;
    unsigned int sleep_interval_us;
    bool verbose_output;
} test_config_t;

// Global variables
thread_stats_t *thread_stats = NULL;
test_config_t test_config = {
    .operation = OP_PUT_GET,
    .num_threads = 2,
    .buffer_size = 0,  // Will be set based on num_threads
    .iterations = 1,
    .sleep_interval_us = 10000,
    .verbose_output = false
};

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

// Helper function to measure time between operations
double measure_time_ms(struct timespec *start) {
    struct timespec end;
    clock_gettime(CLOCK_MONOTONIC, &end);
    return (end.tv_sec - start->tv_sec) * 1000.0 + 
           (end.tv_nsec - start->tv_nsec) / 1000000.0;
}

// Helper function to update thread statistics for put operations
void update_put_stats(unsigned int owner, double time_ms) {
    thread_stats[owner].avg_put_time_ms = 
        (thread_stats[owner].avg_put_time_ms * (thread_stats[owner].successful_puts - 1) + time_ms) / 
        thread_stats[owner].successful_puts;
    
    if (time_ms > thread_stats[owner].max_wait_time_ms) {
        thread_stats[owner].max_wait_time_ms = time_ms;
    }
}

// Helper function to update thread statistics for get operations
void update_get_stats(unsigned int owner, double time_ms) {
    thread_stats[owner].avg_get_time_ms = 
        (thread_stats[owner].avg_get_time_ms * (thread_stats[owner].successful_gets - 1) + time_ms) / 
        thread_stats[owner].successful_gets;
    
    if (time_ms > thread_stats[owner].max_wait_time_ms) {
        thread_stats[owner].max_wait_time_ms = time_ms;
    }
}

// Helper function to update thread statistics for push operations
void update_push_stats(unsigned int owner, double time_ms) {
    thread_stats[owner].avg_push_time_ms = 
        (thread_stats[owner].avg_push_time_ms * (thread_stats[owner].successful_pushes - 1) + time_ms) / 
        thread_stats[owner].successful_pushes;
    
    if (time_ms > thread_stats[owner].max_wait_time_ms) {
        thread_stats[owner].max_wait_time_ms = time_ms;
    }
}

// Helper function to update thread statistics for pop operations
void update_pop_stats(unsigned int owner, double time_ms) {
    thread_stats[owner].avg_pop_time_ms = 
        (thread_stats[owner].avg_pop_time_ms * (thread_stats[owner].successful_pops - 1) + time_ms) / 
        thread_stats[owner].successful_pops;
    
    if (time_ms > thread_stats[owner].max_wait_time_ms) {
        thread_stats[owner].max_wait_time_ms = time_ms;
    }
}

// Standard put-get test function
void *put_get_data(void *data_in)
{
    unsigned int   owner = *((unsigned int *)data_in);
    lr_data_t      data  = lr_data(data_in);
    enum lr_result result;
    int retry_count = 0;
    const int max_retries = 5;
    
    // For timing statistics
    struct timespec start_time;
    double operation_time_ms = 0;
    
    // Initialize thread statistics
    thread_stats[owner].total_operations++;
    
    // Try to put data with limited retries
    clock_gettime(CLOCK_MONOTONIC, &start_time);
    do {
        result = lr_put(&buffer, data, owner);
        if (result == LR_OK) {
            if (test_config.verbose_output) {
                log_info("Thread %d: Put data %lu to owner %d", (int)pthread_self(), data, owner);
            }
            thread_stats[owner].successful_puts++;
            break;
        } else if (result == LR_ERROR_LOCK) {
            // If we couldn't get the lock, sleep a bit and retry
            usleep(test_config.sleep_interval_us);
            thread_stats[owner].lock_contentions++;
        } else if (result == LR_ERROR_BUFFER_FULL) {
            thread_stats[owner].buffer_full_count++;
        }
        retry_count++;
    } while (result != LR_ERROR_BUFFER_FULL && retry_count < max_retries);
    
    operation_time_ms = measure_time_ms(&start_time);
    update_put_stats(owner, operation_time_ms);
    
    if (result != LR_OK) {
        log_error("Thread %d: Failed to put data after %d attempts, error: %d", 
                 (int)pthread_self(), retry_count, result);
        thread_stats[owner].failed_puts++;
        pthread_exit((void *)result);
    }
    
    // Reset retry counter for get operation
    retry_count = 0;
    lr_data_t read;
    
    // Try to get data with limited retries
    clock_gettime(CLOCK_MONOTONIC, &start_time);
    do {
        result = lr_get(&buffer, &read, owner);
        if (result == LR_OK) {
            if (test_config.verbose_output) {
                log_info("Thread %d: Got data %lu from owner %d", (int)pthread_self(), read, owner);
            }
            thread_stats[owner].successful_gets++;
            break;
        } else if (result == LR_ERROR_LOCK) {
            // If we couldn't get the lock, sleep a bit and retry
            usleep(test_config.sleep_interval_us);
            thread_stats[owner].lock_contentions++;
        } else if (result == LR_ERROR_BUFFER_EMPTY) {
            thread_stats[owner].buffer_empty_count++;
        }
        retry_count++;
    } while (retry_count < max_retries);
    
    operation_time_ms = measure_time_ms(&start_time);
    update_get_stats(owner, operation_time_ms);
    
    if (result != LR_OK) {
        log_error("Thread %d: Error while getting data from buffer: %d", 
                 (int)pthread_self(), result);
        thread_stats[owner].failed_gets++;
    } else if (read != data) {
        log_error("Thread %d: Data does not match %lu != %lu", 
                 (int)pthread_self(), read, data);
        result = LR_ERROR_UNKNOWN;
    }
    
    pthread_exit((void *)result);
}

// Put-only test function
void *put_only_data(void *data_in)
{
    unsigned int owner = *((unsigned int *)data_in);
    enum lr_result result;
    int retry_count = 0;
    const int max_retries = 5;
    struct timespec start_time;
    double operation_time_ms = 0;
    
    thread_stats[owner].total_operations++;
    
    // Generate unique data for this thread and iteration
    lr_data_t data = (owner + 1) * 1000 + (uintptr_t)pthread_self() % 1000;
    
    clock_gettime(CLOCK_MONOTONIC, &start_time);
    do {
        result = lr_put(&buffer, data, owner);
        if (result == LR_OK) {
            if (test_config.verbose_output) {
                log_info("Thread %d: Put data %lu to owner %d", (int)pthread_self(), data, owner);
            }
            thread_stats[owner].successful_puts++;
            break;
        } else if (result == LR_ERROR_LOCK) {
            usleep(test_config.sleep_interval_us);
            thread_stats[owner].lock_contentions++;
        } else if (result == LR_ERROR_BUFFER_FULL) {
            thread_stats[owner].buffer_full_count++;
        }
        retry_count++;
    } while (result != LR_ERROR_BUFFER_FULL && retry_count < max_retries);
    
    operation_time_ms = measure_time_ms(&start_time);
    update_put_stats(owner, operation_time_ms);
    
    if (result != LR_OK) {
        if (result == LR_ERROR_BUFFER_FULL) {
            // This is expected in put-only test
            thread_stats[owner].failed_puts++;
            return (void *)LR_OK;
        }
        log_error("Thread %d: Failed to put data, error: %d", (int)pthread_self(), result);
        thread_stats[owner].failed_puts++;
    }
    
    pthread_exit((void *)result);
}

// Get-only test function
void *get_only_data(void *data_in)
{
    unsigned int owner = *((unsigned int *)data_in);
    enum lr_result result;
    int retry_count = 0;
    const int max_retries = 5;
    struct timespec start_time;
    double operation_time_ms = 0;
    lr_data_t read;
    
    thread_stats[owner].total_operations++;
    
    clock_gettime(CLOCK_MONOTONIC, &start_time);
    do {
        result = lr_get(&buffer, &read, owner);
        if (result == LR_OK) {
            if (test_config.verbose_output) {
                log_info("Thread %d: Got data %lu from owner %d", (int)pthread_self(), read, owner);
            }
            thread_stats[owner].successful_gets++;
            break;
        } else if (result == LR_ERROR_LOCK) {
            usleep(test_config.sleep_interval_us);
            thread_stats[owner].lock_contentions++;
        } else if (result == LR_ERROR_BUFFER_EMPTY) {
            thread_stats[owner].buffer_empty_count++;
        }
        retry_count++;
    } while (retry_count < max_retries);
    
    operation_time_ms = measure_time_ms(&start_time);
    update_get_stats(owner, operation_time_ms);
    
    if (result != LR_OK) {
        if (result == LR_ERROR_BUFFER_EMPTY) {
            // This is expected in get-only test
            thread_stats[owner].failed_gets++;
            return (void *)LR_OK;
        }
        log_error("Thread %d: Failed to get data, error: %d", (int)pthread_self(), result);
        thread_stats[owner].failed_gets++;
    }
    
    pthread_exit((void *)result);
}

// Mixed operations test function
void *mixed_operations(void *data_in)
{
    unsigned int owner = *((unsigned int *)data_in);
    enum lr_result result;
    int retry_count = 0;
    const int max_retries = 5;
    struct timespec start_time;
    double operation_time_ms = 0;
    lr_data_t read;
    
    for (unsigned int i = 0; i < test_config.iterations; i++) {
        thread_stats[owner].total_operations++;
        
        // Randomly choose between put and get
        bool do_put = (rand() % 2 == 0);
        
        if (do_put) {
            // Generate unique data
            lr_data_t data = (owner + 1) * 1000 + i;
            
            clock_gettime(CLOCK_MONOTONIC, &start_time);
            retry_count = 0;
            do {
                result = lr_put(&buffer, data, owner);
                if (result == LR_OK) {
                    if (test_config.verbose_output) {
                        log_info("Thread %d: Put data %lu to owner %d", (int)pthread_self(), data, owner);
                    }
                    thread_stats[owner].successful_puts++;
                    break;
                } else if (result == LR_ERROR_LOCK) {
                    usleep(test_config.sleep_interval_us);
                    thread_stats[owner].lock_contentions++;
                } else if (result == LR_ERROR_BUFFER_FULL) {
                    thread_stats[owner].buffer_full_count++;
                }
                retry_count++;
            } while (result != LR_ERROR_BUFFER_FULL && retry_count < max_retries);
            
            operation_time_ms = measure_time_ms(&start_time);
            if (result == LR_OK) {
                update_put_stats(owner, operation_time_ms);
            } else {
                thread_stats[owner].failed_puts++;
            }
        } else {
            clock_gettime(CLOCK_MONOTONIC, &start_time);
            retry_count = 0;
            do {
                result = lr_get(&buffer, &read, owner);
                if (result == LR_OK) {
                    if (test_config.verbose_output) {
                        log_info("Thread %d: Got data %lu from owner %d", (int)pthread_self(), read, owner);
                    }
                    thread_stats[owner].successful_gets++;
                    break;
                } else if (result == LR_ERROR_LOCK) {
                    usleep(test_config.sleep_interval_us);
                    thread_stats[owner].lock_contentions++;
                } else if (result == LR_ERROR_BUFFER_EMPTY) {
                    thread_stats[owner].buffer_empty_count++;
                }
                retry_count++;
            } while (retry_count < max_retries);
            
            operation_time_ms = measure_time_ms(&start_time);
            if (result == LR_OK) {
                update_get_stats(owner, operation_time_ms);
            } else {
                thread_stats[owner].failed_gets++;
            }
        }
        
        // Small random sleep to simulate real-world usage
        usleep(rand() % 1000);
    }
    
    pthread_exit((void *)LR_OK);
}

// Push-pop test function
void *push_pop_data(void *data_in)
{
    unsigned int owner = *((unsigned int *)data_in);
    lr_data_t data = lr_data(data_in);
    enum lr_result result;
    int retry_count = 0;
    const int max_retries = 5;
    struct timespec start_time;
    double operation_time_ms = 0;
    
    thread_stats[owner].total_operations++;
    
    // Try to push data
    clock_gettime(CLOCK_MONOTONIC, &start_time);
    do {
        result = lr_push(&buffer, data, owner);
        if (result == LR_OK) {
            if (test_config.verbose_output) {
                log_info("Thread %d: Pushed data %lu to owner %d", (int)pthread_self(), data, owner);
            }
            thread_stats[owner].successful_pushes++;
            break;
        } else if (result == LR_ERROR_LOCK) {
            usleep(test_config.sleep_interval_us);
            thread_stats[owner].lock_contentions++;
        } else if (result == LR_ERROR_BUFFER_FULL) {
            thread_stats[owner].buffer_full_count++;
        }
        retry_count++;
    } while (result != LR_ERROR_BUFFER_FULL && retry_count < max_retries);
    
    operation_time_ms = measure_time_ms(&start_time);
    update_push_stats(owner, operation_time_ms);
    
    if (result != LR_OK) {
        log_error("Thread %d: Failed to push data, error: %d", (int)pthread_self(), result);
        thread_stats[owner].failed_pushes++;
        pthread_exit((void *)result);
    }
    
    // Reset retry counter for pop operation
    retry_count = 0;
    lr_data_t read;
    
    // Try to pop data
    clock_gettime(CLOCK_MONOTONIC, &start_time);
    do {
        result = lr_pop(&buffer, &read, owner);
        if (result == LR_OK) {
            if (test_config.verbose_output) {
                log_info("Thread %d: Popped data %lu from owner %d", (int)pthread_self(), read, owner);
            }
            thread_stats[owner].successful_pops++;
            break;
        } else if (result == LR_ERROR_LOCK) {
            usleep(test_config.sleep_interval_us);
            thread_stats[owner].lock_contentions++;
        } else if (result == LR_ERROR_BUFFER_EMPTY) {
            thread_stats[owner].buffer_empty_count++;
        }
        retry_count++;
    } while (retry_count < max_retries);
    
    operation_time_ms = measure_time_ms(&start_time);
    update_pop_stats(owner, operation_time_ms);
    
    if (result != LR_OK) {
        log_error("Thread %d: Error while popping data from buffer: %d", (int)pthread_self(), result);
        thread_stats[owner].failed_pops++;
    } else if (read != data) {
        log_error("Thread %d: Data does not match %lu != %lu", (int)pthread_self(), read, data);
        result = LR_ERROR_UNKNOWN;
    }
    
    pthread_exit((void *)result);
}

// Data integrity test function
void *data_integrity_test(void *data_in)
{
    unsigned int owner = *((unsigned int *)data_in);
    enum lr_result result;
    struct timespec start_time;
    double operation_time_ms = 0;
    
    // Create a unique pattern for this thread that won't conflict with others
    // Each thread will only read/write its own pattern
    lr_data_t base_pattern = (owner + 1) * 10000;
    
    for (unsigned int i = 0; i < test_config.iterations; i++) {
        thread_stats[owner].total_operations++;
        
        // Generate unique data with sequence number
        lr_data_t data = base_pattern + i;
        
        // Put operation with retry
        int retry_count = 0;
        const int max_retries = 5;
        
        clock_gettime(CLOCK_MONOTONIC, &start_time);
        do {
            result = lr_put(&buffer, data, owner);
            if (result == LR_OK) {
                if (test_config.verbose_output) {
                    log_info("Thread %d: Put data %lu to owner %d", 
                            (int)pthread_self(), data, owner);
                }
                thread_stats[owner].successful_puts++;
                break;
            } else if (result == LR_ERROR_LOCK) {
                usleep(test_config.sleep_interval_us);
                thread_stats[owner].lock_contentions++;
            } else if (result == LR_ERROR_BUFFER_FULL) {
                thread_stats[owner].buffer_full_count++;
            }
            retry_count++;
        } while (result != LR_ERROR_BUFFER_FULL && retry_count < max_retries);
        
        operation_time_ms = measure_time_ms(&start_time);
        if (result == LR_OK) {
            update_put_stats(owner, operation_time_ms);
        } else {
            thread_stats[owner].failed_puts++;
            continue; // Skip get if put failed
        }
        
        // Small sleep to simulate real workload
        usleep(1000);
        
        // Get operation with retry
        retry_count = 0;
        lr_data_t read;
        
        clock_gettime(CLOCK_MONOTONIC, &start_time);
        do {
            result = lr_get(&buffer, &read, owner);
            if (result == LR_OK) {
                if (test_config.verbose_output) {
                    log_info("Thread %d: Got data %lu from owner %d", 
                            (int)pthread_self(), read, owner);
                }
                thread_stats[owner].successful_gets++;
                break;
            } else if (result == LR_ERROR_LOCK) {
                usleep(test_config.sleep_interval_us);
                thread_stats[owner].lock_contentions++;
            } else if (result == LR_ERROR_BUFFER_EMPTY) {
                thread_stats[owner].buffer_empty_count++;
            }
            retry_count++;
        } while (retry_count < max_retries);
        
        operation_time_ms = measure_time_ms(&start_time);
        if (result == LR_OK) {
            update_get_stats(owner, operation_time_ms);
            
            // Verify data integrity - should match our pattern
            if (read != data) {
                log_error("Thread %d: Data integrity error %lu != %lu", 
                         (int)pthread_self(), read, data);
                return (void *)LR_ERROR_UNKNOWN;
            }
        } else {
            thread_stats[owner].failed_gets++;
        }
    }
    
    return (void *)LR_OK;
}

// Rapid cycling test function
void *rapid_cycle_operations(void *data_in)
{
    unsigned int owner = *((unsigned int *)data_in);
    enum lr_result result;
    struct timespec start_time;
    double operation_time_ms = 0;
    
    for (unsigned int i = 0; i < test_config.iterations; i++) {
        thread_stats[owner].total_operations++;
        
        // Generate unique data
        lr_data_t data = (owner + 1) * 1000 + i;
        
        // Put operation
        clock_gettime(CLOCK_MONOTONIC, &start_time);
        result = lr_put(&buffer, data, owner);
        if (result == LR_OK) {
            thread_stats[owner].successful_puts++;
            operation_time_ms = measure_time_ms(&start_time);
            update_put_stats(owner, operation_time_ms);
        } else if (result == LR_ERROR_LOCK) {
            thread_stats[owner].lock_contentions++;
            thread_stats[owner].failed_puts++;
        } else if (result == LR_ERROR_BUFFER_FULL) {
            thread_stats[owner].buffer_full_count++;
            thread_stats[owner].failed_puts++;
        }
        
        // No sleep between operations to stress test
        
        // Get operation
        lr_data_t read;
        clock_gettime(CLOCK_MONOTONIC, &start_time);
        result = lr_get(&buffer, &read, owner);
        if (result == LR_OK) {
            thread_stats[owner].successful_gets++;
            operation_time_ms = measure_time_ms(&start_time);
            update_get_stats(owner, operation_time_ms);
            
            // In rapid cycle stress tests, we don't check data matching
            // because other threads might have modified the data between put and get
        } else if (result == LR_ERROR_LOCK) {
            thread_stats[owner].lock_contentions++;
            thread_stats[owner].failed_gets++;
        } else if (result == LR_ERROR_BUFFER_EMPTY) {
            thread_stats[owner].buffer_empty_count++;
            thread_stats[owner].failed_gets++;
        }
    }
    
    pthread_exit((void *)LR_OK);
}

// Get the appropriate test function based on operation type
void *(*get_test_function(test_operation_t operation))(void *) {
    switch (operation) {
        case OP_PUT_GET:
            return put_get_data;
        case OP_PUT_ONLY:
            return put_only_data;
        case OP_GET_ONLY:
            return get_only_data;
        case OP_MIXED:
            return mixed_operations;
        case OP_PUSH_POP:
            return push_pop_data;
        case OP_RAPID_CYCLE:
            return rapid_cycle_operations;
        case OP_DATA_INTEGRITY:
            return data_integrity_test;
        default:
            return put_get_data;
    }
}

// Get operation type name as string
const char* get_operation_name(test_operation_t operation) {
    switch (operation) {
        case OP_PUT_GET:
            return "Put-Get";
        case OP_PUT_ONLY:
            return "Put-Only";
        case OP_GET_ONLY:
            return "Get-Only";
        case OP_MIXED:
            return "Mixed";
        case OP_PUSH_POP:
            return "Push-Pop";
        case OP_RAPID_CYCLE:
            return "Rapid-Cycle";
        case OP_DATA_INTEGRITY:
            return "Data-Integrity";
        default:
            return "Unknown";
    }
}

// Print thread statistics
void print_thread_statistics(unsigned int num_threads, const char* mutex_type) {
    printf("\n┌─────────────────────────────────────────────────────────────────────────────────────────────┐\n");
    printf("│         \033[1mThread Statistics with %s (%s operations)\033[0m         │\n", 
           mutex_type, get_operation_name(test_config.operation));
    printf("├─────┬───────┬───────┬───────┬───────┬───────┬───────┬───────┬───────┬───────┬───────┬───────┤\n");
    printf("│ Thd │ Ops   │ Puts  │ Gets  │ Push  │ Pop   │ Fails │ Cont. │ Put ms│ Get ms│ Max ms│ Full  │\n");
    printf("├─────┼───────┼───────┼───────┼───────┼───────┼───────┼───────┼───────┼───────┼───────┼───────┤\n");
    
    size_t total_ops = 0, total_puts = 0, total_gets = 0, total_pushes = 0, total_pops = 0;
    size_t total_fails = 0, total_contentions = 0, total_buffer_full = 0;
    double avg_put_time = 0, avg_get_time = 0, max_wait_time = 0;
    
    for (unsigned int i = 0; i < num_threads; i++) {
        size_t fails = thread_stats[i].failed_puts + thread_stats[i].failed_gets + 
                      thread_stats[i].failed_pushes + thread_stats[i].failed_pops;
        
        printf("│ %3u │ %5zu │ %5zu │ %5zu │ %5zu │ %5zu │ %5zu │ %5zu │ %5.2f │ %5.2f │ %5.2f │ %5zu │\n", 
               i,
               thread_stats[i].total_operations,
               thread_stats[i].successful_puts,
               thread_stats[i].successful_gets,
               thread_stats[i].successful_pushes,
               thread_stats[i].successful_pops,
               fails,
               thread_stats[i].lock_contentions,
               thread_stats[i].avg_put_time_ms,
               thread_stats[i].avg_get_time_ms,
               thread_stats[i].max_wait_time_ms,
               thread_stats[i].buffer_full_count);
               
        total_ops += thread_stats[i].total_operations;
        total_puts += thread_stats[i].successful_puts;
        total_gets += thread_stats[i].successful_gets;
        total_pushes += thread_stats[i].successful_pushes;
        total_pops += thread_stats[i].successful_pops;
        total_fails += fails;
        total_contentions += thread_stats[i].lock_contentions;
        total_buffer_full += thread_stats[i].buffer_full_count;
        
        if (thread_stats[i].successful_puts > 0) {
            avg_put_time += thread_stats[i].avg_put_time_ms;
        }
        
        if (thread_stats[i].successful_gets > 0) {
            avg_get_time += thread_stats[i].avg_get_time_ms;
        }
        
        if (thread_stats[i].max_wait_time_ms > max_wait_time) {
            max_wait_time = thread_stats[i].max_wait_time_ms;
        }
    }
    
    // Calculate averages for threads that performed operations
    unsigned int put_threads = 0, get_threads = 0;
    for (unsigned int i = 0; i < num_threads; i++) {
        if (thread_stats[i].successful_puts > 0) put_threads++;
        if (thread_stats[i].successful_gets > 0) get_threads++;
    }
    
    if (put_threads > 0) avg_put_time /= put_threads;
    if (get_threads > 0) avg_get_time /= get_threads;
    
    printf("├─────┼───────┼───────┼───────┼───────┼───────┼───────┼───────┼───────┼───────┼───────┼───────┤\n");
    printf("│Total│ %5zu │ %5zu │ %5zu │ %5zu │ %5zu │ %5zu │ %5zu │ %5.2f │ %5.2f │ %5.2f │ %5zu │\n",
           total_ops, total_puts, total_gets, total_pushes, total_pops, 
           total_fails, total_contentions, avg_put_time, avg_get_time, max_wait_time, total_buffer_full);
    printf("└─────┴───────┴───────┴───────┴───────┴───────┴───────┴───────┴───────┴───────┴───────┴───────┘\n");
    
    // Print buffer statistics
    printf("\n┌───────────────────────────────────────────────────┐\n");
    printf("│              \033[1mBuffer Statistics\033[0m                    │\n");
    printf("├───────────────────────────────┬───────────────────┤\n");
    printf("│ Buffer size                   │ %17u │\n", test_config.buffer_size);
    printf("│ Success rate                  │ %16.2f%% │\n", 
           100.0 * (total_puts + total_gets + total_pushes + total_pops) / 
           (total_puts + total_gets + total_pushes + total_pops + total_fails));
    printf("│ Contention rate               │ %16.2f%% │\n", 
           100.0 * total_contentions / (total_ops > 0 ? total_ops : 1));
    printf("│ Buffer full rate              │ %16.2f%% │\n", 
           100.0 * total_buffer_full / (total_ops > 0 ? total_ops : 1));
    printf("└───────────────────────────────┴───────────────────┘\n");
}

/** Invokes `num_threads` threads that each run `func`
 *  Returns LR_OK on success.
 *  If at least one of the threads reported an error, then this function returns
 * the error of the thread that reported an error and that was joined last.
 */
lr_result_t test_multiple_threads_with_pthread_mutex(unsigned int num_threads,
                                  void *(*func)(void *))
{
    // use buffer size based on configuration or default to num_threads
    unsigned int buffer_size = test_config.buffer_size > 0 ? 
                              test_config.buffer_size : 
                              (num_threads > 1) ? num_threads : 1;
                              
    test_assert(init_buffer(buffer_size) == LR_OK, "Buffer created");
    pthread_t    threads[num_threads];
    unsigned int owners[num_threads];
    struct timespec start_time, end_time;

    log_info("Using pthread mutex with %s operations", get_operation_name(test_config.operation));

    // Initialize statistics
    thread_stats = calloc(num_threads, sizeof(thread_stats_t));
    if (!thread_stats) {
        log_error("Failed to allocate memory for thread statistics");
        return LR_ERROR_NOMEMORY;
    }

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

    // Start timing
    clock_gettime(CLOCK_MONOTONIC, &start_time);

    // Pre-fill buffer for get-only tests
    if (test_config.operation == OP_GET_ONLY) {
        log_info("Pre-filling buffer for get-only test");
        for (unsigned int i = 0; i < num_threads; i++) {
            for (unsigned int j = 0; j < test_config.iterations; j++) {
                lr_data_t data = (i + 1) * 1000 + j;
                lr_put(&buffer, data, i);
            }
        }
    }

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

    // End timing
    clock_gettime(CLOCK_MONOTONIC, &end_time);
    double total_time_ms = (end_time.tv_sec - start_time.tv_sec) * 1000.0 + 
                          (end_time.tv_nsec - start_time.tv_nsec) / 1000000.0;
    
    // Record total runtime in each thread's stats
    for (unsigned int i = 0; i < num_threads; i++) {
        thread_stats[i].total_runtime_ms = total_time_ms;
    }
    
    // Print statistics
    print_thread_statistics(num_threads, "pthread mutex");
    
    // Clean up
    free(thread_stats);
    thread_stats = NULL;
    
    return result;
}


lr_result_t test_multiple_threads_with_bare_metal_mutex(unsigned int num_threads,
                                  void *(*func)(void *))
{
    // use buffer size based on configuration or default to num_threads
    unsigned int buffer_size = test_config.buffer_size > 0 ? 
                              test_config.buffer_size : 
                              (num_threads > 1) ? num_threads : 1;
                              
    test_assert(init_buffer(buffer_size) == LR_OK, "Buffer created");
    pthread_t    threads[num_threads];
    unsigned int owners[num_threads];
    struct timespec start_time, end_time;

    struct lr_mutex_attr attr;
    enum lr_result result = LR_OK;

    log_info("Using atomics with %s operations", get_operation_name(test_config.operation));

    // Initialize statistics
    thread_stats = calloc(num_threads, sizeof(thread_stats_t));
    if (!thread_stats) {
        log_error("Failed to allocate memory for thread statistics");
        return LR_ERROR_NOMEMORY;
    }

    // Initialize mutex to unlocked state
    bare_metal_mutex = UINTPTR_MAX;
    
    // Set up mutex functions
    attr.lock        = bare_metal_lock;
    attr.unlock      = bare_metal_unlock;
    attr.state       = (void *)&bare_metal_mutex;
    lr_set_mutex(&buffer, &attr);

    // Start timing
    clock_gettime(CLOCK_MONOTONIC, &start_time);

    // Pre-fill buffer for get-only tests
    if (test_config.operation == OP_GET_ONLY) {
        log_info("Pre-filling buffer for get-only test");
        for (unsigned int i = 0; i < num_threads; i++) {
            for (unsigned int j = 0; j < test_config.iterations; j++) {
                lr_data_t data = (i + 1) * 1000 + j;
                lr_put(&buffer, data, i);
            }
        }
    }

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

    // End timing
    clock_gettime(CLOCK_MONOTONIC, &end_time);
    double total_time_ms = (end_time.tv_sec - start_time.tv_sec) * 1000.0 + 
                          (end_time.tv_nsec - start_time.tv_nsec) / 1000000.0;
    
    // Record total runtime in each thread's stats
    for (unsigned int i = 0; i < num_threads; i++) {
        thread_stats[i].total_runtime_ms = total_time_ms;
    }
    
    // Print statistics
    print_thread_statistics(num_threads, "atomic mutex");
    
    // Clean up
    free(thread_stats);
    thread_stats = NULL;

    return result;
}

// Print usage information
void print_usage(const char* program_name) {
    printf("Usage: %s [options]\n", program_name);
    printf("Options:\n");
    printf("  -t, --threads N       Number of threads to use (default: 2)\n");
    printf("  -b, --buffer-size N   Buffer size (default: same as thread count)\n");
    printf("  -i, --iterations N    Number of iterations per thread (default: 1)\n");
    printf("  -s, --sleep N         Sleep interval in microseconds (default: 10000)\n");
    printf("  -o, --operation TYPE  Operation type to test:\n");
    printf("                         0: Put-Get (default)\n");
    printf("                         1: Put-Only\n");
    printf("                         2: Get-Only\n");
    printf("                         3: Mixed\n");
    printf("                         4: Push-Pop\n");
    printf("                         5: Rapid-Cycle\n");
    printf("                         6: Data-Integrity\n");
    printf("  -v, --verbose         Enable verbose output\n");
    printf("  -h, --help            Display this help message\n");
    printf("\nExample: %s -t 4 -b 8 -i 10 -o 3\n", program_name);
}

// Parse command line arguments
void parse_args(int argc, char **argv) {
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-t") == 0 || strcmp(argv[i], "--threads") == 0) {
            if (i + 1 < argc) {
                test_config.num_threads = atoi(argv[++i]);
                if (test_config.num_threads < 1) {
                    test_config.num_threads = 2;
                }
            }
        } else if (strcmp(argv[i], "-b") == 0 || strcmp(argv[i], "--buffer-size") == 0) {
            if (i + 1 < argc) {
                test_config.buffer_size = atoi(argv[++i]);
            }
        } else if (strcmp(argv[i], "-i") == 0 || strcmp(argv[i], "--iterations") == 0) {
            if (i + 1 < argc) {
                test_config.iterations = atoi(argv[++i]);
                if (test_config.iterations < 1) {
                    test_config.iterations = 1;
                }
            }
        } else if (strcmp(argv[i], "-s") == 0 || strcmp(argv[i], "--sleep") == 0) {
            if (i + 1 < argc) {
                test_config.sleep_interval_us = atoi(argv[++i]);
            }
        } else if (strcmp(argv[i], "-o") == 0 || strcmp(argv[i], "--operation") == 0) {
            if (i + 1 < argc) {
                int op = atoi(argv[++i]);
                if (op >= 0 && op <= 6) {
                    test_config.operation = (test_operation_t)op;
                }
            }
        } else if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--verbose") == 0) {
            test_config.verbose_output = true;
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            exit(0);
        }
    }
}

// Run all test scenarios
lr_result_t run_all_test_scenarios(unsigned int num_threads) {
    enum lr_result result = LR_OK;
    test_operation_t original_op = test_config.operation;
    
    printf("\n┌───────────────────────────────────────────────────────────┐\n");
    printf("│               \033[1mRunning All Test Scenarios\033[0m                  │\n");
    printf("└───────────────────────────────────────────────────────────┘\n");
    
    // Run each operation type
    for (int op = OP_PUT_GET; op <= OP_RAPID_CYCLE; op++) {
        test_config.operation = (test_operation_t)op;
        printf("\n\033[1m=== Testing %s Operations ===\033[0m\n", get_operation_name(test_config.operation));
        
        // Run with pthread mutex
        result = test_multiple_threads_with_pthread_mutex(num_threads, get_test_function(test_config.operation));
        if (result != LR_OK) {
            log_error("Pthread mutex test failed with operation %s", get_operation_name(test_config.operation));
            break;
        }
        
        // Run with atomic mutex
        result = test_multiple_threads_with_bare_metal_mutex(num_threads, get_test_function(test_config.operation));
        if (result != LR_OK) {
            log_error("Atomic mutex test failed with operation %s", get_operation_name(test_config.operation));
            break;
        }
    }
    
    // Restore original operation
    test_config.operation = original_op;
    return result;
}

int main(int argc, char **argv)
{
    // Initialize random seed
    srand(time(NULL));
    
    // Parse command line arguments
    parse_args(argc, argv);
    
    printf("\n┌───────────────────────────────────────────────────────────┐\n");
    printf("│               \033[1mMulti-threaded Buffer Test\033[0m                  │\n");
    printf("└───────────────────────────────────────────────────────────┘\n");
    printf("Configuration:\n");
    printf("  Threads:    %u\n", test_config.num_threads);
    printf("  Buffer:     %u\n", test_config.buffer_size > 0 ? test_config.buffer_size : test_config.num_threads);
    printf("  Iterations: %u\n", test_config.iterations);
    printf("  Operation:  %s\n", get_operation_name(test_config.operation));
    
    enum lr_result result;
    
    // Check if we should run all test scenarios
    if (argc > 1 && (strcmp(argv[1], "all") == 0 || strcmp(argv[1], "--all") == 0)) {
        result = run_all_test_scenarios(test_config.num_threads);
    } else {
        // Run the specified test
        void *(*test_func)(void *) = get_test_function(test_config.operation);
        
        // Run with pthread mutex
        result = test_multiple_threads_with_pthread_mutex(test_config.num_threads, test_func);
        
        // Run with atomic mutex if pthread test passed
        if (result == LR_OK) {
            result = test_multiple_threads_with_bare_metal_mutex(test_config.num_threads, test_func);
        }
    }

    // Print final result
    if (result == LR_OK) {
        log_ok("All tests passed");
    } else {
        log_error("Test failed");
    }
    
    printf("\n┌───────────────────────────────────────────────────────────┐\n");
    printf("│               \033[1mTest Summary\033[0m                                │\n");
    printf("└───────────────────────────────────────────────────────────┘\n");
    printf("Threads:    %u\n", test_config.num_threads);
    printf("Buffer:     %u\n", test_config.buffer_size > 0 ? test_config.buffer_size : test_config.num_threads);
    printf("Iterations: %u\n", test_config.iterations);
    printf("Operation:  %s\n", get_operation_name(test_config.operation));
    printf("Result:     %s\n", result == LR_OK ? "\033[32mPASSED\033[0m" : "\033[31mFAILED\033[0m");
    
    return result == LR_OK ? 0 : 1;
}
