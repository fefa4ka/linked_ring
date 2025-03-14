#include <lr.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Test utilities */
#define log_print(type, message, ...)                                          \
    printf(type "\t" message "\n", ##__VA_ARGS__)
#define log_info(message, ...) log_print("INFO", message, ##__VA_ARGS__)
#define log_ok(message, ...)   log_print("OK", message, ##__VA_ARGS__)
#define log_error(message, ...)                                                \
    log_print("\e[1m\e[31mERROR\e[39m\e[0m", message " (%s:%d)",               \
              ##__VA_ARGS__, __FILE__, __LINE__)

#define test_assert(test, message, ...)                                        \
    do {                                                                       \
        if (!(test)) {                                                         \
            log_error(message, ##__VA_ARGS__);                                 \
            return LR_ERROR_UNKNOWN;                                           \
        } else {                                                               \
            log_ok(message, ##__VA_ARGS__);                                    \
        }                                                                      \
    } while (0)

/* Test function declarations */
lr_result_t test_resize_empty_buffer();
lr_result_t test_resize_with_data();
lr_result_t test_resize_with_multiple_owners();
lr_result_t test_resize_larger();
lr_result_t test_resize_smaller();
lr_result_t test_resize_edge_cases();
lr_result_t run_all_tests();

/* Global buffer for testing */
struct linked_ring buffer;

int main()
{
    lr_result_t result = run_all_tests();

    if (result == LR_OK) {
        log_info("All resize tests passed successfully!");
        return 0;
    } else {
        log_error("Resize tests failed with code %d", result);
        return 1;
    }
}

/* Run all test suites */
lr_result_t run_all_tests()
{
    lr_result_t result;

    log_info("=== Running Linked Ring Buffer Resize Tests ===");

    result = test_resize_empty_buffer();
    if (result != LR_OK)
        return result;

    result = test_resize_with_data();
    if (result != LR_OK)
        return result;

    result = test_resize_with_multiple_owners();
    if (result != LR_OK)
        return result;

    result = test_resize_larger();
    if (result != LR_OK)
        return result;

    result = test_resize_smaller();
    if (result != LR_OK)
        return result;

    result = test_resize_edge_cases();
    if (result != LR_OK)
        return result;

    return LR_OK;
}

/* Test resizing an empty buffer */
lr_result_t test_resize_empty_buffer()
{
    lr_result_t        result;
    struct lr_cell    *cells;
    struct lr_cell    *new_cells;
    const unsigned int initial_size = 5;
    const unsigned int new_size     = 10;

    log_info("Testing resizing an empty buffer...");

    /* Initialize buffer */
    cells  = malloc(initial_size * sizeof(struct lr_cell));
    result = lr_init(&buffer, initial_size, cells);
    test_assert(result == LR_OK, "Buffer initialization should succeed");

    /* Verify initial state */
    test_assert(buffer.size == initial_size, "Initial buffer size should be %d",
                initial_size);
    test_assert(lr_count(&buffer) == 0, "Buffer should be empty");

    /* Resize buffer */
    new_cells = malloc(new_size * sizeof(struct lr_cell));
    result    = lr_resize(&buffer, new_size, new_cells);
    test_assert(result == LR_OK, "Buffer resize should succeed");

    /* Verify resized state */
    test_assert(buffer.size == new_size, "Buffer size should be updated to %d",
                new_size);
    test_assert(lr_count(&buffer) == 0,
                "Buffer should still be empty after resize");

    /* Clean up */
    free(cells);
    free(new_cells);

    return LR_OK;
}

/* Test resizing a buffer with data */
lr_result_t test_resize_with_data()
{
    lr_result_t        result;
    struct lr_cell    *cells;
    struct lr_cell    *new_cells;
    lr_data_t          data;
    const unsigned int initial_size = 8;
    const unsigned int new_size     = 12;

    log_info("Testing resizing a buffer with data...");

    /* Initialize buffer */
    cells  = malloc(initial_size * sizeof(struct lr_cell));
    result = lr_init(&buffer, initial_size, cells);
    test_assert(result == LR_OK, "Buffer initialization should succeed");

    /* Add data */
    for (int i = 0; i < 5; i++) {
        result = lr_put(&buffer, i * 10, 1);
        test_assert(result == LR_OK, "Put %d should succeed", i);
    }

    /* Verify initial state */
    test_assert(lr_count(&buffer) == 5, "Buffer should contain 5 elements");
    test_assert(lr_count_owned(&buffer, 1) == 5,
                "Owner 1 should have 5 elements");

    /* Resize buffer */
    lr_dump(&buffer);
    lr_debug_strucuture_cells(&buffer);
    new_cells = malloc(new_size * sizeof(struct lr_cell));
    result    = lr_resize(&buffer, new_size, new_cells);
    lr_debug_strucuture_cells(&buffer);
    test_assert(result == LR_OK, "Buffer resize should succeed");
    lr_dump(&buffer);

    /* Verify data is preserved */
    test_assert(lr_count(&buffer) == 5,
                "Buffer should still contain 5 elements after resize");
    test_assert(lr_count_owned(&buffer, 1) == 5,
                "Owner 1 should still have 5 elements");

    /* Verify data integrity */
    for (int i = 0; i < 5; i++) {
        result = lr_get(&buffer, &data, 1);
        test_assert(result == LR_OK, "Get %d should succeed", i);
        test_assert(data == i * 10, "Retrieved data should be %d, got %lu",
                    i * 10, data);
    }

    /* Clean up */
    free(cells);
    free(new_cells);

    return LR_OK;
}

/* Test resizing a buffer with multiple owners */
lr_result_t test_resize_with_multiple_owners()
{
    lr_result_t        result;
    struct lr_cell    *cells;
    struct lr_cell    *new_cells;
    lr_data_t          data;
    const unsigned int initial_size = 10;
    const unsigned int new_size     = 15;

    log_info("Testing resizing a buffer with multiple owners...");

    /* Initialize buffer */
    cells  = malloc(initial_size * sizeof(struct lr_cell));
    result = lr_init(&buffer, initial_size, cells);
    test_assert(result == LR_OK, "Buffer initialization should succeed");

    /* Add data for different owners */
    result = lr_put(&buffer, 100, 1);
    test_assert(result == LR_OK, "Put for owner 1 should succeed");
    result = lr_put(&buffer, 101, 1);
    test_assert(result == LR_OK, "Put for owner 1 should succeed");

    result = lr_put(&buffer, 200, 2);
    test_assert(result == LR_OK, "Put for owner 2 should succeed");
    result = lr_put(&buffer, 201, 2);
    test_assert(result == LR_OK, "Put for owner 2 should succeed");

    result = lr_put(&buffer, 300, 3);
    test_assert(result == LR_OK, "Put for owner 3 should succeed");

    /* Verify initial state */
    test_assert(lr_count(&buffer) == 5, "Buffer should contain 5 elements");
    test_assert(lr_count_owned(&buffer, 1) == 2,
                "Owner 1 should have 2 elements");
    test_assert(lr_count_owned(&buffer, 2) == 2,
                "Owner 2 should have 2 elements");
    test_assert(lr_count_owned(&buffer, 3) == 1,
                "Owner 3 should have 1 element");

    /* Resize buffer */
    new_cells = malloc(new_size * sizeof(struct lr_cell));
    result    = lr_resize(&buffer, new_size, new_cells);
    test_assert(result == LR_OK, "Buffer resize should succeed");

    /* Verify data is preserved */
    test_assert(lr_count(&buffer) == 5,
                "Buffer should still contain 5 elements after resize");
    test_assert(lr_count_owned(&buffer, 1) == 2,
                "Owner 1 should still have 2 elements");
    test_assert(lr_count_owned(&buffer, 2) == 2,
                "Owner 2 should still have 2 elements");
    test_assert(lr_count_owned(&buffer, 3) == 1,
                "Owner 3 should still have 1 element");

    /* Verify data integrity for each owner */
    result = lr_get(&buffer, &data, 1);
    test_assert(result == LR_OK && data == 100,
                "First element for owner 1 should be 100");
    result = lr_get(&buffer, &data, 1);
    test_assert(result == LR_OK && data == 101,
                "Second element for owner 1 should be 101");

    result = lr_get(&buffer, &data, 2);
    test_assert(result == LR_OK && data == 200,
                "First element for owner 2 should be 200");
    result = lr_get(&buffer, &data, 2);
    test_assert(result == LR_OK && data == 201,
                "Second element for owner 2 should be 201");

    result = lr_get(&buffer, &data, 3);
    test_assert(result == LR_OK && data == 300,
                "Element for owner 3 should be 300");

    /* Clean up */
    free(cells);
    free(new_cells);

    return LR_OK;
}

/* Test resizing to a larger buffer */
lr_result_t test_resize_larger()
{
    lr_result_t        result;
    struct lr_cell    *cells;
    struct lr_cell    *new_cells;
    const unsigned int initial_size = 6;
    const unsigned int new_size     = 20;

    log_info("Testing resizing to a larger buffer...");

    /* Initialize buffer */
    cells  = malloc(initial_size * sizeof(struct lr_cell));
    result = lr_init(&buffer, initial_size, cells);
    test_assert(result == LR_OK, "Buffer initialization should succeed");

    /* Fill buffer close to capacity */
    for (int i = 0; i < 4; i++) {
        result = lr_put(&buffer, i * 10, 1);
        test_assert(result == LR_OK, "Put %d should succeed", i);
    }

    /* Verify buffer is nearly full */
    test_assert(lr_available(&buffer) == 1,
                "Buffer should have 1 slot available");

    /* Resize to larger buffer */
    new_cells = malloc(new_size * sizeof(struct lr_cell));
    result    = lr_resize(&buffer, new_size, new_cells);
    test_assert(result == LR_OK, "Buffer resize should succeed");

    /* Verify increased capacity */
    test_assert(buffer.size == new_size, "Buffer size should be updated to %d",
                new_size);
    test_assert(lr_available(&buffer) > 1,
                "Buffer should have more available space after resize");

    lr_debug_strucuture_cells(&buffer);
    lr_dump(&buffer);
    /* Add more data that wouldn't fit in original buffer */
    for (int i = 4; i < 15; i++) {
        result = lr_put(&buffer, i * 10, 1);
        test_assert(result == LR_OK, "Put %d after resize should succeed", i);
    }

    /* Clean up */
    free(cells);
    free(new_cells);

    return LR_OK;
}

/* Test resizing to a smaller buffer */
lr_result_t test_resize_smaller()
{
    lr_result_t        result;
    struct lr_cell    *cells;
    struct lr_cell    *new_cells;
    lr_data_t          data;
    const unsigned int initial_size = 15;
    const unsigned int new_size     = 8;

    log_info("Testing resizing to a smaller buffer...");

    /* Initialize buffer */
    cells  = malloc(initial_size * sizeof(struct lr_cell));
    result = lr_init(&buffer, initial_size, cells);
    test_assert(result == LR_OK, "Buffer initialization should succeed");

    /* Add just a few elements */
    for (int i = 0; i < 5; i++) {
        result = lr_put(&buffer, i * 10, 1);
        test_assert(result == LR_OK, "Put %d should succeed", i);
    }

    /* Resize to smaller buffer that can still hold the data */
    new_cells = malloc(new_size * sizeof(struct lr_cell));
    result    = lr_resize(&buffer, new_size, new_cells);
    test_assert(result == LR_OK, "Buffer resize should succeed");

    /* Verify data is preserved */
    test_assert(lr_count(&buffer) == 5,
                "Buffer should still contain 5 elements after resize");

    /* Verify data integrity */
    for (int i = 0; i < 5; i++) {
        result = lr_get(&buffer, &data, 1);
        test_assert(result == LR_OK, "Get %d should succeed", i);
        test_assert(data == i * 10, "Retrieved data should be %d, got %lu",
                    i * 10, data);
    }

    /* Clean up */
    free(cells);
    free(new_cells);

    return LR_OK;
}

/* Test edge cases for resize */
lr_result_t test_resize_edge_cases()
{
    lr_result_t     result;
    struct lr_cell *cells;
    struct lr_cell *new_cells;

    log_info("Testing resize edge cases...");

    /* Initialize buffer */
    cells  = malloc(10 * sizeof(struct lr_cell));
    result = lr_init(&buffer, 10, cells);
    test_assert(result == LR_OK, "Buffer initialization should succeed");

    /* Test resize with NULL cells */
    result = lr_resize(&buffer, 15, NULL);
    test_assert(result == LR_ERROR_NOMEMORY,
                "Resize with NULL cells should fail");

    /* Test resize with zero size */
    new_cells = malloc(5 * sizeof(struct lr_cell));
    result    = lr_resize(&buffer, 0, new_cells);
    test_assert(result == LR_ERROR_NOMEMORY, "Resize with size 0 should fail");

    /* Test resize to minimum viable size */
    result = lr_put(&buffer, 42, 1);
    test_assert(result == LR_OK, "Put should succeed");

    new_cells = malloc(3 * sizeof(struct lr_cell));
    result    = lr_resize(&buffer, 3, new_cells);
    test_assert(result == LR_OK,
                "Resize to minimum viable size should succeed");

    /* Clean up */
    free(cells);
    free(new_cells);

    return LR_OK;
}
