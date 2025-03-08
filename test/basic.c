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

/* Test function declarations */
lr_result_t test_initialization();
lr_result_t test_basic_operations();
lr_result_t test_multiple_owners();
lr_result_t test_buffer_boundaries();
lr_result_t test_string_operations();
lr_result_t test_insert_operations();
lr_result_t test_edge_cases();
lr_result_t run_all_tests();

/* Global buffer for testing */
struct linked_ring buffer;

int main() {
    lr_result_t result = run_all_tests();
    
    if (result == LR_OK) {
        log_info("All tests passed successfully!");
        return 0;
    } else {
        log_error("Tests failed with code %d", result);
        return 1;
    }
}

/* Run all test suites */
lr_result_t run_all_tests() {
    lr_result_t result;
    
    log_info("=== Running Linked Ring Buffer Tests ===");
    
    result = test_initialization();
    if (result != LR_OK) return result;
    
    result = test_basic_operations();
    if (result != LR_OK) return result;
    
    result = test_multiple_owners();
    if (result != LR_OK) return result;
    
    result = test_buffer_boundaries();
    if (result != LR_OK) return result;
    
    result = test_string_operations();
    if (result != LR_OK) return result;
    
    result = test_insert_operations();
    if (result != LR_OK) return result;
    
    result = test_edge_cases();
    if (result != LR_OK) return result;
    
    return LR_OK;
}

/* Test buffer initialization */
lr_result_t test_initialization() {
    lr_result_t result;
    struct lr_cell *cells;
    
    log_info("Testing buffer initialization...");
    
    /* Test with NULL cells */
    result = lr_init(&buffer, 10, NULL);
    test_assert(result == LR_ERROR_NOMEMORY, 
                "Buffer initialization with NULL cells should fail");
    
    /* Test with zero size */
    cells = malloc(10 * sizeof(struct lr_cell));
    result = lr_init(&buffer, 0, cells);
    test_assert(result == LR_ERROR_NOMEMORY, 
                "Buffer initialization with size 0 should fail");
    
    /* Test valid initialization */
    result = lr_init(&buffer, 10, cells);
    test_assert(result == LR_OK, 
                "Buffer initialization with valid parameters should succeed");
    
    /* Verify initial state */
    test_assert(buffer.size == 10, 
                "Buffer size should be 10, got %d", buffer.size);
    test_assert(buffer.cells == cells, 
                "Buffer cells pointer should match allocated cells");
    test_assert(buffer.owners == NULL, 
                "Buffer should have no owners initially");
    test_assert(buffer.write == buffer.cells, 
                "Write pointer should point to first cell");
    
    /* Clean up */
    free(cells);
    
    return LR_OK;
}

/* Test basic put/get operations */
lr_result_t test_basic_operations() {
    lr_result_t result;
    struct lr_cell *cells;
    lr_data_t data;
    const unsigned int size = 8;
    
    log_info("Testing basic operations...");
    
    /* Initialize buffer */
    cells = malloc(size * sizeof(struct lr_cell));
    result = lr_init(&buffer, size, cells);
    test_assert(result == LR_OK, "Buffer initialization should succeed");
    
    /* Test get from empty buffer */
    result = lr_get(&buffer, &data, 1);
    test_assert(result == LR_ERROR_BUFFER_EMPTY, 
                "Get from empty buffer should return BUFFER_EMPTY");
    
    /* Test exists on empty buffer */
    test_assert(lr_exists(&buffer, 1) == 0, 
                "Owner should not exist in empty buffer");
    
    /* Test put and get with single owner */
    result = lr_put(&buffer, 42, 1);
    test_assert(result == LR_OK, "Put should succeed");
    test_assert(lr_exists(&buffer, 1) == 1, "Owner should exist after put");
    test_assert(lr_count(&buffer) == 1, "Buffer should contain 1 element");
    
    result = lr_get(&buffer, &data, 1);
    test_assert(result == LR_OK, "Get should succeed");
    test_assert(data == 42, "Retrieved data should be 42, got %lu", data);
    test_assert(lr_count(&buffer) == 0, "Buffer should be empty after get");
    
    /* Test multiple puts and gets */
    for (int i = 0; i < 5; i++) {
        result = lr_put(&buffer, i * 10, 1);
        test_assert(result == LR_OK, "Put %d should succeed", i);
    }
    
    test_assert(lr_count(&buffer) == 5, 
                "Buffer should contain 5 elements, has %lu", lr_count(&buffer));
    
    for (int i = 0; i < 5; i++) {
        result = lr_get(&buffer, &data, 1);
        test_assert(result == LR_OK, "Get %d should succeed", i);
        test_assert(data == i * 10, 
                    "Retrieved data should be %d, got %lu", i * 10, data);
    }
    
    test_assert(lr_count(&buffer) == 0, "Buffer should be empty after gets");
    
    /* Clean up */
    free(cells);
    
    return LR_OK;
}

/* Test operations with multiple owners */
lr_result_t test_multiple_owners() {
    lr_result_t result;
    struct lr_cell *cells;
    lr_data_t data;
    const unsigned int size = 10;
    
    log_info("Testing multiple owners...");
    
    /* Initialize buffer */
    cells = malloc(size * sizeof(struct lr_cell));
    result = lr_init(&buffer, size, cells);
    test_assert(result == LR_OK, "Buffer initialization should succeed");
    
    /* Add data for different owners */
    result = lr_put(&buffer, 100, 1);
    test_assert(result == LR_OK, "Put for owner 1 should succeed");
    
    result = lr_put(&buffer, 200, 2);
    test_assert(result == LR_OK, "Put for owner 2 should succeed");
    
    result = lr_put(&buffer, 300, 3);
    test_assert(result == LR_OK, "Put for owner 3 should succeed");
    
    result = lr_put(&buffer, 101, 1);
    test_assert(result == LR_OK, "Second put for owner 1 should succeed");
    
    /* Verify counts */
    test_assert(lr_count(&buffer) == 4, 
                "Buffer should contain 4 elements total");
    test_assert(lr_count_owned(&buffer, 1) == 2, 
                "Owner 1 should have 2 elements");
    test_assert(lr_count_owned(&buffer, 2) == 1, 
                "Owner 2 should have 1 element");
    test_assert(lr_count_owned(&buffer, 3) == 1, 
                "Owner 3 should have 1 element");
    
    /* Retrieve data for specific owners */
    result = lr_get(&buffer, &data, 2);
    test_assert(result == LR_OK, "Get for owner 2 should succeed");
    test_assert(data == 200, "Retrieved data should be 200, got %lu", data);
    
    result = lr_get(&buffer, &data, 1);
    test_assert(result == LR_OK, "Get for owner 1 should succeed");
    test_assert(data == 100, "Retrieved data should be 100, got %lu", data);
    
    /* Verify updated counts */
    test_assert(lr_count(&buffer) == 2, 
                "Buffer should contain 2 elements after gets");
    test_assert(lr_count_owned(&buffer, 1) == 1, 
                "Owner 1 should have 1 element left");
    test_assert(lr_count_owned(&buffer, 2) == 0, 
                "Owner 2 should have 0 elements left");
    
    /* Clean up */
    free(cells);
    
    return LR_OK;
}

/* Test buffer boundaries and full conditions */
lr_result_t test_buffer_boundaries() {
    lr_result_t result;
    struct lr_cell *cells;
    lr_data_t data;
    const unsigned int size = 5;
    
    log_info("Testing buffer boundaries...");
    
    /* Initialize small buffer */
    cells = malloc(size * sizeof(struct lr_cell));
    result = lr_init(&buffer, size, cells);
    test_assert(result == LR_OK, "Buffer initialization should succeed");
    
    /* Fill buffer to capacity */
    for (int i = 0; i < size - 1; i++) {  /* -1 because one cell is for owner */
        result = lr_put(&buffer, i, 1);
        test_assert(result == LR_OK, "Put %d should succeed", i);
    }
    
    /* Verify buffer is full */
    test_assert(lr_count(&buffer) == size - 1, 
                "Buffer should contain %d elements", size - 1);
    
    /* The buffer should be full at this point - one cell for owner, the rest for data */
    unsigned int available = lr_available(&buffer);
    test_assert(available == 0, 
                "Buffer should have 0 available slots, has %u", available);
    
    /* Try to add more data */
    result = lr_put(&buffer, 999, 1);
    test_assert(result == LR_ERROR_BUFFER_FULL, 
                "Put to full buffer should return BUFFER_FULL, got %d", result);
    
    /* Try to add data for new owner */
    result = lr_put(&buffer, 888, 2);
    test_assert(result == LR_ERROR_BUFFER_FULL, 
                "Put with new owner to full buffer should return BUFFER_FULL");
    
    /* Make space and try again */
    result = lr_get(&buffer, &data, 1);
    test_assert(result == LR_OK, "Get should succeed");
    
    result = lr_put(&buffer, 777, 1);
    test_assert(result == LR_OK, 
                "Put after making space should succeed");
    
    /* Clean up */
    free(cells);
    
    return LR_OK;
}

/* Test string operations */
lr_result_t test_string_operations() {
    lr_result_t result;
    struct lr_cell *cells;
    lr_data_t data;
    const unsigned int size = 20;
    
    log_info("Testing string operations...");
    
    /* Initialize buffer */
    cells = malloc(size * sizeof(struct lr_cell));
    result = lr_init(&buffer, size, cells);
    test_assert(result == LR_OK, "Buffer initialization should succeed");
    
    /* Test putting a string */
    result = lr_put_string(&buffer, (unsigned char*)"Hello", 1);
    test_assert(result == LR_OK, "Put string should succeed");
    test_assert(lr_count_owned(&buffer, 1) == 5, 
                "Owner 1 should have 5 elements (length of 'Hello')");
    
    /* Read characters one by one */
    for (int i = 0; i < 5; i++) {
        result = lr_get(&buffer, &data, 1);
        test_assert(result == LR_OK, "Get character should succeed");
        test_assert(data == "Hello"[i], 
                    "Character %d should be '%c', got '%c'", 
                    i, "Hello"[i], (char)data);
    }
    
    /* Test with multiple strings for different owners */
    result = lr_put_string(&buffer, (unsigned char*)"World", 1);
    test_assert(result == LR_OK, "Put 'World' should succeed");
    
    result = lr_put_string(&buffer, (unsigned char*)"Test", 2);
    test_assert(result == LR_OK, "Put 'Test' should succeed");
    
    /* Verify counts */
    test_assert(lr_count_owned(&buffer, 1) == 5, 
                "Owner 1 should have 5 elements");
    test_assert(lr_count_owned(&buffer, 2) == 4, 
                "Owner 2 should have 4 elements");
    
    /* Clean up */
    free(cells);
    
    return LR_OK;
}

/* Test insert operations */
lr_result_t test_insert_operations() {
    lr_result_t result;
    struct lr_cell *cells;
    lr_data_t data;
    const unsigned int size = 15;
    
    log_info("Testing insert operations...");
    
    /* Initialize buffer */
    cells = malloc(size * sizeof(struct lr_cell));
    result = lr_init(&buffer, size, cells);
    test_assert(result == LR_OK, "Buffer initialization should succeed");
    
    /* Add some initial data */
    result = lr_put(&buffer, 'A', 1);
    test_assert(result == LR_OK, "Put 'A' should succeed");
    
    result = lr_put(&buffer, 'C', 1);
    test_assert(result == LR_OK, "Put 'C' should succeed");
    
    /* Insert in the middle */
    result = lr_insert(&buffer, 'B', 1, 1);
    test_assert(result == LR_OK, "Insert 'B' at index 1 should succeed");
    
    /* Verify order with gets */
    result = lr_get(&buffer, &data, 1);
    test_assert(result == LR_OK && data == 'A', 
                "First character should be 'A', got '%c'", (char)data);
    
    result = lr_get(&buffer, &data, 1);
    test_assert(result == LR_OK && data == 'B', 
                "Second character should be 'B', got '%c'", (char)data);
    
    result = lr_get(&buffer, &data, 1);
    test_assert(result == LR_OK && data == 'C', 
                "Third character should be 'C', got '%c'", (char)data);
    
    /* Test insert at beginning */
    result = lr_put(&buffer, 'Y', 1);
    test_assert(result == LR_OK, "Put 'Y' should succeed");
    
    result = lr_put(&buffer, 'Z', 1);
    test_assert(result == LR_OK, "Put 'Z' should succeed");
    
    /* Now insert X at the beginning */
    result = lr_insert(&buffer, 'X', 1, 0);
    test_assert(result == LR_OK, "Insert 'X' at beginning should succeed");
    
    /* Verify order */
    result = lr_get(&buffer, &data, 1);
    test_assert(result == LR_OK && data == 'X', 
                "First character should be 'X', got '%c'", (char)data);
    
    /* Clean up */
    free(cells);
    
    return LR_OK;
}

/* Test edge cases */
lr_result_t test_edge_cases() {
    lr_result_t result;
    struct lr_cell *cells;
    lr_data_t data;
    
    log_info("Testing edge cases...");
    
    /* Test with minimum size buffer */
    cells = malloc(2 * sizeof(struct lr_cell));
    result = lr_init(&buffer, 2, cells);
    test_assert(result == LR_OK, "Buffer initialization with size 2 should succeed");
    
    /* One cell for data, one for owner */
    result = lr_put(&buffer, 42, 1);
    test_assert(result == LR_OK, "Put to minimum size buffer should succeed");
    
    result = lr_put(&buffer, 43, 1);
    test_assert(result == LR_ERROR_BUFFER_FULL, 
                "Second put should fail with BUFFER_FULL");
    
    /* Test with very large values */
    result = lr_get(&buffer, &data, 1);
    test_assert(result == LR_OK, "Get should succeed");
    
    result = lr_put(&buffer, UINTPTR_MAX, 1);
    test_assert(result == LR_OK, "Put with maximum value should succeed");
    
    result = lr_get(&buffer, &data, 1);
    test_assert(result == LR_OK && data == UINTPTR_MAX, 
                "Retrieved data should be UINTPTR_MAX");
    
    /* Test with non-existent owner */
    result = lr_get(&buffer, &data, 999);
    test_assert(result == LR_ERROR_BUFFER_EMPTY, 
                "Get with non-existent owner should return BUFFER_EMPTY");
    
    /* Test pull operation */
    result = lr_put(&buffer, 10, 1);
    test_assert(result == LR_OK, "Put should succeed");
    
    result = lr_put(&buffer, 20, 1);
    test_assert(result == LR_OK, "Put should succeed");
    
    result = lr_put(&buffer, 30, 1);
    test_assert(result == LR_OK, "Put should succeed");
    
    result = lr_pull(&buffer, &data, 1, 1);
    test_assert(result == LR_OK && data == 20, 
                "Pull from middle should return 20, got %lu", data);
    
    /* Clean up */
    free(cells);
    
    return LR_OK;
}
