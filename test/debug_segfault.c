
#include <lr.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Test utilities */
#define log_print(type, message, ...)                                          \
    printf(type "\t" message "\n", ##__VA_ARGS__)
#define log_info(message, ...) log_print("INFO", message, ##__VA_ARGS__)
#define log_ok(message, ...)   log_print("OK", message, ##__VA_ARGS__)
#define log_error(message, ...)                                                \
    log_print("\e[1m\e[31mERROR\e[39m\e[0m", message " (%s:%d)",               \
              ##__VA_ARGS__, __FILE__, __LINE__)
#define log_debug(message, ...) log_print("DEBUG", message, ##__VA_ARGS__)

#define test_assert(test, message, ...)                                        \
    do {                                                                       \
        if (!(test)) {                                                         \
            log_error(message, ##__VA_ARGS__);                                 \
            return LR_ERROR_UNKNOWN;                                           \
        } else {                                                               \
            log_ok(message, ##__VA_ARGS__);                                    \
        }                                                                      \
    } while (0)

/* Global buffer for testing */
struct linked_ring buffer;

/* Debug function to validate buffer integrity */
void validate_buffer(struct linked_ring *lr, const char *checkpoint)
{
    log_debug("=== Buffer validation at checkpoint: %s ===", checkpoint);

    if (lr == NULL) {
        log_error("Buffer is NULL");
        return;
    }

    log_debug("Buffer address: %p", lr);
    log_debug("Cells address: %p", lr->cells);
    log_debug("Size: %u", lr->size);
    log_debug("Write pointer: %p", lr->write);
    log_debug("Owners pointer: %p", lr->owners);

    // Check if owners is NULL
    if (lr->owners == NULL) {
        log_debug(
            "Owners is NULL - this is valid if no owners have been added");
        return;
    }

    // Check owners->next
    if (lr->owners->next == NULL) {
        log_debug("WARNING: owners->next is NULL, this may cause segfault in "
                  "lr_count");
        return;
    }

    // Validate the circular list structure
    struct lr_cell *head = lr->owners->next;
    if (head == NULL) {
        log_debug("WARNING: head (owners->next) is NULL");
        return;
    }

    log_debug("Head pointer: %p", head);

    // Check if we can safely traverse the list
    struct lr_cell *needle         = head;
    size_t          count          = 0;
    const size_t    MAX_ITERATIONS = lr->size * 2; // Safety limit

    log_debug("Starting list traversal...");
    while (needle != NULL && count < MAX_ITERATIONS) {
        log_debug("  Node %zu: %p, data: 0x%lx, next: %p", count, needle,
                  needle->data, needle->next);

        if (needle->next == head) {
            log_debug("  Found circular reference back to head");
            break;
        }

        if (needle->next == NULL) {
            log_error("  Found NULL next pointer before completing circle!");
            break;
        }

        needle = needle->next;
        count++;
    }

    if (count >= MAX_ITERATIONS) {
        log_error("Possible infinite loop detected in buffer list!");
    } else {
        log_debug("List traversal complete, found %zu nodes", count);
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
    }
}

/* Safe version of lr_count that won't segfault */
size_t safe_lr_count(struct linked_ring *lr)
{
    struct lr_cell *head;
    struct lr_cell *needle;
    size_t          length = 0;

    log_debug("Performing safe count...");

    if (lr == NULL || lr->owners == NULL) {
        log_debug("Buffer or owners is NULL, count = 0");
        return 0;
    }

    if (lr->owners->next == NULL) {
        log_debug("owners->next is NULL, count = 0");
        return 0;
    }

    head   = lr->owners->next;
    length = 1;
    needle = head;

    log_debug("Starting count with head=%p, needle=%p", head, needle);

    // Safety limit to prevent infinite loops
    size_t max_iterations = lr->size * 2;
    size_t iterations     = 0;

    while (needle != NULL && needle->next != NULL
           && iterations < max_iterations) {
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
    }

    log_debug("Count complete, length = %zu", length);
    return length;
}

/* Test function to reproduce and diagnose segfault */
lr_result_t test_segfault_reproduction()
{
    lr_result_t        result;
    struct lr_cell    *cells;
    const unsigned int size = 20;

    log_info("Testing segfault reproduction scenario...");

    /* Initialize buffer */
    cells = malloc(size * sizeof(struct lr_cell));
    if (!cells) {
        log_error("Failed to allocate memory for cells");
        return LR_ERROR_NOMEMORY;
    }

    result = lr_init(&buffer, size, cells);
    test_assert(result == LR_OK, "Buffer initialization should succeed");

    validate_buffer(&buffer, "After initialization");

    /* Test scenario 1: Empty buffer operations */
    log_info("Scenario 1: Empty buffer operations");

    // Count on empty buffer (should be safe)
    size_t count = safe_lr_count(&buffer);
    log_debug("Count on empty buffer: %zu", count);

    // Try to get from empty buffer
    lr_data_t data;
    result = lr_get(&buffer, &data, 1);
    test_assert(result == LR_ERROR_BUFFER_EMPTY,
                "Get from empty buffer should return BUFFER_EMPTY");

    validate_buffer(&buffer, "After empty buffer operations");

    /* Test scenario 2: Add and remove data with single owner */
    log_info("Scenario 2: Add and remove data with single owner");

    // Add some data
    for (int i = 0; i < 5; i++) {
        result = lr_put(&buffer, i * 10, 1);
        test_assert(result == LR_OK, "Put %d should succeed", i);

        // Validate after each put
        char checkpoint[64];
        snprintf(checkpoint, sizeof(checkpoint), "After put %d", i);
        validate_buffer(&buffer, checkpoint);

        // Check count after each put
        count = safe_lr_count(&buffer);
        log_debug("Count after put %d: %zu", i, count);
    }

    // Remove all data
    for (int i = 0; i < 5; i++) {
        result = lr_get(&buffer, &data, 1);
        test_assert(result == LR_OK, "Get %d should succeed", i);
        test_assert(data == i * 10, "Retrieved data should be %d, got %lu",
                    i * 10, data);

        // Validate after each get
        char checkpoint[64];
        snprintf(checkpoint, sizeof(checkpoint), "After get %d", i);
        validate_buffer(&buffer, checkpoint);

        // Check count after each get
        count = safe_lr_count(&buffer);
        log_debug("Count after get %d: %zu", i, count);
    }

    /* Test scenario 3: Multiple owners */
    log_info("Scenario 3: Multiple owners");

    // Add data for different owners
    for (int owner = 1; owner <= 3; owner++) {
        for (int i = 0; i < 3; i++) {
            result = lr_put(&buffer, owner * 100 + i, owner);
            test_assert(result == LR_OK, "Put for owner %d should succeed",
                        owner);

            // Validate after each put
            char checkpoint[64];
            snprintf(checkpoint, sizeof(checkpoint),
                     "After put for owner %d, item %d", owner, i);
            validate_buffer(&buffer, checkpoint);
        }
    }

    // Remove data in mixed order
    for (int owner = 3; owner >= 1; owner--) {
        result = lr_get(&buffer, &data, owner);
        test_assert(result == LR_OK, "Get for owner %d should succeed", owner);

        char checkpoint[64];
        snprintf(checkpoint, sizeof(checkpoint), "After get for owner %d",
                 owner);
        validate_buffer(&buffer, checkpoint);
    }

    /* Test scenario 4: Edge case - fill buffer to capacity */
    log_info("Scenario 4: Fill buffer to capacity");

    // Fill buffer
    int i = 0;
    while (true) {
        result = lr_put(&buffer, i, 1);
        if (result != LR_OK) {
            log_debug("Buffer full after %d puts", i);
            break;
        }
        i++;
    }

    validate_buffer(&buffer, "After filling buffer");

    // Empty buffer
    while (lr_get(&buffer, &data, 1) == LR_OK) {
        // Just empty the buffer
    }

    validate_buffer(&buffer, "After emptying buffer");

    /* Test scenario 5: Rapid add/remove cycles */
    log_info("Scenario 5: Rapid add/remove cycles");

    for (int cycle = 0; cycle < 10; cycle++) {
        // Add some data
        for (int i = 0; i < 3; i++) {
            result = lr_put(&buffer, cycle * 100 + i, cycle % 3 + 1);
            test_assert(result == LR_OK, "Put in cycle %d should succeed",
                        cycle);
        }

        validate_buffer(&buffer, "After adding in cycle");

        // Remove some data
        for (int owner = 1; owner <= 3; owner++) {
            while (lr_get(&buffer, &data, owner) == LR_OK) {
                // Remove all data for this owner
            }
        }

        validate_buffer(&buffer, "After removing in cycle");
    }

    /* Clean up */
    free(cells);

    log_ok("All segfault reproduction tests completed successfully");
    return LR_OK;
}

int main()
{
    // Seed random number generator
    srand(time(NULL));

    lr_result_t result = test_segfault_reproduction();

    if (result == LR_OK) {
        log_info("All tests passed successfully!");
        return 0;
    } else {
        log_error("Tests failed with code %d", result);
        return 1;
    }
}
