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

#define BUFFER_SIZE 4
struct linked_ring buffer; // declare a buffer for the Linked Ring
                           //
// define enum for owners
// owners have input and ouput stream
typedef enum {
    OWNER_SPI_IN,
    OWNER_SPI_OUT,
    OWNER_I2C_IN,
    OWNER_I2C_OUT,
    OWNER_UART_IN,
    OWNER_UART_OUT,
    NUM_OWNERS
} owner_t;

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

lr_result_t add_random_data()
{
    lr_result_t  result;
    unsigned int count;
    lr_owner_t   owner = lr_owner(rand() % NUM_OWNERS);
    lr_data_t    value = lr_data(rand() % 1000);
    count              = lr_count(&buffer);
    if (BUFFER_SIZE > count) {
        result = lr_put(&buffer, value, owner);
        test_assert(result == LR_OK,
                    "Add element to buffer (owner: %x, data: %x, length: %d)",
                    owner, value, count);

        lr_dump(&buffer);
    } else {
        printf("Buffer is full (length: %d, result: %d)\n", count, result);
    }
}

lr_result_t get_random_owner_data()
{
    lr_result_t  result;
    unsigned int count;
    lr_owner_t   owner = lr_owner(rand() % NUM_OWNERS);
    count              = lr_count(&buffer);
    lr_data_t value;
    result = lr_get(&buffer, &value, owner);
    if (result == LR_OK) {
        test_assert(
            count != 0,
            "Get element from buffer (value: %x, owner: %x, remain: %d)", value,
            owner, count - 1);
        lr_dump(&buffer);
    }
}

lr_result_t test_multiple_owners()
{
    // initialize the buffer
    unsigned int count;
    unsigned int ages   = 0;
    lr_result_t  result = init_buffer(BUFFER_SIZE);
    test_assert(result == LR_OK, "Initialize buffer");

    do {
        unsigned int current_size = lr_count(&buffer);
        unsigned int remain = BUFFER_SIZE - current_size;
        if (remain && rand() & 1) {
            for (int i = 0; i <= rand() % remain; ++i) {
                add_random_data();
            }
        } else {
            get_random_owner_data();
        }
        ages++;
    } while (lr_count(&buffer) > 0 || ages < 100);


    return LR_OK;
}

int main(int argc, char **argv)
{
    // run the test function
    lr_result_t result = test_multiple_owners();
    if (result == LR_OK) {
        log_ok("All tests passed");
    } else {
        log_error("Test failed");
    }

    return 0;
}
