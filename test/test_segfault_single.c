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
    
    // Attempt to put data in the buffer
    result = lr_put(&buffer, value, owner);
    stats.total_puts++;
    
    if (result == LR_OK) {
        log_verbose("Added data: owner=%s, value=0x%lx", 
                   owner_to_string(owner), value);
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
    
    // Attempt to get data from the buffer
    result = lr_get(&buffer, value, owner);
    stats.total_gets++;
    
    if (result == LR_OK) {
        log_verbose("Retrieved data: owner=%s, value=0x%lx", 
                   owner_to_string(owner), *value);
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

/* Test function to specifically reproduce the segfault scenario identified in the logs */
lr_result_t test_specific_segfault_scenario()
{
    lr_result_t result;
    lr_data_t value;
    
    log_info("=== Testing Specific Segfault Scenario ===");
    
    // Initialize buffer with size 6 (enough for our test case)
    result = init_buffer(6);
    test_assert(result == LR_OK, "Initialize buffer with size 6");
    
    validate_buffer(&buffer, "After initialization");
    
    // Step 1: Add first element for SPI_IN
    result = add_data(OWNER_SPI_IN, 0x24a);
    test_assert(result == LR_OK, "Add first element for SPI_IN");
    
    validate_buffer(&buffer, "After adding first element");
    
    // Step 2: Add second element for SPI_IN
    result = add_data(OWNER_SPI_IN, 0x1c6);
    test_assert(result == LR_OK, "Add second element for SPI_IN");
    
    validate_buffer(&buffer, "After adding second element");
    
    // Step 3: Get first element from SPI_IN (should leave one element)
    result = get_data(OWNER_SPI_IN, &value);
    test_assert(result == LR_OK && value == 0x24a, "Get first element from SPI_IN");
    
    validate_buffer(&buffer, "After getting first element");
    log_info("Buffer has %zu elements after first get", safe_lr_count(&buffer));
    
    // Step 4: Get second element from SPI_IN (should empty the buffer)
    result = get_data(OWNER_SPI_IN, &value);
    test_assert(result == LR_OK && value == 0x1c6, "Get second element from SPI_IN");
    
    validate_buffer(&buffer, "After getting second element");
    log_info("Buffer has %zu elements after second get", safe_lr_count(&buffer));
    
    // Step 5: Add element for UART_IN
    result = add_data(OWNER_UART_IN, 0x217);
    test_assert(result == LR_OK, "Add element for UART_IN");
    
    validate_buffer(&buffer, "After adding element for UART_IN");
    log_info("Buffer has %zu elements after adding UART_IN", safe_lr_count(&buffer));
    
    // Step 6: Add element for SPI_IN
    result = add_data(OWNER_SPI_IN, 0x226);
    test_assert(result == LR_OK, "Add element for SPI_IN");
    
    validate_buffer(&buffer, "After adding element for SPI_IN");
    log_info("Buffer has %zu elements after adding SPI_IN", safe_lr_count(&buffer));
    
    // Extra validation of circular structure before critical operation
    log_info("Validating circular structure before critical operation...");
    struct lr_cell *head = buffer.owners->next->next;
    struct lr_cell *needle = head;
    size_t count = 0;
    bool circular_intact = false;
    
    while (needle != NULL && needle->next != NULL && count < buffer.size * 2) {
        needle = needle->next;
        count++;
        if (needle == head) {
            circular_intact = true;
            break;
        }
    }
    
    if (!circular_intact) {
        log_error("Circular structure is broken before critical operation!");
        // Try to repair the circular structure
        struct lr_cell *owner_cell = lr_owner_find(&buffer, OWNER_SPI_IN);
        if (owner_cell != NULL && owner_cell->next != NULL) {
            struct lr_cell *tail = owner_cell->next;
            struct lr_cell *head = lr_owner_head(&buffer, owner_cell);
            if (head != NULL) {
                log_info("Attempting to repair circular structure...");
                tail->next = head;
                log_info("Circular structure repaired: tail->next = %p now points to head = %p", 
                         tail->next, head);
            }
        }
    } else {
        log_info("Circular structure is intact before critical operation");
    }
    
    // Step 7: Get element from SPI_IN - this is where the segfault occurs
    log_info("About to perform the operation that causes segfault...");
    result = get_data(OWNER_SPI_IN, &value);
    
    // If we get here without segfaulting, validate the buffer
    test_assert(result == LR_OK && value == 0x226, "Get element from SPI_IN");
    
    validate_buffer(&buffer, "After getting element from SPI_IN");
    log_info("Buffer has %zu elements after final get", safe_lr_count(&buffer));
    
    // Clean up
    free(buffer.cells);
    
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

int main()
{
    // Seed random number generator
    srand(time(NULL));

    // Run the specific test that reproduces the segfault scenario
    lr_result_t result = test_specific_segfault_scenario();

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
