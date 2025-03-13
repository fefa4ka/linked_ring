#include <lr.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Test utilities */
#define log_print(type, message, ...) \
    printf(type "\t" message "\n", ##__VA_ARGS__)
#define log_info(message, ...) log_print("INFO", message, ##__VA_ARGS__)
#define log_ok(message, ...) log_print("OK", message, ##__VA_ARGS__)
#define log_error(message, ...) \
    log_print("\e[1m\e[31mERROR\e[39m\e[0m", message " (%s:%d)", \
              ##__VA_ARGS__, __FILE__, __LINE__)
#define log_debug(message, ...) log_print("DEBUG", message, ##__VA_ARGS__)
#define log_verbose(message, ...) log_print("VERBOSE", message, ##__VA_ARGS__)

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

/* Statistics tracking */
typedef struct {
    size_t total_puts;
    size_t total_gets;
    size_t failed_puts;
    size_t failed_gets;
    size_t max_occupancy;
    size_t segfault_risk_count;
} buffer_stats_t;

buffer_stats_t stats = {0};

/* Define enum for owners */
typedef enum {
    OWNER_SPI_IN,
    OWNER_SPI_OUT,
    OWNER_I2C_IN,
    OWNER_I2C_OUT,
    OWNER_UART_IN,
    OWNER_UART_OUT,
    NUM_OWNERS
} owner_t;

/* Convert owner enum to string for better logging */
const char* owner_to_string(owner_t owner) {
    static const char* owner_names[] = {
        "SPI_IN", "SPI_OUT", "I2C_IN", "I2C_OUT", "UART_IN", "UART_OUT"
    };
    
    if (owner < NUM_OWNERS) {
        return owner_names[owner];
    }
    return "UNKNOWN";
}

/* Debug function to validate buffer integrity */
void validate_buffer(struct linked_ring *lr, const char *checkpoint)
{
    log_debug("=== Buffer validation at checkpoint: %s ===", checkpoint);

    if (lr == NULL) {
        log_error("Buffer is NULL");
        stats.segfault_risk_count++;
        return;
    }

    log_debug("Buffer address: %p", lr);
    log_debug("Cells address: %p", lr->cells);
    log_debug("Size: %u", lr->size);
    log_debug("Write pointer: %p", lr->write);
    log_debug("Owners pointer: %p", lr->owners);

    // Check if owners is NULL
    if (lr->owners == NULL) {
        log_debug("Owners is NULL - this is valid if no owners have been added");
        return;
    }

    // Check owners->next
    if (lr->owners->next == NULL) {
        log_debug("WARNING: owners->next is NULL, this may cause segfault in lr_count");
        stats.segfault_risk_count++;
        return;
    }

    // Validate the circular list structure
    struct lr_cell *head = lr->owners->next->next;
    if (head == NULL) {
        log_debug("WARNING: head (owners->next->next) is NULL");
        stats.segfault_risk_count++;
        return;
    }

    log_debug("Head pointer: %p", head);

    // Check if we can safely traverse the list
    struct lr_cell *needle = head;
    size_t count = 0;
    const size_t MAX_ITERATIONS = lr->size * 2; // Safety limit
    bool circular_found = false;

    log_debug("Starting list traversal...");
    while (needle != NULL && count < MAX_ITERATIONS) {
        log_debug("  Node %zu: %p, data: 0x%lx, next: %p", count, needle,
                  needle->data, needle->next);

        if (needle->next == head) {
            log_debug("  Found circular reference back to head");
            circular_found = true;
            break;
        }

        if (needle->next == NULL) {
            log_error("  Found NULL next pointer before completing circle!");
            stats.segfault_risk_count++;
            break;
        }

        needle = needle->next;
        count++;
    }

    if (count >= MAX_ITERATIONS) {
        log_error("Possible infinite loop detected in buffer list!");
        stats.segfault_risk_count++;
    } else if (!circular_found) {
        log_error("Circular structure is broken - did not find path back to head!");
        stats.segfault_risk_count++;
    } else {
        log_debug("List traversal complete, found %zu nodes in circular structure", count);
    }

    // Check owners
    log_debug("Checking owners...");
    size_t owner_count = lr_owners_count(lr);
    log_debug("Owner count: %zu", owner_count);

    for (struct lr_cell *owner_cell = lr->owners;
         owner_cell < lr->owners + owner_count; owner_cell++) {
        log_debug("  Owner %zu: data: 0x%lx, next: %p",
                  (size_t)(owner_cell - lr->owners), owner_cell->data,
                  owner_cell->next);
        
        // Verify owner's tail pointer
        if (owner_cell->next == NULL) {
            log_debug("  WARNING: Owner %zu has NULL tail pointer", 
                     (size_t)(owner_cell - lr->owners));
            stats.segfault_risk_count++;
        }
    }
}

/* Safe version of lr_count that won't segfault */
size_t safe_lr_count(struct linked_ring *lr)
{
    struct lr_cell *head;
    struct lr_cell *needle;
    size_t length = 0;

    log_debug("Performing safe count...");

    if (lr == NULL || lr->owners == NULL) {
        log_debug("Buffer or owners is NULL, count = 0");
        return 0;
    }

    if (lr->owners->next == NULL) {
        log_debug("owners->next is NULL, count = 0");
        stats.segfault_risk_count++;
        return 0;
    }

    head = lr->owners->next->next;
    if (head == NULL) {
        log_debug("head (owners->next->next) is NULL, count = 0");
        stats.segfault_risk_count++;
        return 0;
    }

    length = 1;
    needle = head;

    log_debug("Starting count with head=%p, needle=%p", head, needle);

    // Safety limit to prevent infinite loops
    size_t max_iterations = lr->size * 2;
    size_t iterations = 0;

    while (needle != NULL && needle->next != NULL && iterations < max_iterations) {
        if (needle->next == head) {
            log_debug("Found circular reference, ending count");
            break;
        }

        needle = needle->next;
        length += 1;
        iterations++;

        log_debug("  Iteration %zu: needle=%p, needle->next=%p", iterations,
                  needle, needle->next ? needle->next : NULL);
    }

    if (iterations >= max_iterations) {
        log_error("Possible infinite loop in count!");
        stats.segfault_risk_count++;
        return 0;  // Return 0 instead of potentially incorrect count
    }

    if (needle == NULL || needle->next == NULL) {
        log_error("Broken circular structure in count!");
        stats.segfault_risk_count++;
        return length;  // Return what we counted so far
    }

    log_debug("Count complete, length = %zu", length);
    return length;
}

/* Add data with a specific owner */
lr_result_t add_data(lr_owner_t owner, lr_data_t value)
{
    lr_result_t result;
    size_t count = safe_lr_count(&buffer);
    size_t available = lr_available(&buffer);
    
    // Track maximum occupancy
    if (count > stats.max_occupancy) {
        stats.max_occupancy = count;
    }
    
    // Attempt to put data in the buffer
    result = lr_put(&buffer, value, owner);
    stats.total_puts++;
    
    if (result == LR_OK) {
        log_verbose("Added data: owner=%s, value=0x%lx, buffer_count=%lu/%lu", 
                   owner_to_string(owner), value, count + 1, count + available + 1);
        return LR_OK;
    } else {
        stats.failed_puts++;
        if (result == LR_ERROR_BUFFER_FULL) {
            log_verbose("Buffer full: Failed to add data (owner=%s, value=0x%lx)", 
                       owner_to_string(owner), value);
        } else {
            log_error("Failed to add data: owner=%s, value=0x%lx, error=%d", 
                     owner_to_string(owner), value, result);
        }
        return result;
    }
}

/* Get data from a specific owner */
lr_result_t get_data(lr_owner_t owner, lr_data_t *value)
{
    lr_result_t result;
    size_t count = safe_lr_count(&buffer);
    
    // Attempt to get data from the buffer
    result = lr_get(&buffer, value, owner);
    stats.total_gets++;
    
    if (result == LR_OK) {
        log_verbose("Retrieved data: owner=%s, value=0x%lx, buffer_count=%lu", 
                   owner_to_string(owner), *value, count - 1);
        return LR_OK;
    } else {
        stats.failed_gets++;
        if (result == LR_ERROR_BUFFER_EMPTY) {
            log_verbose("No data for owner %s", owner_to_string(owner));
        } else {
            log_error("Failed to get data: owner=%s, error=%d", 
                     owner_to_string(owner), result);
        }
        return result;
    }
}

/* Add random data to the buffer */
lr_result_t add_random_data()
{
    lr_owner_t owner = rand() % NUM_OWNERS;
    lr_data_t value = rand() % 1000;
    return add_data(owner, value);
}

/* Get data from a random owner */
lr_result_t get_random_owner_data()
{
    lr_owner_t owner = rand() % NUM_OWNERS;
    lr_data_t value;
    return get_data(owner, &value);
}

/* Function to initialize the buffer */
lr_result_t init_buffer(int buffer_size)
{
    // Create an array of lr_cell for the buffer
    struct lr_cell *cells = calloc(buffer_size, sizeof(struct lr_cell));
    if (!cells) {
        log_error("Failed to allocate memory for buffer cells");
        return LR_ERROR_NOMEMORY;
    }

    // Initialize the buffer
    lr_result_t result = lr_init(&buffer, buffer_size, cells);
    if (result != LR_OK) {
        log_error("Failed to initialize buffer with size %d", buffer_size);
        free(cells);
        return LR_ERROR_UNKNOWN;
    }

    // Reset statistics
    memset(&stats, 0, sizeof(buffer_stats_t));

    log_info("Buffer initialized with size %d", buffer_size);
    return LR_OK;
}

/* Test function to reproduce and diagnose segfault */
lr_result_t test_segfault_reproduction()
{
    lr_result_t result;
    
    log_info("Testing segfault reproduction scenario...");
    
    // First test edge cases that might cause segfaults
    result = test_edge_cases();
    test_assert(result == LR_OK, "Edge case tests");
    
    // Then run high load tests with different buffer sizes
    int buffer_sizes[] = {5, 10, 20, 50};
    int iterations = 1000;
    
    for (int i = 0; i < sizeof(buffer_sizes)/sizeof(buffer_sizes[0]); i++) {
        int size = buffer_sizes[i];
        log_info("\n=== Testing buffer with size %d ===", size);
        
        result = test_high_load(size, iterations);
        test_assert(result == LR_OK, "High load test with buffer size %d", size);
    }
    
    log_ok("All segfault reproduction tests completed successfully");
    return LR_OK;
}

/* Print buffer statistics */
void print_stats()
{
    printf("\n┌─────────────────────────────────────────────────┐\n");
    printf("│           \033[1mBuffer Test Statistics\033[0m               │\n");
    printf("├─────────────────────────┬───────────────────────┤\n");
    printf("│ Operations              │ Count                 │\n");
    printf("├─────────────────────────┼───────────────────────┤\n");
    printf("│ Total puts              │ %-21lu │\n", stats.total_puts);
    printf("│ Total gets              │ %-21lu │\n", stats.total_gets);
    printf("│ Failed puts             │ %-21lu │\n", stats.failed_puts);
    printf("│ Failed gets             │ %-21lu │\n", stats.failed_gets);
    printf("│ Maximum occupancy       │ %-21lu │\n", stats.max_occupancy);
    printf("│ Segfault risks detected │ %-21lu │\n", stats.segfault_risk_count);
    printf("└─────────────────────────┴───────────────────────┘\n");
}

/* Test buffer under high load with multiple owners */
lr_result_t test_high_load(int buffer_size, int iterations)
{
    lr_result_t result;
    
    // Initialize the buffer
    result = init_buffer(buffer_size);
    test_assert(result == LR_OK, "Initialize buffer with size %d", buffer_size);
    
    log_info("Starting high load test with %d iterations", iterations);
    
    // Run the test for specified iterations
    for (int i = 0; i < iterations; i++) {
        size_t current_size = safe_lr_count(&buffer);
        size_t available = lr_available(&buffer);
        
        // Randomly decide whether to add or remove data
        // More likely to add when buffer is empty, more likely to remove when full
        bool should_add;
        if (current_size == 0) {
            should_add = true;
        } else if (available == 0) {
            should_add = false;
        } else {
            should_add = (rand() % 100) < (50 - (current_size * 50) / (current_size + available));
        }
        
        if (should_add) {
            // Try to add 1-3 elements at once
            int elements_to_add = 1 + (rand() % 3);
            for (int j = 0; j < elements_to_add && available > 0; j++) {
                add_random_data();
                available = lr_available(&buffer);
            }
        } else {
            // Try to get 1-2 elements at once
            int elements_to_get = 1 + (rand() % 2);
            for (int j = 0; j < elements_to_get && current_size > 0; j++) {
                get_random_owner_data();
                current_size = safe_lr_count(&buffer);
            }
        }
        
        // Periodically validate buffer integrity
        if (i % (iterations / 10) == 0) {
            char checkpoint[64];
            snprintf(checkpoint, sizeof(checkpoint), "Iteration %d/%d", i, iterations);
            validate_buffer(&buffer, checkpoint);
            
            // Also periodically show progress
            if (i % (iterations / 5) == 0) {
                log_info("Progress: %d%% (%d/%d iterations)", 
                        (i * 100) / iterations, i, iterations);
            }
        }
    }
    
    // Drain the buffer at the end
    log_info("Draining buffer...");
    lr_data_t value;
    while (safe_lr_count(&buffer) > 0) {
        for (int owner = 0; owner < NUM_OWNERS; owner++) {
            while (get_data(owner, &value) == LR_OK) {
                // Just drain the buffer
            }
        }
    }
    
    // Print final statistics
    print_stats();
    
    // Free the buffer memory
    free(buffer.cells);
    
    return LR_OK;
}

/* Test specific edge cases that might cause segfaults */
lr_result_t test_edge_cases()
{
    lr_result_t result;
    lr_data_t value;
    
    log_info("=== Testing Specific Edge Cases ===");
    
    // Test with minimum viable buffer size (4)
    result = init_buffer(4);
    test_assert(result == LR_OK, "Initialize minimum size buffer (4)");
    
    validate_buffer(&buffer, "After initialization with minimum size");
    
    // Add one element
    result = add_data(OWNER_SPI_IN, 42);
    test_assert(result == LR_OK, "Add element to minimum buffer");
    
    validate_buffer(&buffer, "After adding one element");
    
    // Try to add another element (should succeed with one owner)
    result = add_data(OWNER_SPI_IN, 43);
    test_assert(result == LR_OK, "Add second element to minimum buffer");
    
    validate_buffer(&buffer, "After adding second element");
    
    // Get the elements back
    result = get_data(OWNER_SPI_IN, &value);
    test_assert(result == LR_OK && value == 42, "Retrieved first element correctly");
    
    validate_buffer(&buffer, "After getting first element");
    
    result = get_data(OWNER_SPI_IN, &value);
    test_assert(result == LR_OK && value == 43, "Retrieved second element correctly");
    
    validate_buffer(&buffer, "After getting second element");
    
    // Test with extreme values
    result = add_data(OWNER_SPI_IN, UINTPTR_MAX);
    test_assert(result == LR_OK, "Add maximum value to buffer");
    
    validate_buffer(&buffer, "After adding maximum value");
    
    result = get_data(OWNER_SPI_IN, &value);
    test_assert(result == LR_OK && value == UINTPTR_MAX, 
               "Retrieved maximum value correctly (0x%lx)", value);
    
    validate_buffer(&buffer, "After getting maximum value");
    
    // Test with multiple owners in minimum buffer
    free(buffer.cells);
    
    // Initialize with size 5 for multiple owners
    result = init_buffer(5);
    test_assert(result == LR_OK, "Initialize buffer for multiple owners (size 5)");
    
    validate_buffer(&buffer, "After initialization for multiple owners");
    
    // Add elements for different owners
    result = add_data(OWNER_SPI_IN, 10);
    test_assert(result == LR_OK, "Add element for first owner");
    
    validate_buffer(&buffer, "After adding for first owner");
    
    result = add_data(OWNER_I2C_IN, 20);
    test_assert(result == LR_OK, "Add element for second owner");
    
    validate_buffer(&buffer, "After adding for second owner");
    
    // This might fail (buffer full)
    result = add_data(OWNER_UART_IN, 30);
    if (result == LR_ERROR_BUFFER_FULL) {
        log_info("Buffer correctly reports full with multiple owners");
    }
    
    validate_buffer(&buffer, "After attempting to add third owner");
    
    // Get data from owners in reverse order
    result = get_data(OWNER_I2C_IN, &value);
    test_assert(result == LR_OK && value == 20, "Retrieved data from second owner");
    
    validate_buffer(&buffer, "After getting from second owner");
    
    result = get_data(OWNER_SPI_IN, &value);
    test_assert(result == LR_OK && value == 10, "Retrieved data from first owner");
    
    validate_buffer(&buffer, "After getting from first owner");
    
    // Clean up
    free(buffer.cells);
    
    return LR_OK;
}

int main()
{
    // Seed random number generator
    srand(time(NULL));

    // Run the test that reproduces the segfault scenario
    lr_result_t result = test_segfault_reproduction();

    if (result == LR_OK) {
        log_info("Segfault reproduction test passed successfully!");
        if (stats.segfault_risk_count > 0) {
            log_info("Detected %lu potential segfault risks during testing", 
                    stats.segfault_risk_count);
        } else {
            log_info("No segfault risks detected during testing");
        }
        print_stats();
        return 0;
    } else {
        log_error("Test failed with code %d", result);
        print_stats();
        return 1;
    }
}
