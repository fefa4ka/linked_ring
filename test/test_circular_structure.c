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
lr_result_t test_basic_circular_structure();
lr_result_t test_circular_after_operations();
lr_result_t test_multiple_owner_circularity();
lr_result_t test_circular_after_full();
lr_result_t run_all_tests();

/* Global buffer for testing */
struct linked_ring buffer;

/* Helper function to verify circular structure */
lr_result_t verify_circular_structure(struct linked_ring *lr, lr_owner_t owner) {
    struct lr_cell *owner_cell = lr_owner_find(lr, owner);
    if (owner_cell == NULL) {
        log_error("Owner cell not found for owner %lu", owner);
        return LR_ERROR_UNKNOWN;
    }
    
    log_info("Verifying circular structure for owner %lu", owner);
    log_info("Owner cell address: %p, data: %lu", (void*)owner_cell, owner_cell->data);
    
    struct lr_cell *head = lr_owner_head(lr, owner_cell);
    struct lr_cell *tail = lr_owner_tail(owner_cell);
    
    log_info("Head address: %p", (void*)head);
    log_info("Tail address: %p", (void*)tail);
    
    if (head == NULL || tail == NULL) {
        log_error("Head or tail is NULL for owner %lu", owner);
        return LR_ERROR_UNKNOWN;
    }
    
    log_info("Tail->next address: %p", (void*)tail->next);
    
    // If there's only one element, it should point to itself
    if (head == tail) {
        log_info("Single element case detected");
        if (head->next != head) {
            log_error("Single element doesn't form a circle (next: %p, head: %p)", 
                     (void*)head->next, (void*)head);
            return LR_ERROR_UNKNOWN;
        }
        log_info("Single element forms a proper circle");
        return LR_OK;
    }
    
    // Traverse the list and ensure we can reach the tail
    size_t count = 0;
    size_t max_count = lr_count_owned(lr, owner) * 2; // Safety limit
    struct lr_cell *current = head;
    
    log_info("Tracing path from head to tail (max steps: %zu):", max_count);
    log_info("  [0] %p (head)", (void*)current);
    
    while (current != tail && count < max_count) {
        current = current->next;
        count++;
        log_info("  [%zu] %p %s", count, (void*)current, 
                (current == tail ? "(tail)" : ""));
    }
    
    if (current != tail) {
        log_error("Could not reach tail from head within %zu steps", max_count);
        return LR_ERROR_UNKNOWN;
    }
    
    log_info("Successfully traced from head to tail in %zu steps", count);
    
    // Verify the global circular structure
    // In a single-circle design, we don't check that tail->next == head for each owner
    // Instead, we verify that we can traverse from head and eventually get back to it
    log_info("Verifying global circular structure...");
    current = head;
    count = 0;
    max_count = lr->size * 2; // Safety limit for the entire buffer
    bool found_head_again = false;
    
    do {
        current = current->next;
        count++;
        
        if (current == head) {
            found_head_again = true;
            break;
        }
        
        if (count >= max_count) {
            log_error("Could not complete the circle within %zu steps", max_count);
            return LR_ERROR_UNKNOWN;
        }
    } while (current != NULL);
    
    if (!found_head_again) {
        log_error("Global circular structure is broken - could not find way back to head");
        return LR_ERROR_UNKNOWN;
    }
    
    log_info("Global circular structure verified - found way back to head in %zu steps", count);
    log_info("Circular structure verified for owner %lu", owner);
    return LR_OK;
}

int main()
{
    lr_result_t result = run_all_tests();

    if (result == LR_OK) {
        log_info("All circular structure tests passed successfully!");
        return 0;
    } else {
        log_error("Circular structure tests failed with code %d", result);
        return 1;
    }
}

/* Helper function to visualize the circular structure */
void visualize_circular_structure(struct linked_ring *lr, lr_owner_t owner) {
    struct lr_cell *owner_cell = lr_owner_find(lr, owner);
    if (owner_cell == NULL) {
        log_error("Cannot visualize: Owner %lu not found", owner);
        return;
    }
    
    struct lr_cell *head = lr_owner_head(lr, owner_cell);
    struct lr_cell *tail = lr_owner_tail(owner_cell);
    
    if (head == NULL || tail == NULL) {
        log_error("Cannot visualize: Head or tail is NULL for owner %lu", owner);
        return;
    }
    
    printf("\n=== Circular Structure Visualization for Owner %lu ===\n", owner);
    printf("Owner cell: %p (data: %lu)\n", (void*)owner_cell, owner_cell->data);
    printf("Head: %p, Tail: %p\n", (void*)head, (void*)tail);
    
    printf("\nElements in owner's list:\n");
    printf("┌───────┬─────────────────┬────────────┬─────────────────┬──────────┐\n");
    printf("│ Index │     Address     │    Data    │    Next Addr    │   Role   │\n");
    printf("├───────┼─────────────────┼────────────┼─────────────────┼──────────┤\n");
    
    // First print the owner's elements from head to tail
    struct lr_cell *current = head;
    size_t index = 0;
    size_t max_count = lr_count_owned(lr, owner) * 2; // Safety limit
    
    while (current != tail && index < max_count) {
        const char* role = "";
        if (current == head) role = "HEAD";
        
        printf("│ %5zu │ %15p │ %10lu │ %15p │ %-8s │\n", 
               index, (void*)current, current->data, (void*)current->next, role);
        
        current = current->next;
        index++;
    }
    
    // Print the tail
    if (index < max_count) {
        printf("│ %5zu │ %15p │ %10lu │ %15p │ %-8s │\n", 
               index, (void*)tail, tail->data, (void*)tail->next, "TAIL");
    }
    
    printf("└───────┴─────────────────┴────────────┴─────────────────┴──────────┘\n");
    
    // Now trace the global circular structure
    printf("\nTracing global circular path from head:\n");
    printf("┌───────┬─────────────────┬────────────┬─────────────────┬──────────┐\n");
    printf("│ Step  │     Address     │    Data    │    Next Addr    │   Note   │\n");
    printf("├───────┼─────────────────┼────────────┼─────────────────┼──────────┤\n");
    
    current = head;
    size_t step = 0;
    max_count = lr->size * 2; // Safety limit for the entire buffer
    bool found_head_again = false;
    
    do {
        const char* note = "";
        if (current == head) note = "HEAD";
        else if (current == tail) note = "TAIL";
        
        printf("│ %5zu │ %15p │ %10lu │ %15p │ %-8s │\n", 
               step, (void*)current, current->data, (void*)current->next, note);
        
        current = current->next;
        step++;
        
        if (current == head) {
            found_head_again = true;
            break;
        }
        
        if (step >= max_count) {
            printf("│       │                 │            │                 │          │\n");
            printf("│  ...  │      LOOP DETECTED OR STRUCTURE BROKEN         │          │\n");
            break;
        }
    } while (current != NULL);
    
    printf("└───────┴─────────────────┴────────────┴─────────────────┴──────────┘\n");
    
    // Check if the global structure is circular
    if (found_head_again) {
        printf("✓ Global structure is circular (can traverse back to head)\n\n");
    } else {
        printf("✗ Global structure is NOT circular!\n\n");
    }
}

/* Run all test suites */
lr_result_t run_all_tests()
{
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
lr_result_t test_basic_circular_structure()
{
    lr_result_t result;
    struct lr_cell *cells;
    const unsigned int size = 10;
    
    log_info("Testing basic circular structure...");
    
    /* Initialize buffer */
    cells = malloc(size * sizeof(struct lr_cell));
    result = lr_init(&buffer, size, cells);
    test_assert(result == LR_OK, "Buffer initialization should succeed");
    
    /* Add a single element and verify circularity */
    result = lr_put(&buffer, 42, 1);
    test_assert(result == LR_OK, "Put should succeed");
    
    result = verify_circular_structure(&buffer, 1);
    test_assert(result == LR_OK, "Single element should form a circular structure");
    
    /* Add more elements and verify circularity */
    result = lr_put(&buffer, 43, 1);
    test_assert(result == LR_OK, "Put should succeed");
    
    result = lr_put(&buffer, 44, 1);
    test_assert(result == LR_OK, "Put should succeed");
    
    result = verify_circular_structure(&buffer, 1);
    test_assert(result == LR_OK, "Multiple elements should form a circular structure");
    
    /* Clean up */
    free(cells);
    
    return LR_OK;
}

/* Test circular structure after various operations */
lr_result_t test_circular_after_operations()
{
    lr_result_t result;
    struct lr_cell *cells;
    lr_data_t data;
    const unsigned int size = 10;
    
    log_info("Testing circular structure after operations...");
    
    /* Initialize buffer */
    cells = malloc(size * sizeof(struct lr_cell));
    result = lr_init(&buffer, size, cells);
    test_assert(result == LR_OK, "Buffer initialization should succeed");
    
    /* Add elements */
    for (int i = 0; i < 5; i++) {
        result = lr_put(&buffer, i * 10, 1);
        test_assert(result == LR_OK, "Put should succeed");
    }
    
    /* Verify initial circular structure */
    result = verify_circular_structure(&buffer, 1);
    test_assert(result == LR_OK, "Initial structure should be circular");
    
    /* Remove an element from the head */
    result = lr_get(&buffer, &data, 1);
    test_assert(result == LR_OK, "Get should succeed");
    test_assert(data == 0, "First element should be 0");
    
    /* Verify circularity after head removal */
    result = verify_circular_structure(&buffer, 1);
    test_assert(result == LR_OK, "Structure should remain circular after head removal");
    
    /* Remove an element from the tail */
    result = lr_pop(&buffer, &data, 1);
    test_assert(result == LR_OK, "Pop should succeed");
    test_assert(data == 40, "Last element should be 40");
    
    /* Verify circularity after tail removal */
    result = verify_circular_structure(&buffer, 1);
    test_assert(result == LR_OK, "Structure should remain circular after tail removal");
    
    /* Remove from the middle */
    result = lr_pull(&buffer, &data, 1, 1);
    test_assert(result == LR_OK, "Pull should succeed");
    
    /* Verify circularity after middle removal */
    result = verify_circular_structure(&buffer, 1);
    test_assert(result == LR_OK, "Structure should remain circular after middle removal");
    
    /* Add more elements */
    result = lr_put(&buffer, 100, 1);
    test_assert(result == LR_OK, "Put should succeed");
    
    result = lr_push(&buffer, 200, 1);
    test_assert(result == LR_OK, "Push should succeed");
    
    /* Verify circularity after additions */
    result = verify_circular_structure(&buffer, 1);
    test_assert(result == LR_OK, "Structure should remain circular after additions");
    
    /* Clean up */
    free(cells);
    
    return LR_OK;
}

/* Test circular structure with multiple owners */
lr_result_t test_multiple_owner_circularity()
{
    lr_result_t result;
    struct lr_cell *cells;
    const unsigned int size = 15;
    
    log_info("Testing circular structure with multiple owners...");
    
    /* Initialize buffer */
    cells = malloc(size * sizeof(struct lr_cell));
    result = lr_init(&buffer, size, cells);
    test_assert(result == LR_OK, "Buffer initialization should succeed");
    
    /* Add elements for owner 1 */
    for (int i = 0; i < 3; i++) {
        result = lr_put(&buffer, i * 10, 1);
        test_assert(result == LR_OK, "Put for owner 1 should succeed");
    }
    
    /* Add elements for owner 2 */
    for (int i = 0; i < 3; i++) {
        result = lr_put(&buffer, i * 100, 2);
        test_assert(result == LR_OK, "Put for owner 2 should succeed");
    }
    
    /* Add elements for owner 3 */
    for (int i = 0; i < 3; i++) {
        result = lr_put(&buffer, i * 1000, 3);
        test_assert(result == LR_OK, "Put for owner 3 should succeed");
    }
    
    /* Debug the buffer state before verification */
    log_info("Buffer state after adding all elements:");
    log_info("Total elements: %zu", lr_count(&buffer));
    log_info("Owner 1 elements: %zu", lr_count_owned(&buffer, 1));
    log_info("Owner 2 elements: %zu", lr_count_owned(&buffer, 2));
    log_info("Owner 3 elements: %zu", lr_count_owned(&buffer, 3));
    
    /* Debug the raw cell structure */
    log_info("Examining raw buffer structure...");
    lr_debug_strucuture_cells(&buffer);
    
    /* Debug the circular structure for each owner */
    log_info("Debugging circular structure for each owner...");
    lr_debug_structure_circular(&buffer, 1);
    lr_debug_structure_circular(&buffer, 2);
    lr_debug_structure_circular(&buffer, 3);
    
    /* Visualize and verify circular structure for each owner */
    log_info("Visualizing and verifying circular structure for owner 1...");
    visualize_circular_structure(&buffer, 1);
    result = verify_circular_structure(&buffer, 1);
    test_assert(result == LR_OK, "Owner 1's structure should be circular");
    
    log_info("Visualizing and verifying circular structure for owner 2...");
    visualize_circular_structure(&buffer, 2);
    result = verify_circular_structure(&buffer, 2);
    test_assert(result == LR_OK, "Owner 2's structure should be circular");
    
    log_info("Visualizing and verifying circular structure for owner 3...");
    visualize_circular_structure(&buffer, 3);
    result = verify_circular_structure(&buffer, 3);
    test_assert(result == LR_OK, "Owner 3's structure should be circular");
    
    /* Clean up */
    free(cells);
    
    return LR_OK;
}

/* Test circular structure after buffer full condition */
lr_result_t test_circular_after_full()
{
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
    for (int i = 0; i < size - 1; i++) { /* -1 because one cell is for owner */
        result = lr_put(&buffer, i * 10, 1);
        test_assert(result == LR_OK, "Put %d should succeed", i);
    }
    
    /* Verify buffer is full */
    result = lr_put(&buffer, 999, 1);
    test_assert(result == LR_ERROR_BUFFER_FULL, "Put to full buffer should return BUFFER_FULL");
    
    /* Verify circular structure when full */
    result = verify_circular_structure(&buffer, 1);
    test_assert(result == LR_OK, "Structure should be circular when buffer is full");
    
    /* Remove an element */
    result = lr_get(&buffer, &data, 1);
    test_assert(result == LR_OK, "Get should succeed");
    
    /* Add another element */
    result = lr_put(&buffer, 999, 1);
    test_assert(result == LR_OK, "Put after making space should succeed");
    
    /* Verify circular structure after cycling */
    result = verify_circular_structure(&buffer, 1);
    test_assert(result == LR_OK, "Structure should remain circular after cycling");
    
    /* Clean up */
    free(cells);
    
    return LR_OK;
}
