#include <lr.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Test utilities */
#define log_print(type, message, ...) \
    printf(type "\t" message "\n", ##__VA_ARGS__)
#define log_info(message, ...)    log_print("INFO", message, ##__VA_ARGS__)
#define log_ok(message, ...)      log_print("OK", message, ##__VA_ARGS__)
#define log_error(message, ...)   \
    log_print("\e[1m\e[31mERROR\e[39m\e[0m", message " (%s:%d)", ##__VA_ARGS__, __FILE__, __LINE__)

#define test_assert(test, message, ...) \
    do { \
        if (!(test)) { \
            log_error(message, ##__VA_ARGS__); \
            return LR_ERROR_UNKNOWN; \
        } else { \
            log_ok(message, ##__VA_ARGS__); \
        } \
    } while (0)

/* Test function to verify UINTPTR_MAX handling */
lr_result_t test_max_value_handling() {
    lr_result_t result;
    struct lr_cell *cells;
    lr_data_t data;
    const unsigned int size = 5;
    
    log_info("Testing UINTPTR_MAX value handling...");
    
    /* Initialize buffer */
    cells = malloc(size * sizeof(struct lr_cell));
    result = lr_init(&buffer, size, cells);
    test_assert(result == LR_OK, "Buffer initialization should succeed");
    
    /* Test with UINTPTR_MAX */
    log_info("Putting UINTPTR_MAX (0x%lx) into buffer", UINTPTR_MAX);
    result = lr_put(&buffer, UINTPTR_MAX, 1);
    test_assert(result == LR_OK, "Put with UINTPTR_MAX should succeed");
    
    /* Dump buffer state for debugging */
    lr_dump(&buffer);
    
    /* Retrieve the value */
    result = lr_get(&buffer, &data, 1);
    test_assert(result == LR_OK, "Get should succeed");
    test_assert(data == UINTPTR_MAX, 
                "Retrieved data should be UINTPTR_MAX (0x%lx), got 0x%lx", 
                UINTPTR_MAX, data);
    
    /* Test with other large values */
    lr_data_t large_values[] = {
        UINTPTR_MAX,
        UINTPTR_MAX - 1,
        UINTPTR_MAX / 2,
        (lr_data_t)-1,
        (lr_data_t)0xFFFFFFFF,
        (lr_data_t)0xFFFF0000,
        (lr_data_t)0x0000FFFF
    };
    
    for (int i = 0; i < sizeof(large_values)/sizeof(large_values[0]); i++) {
        result = lr_put(&buffer, large_values[i], 1);
        test_assert(result == LR_OK, 
                    "Put with large value 0x%lx should succeed", large_values[i]);
        
        result = lr_get(&buffer, &data, 1);
        test_assert(result == LR_OK && data == large_values[i], 
                    "Retrieved data should be 0x%lx, got 0x%lx", 
                    large_values[i], data);
    }
    
    /* Test with alternating small and large values */
    for (int i = 0; i < 10; i++) {
        lr_data_t value = (i % 2 == 0) ? i : UINTPTR_MAX - i;
        
        result = lr_put(&buffer, value, 1);
        test_assert(result == LR_OK, 
                    "Put with value 0x%lx should succeed", value);
        
        result = lr_get(&buffer, &data, 1);
        test_assert(result == LR_OK && data == value, 
                    "Retrieved data should be 0x%lx, got 0x%lx", 
                    value, data);
    }
    
    /* Clean up */
    free(cells);
    
    log_ok("All UINTPTR_MAX tests passed successfully");
    return LR_OK;
}

/* Global buffer for testing */
struct linked_ring buffer;

int main() {
    lr_result_t result = test_max_value_handling();
    
    if (result == LR_OK) {
        log_info("All tests passed successfully!");
        return 0;
    } else {
        log_error("Tests failed with code %d", result);
        return 1;
    }
}
