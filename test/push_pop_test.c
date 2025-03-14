#include <lr.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
lr_result_t test_push_pop_basic();
lr_result_t test_push_pop_multiple_owners();
lr_result_t test_push_pop_mixed_operations();
lr_result_t test_push_pop_edge_cases();
lr_result_t run_all_tests();

/* Global buffer for testing */
struct linked_ring buffer;

int main()
{
    lr_result_t result = run_all_tests();
    
    if (result == LR_OK) {
        log_info("All push/pop tests passed successfully!");
        return 0;
    } else {
        log_error("Push/pop tests failed with code %d", result);
        return 1;
    }
}

/* Run all test suites */
lr_result_t run_all_tests()
{
    lr_result_t result;
    
    log_info("=== Running Linked Ring Buffer Push/Pop Tests ===");
    
    result = test_push_pop_basic();
    if (result != LR_OK)
        return result;
        
    result = test_push_pop_multiple_owners();
    if (result != LR_OK)
        return result;
        
    result = test_push_pop_mixed_operations();
    if (result != LR_OK)
        return result;
        
    result = test_push_pop_edge_cases();
    if (result != LR_OK)
        return result;
    
    return LR_OK;
}

/* Test basic push and pop operations */
lr_result_t test_push_pop_basic()
{
    lr_result_t result;
    struct lr_cell *cells;
    lr_data_t data;
    const unsigned int size = 10;
    
    log_info("Testing basic push and pop operations...");
    
    /* Initialize buffer */
    cells = malloc(size * sizeof(struct lr_cell));
    result = lr_init(&buffer, size, cells);
    test_assert(result == LR_OK, "Buffer initialization should succeed");
    
    /* Test push operation */
    result = lr_put(&buffer, 10, 1);  // Add first element with put
    test_assert(result == LR_OK, "Put first element should succeed");
    
    result = lr_push(&buffer, 20, 1);  // Push to tail
    test_assert(result == LR_OK, "Push to tail should succeed");
    
    result = lr_push(&buffer, 30, 1);  // Push another to tail
    test_assert(result == LR_OK, "Push another element should succeed");
    
    /* Verify elements with get and pop */
    result = lr_get(&buffer, &data, 1);  // Get from head
    test_assert(result == LR_OK && data == 10, 
                "First element should be 10, got %lu", data);
    
    result = lr_pop(&buffer, &data, 1);  // Pop from tail
    test_assert(result == LR_OK && data == 30, 
                "Popped element should be 30, got %lu", data);
    
    result = lr_pop(&buffer, &data, 1);  // Pop again from tail
    test_assert(result == LR_OK && data == 20, 
                "Popped element should be 20, got %lu", data);
    
    /* Verify buffer is now empty */
    result = lr_get(&buffer, &data, 1);
    test_assert(result == LR_ERROR_BUFFER_EMPTY, 
                "Buffer should be empty after all operations");
    
    /* Clean up */
    free(cells);
    
    return LR_OK;
}

/* Test push and pop with multiple owners */
lr_result_t test_push_pop_multiple_owners()
{
    lr_result_t result;
    struct lr_cell *cells;
    lr_data_t data;
    const unsigned int size = 15;
    
    log_info("Testing push and pop with multiple owners...");
    
    /* Initialize buffer */
    cells = malloc(size * sizeof(struct lr_cell));
    result = lr_init(&buffer, size, cells);
    test_assert(result == LR_OK, "Buffer initialization should succeed");
    
    /* Add elements for owner 1 */
    result = lr_put(&buffer, 10, 1);
    test_assert(result == LR_OK, "Put for owner 1 should succeed");
    
    result = lr_push(&buffer, 20, 1);
    test_assert(result == LR_OK, "Push for owner 1 should succeed");
    
    /* Add elements for owner 2 */
    result = lr_put(&buffer, 100, 2);
    test_assert(result == LR_OK, "Put for owner 2 should succeed");
    
    result = lr_push(&buffer, 200, 2);
    test_assert(result == LR_OK, "Push for owner 2 should succeed");
    
    /* Verify elements with pop for each owner */
    result = lr_pop(&buffer, &data, 1);
    test_assert(result == LR_OK && data == 20, 
                "Popped element for owner 1 should be 20, got %lu", data);
    
    result = lr_pop(&buffer, &data, 2);
    test_assert(result == LR_OK && data == 200, 
                "Popped element for owner 2 should be 200, got %lu", data);
    
    /* Verify remaining elements with get */
    result = lr_get(&buffer, &data, 1);
    test_assert(result == LR_OK && data == 10, 
                "Remaining element for owner 1 should be 10, got %lu", data);
    
    result = lr_get(&buffer, &data, 2);
    test_assert(result == LR_OK && data == 100, 
                "Remaining element for owner 2 should be 100, got %lu", data);
    
    /* Clean up */
    free(cells);
    
    return LR_OK;
}

/* Test mixed push, pop, put, and get operations */
lr_result_t test_push_pop_mixed_operations()
{
    lr_result_t result;
    struct lr_cell *cells;
    lr_data_t data;
    const unsigned int size = 10;
    
    log_info("Testing mixed push, pop, put, and get operations...");
    
    /* Initialize buffer */
    cells = malloc(size * sizeof(struct lr_cell));
    result = lr_init(&buffer, size, cells);
    test_assert(result == LR_OK, "Buffer initialization should succeed");
    
    /* Add elements with put */
    result = lr_put(&buffer, 10, 1);
    test_assert(result == LR_OK, "Put first element should succeed");
    
    result = lr_put(&buffer, 20, 1);
    test_assert(result == LR_OK, "Put second element should succeed");
    
    /* Add elements with push */
    result = lr_push(&buffer, 30, 1);
    test_assert(result == LR_OK, "Push to tail should succeed");
    
    /* Verify order with get and pop */
    result = lr_get(&buffer, &data, 1);
    test_assert(result == LR_OK && data == 10, 
                "First element should be 10, got %lu", data);
    
    result = lr_pop(&buffer, &data, 1);
    test_assert(result == LR_OK && data == 30, 
                "Popped element should be 30, got %lu", data);
    
    /* Add more elements in mixed order */
    result = lr_put(&buffer, 40, 1);
    test_assert(result == LR_OK, "Put after pop should succeed");
    
    result = lr_push(&buffer, 50, 1);
    test_assert(result == LR_OK, "Push after put should succeed");
    
    /* Verify final order */
    result = lr_get(&buffer, &data, 1);
    test_assert(result == LR_OK && data == 20, 
                "First element should be 20, got %lu", data);
    
    result = lr_get(&buffer, &data, 1);
    test_assert(result == LR_OK && data == 40, 
                "Second element should be 40, got %lu", data);
    
    result = lr_pop(&buffer, &data, 1);
    test_assert(result == LR_OK && data == 50, 
                "Popped element should be 50, got %lu", data);
    
    /* Clean up */
    free(cells);
    
    return LR_OK;
}

/* Test edge cases for push and pop */
lr_result_t test_push_pop_edge_cases()
{
    lr_result_t result;
    struct lr_cell *cells;
    lr_data_t data;
    const unsigned int size = 5;
    
    log_info("Testing push and pop edge cases...");
    
    /* Initialize buffer */
    cells = malloc(size * sizeof(struct lr_cell));
    result = lr_init(&buffer, size, cells);
    test_assert(result == LR_OK, "Buffer initialization should succeed");
    
    /* Test pop on empty buffer */
    result = lr_pop(&buffer, &data, 1);
    test_assert(result == LR_ERROR_BUFFER_EMPTY, 
                "Pop on empty buffer should return BUFFER_EMPTY");
    
    /* Test push to full buffer */
    /* First fill the buffer */
    result = lr_put(&buffer, 10, 1);
    test_assert(result == LR_OK, "Put first element should succeed");
    
    /* Fill buffer to capacity */
    for (int i = 0; i < size - 2; i++) {  // -2 for owner cell and first element
        result = lr_push(&buffer, 20 + i, 1);
        test_assert(result == LR_OK, "Push %d should succeed", i);
    }
    
    /* Verify buffer is full */
    result = lr_push(&buffer, 99, 1);
    test_assert(result == LR_ERROR_BUFFER_FULL, 
                "Push to full buffer should return BUFFER_FULL");
    
    /* Test pop all elements */
    for (int i = 0; i < size - 1; i++) {  // -1 for owner cell
        result = lr_pop(&buffer, &data, 1);
        test_assert(result == LR_OK, "Pop %d should succeed", i);
    }
    
    /* Verify buffer is empty */
    result = lr_pop(&buffer, &data, 1);
    test_assert(result == LR_ERROR_BUFFER_EMPTY, 
                "Pop on empty buffer should return BUFFER_EMPTY");
    
    /* Clean up */
    free(cells);
    
    return LR_OK;
}
