#include <lr.h> // include header for Linked Ring library
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>


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
pthread_mutex_t mutex;

enum lr_result lock(void *state, lr_owner_t owner) 
{
    pthread_mutex_t *mutex = (pthread_mutex_t *) state;
    if (pthread_mutex_lock(mutex) == 0)
    {
        return LR_OK; 
    }
    return LR_ERROR_UNKNOWN;
}

lr_result_t unlock(void *state)
{
    pthread_mutex_t *mutex = (pthread_mutex_t *) state;
    if (pthread_mutex_unlock(mutex) == 0)
    {
        return LR_OK;
    }
    return LR_ERROR_UNKNOWN;
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


    struct lr_mutex_attr attr;
    attr.lock = lock;
    attr.unlock = unlock;    
    attr.state = (void *) &mutex;
    lr_set_mutex(&buffer, &attr);

    return LR_OK;
}

void *put_get_data(void *data_in)
{
    unsigned int owner = *((unsigned int *) data_in);
    lr_data_t data = (lr_data_t) data_in;
    enum lr_result result;
    do 
    {
        result = lr_put(&buffer, (lr_data_t) data, owner);
    } while(result != LR_ERROR_BUFFER_FULL && result != LR_OK);
    lr_data_t read;
    result = lr_get(&buffer, &read, owner);
    if (result != LR_OK)
    {
        log_error("Error while getting data from buffer");
        pthread_exit((void *) result);
    }
    if (read != data)
    {
        log_error("Data does not match");
        result = LR_ERROR_UNKNOWN;
        pthread_exit((void *) result);
    }
    else
    {
        pthread_exit((void *) result);
    }
}

/** Invokes `num_threads` threads that each run `func`
 *  Returns LR_OK on success.
 *  If at least one of the threads reported an error, then this function returns the error of the thread that reported an error and that was joined last.
 */
lr_result_t test_multiple_threads(unsigned int num_threads, void *(*func)(void*))
{
    // use less cells than threads to force contention
    unsigned int buffer_size = (num_threads > 1) ? num_threads / 2 : 1;
    test_assert(init_buffer(buffer_size) == LR_OK, "Failed to initialize buffer");
    pthread_t threads[num_threads];
    unsigned int owners[num_threads];
    for (unsigned int i = 0; i < num_threads; i++)
    {
        owners[i] = i;
        int ret = pthread_create(&threads[i], NULL, func, (void *) &i);
        test_assert(ret == 0, "Failed to create thread");
    }

    enum lr_result result = LR_OK;
    for (unsigned int i = 0; i < num_threads; i++)
    {
        void *ret;
        pthread_join(threads[i], &ret);
        enum lr_result ret_as_result = (enum lr_result) ret;
        result = (ret_as_result == LR_OK) ?  result : ret_as_result;
    }
    return result;
}

int main(int argc, char **argv)
{
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&mutex, &attr);
    // run the test function
    enum lr_result result = test_multiple_threads(2, put_get_data);
    if (result == LR_OK) {
        log_ok("All tests passed");
    } else {
        log_error("Test failed");
    }
    return 0;
}