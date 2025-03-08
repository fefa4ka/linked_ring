#include <lr.h> // include header for Linked Ring library
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>

#define log_print(type, message, ...)                                          \
    printf(type "\t" message "\n", ##__VA_ARGS__)
#define log_debug(type, message, ...)                                          \
    log_print(type, message " (%s:%d)", ##__VA_ARGS__, __FILE__, __LINE__)
#define log_verbose(message, ...) log_print("VERBOSE", message, ##__VA_ARGS__)
#define log_info(message, ...)    log_print("INFO", message, ##__VA_ARGS__)
#define log_ok(message, ...)      log_print("OK", message, ##__VA_ARGS__)
#define log_error(message, ...)                                                \
    log_print("\e[1m\e[31mERROR\e[39m\e[0m", message " (%s:%d)",               \
              ##__VA_ARGS__, __FILE__, __LINE__)

#define test_assert(test, message, ...)                                        \
    if (!(test)) {                                                             \
        log_error(message, ##__VA_ARGS__);                                     \
        return LR_ERROR_UNKNOWN;                                               \
    } else {                                                                   \
        log_ok(message, ##__VA_ARGS__);                                        \
    }

// Buffer size can be adjusted to test different capacities
#define DEFAULT_BUFFER_SIZE 20

// Statistics tracking
typedef struct {
    size_t total_puts;
    size_t total_gets;
    size_t failed_puts;
    size_t failed_gets;
    size_t max_occupancy;
    size_t owner_puts[10];
    size_t owner_gets[10];
} buffer_stats_t;

struct linked_ring buffer; // declare a buffer for the Linked Ring
buffer_stats_t stats = {0};

// define enum for owners
// owners have input and output stream
typedef enum {
    OWNER_SPI_IN,
    OWNER_SPI_OUT,
    OWNER_I2C_IN,
    OWNER_I2C_OUT,
    OWNER_UART_IN,
    OWNER_UART_OUT,
    NUM_OWNERS
} owner_t;

// Convert owner enum to string for better logging
const char* owner_to_string(owner_t owner) {
    static const char* owner_names[] = {
        "SPI_IN", "SPI_OUT", "I2C_IN", "I2C_OUT", "UART_IN", "UART_OUT"
    };
    
    if (owner < NUM_OWNERS) {
        return owner_names[owner];
    }
    return "UNKNOWN";
}

// function to initialize the linked ring buffer
lr_result_t init_buffer(int buffer_size)
{
    // create an array of lr_cell for the buffer
    struct lr_cell *cells = calloc(buffer_size, sizeof(struct lr_cell));
    if (!cells) {
        log_error("Failed to allocate memory for buffer cells");
        return LR_ERROR_NOMEMORY;
    }

    // initialize the buffer
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

// Add data with a specific owner
lr_result_t add_data(lr_owner_t owner, lr_data_t value)
{
    lr_result_t result;
    size_t count = lr_count(&buffer);
    size_t available = lr_available(&buffer);
    
    // Track maximum occupancy
    if (count > stats.max_occupancy) {
        stats.max_occupancy = count;
    }
    
    // Attempt to put data in the buffer
    result = lr_put(&buffer, value, owner);
    stats.total_puts++;
    
    if (owner < NUM_OWNERS) {
        stats.owner_puts[owner]++;
    }
    
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

// Add random data to the buffer
lr_result_t add_random_data()
{
    lr_owner_t owner = lr_owner(rand() % NUM_OWNERS);
    lr_data_t value = lr_data(rand() % 1000);
    return add_data(owner, value);
}

// Get data from a specific owner
lr_result_t get_data(lr_owner_t owner, lr_data_t *value)
{
    lr_result_t result;
    size_t count = lr_count(&buffer);
    
    // Attempt to get data from the buffer
    result = lr_get(&buffer, value, owner);
    stats.total_gets++;
    
    if (owner < NUM_OWNERS) {
        stats.owner_gets[owner]++;
    }
    
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

// Get data from a random owner
lr_result_t get_random_owner_data()
{
    lr_owner_t owner = lr_owner(rand() % NUM_OWNERS);
    lr_data_t value;
    return get_data(owner, &value);
}

// Print buffer statistics
void print_stats()
{
    printf("\n┌─────────────────────────────────────────────────┐\n");
    printf("│           \033[1mBuffer Test Statistics\033[0m              │\n");
    printf("├─────────────────────────┬───────────────────────┤\n");
    printf("│ Operations              │ Count                 │\n");
    printf("├─────────────────────────┼───────────────────────┤\n");
    printf("│ Total puts              │ %-21lu │\n", stats.total_puts);
    printf("│ Total gets              │ %-21lu │\n", stats.total_gets);
    printf("│ Failed puts             │ %-21lu │\n", stats.failed_puts);
    printf("│ Failed gets             │ %-21lu │\n", stats.failed_gets);
    printf("│ Maximum occupancy       │ %-21lu │\n", stats.max_occupancy);
    printf("├─────────────────────────┼───────────────────────┤\n");
    printf("│ Owner                   │ Puts / Gets           │\n");
    printf("├─────────────────────────┼───────────────────────┤\n");
    
    for (int i = 0; i < NUM_OWNERS; i++) {
        printf("│ %-23s │ %lu / %-16lu │\n", 
               owner_to_string(i), stats.owner_puts[i], stats.owner_gets[i]);
    }
    
    printf("└─────────────────────────┴───────────────────────┘\n");
}

// Test buffer under high load with multiple owners
lr_result_t test_high_load(int buffer_size, int iterations)
{
    lr_result_t result;
    
    // Initialize the buffer
    result = init_buffer(buffer_size);
    test_assert(result == LR_OK, "Initialize buffer with size %d", buffer_size);
    
    log_info("Starting high load test with %d iterations", iterations);
    
    // Run the test for specified iterations
    for (int i = 0; i < iterations; i++) {
        size_t current_size = lr_count(&buffer);
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
                current_size = lr_count(&buffer);
            }
        }
        
        // Periodically show buffer status (every 20% of iterations)
        if (i % (iterations / 5) == 0) {
            log_info("Progress: %d%% (%d/%d iterations)", 
                    (i * 100) / iterations, i, iterations);
            lr_dump(&buffer);
        }
    }
    
    // Drain the buffer at the end
    log_info("Draining buffer...");
    lr_data_t value;
    while (lr_count(&buffer) > 0) {
        for (int owner = 0; owner < NUM_OWNERS; owner++) {
            while (lr_get(&buffer, &value, owner) == LR_OK) {
                stats.total_gets++;
                stats.owner_gets[owner]++;
            }
        }
    }
    
    // Print final statistics
    print_stats();
    
    // Free the buffer memory
    free(buffer.cells);
    
    return LR_OK;
}

// Test buffer at different occupancy levels
lr_result_t test_occupancy_limits()
{
    int buffer_sizes[] = {5, 10, 20, 50};
    int iterations = 1000;
    
    log_info("=== Testing Buffer at Different Occupancy Limits ===");
    
    for (int i = 0; i < sizeof(buffer_sizes)/sizeof(buffer_sizes[0]); i++) {
        int size = buffer_sizes[i];
        log_info("\n=== Testing buffer with size %d ===", size);
        
        lr_result_t result = test_high_load(size, iterations);
        test_assert(result == LR_OK, "High load test with buffer size %d", size);
        
        // Verify we reached high occupancy
        double occupancy_percent = (100.0 * stats.max_occupancy) / (size - 1);
        test_assert(occupancy_percent > 70.0, 
                   "Buffer reached high occupancy (%.1f%%)", occupancy_percent);
    }
    
    return LR_OK;
}

// Test specific edge cases
lr_result_t test_edge_cases()
{
    lr_result_t result;
    lr_data_t value;
    
    log_info("=== Testing Specific Edge Cases ===");
    
    // Test with minimum viable buffer size (3)
    result = init_buffer(3);
    test_assert(result == LR_OK, "Initialize minimum size buffer (3)");
    
    // Add one element
    result = add_data(OWNER_SPI_IN, 42);
    test_assert(result == LR_OK, "Add element to minimum buffer");
    
    // Try to add another element (should succeed with one owner)
    result = add_data(OWNER_SPI_IN, 43);
    test_assert(result == LR_OK, "Add second element to minimum buffer");
    
    // Try to add a third element (should fail - buffer full)
    result = add_data(OWNER_SPI_IN, 44);
    test_assert(result == LR_ERROR_BUFFER_FULL, "Buffer correctly reports full state");
    
    // Get the elements back
    result = get_data(OWNER_SPI_IN, &value);
    test_assert(result == LR_OK && value == 42, "Retrieved first element correctly");
    
    result = get_data(OWNER_SPI_IN, &value);
    test_assert(result == LR_OK && value == 43, "Retrieved second element correctly");
    
    // Test with extreme values
    result = add_data(OWNER_SPI_IN, UINTPTR_MAX);
    test_assert(result == LR_OK, "Add maximum value to buffer");
    
    result = get_data(OWNER_SPI_IN, &value);
    test_assert(result == LR_OK && value == UINTPTR_MAX, 
               "Retrieved maximum value correctly (0x%lx)", value);
    
    // Test with multiple owners in minimum buffer
    result = add_data(OWNER_SPI_IN, 10);
    test_assert(result == LR_OK, "Add element for first owner");
    
    // This should succeed but make the buffer full
    result = add_data(OWNER_I2C_IN, 20);
    test_assert(result == LR_OK, "Add element for second owner");
    
    // This should fail (buffer full)
    result = add_data(OWNER_UART_IN, 30);
    test_assert(result == LR_ERROR_BUFFER_FULL, "Buffer correctly reports full with multiple owners");
    
    // Clean up
    free(buffer.cells);
    
    return LR_OK;
}

int main(int argc, char **argv)
{
    // Seed the random number generator
    srand(time(NULL));
    
    log_info("=== Linked Ring Buffer Edge Case and High Load Testing ===");
    
    // Run the edge case tests
    lr_result_t result = test_edge_cases();
    test_assert(result == LR_OK, "Edge case tests");
    
    // Run the occupancy limit tests
    result = test_occupancy_limits();
    test_assert(result == LR_OK, "Occupancy limit tests");
    
    log_ok("All tests passed successfully!");
    return 0;
}
