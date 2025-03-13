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

/* Global buffer for testing */
struct linked_ring buffer;

/* Function to verify circular structure integrity */
lr_result_t verify_circular_structure(struct linked_ring *lr, lr_owner_t owner) {
    struct lr_cell *owner_cell = lr_owner_find(lr, owner);
    if (owner_cell == NULL) {
        log_error("Owner cell not found");
        return LR_ERROR_UNKNOWN;
    }
    
    struct lr_cell *head = lr_owner_head(lr, owner_cell);
    struct lr_cell *tail = lr_owner_tail(owner_cell);
    struct lr_cell *current = head;
    size_t count = 0;
    size_t expected_count = lr_count_owned(lr, owner);
    
    /* Print detailed debug information */
    log_info("Verifying circular structure for owner %lu", owner);
    log_info("Owner cell: %p, Head: %p, Tail: %p, Tail->next: %p", 
             owner_cell, head, tail, tail->next);
    log_info("Expected count: %zu", expected_count);
    
    /* Follow the links and count elements */
    printf("Element chain: ");
    do {
        printf("%p -> ", current);
        count++;
        current = current->next;
        
        /* Detect infinite loops */
        if (count > lr->size) {
            printf("LOOP!\n");
            log_error("Circular structure broken - infinite loop detected");
            return LR_ERROR_UNKNOWN;
        }
    } while (current != head && count < expected_count);
    printf("%p (should be head)\n", current);
    
    /* Verify we've seen all elements */
    if (count != expected_count) {
        log_error("Circular structure broken - expected %zu elements, found %zu", 
                  expected_count, count);
        return LR_ERROR_UNKNOWN;
    }
    
    /* Verify tail points to the last element */
    current = head;
    for (size_t i = 1; i < count; i++) {
        current = current->next;
    }
    
    if (current != tail) {
        log_error("Tail pointer is incorrect - expected %p, got %p", 
                  tail, current);
        return LR_ERROR_UNKNOWN;
    }
    
	lr_debug_circular_structure(&buffer, 1);
	lr_debug_circular_structure(&buffer,        2);
    /* Verify the circular nature - tail's next should point to head */
    if (tail->next != head) {
        log_error("Circular structure broken - tail->next (%p) does not point to head (%p)",
                  tail->next, head);
        return LR_ERROR_UNKNOWN;
    }
    
    log_ok("Circular structure verified successfully");
    return LR_OK;
}

/* Test function declarations */
lr_result_t test_basic_circular_structure();
lr_result_t test_circular_after_operations();
lr_result_t test_multiple_owner_circularity();
lr_result_t test_circular_after_full();
lr_result_t run_all_tests();

int main() {
    lr_result_t result = run_all_tests();
    
    if (result == LR_OK) {
        log_info("All circular structure tests passed successfully!");
        return 0;
    } else {
        log_error("Circular structure tests failed with code %d", result);
        return 1;
    }
}

/* Run all test suites */
lr_result_t run_all_tests() {
    lr_result_t result;
    
    log_info("=== Running Linked Ring Buffer Circular Structure Tests ===");
    
    result = test_basic_circular_structure();
    if (result != LR_OK)
        return result;
        
    result = test_circular_after_operations();
    if (result != LR_OK)
        return result;
        
    result = test_multiple_owner_circularity();
    if (result != LR_OK)
        return result;
        
    result = test_circular_after_full();
    if (result != LR_OK)
        return result;
    
    return LR_OK;
}

/* Test basic circular structure */
lr_result_t test_basic_circular_structure() {
    lr_result_t result;
    struct lr_cell *cells;
    const unsigned int size = 10;
    
    log_info("Testing basic circular structure...");
    
    /* Initialize buffer */
    cells = malloc(size * sizeof(struct lr_cell));
    result = lr_init(&buffer, size, cells);
    test_assert(result == LR_OK, "Buffer initialization should succeed");
    
    /* Add some data */
    for (int i = 0; i < 5; i++) {
        result = lr_put(&buffer, i * 10, 1);
        test_assert(result == LR_OK, "Put %d should succeed", i);
    }
    
    /* Verify circular structure */
    result = verify_circular_structure(&buffer, 1);
    test_assert(result == LR_OK, "Circular structure should be intact after puts");
    
    /* Clean up */
    free(cells);
    
    return LR_OK;
}

/* Test circular structure after various operations */
lr_result_t test_circular_after_operations() {
    lr_result_t result;
    struct lr_cell *cells;
    lr_data_t data;
    const unsigned int size = 10;
    
    log_info("Testing circular structure after operations...");
    
    /* Initialize buffer */
    cells = malloc(size * sizeof(struct lr_cell));
    result = lr_init(&buffer, size, cells);
    test_assert(result == LR_OK, "Buffer initialization should succeed");
    
    /* Add some data */
    for (int i = 0; i < 5; i++) {
        result = lr_put(&buffer, i * 10, 1);
        test_assert(result == LR_OK, "Put %d should succeed", i);
    }
    
    /* Verify initial circular structure */
    result = verify_circular_structure(&buffer, 1);
    test_assert(result == LR_OK, "Initial circular structure should be intact");
    
    /* Perform a get operation */
    result = lr_get(&buffer, &data, 1);
    test_assert(result == LR_OK, "Get should succeed");
    test_assert(data == 0, "Retrieved data should be 0, got %lu", data);
    
    /* Verify circular structure after get */
    result = verify_circular_structure(&buffer, 1);
    test_assert(result == LR_OK, "Circular structure should be intact after get");
    
    /* Perform an insert operation */
    result = lr_insert(&buffer, 25, 1, 1);
    test_assert(result == LR_OK, "Insert should succeed");
    
    /* Verify circular structure after insert */
    result = verify_circular_structure(&buffer, 1);
    test_assert(result == LR_OK, "Circular structure should be intact after insert");
    
    /* Perform a pull operation */
    result = lr_pull(&buffer, &data, 1, 2);
    test_assert(result == LR_OK, "Pull should succeed");
    
    /* Verify circular structure after pull */
    result = verify_circular_structure(&buffer, 1);
    test_assert(result == LR_OK, "Circular structure should be intact after pull");
    
    /* Clean up */
    free(cells);
    
    return LR_OK;
}

/* Test circular structure with multiple owners */
lr_result_t test_multiple_owner_circularity() {
    lr_result_t result;
    struct lr_cell *cells;
    const unsigned int size = 15;
    
    log_info("Testing circular structure with multiple owners...");
    
    /* Initialize buffer */
    cells = malloc(size * sizeof(struct lr_cell));
    result = lr_init(&buffer, size, cells);
    test_assert(result == LR_OK, "Buffer initialization should succeed");
    
    /* Add data for different owners one at a time and verify after each */
    result = lr_put(&buffer, 10, 1);
    test_assert(result == LR_OK, "Put for owner 1 should succeed");
    result = verify_circular_structure(&buffer, 1);
    test_assert(result == LR_OK, "Circular structure for owner 1 should be intact after first put");
    
    result = lr_put(&buffer, 15, 2);
    test_assert(result == LR_OK, "Put for owner 2 should succeed");
    result = verify_circular_structure(&buffer, 2);
    test_assert(result == LR_OK, "Circular structure for owner 2 should be intact after first put");
    
    result = lr_put(&buffer, 19, 3);
    test_assert(result == LR_OK, "Put for owner 3 should succeed");
    
    /* Debug the circular structure for owner 3 */
    log_info("Detailed debug for owner 3 after first put:");
    lr_debug_circular_structure(&buffer, 3);
    
    result = verify_circular_structure(&buffer, 3);
    test_assert(result == LR_OK, "Circular structure for owner 3 should be intact after first put");
    
    /* Continue adding more data */
    for (int i = 1; i < 3; i++) {
        result = lr_put(&buffer, i * 10, 1);
        test_assert(result == LR_OK, "Put for owner 1 should succeed");
        result = verify_circular_structure(&buffer, 1);
        test_assert(result == LR_OK, "Circular structure for owner 1 should be intact");
        
        result = lr_put(&buffer, i * 10 + 5, 2);
        test_assert(result == LR_OK, "Put for owner 2 should succeed");
        result = verify_circular_structure(&buffer, 2);
        test_assert(result == LR_OK, "Circular structure for owner 2 should be intact");
        
        result = lr_put(&buffer, i * 10 + 9, 3);
        test_assert(result == LR_OK, "Put for owner 3 should succeed");
        
        /* Debug the circular structure for owner 3 */
        log_info("Detailed debug for owner 3 after put %d:", i+1);
        lr_debug_circular_structure(&buffer, 3);
        
        result = verify_circular_structure(&buffer, 3);
        test_assert(result == LR_OK, "Circular structure for owner 3 should be intact");
    }
    
    /* Final verification for all owners */
    log_info("Final verification for all owners:");
    result = verify_circular_structure(&buffer, 1);
    test_assert(result == LR_OK, "Final circular structure for owner 1 should be intact");
    
    result = verify_circular_structure(&buffer, 2);
    test_assert(result == LR_OK, "Final circular structure for owner 2 should be intact");
    
    result = verify_circular_structure(&buffer, 3);
    test_assert(result == LR_OK, "Final circular structure for owner 3 should be intact");
    
    /* Clean up */
    free(cells);
    
    return LR_OK;
}

/* Test circular structure after buffer full condition */
lr_result_t test_circular_after_full() {
    lr_result_t result;
    struct lr_cell *cells;
    lr_data_t data;
    const unsigned int size = 6;
    
    log_info("Testing circular structure after buffer full condition...");
    
    /* Initialize buffer */
    cells = malloc(size * sizeof(struct lr_cell));
    result = lr_init(&buffer, size, cells);
    test_assert(result == LR_OK, "Buffer initialization should succeed");
    
    /* Fill buffer to capacity */
    for (int i = 0; i < size - 1; i++) {
        result = lr_put(&buffer, i * 10, 1);
        test_assert(result == LR_OK, "Put %d should succeed", i);
    }
    
    /* Verify buffer is full */
    result = lr_put(&buffer, 999, 1);
    test_assert(result == LR_ERROR_BUFFER_FULL, 
                "Put to full buffer should return BUFFER_FULL");
    
    /* Verify circular structure when buffer is full */
    result = verify_circular_structure(&buffer, 1);
    test_assert(result == LR_OK, "Circular structure should be intact when buffer is full");
    
    /* Remove an element */
    result = lr_get(&buffer, &data, 1);
    test_assert(result == LR_OK, "Get should succeed");
    
    /* Add another element */
    result = lr_put(&buffer, 999, 1);
    test_assert(result == LR_OK, "Put after making space should succeed");
    
    /* Verify circular structure after recovery from full */
    result = verify_circular_structure(&buffer, 1);
    test_assert(result == LR_OK, "Circular structure should be intact after recovery from full");
    
    /* Clean up */
    free(cells);
    
    return LR_OK;
}
