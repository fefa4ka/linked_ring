#include <lr.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

/* Test utilities */
#define log_print(type, message, ...) \
    printf(type "\t" message "\n", ##__VA_ARGS__)
#define log_info(message, ...) log_print("INFO", message, ##__VA_ARGS__)
#define log_ok(message, ...) log_print("OK", message, ##__VA_ARGS__)
#define log_error(message, ...) \
    log_print("\e[1m\e[31mERROR\e[39m\e[0m", message " (%s:%d)", \
              ##__VA_ARGS__, __FILE__, __LINE__)

#define test_assert(test, message, ...) \
    do { \
        if (!(test)) { \
            log_error(message, ##__VA_ARGS__); \
            return LR_ERROR_UNKNOWN; \
        } else { \
            log_ok(message, ##__VA_ARGS__); \
        } \
    } while (0)

/* Test function declarations */
lr_result_t test_zero_value();
lr_result_t test_extreme_values();
lr_result_t test_buffer_full_recovery();
lr_result_t test_owner_edge_cases();
lr_result_t test_empty_string();
lr_result_t test_rapid_put_get();
lr_result_t test_mixed_operations();
lr_result_t test_boundary_indices();
lr_result_t test_put_robustness();
lr_result_t run_all_tests();

/* Global buffer for testing */
struct linked_ring buffer;

int main()
{
    lr_result_t result = run_all_tests();
    
    if (result == LR_OK) {
        log_info("All edge case tests passed successfully!");
        return 0;
    } else {
        log_error("Edge case tests failed with code %d", result);
        return 1;
    }
}

/* Run all test suites */
lr_result_t run_all_tests()
{
    lr_result_t result;
    
    log_info("=== Running Linked Ring Buffer Edge Case Tests ===");
    
    result = test_zero_value();
    if (result != LR_OK)
        return result;
        
    result = test_extreme_values();
    if (result != LR_OK)
        return result;
        
    result = test_buffer_full_recovery();
    if (result != LR_OK)
        return result;
        
    result = test_owner_edge_cases();
    if (result != LR_OK)
        return result;
        
    result = test_empty_string();
    if (result != LR_OK)
        return result;
        
    result = test_rapid_put_get();
    if (result != LR_OK)
        return result;
        
    result = test_mixed_operations();
    if (result != LR_OK)
        return result;
        
    result = test_boundary_indices();
    if (result != LR_OK)
        return result;
    
    result = test_put_robustness();
    if (result != LR_OK)
        return result;
    
    return LR_OK;
}

/* Test handling of zero values */
lr_result_t test_zero_value()
{
    lr_result_t result;
    struct lr_cell *cells;
    lr_data_t data;
    const unsigned int size = 5;
    
    log_info("Testing zero value handling...");
    
    /* Initialize buffer */
    cells = malloc(size * sizeof(struct lr_cell));
    result = lr_init(&buffer, size, cells);
    test_assert(result == LR_OK, "Buffer initialization should succeed");
    
    /* Test with zero data value */
    result = lr_put(&buffer, 0, 1);
    test_assert(result == LR_OK, "Put with zero value should succeed");
    
    result = lr_get(&buffer, &data, 1);
    test_assert(result == LR_OK, "Get after zero value put should succeed");
    test_assert(data == 0, "Retrieved data should be 0, got %lu", data);
    
    /* Test with zero owner value */
    result = lr_put(&buffer, 42, 0);
    test_assert(result == LR_OK, "Put with zero owner should succeed");
    
    result = lr_get(&buffer, &data, 0);
    test_assert(result == LR_OK, "Get with zero owner should succeed");
    test_assert(data == 42, "Retrieved data should be 42, got %lu", data);
    
    /* Clean up */
    free(cells);
    
    return LR_OK;
}

/* Test extreme values (min/max) */
lr_result_t test_extreme_values()
{
    lr_result_t result;
    struct lr_cell *cells;
    lr_data_t data;
    const unsigned int size = 5;
    
    log_info("Testing extreme values...");
    
    /* Initialize buffer */
    cells = malloc(size * sizeof(struct lr_cell));
    result = lr_init(&buffer, size, cells);
    test_assert(result == LR_OK, "Buffer initialization should succeed");
    
    /* Test with UINTPTR_MAX value */
    result = lr_put(&buffer, UINTPTR_MAX, 1);
    test_assert(result == LR_OK, "Put with UINTPTR_MAX value should succeed");
    
    result = lr_get(&buffer, &data, 1);
    test_assert(result == LR_OK, "Get after UINTPTR_MAX put should succeed");
    test_assert(data == UINTPTR_MAX, 
                "Retrieved data should be UINTPTR_MAX, got %lu", data);
    
    /* Test with UINTPTR_MAX owner */
    result = lr_put(&buffer, 42, UINTPTR_MAX);
    test_assert(result == LR_OK, "Put with UINTPTR_MAX owner should succeed");
    
    result = lr_get(&buffer, &data, UINTPTR_MAX);
    test_assert(result == LR_OK, "Get with UINTPTR_MAX owner should succeed");
    test_assert(data == 42, "Retrieved data should be 42, got %lu", data);
    
    /* Test with alternating bit patterns */
    lr_data_t alternating = 0xAAAAAAAAAAAAAAAA;
    result = lr_put(&buffer, alternating, 1);
    test_assert(result == LR_OK, "Put with alternating bits should succeed");
    
    result = lr_get(&buffer, &data, 1);
    test_assert(result == LR_OK, "Get after alternating bits put should succeed");
    test_assert(data == alternating, 
                "Retrieved data should match alternating pattern, got 0x%lx", data);
    
    /* Clean up */
    free(cells);
    
    return LR_OK;
}

/* Test buffer recovery from full state */
lr_result_t test_buffer_full_recovery()
{
    lr_result_t result;
    struct lr_cell *cells;
    lr_data_t data;
    const unsigned int size = 6;
    
    log_info("Testing buffer full recovery...");
    
    /* Initialize buffer */
    cells = malloc(size * sizeof(struct lr_cell));
    result = lr_init(&buffer, size, cells);
    test_assert(result == LR_OK, "Buffer initialization should succeed");
    
    /* Fill buffer to capacity with one owner */
    for (int i = 0; i < size - 1; i++) {
        result = lr_put(&buffer, i, 1);
        test_assert(result == LR_OK, "Put %d should succeed", i);
    }
    
    /* Verify buffer is full */
    result = lr_put(&buffer, 999, 1);
    test_assert(result == LR_ERROR_BUFFER_FULL, 
                "Put to full buffer should return BUFFER_FULL");
    
    /* Try to add with a different owner - should still fail */
    result = lr_put(&buffer, 888, 2);
    test_assert(result == LR_ERROR_BUFFER_FULL, 
                "Put with new owner to full buffer should return BUFFER_FULL");
    
    /* Remove all elements */
    for (int i = 0; i < size - 1; i++) {
        result = lr_get(&buffer, &data, 1);
        test_assert(result == LR_OK, "Get %d should succeed", i);
    }
    
    /* Verify buffer is empty for owner 1 */
    result = lr_get(&buffer, &data, 1);
    test_assert(result == LR_ERROR_BUFFER_EMPTY, 
                "Get from empty buffer should return BUFFER_EMPTY");
    
    /* Fill buffer again with alternating owners */
    for (int i = 0; i < 2; i++) {
        result = lr_put(&buffer, i * 10, 1);
        test_assert(result == LR_OK, "Put for owner 1 should succeed");
        
        result = lr_put(&buffer, i * 10 + 5, 2);
        test_assert(result == LR_OK, "Put for owner 2 should succeed");
    }
    
    /* Verify buffer is full again */
    result = lr_put(&buffer, 999, 1);
    test_assert(result == LR_ERROR_BUFFER_FULL, 
                "Put to full buffer should return BUFFER_FULL");
    
    /* Clean up */
    free(cells);
    
    return LR_OK;
}

/* Test edge cases with owners */
lr_result_t test_owner_edge_cases()
{
    lr_result_t result;
    struct lr_cell *cells;
    lr_data_t data;
    const unsigned int size = 10;
    
    log_info("Testing owner edge cases...");
    
    /* Initialize buffer */
    cells = malloc(size * sizeof(struct lr_cell));
    result = lr_init(&buffer, size, cells);
    test_assert(result == LR_OK, "Buffer initialization should succeed");
    
    /* Test with many different owners */
    for (int i = 0; i < 5; i++) {
        result = lr_put(&buffer, i, i);
        test_assert(result == LR_OK, "Put with owner %d should succeed", i);
    }
    
    /* Verify each owner has one element */
    for (int i = 0; i < 5; i++) {
        test_assert(lr_count_owned(&buffer, i) == 1, 
                    "Owner %d should have 1 element", i);
    }
    
    /* Test retrieving data for each owner */
    for (int i = 0; i < 5; i++) {
        result = lr_get(&buffer, &data, i);
        test_assert(result == LR_OK, "Get for owner %d should succeed", i);
        test_assert(data == i, "Retrieved data should be %d, got %lu", i, data);
    }
    
    /* Test with non-existent owner */
    result = lr_get(&buffer, &data, 999);
    test_assert(result == LR_ERROR_BUFFER_EMPTY, 
                "Get with non-existent owner should return BUFFER_EMPTY");
    
    /* Test removing and re-adding owners */
    result = lr_put(&buffer, 42, 1);
    test_assert(result == LR_OK, "Put for previously used owner should succeed");
    
    result = lr_get(&buffer, &data, 1);
    test_assert(result == LR_OK, "Get for re-added owner should succeed");
    test_assert(data == 42, "Retrieved data should be 42, got %lu", data);
    
    /* Clean up */
    free(cells);
    
    return LR_OK;
}

/* Test empty string handling */
lr_result_t test_empty_string()
{
    lr_result_t result;
    struct lr_cell *cells;
    unsigned char str_data[10];
    size_t length;
    const unsigned int size = 5;
    
    log_info("Testing empty string handling...");
    
    /* Initialize buffer */
    cells = malloc(size * sizeof(struct lr_cell));
    result = lr_init(&buffer, size, cells);
    test_assert(result == LR_OK, "Buffer initialization should succeed");
    
    /* Test with empty string */
    result = lr_put_string(&buffer, (unsigned char *)"", 1);
    test_assert(result == LR_OK, "Put empty string should succeed");
    
    /* Verify no elements were added */
    test_assert(lr_count_owned(&buffer, 1) == 0, 
                "Owner 1 should have 0 elements after empty string put");
    
    /* Test with null terminator only */
    result = lr_put(&buffer, 0, 1);
    test_assert(result == LR_OK, "Put null terminator should succeed");
    
    result = lr_read_string(&buffer, str_data, &length, 1);
    test_assert(result == LR_OK, "Read string should succeed");
    test_assert(length == 0, "String length should be 0, got %zu", length);
    test_assert(str_data[0] == '\0', "String should be empty");
    
    /* Clean up */
    free(cells);
    
    return LR_OK;
}

/* Test rapid put/get operations */
lr_result_t test_rapid_put_get()
{
    lr_result_t result;
    struct lr_cell *cells;
    lr_data_t data;
    const unsigned int size = 20;
    const unsigned int iterations = 1000;
    
    log_info("Testing rapid put/get operations...");
    
    /* Initialize buffer */
    cells = malloc(size * sizeof(struct lr_cell));
    result = lr_init(&buffer, size, cells);
    test_assert(result == LR_OK, "Buffer initialization should succeed");
    
    /* Perform rapid put/get operations */
    for (unsigned int i = 0; i < iterations; i++) {
        /* Add data */
        result = lr_put(&buffer, i, 1);
        test_assert(result == LR_OK, "Put %u should succeed", i);
        
        /* Retrieve data */
        result = lr_get(&buffer, &data, 1);
        test_assert(result == LR_OK, "Get %u should succeed", i);
        test_assert(data == i, "Retrieved data should be %u, got %lu", i, data);
    }
    
    /* Verify buffer is empty */
    test_assert(lr_count(&buffer) == 0, "Buffer should be empty after rapid put/get");
    
    /* Clean up */
    free(cells);
    
    return LR_OK;
}

/* Test mixed operations */
lr_result_t test_mixed_operations()
{
    lr_result_t result;
    struct lr_cell *cells;
    lr_data_t data;
    const unsigned int size = 10;
    
    log_info("Testing mixed operations...");
    
    /* Initialize buffer */
    cells = malloc(size * sizeof(struct lr_cell));
    result = lr_init(&buffer, size, cells);
    test_assert(result == LR_OK, "Buffer initialization should succeed");
    
    /* Add some initial data */
    result = lr_put(&buffer, 10, 1);
    test_assert(result == LR_OK, "Put 10 should succeed");
    
    result = lr_put(&buffer, 20, 1);
    test_assert(result == LR_OK, "Put 20 should succeed");
    
    result = lr_put(&buffer, 30, 2);
    test_assert(result == LR_OK, "Put 30 should succeed");
    
    /* Mix operations: insert, get, put */
    result = lr_insert(&buffer, 15, 1, 1);
    test_assert(result == LR_OK, "Insert 15 at index 1 should succeed");
    
    result = lr_get(&buffer, &data, 1);
    test_assert(result == LR_OK && data == 10, 
                "Get should return 10, got %lu", data);
    
    result = lr_put(&buffer, 40, 2);
    test_assert(result == LR_OK, "Put 40 should succeed");
    
    result = lr_get(&buffer, &data, 1);
    test_assert(result == LR_OK && data == 15, 
                "Get should return 15, got %lu", data);
    
    lr_dump(&buffer);
    result = lr_pull(&buffer, &data, 1, 0);
    test_assert(result == LR_OK && data == 20, 
                "Pull at index 0 should return 20, got %lu", data);
    
    result = lr_get(&buffer, &data, 2);
    test_assert(result == LR_OK && data == 30, 
                "Get should return 30, got %lu", data);
    
    /* Clean up */
    free(cells);
    
    return LR_OK;
}

/* Test lr_put robustness */
lr_result_t test_put_robustness()
{
    lr_result_t result;
    struct lr_cell *cells;
    lr_data_t data;
    const unsigned int size = 8;
    
    log_info("Testing lr_put robustness...");
    
    /* Initialize buffer */
    cells = malloc(size * sizeof(struct lr_cell));
    result = lr_init(&buffer, size, cells);
    test_assert(result == LR_OK, "Buffer initialization should succeed");
    
    /* Test case 1: Add and remove owners in specific order to test edge cases */
    result = lr_put(&buffer, 10, 1);
    test_assert(result == LR_OK, "Put for owner 1 should succeed");
    
    result = lr_put(&buffer, 20, 2);
    test_assert(result == LR_OK, "Put for owner 2 should succeed");
    
    result = lr_put(&buffer, 30, 3);
    test_assert(result == LR_OK, "Put for owner 3 should succeed");
    
    /* Remove middle owner's data */
    result = lr_get(&buffer, &data, 2);
    test_assert(result == LR_OK && data == 20, 
                "Get for owner 2 should return 20, got %lu", data);
    
    /* Add data for removed owner */
    result = lr_put(&buffer, 25, 2);
    test_assert(result == LR_OK, "Put for previously removed owner should succeed");
    
    /* Verify data integrity */
    result = lr_get(&buffer, &data, 2);
    test_assert(result == LR_OK && data == 25, 
                "Get for owner 2 should return 25, got %lu", data);
    
    /* Test case 2: Fill buffer to capacity with alternating owners */
    /* Reset buffer */
    free(cells);
    cells = malloc(size * sizeof(struct lr_cell));
    result = lr_init(&buffer, size, cells);
    test_assert(result == LR_OK, "Buffer initialization should succeed");
    
    /* Fill buffer with alternating owners */
    for (int i = 0; i < (size/2) - 1; i++) {
        result = lr_put(&buffer, i * 10, 1);
        test_assert(result == LR_OK, "Put for owner 1 should succeed");
        
        result = lr_put(&buffer, i * 10 + 5, 2);
        test_assert(result == LR_OK, "Put for owner 2 should succeed");
    }
    
    /* Verify buffer is full */
    result = lr_put(&buffer, 99, 3);
    test_assert(result == LR_ERROR_BUFFER_FULL, 
                "Put to full buffer should return BUFFER_FULL");
    
    /* Remove all data for owner 1 */
    while (lr_count_owned(&buffer, 1) > 0) {
        result = lr_get(&buffer, &data, 1);
        test_assert(result == LR_OK, "Get for owner 1 should succeed");
    }
    
    /* Now we should be able to add data for a new owner */
    result = lr_put(&buffer, 100, 3);
    test_assert(result == LR_OK, "Put for new owner after making space should succeed");
    
    result = lr_put(&buffer, 110, 3);
    test_assert(result == LR_OK, "Second put for new owner should succeed");
    
    /* Verify data integrity */
    result = lr_get(&buffer, &data, 3);
    test_assert(result == LR_OK && data == 100, 
                "Get for owner 3 should return 100, got %lu", data);
    
    /* Clean up */
    free(cells);
    
    return LR_OK;
}

/* Test boundary indices */
lr_result_t test_boundary_indices()
{
    lr_result_t result;
    struct lr_cell *cells;
    lr_data_t data;
    const unsigned int size = 10;
    
    log_info("Testing boundary indices...");
    
    /* Initialize buffer */
    cells = malloc(size * sizeof(struct lr_cell));
    result = lr_init(&buffer, size, cells);
    test_assert(result == LR_OK, "Buffer initialization should succeed");
    
    /* Add some data */
    for (int i = 0; i < 5; i++) {
        result = lr_put(&buffer, i * 10, 1);
        test_assert(result == LR_OK, "Put %d should succeed", i);
    }
    
    /* Test insert at beginning (index 0) */
    result = lr_insert(&buffer, 5, 1, 0);
    test_assert(result == LR_OK, "Insert at beginning should succeed");
    
    result = lr_get(&buffer, &data, 1);
    test_assert(result == LR_OK && data == 5, 
                "First element should be 5, got %lu", data);
    
    /* Add more data */
    for (int i = 0; i < 3; i++) {
        result = lr_get(&buffer, &data, 1);
        test_assert(result == LR_OK, "Get should succeed");
    }
    
    /* Test pull at last index */
    size_t count = lr_count_owned(&buffer, 1);
    result = lr_pull(&buffer, &data, 1, count - 1);
    test_assert(result == LR_OK, 
                "Pull at last index should succeed");
    
    /* Test insert at invalid index (beyond end) */
    result = lr_insert(&buffer, 99, 1, 100);
    test_assert(result == LR_OK, 
                "Insert at invalid index should handle gracefully");
    
    /* Test multiple owner edge case */
    log_info("Testing multiple owner edge cases with put...");
    
    /* Create a situation with multiple owners in specific order */
    result = lr_put(&buffer, 101, 2);
    test_assert(result == LR_OK, "Put for owner 2 should succeed");
    
    result = lr_put(&buffer, 102, 3);
    test_assert(result == LR_OK, "Put for owner 3 should succeed");
    
    /* Remove all data for owner 2 */
    result = lr_get(&buffer, &data, 2);
    test_assert(result == LR_OK, "Get for owner 2 should succeed");
    
    /* Now add data for owner 2 again - this tests the case where
       we have a "hole" in the owner list */
    result = lr_put(&buffer, 201, 2);
    test_assert(result == LR_OK, "Put for owner 2 after removal should succeed");
    
    /* Verify data integrity */
    result = lr_get(&buffer, &data, 2);
    test_assert(result == LR_OK && data == 201, 
                "Retrieved data should be 201, got %lu", data);
    
    /* Clean up */
    free(cells);
    
    return LR_OK;
}
