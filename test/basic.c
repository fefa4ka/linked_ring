#include <lr.h> // include header for Linked Ring library
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


// used to mock lock and unlock for single-threaded tests
enum lr_result always_return_ok() 
{
    return LR_OK;
}

int main()
{

    struct lr_recursive_mutex mutex;
    mutex.lock = always_return_ok;
    mutex.unlock = always_return_ok;

    
    // allocate memory for the cells in the Linked Ring buffer
    lr_result_t     result;
    unsigned int    size  = 10;
    struct lr_cell *cells = malloc(size * sizeof(struct lr_cell));

    // Test lr_init() with size 0
    result = lr_init(&buffer, 0, cells, mutex);
    test_assert(result == LR_ERROR_NOMEMORY, "Buffer could not be 0 size");

    // Test lr_init() with size
    result = lr_init(&buffer, size, cells, mutex);
    test_assert(result == LR_OK, "Buffer with size %d should be initialized",
                size);


    // Test lr_put(): Test that the lr_put() function correctly adds elements to
    // the linked ring buffer.
    lr_data_t  data  = 123;
    lr_owner_t owner = 456;
    bool exists;
    unsigned int count;
    lr_data_t obtain_data;


    // Test lr_exists(): Test that the lr_exists() function correctly checks if
    // an element with a specific owner exists in the linked ring buffer.
    exists = lr_exists(&buffer, owner);
    test_assert(exists == false,
                "Element with owner %d should not exist in the buffer", owner);

    // Test lr_get(): Test that lr_get() returns an error when the buffer is
    // empty.
    result = lr_get(&buffer, &obtain_data, owner);
    test_assert(result == LR_ERROR_BUFFER_EMPTY,
                "lr_get() should return an error when the buffer is empty");

    // Fill the buffer completely with elements
    for (unsigned int i = 1; i <= size; ++i) {
        result = lr_put(&buffer, i * 2, i);
        test_assert(result == LR_OK,
                    "Data %d with owner %d should be added to the buffer", i * 2,
                    i);
    }

    // Test lr_count(): Test that the lr_count() function correctly returns the
    // number of elements in the linked ring buffer.
    count = lr_count(&buffer);
    test_assert(count == size, "Buffer should be full, count should be %d but %d",
                size, count);


    // Test lr_put(): Test that the lr_put() function returns an error when the
    // buffer is full.
    result = lr_put(&buffer, data, owner);
    test_assert(result == LR_ERROR_BUFFER_FULL,
                "lr_put() should return an error when the buffer is full");

    // Test lr_get(): Test that the lr_get() function correctly removes elements
    // from the linked ring buffer.
    result = lr_get(&buffer, &obtain_data, 2);
    test_assert(result == LR_OK && 4 == obtain_data,
                "Data %d with owner %d should be removed from the buffer, but data is %d", data,
                owner, obtain_data);


    // Test lr_count(): Test that the lr_count() function correctly returns the
    // number of elements in the linked ring buffer.
    count = lr_count(&buffer);
    test_assert(count == 9, "Buffer count should be contain 9 elements");

    // Test lr_put(): Test that the lr_put() function returns an error when the
    // buffer is full.
    result = lr_put(&buffer, data, owner);
    test_assert(result == LR_OK,
                "lr_put() should added when single element available");

    // Test lr_put(): Test that the lr_put() function returns an error when the
    // buffer is full.
    result = lr_put(&buffer, data, owner);
    test_assert(result == LR_ERROR_BUFFER_FULL,
                "lr_put() should return an error when the buffer is full");


    // Test lr_exists(): Test that the lr_exists() function correctly checks if
    // an element with a specific owner exists in the linked ring buffer.
    exists = lr_exists(&buffer, owner);
    test_assert(exists, "Element with owner %d should exist in the buffer",
                owner);

    // Test lr_get(): Test that the lr_get() function correctly removes elements
    // from the linked ring buffer.
    result = lr_get(&buffer, &obtain_data, owner);
    test_assert(result == LR_OK && obtain_data == data,
                "Data %d with owner %d should be removed from the buffer, but data is %d", data,
                owner, obtain_data);

    result = lr_put(&buffer, data, owner);
    test_assert(result == LR_OK,
                "lr_put() should ok");


    lr_dump(&buffer);
    free(cells); // free memory for cells in the buffer

    return LR_OK;
}
