/*
 * Linked Ring Buffer - A circular buffer with ownership tracking
 *
 * This implementation uses a unique approach where:
 * 1. The buffer is a fixed-size array of cells organized as a circular linked list
 * 2. Each data element belongs to a specific "owner" (identified by owner_t)
 * 3. Owners can only access their own data elements
 * 4. The buffer manages memory efficiently by reusing cells
 *
 * Implementation Challenges:
 * - Complex Circular Structure: The buffer maintains circular linked lists for each owner
 *   while also managing a free list of available cells.
 * - Reverse Owner Storage: Owner cells are stored at the end of the buffer in reverse order.
 * - Unintuitive Head/Tail: An owner's head is found through the previous owner's cell,
 *   while the owner cell itself points to its tail element.
 * - Edge Case Handling: Special handling is required for first/last owners and empty lists.
 */

#pragma once

#include <stdint.h>
#include <stdio.h>

/* Type definitions */
#define lr_data_t uintptr_t    /* Data type stored in the buffer (pointer-sized) */
#define lr_owner_t uintptr_t   /* Owner identifier type (pointer-sized) */

/* Conversion macros for type safety */
#define lr_data(ptr) (uintptr_t)ptr    /* Cast a pointer to data type */
#define lr_owner(ptr) (uintptr_t)ptr   /* Cast a pointer to owner type */

/* Result codes for all operations */
typedef enum lr_result {
    LR_OK = 0,               /* Operation completed successfully */
    LR_ERROR_UNKNOWN,        /* Unknown error occurred */
    LR_ERROR_NOMEMORY,       /* Memory allocation failed or buffer is NULL */
    LR_ERROR_LOCK,           /* Failed to acquire mutex lock */
    LR_ERROR_UNLOCK,         /* Failed to release mutex lock */
    LR_ERROR_BUFFER_FULL,    /* No space available in the buffer */
    LR_ERROR_BUFFER_EMPTY,   /* No data available for the specified owner */
    LR_ERROR_BUFFER_BUSY,    /* Buffer is currently in use by another thread */
    LR_ERROR_INVALID_INDEX   /* Requested index is out of bounds */
} lr_result_t;

/*
 * Core structures
 */

/*
 * Cell structure - the basic unit of the linked ring
 * Each cell either contains data or represents an owner
 */
struct lr_cell {
    lr_data_t data;           /* The data stored in this cell or owner ID */
    struct lr_cell *next;     /* Pointer to the next cell in the ring */
};

/*
 * Main buffer structure
 * The buffer is organized as:
 * 1. A fixed array of cells (cells)
 * 2. A subset of these cells form the data storage
 * 3. Another subset (at the end) forms the owner registry
 * 4. Owners and their data form circular linked lists
 */
struct linked_ring {
    struct lr_cell *cells;    /* The entire array of cells */
    unsigned int size;        /* Total number of cells in the array */
    struct lr_cell *write;    /* Next available cell for writing */
    struct lr_cell *owners;   /* Pointer to the first owner cell */

    /* Thread safety functions */
    enum lr_result (*lock)(void *state);     /* Function to acquire mutex */
    enum lr_result (*unlock)(void *state);   /* Function to release mutex */
    void *mutex_state;                       /* State passed to lock/unlock */
};

/*
 * Mutex attribute structure for thread safety
 * Allows plugging in different mutex implementations
 */
struct lr_mutex_attr {
    void *state;                             /* Mutex state (implementation-specific) */
    enum lr_result (*lock)(void *state);     /* Function to acquire mutex */
    enum lr_result (*unlock)(void *state);   /* Function to release mutex */
};

/*
 * Utility macros
 */

/* Calculate number of free cells in the buffer */
#define lr_available(lr) ((lr)->size - lr_count(lr) - lr_owners_count(lr))

/* Get the size of the buffer (note: this is rarely used and may be deprecated) */
#define lr_size(lr) (lr->cells - lr->owners)

/*
 * Count the number of owner cells
 * Owner cells are stored at the end of the buffer in reverse order
 * This calculates how many cells are used for owners
 */
#define lr_owners_count(lr) ((lr)->owners == NULL ? 0 : (lr)->cells + (lr)->size - (lr)->owners)

/* Check if an owner exists in the buffer (optimized version of count) */
#define lr_exists(lr, owner) lr_count_limited_owned(lr, 1, owner)

/* Count all elements owned by a specific owner */
#define lr_count_owned(lr, owner) lr_count_limited_owned(lr, 0, owner)

/* Get pointer to the last cell in the buffer */
#define lr_last_cell(lr) ((lr)->cells + (lr)->size - 1)

/*
 * Get the tail cell for an owner
 * The owner cell's next pointer points to its tail element
 * This is a key design aspect - owner cells directly reference their last element
 */
#define lr_owner_tail(owner_cell) owner_cell->next

/*
 * Buffer initialization and configuration
 */

/* Initialize a new buffer with external cell storage */
lr_result_t lr_init(struct linked_ring *lr, size_t size, struct lr_cell *cells);

/* Resize an existing buffer (preserves data if possible) */
lr_result_t lr_resize(struct linked_ring *lr, size_t size, struct lr_cell *cells);

/* Configure thread safety by providing mutex functions */
void lr_set_mutex(struct linked_ring *lr, struct lr_mutex_attr *attr);

/*
 * Data insertion operations
 */

/* Add data to the head of an owner's list (FIFO behavior when paired with lr_get) */
lr_result_t lr_put(struct linked_ring *lr, lr_data_t data, lr_owner_t owner);

/* Add data to the tail of an owner's list (LIFO behavior when paired with lr_get) */
lr_result_t lr_push(struct linked_ring *lr, lr_data_t data, lr_owner_t owner);

/* Insert data at a specific position in an owner's list */
lr_result_t lr_insert(struct linked_ring *lr, lr_data_t data, lr_owner_t owner, size_t index);

/* Low-level insertion after a specific cell (used internally) */
lr_result_t lr_insert_next(struct linked_ring *lr, lr_data_t data, struct lr_cell *needle);

/* Add a null-terminated string to the buffer (each character as separate cell) */
lr_result_t lr_put_string(struct linked_ring *lr, unsigned char *data, lr_owner_t owner);

/*
 * Data retrieval operations
 */

/* Get and remove data from the head of an owner's list */
lr_result_t lr_get(struct linked_ring *lr, lr_data_t *data, lr_owner_t owner);

/* Get and remove data from the tail of an owner's list */
lr_result_t lr_pop(struct linked_ring *lr, lr_data_t *data, lr_owner_t owner);

/* Get and remove data from a specific position in an owner's list */
lr_result_t lr_pull(struct linked_ring *lr, lr_data_t *data, lr_owner_t owner, size_t index);

/* Read data from the head without removing it (peek operation) */
lr_result_t lr_read(struct linked_ring *lr, lr_data_t *data, lr_owner_t owner);

/* Read a string from the buffer without removing it */
lr_result_t lr_read_string(struct linked_ring *lr, unsigned char *data, size_t *length, lr_owner_t owner);

/* Read data at a specific position without removing it */
lr_result_t lr_read_at(struct linked_ring *lr, lr_data_t *data, lr_owner_t owner, size_t index);

/*
 * Owner management - internal functions
 */

/* Find an owner's cell in the buffer (returns NULL if not found) */
struct lr_cell *lr_owner_find(struct linked_ring *lr, lr_data_t owner);

/* Allocate a new owner cell from the free list */
struct lr_cell *lr_owner_allocate(struct linked_ring *lr);

/* Get the head cell for an owner (first element in the owner's list) */
struct lr_cell *lr_owner_head(struct linked_ring *lr, struct lr_cell *owner_cell);

struct lr_cell *lr_cell_lookup(struct linked_ring *lr, struct lr_cell *head,
                               struct lr_cell *cell);

/*
 * Buffer information
 */

/* Count elements owned by a specific owner, with optional limit for efficiency */
size_t lr_count_limited_owned(struct linked_ring *lr, size_t limit, lr_owner_t owner);

/* Count the total number of data elements in the buffer (across all owners) */
size_t lr_count(struct linked_ring *lr);

/*
 * Debugging functions
 */

/* Print detailed buffer information including contents by owner */
lr_result_t lr_dump(struct linked_ring *lr);

/* Debug the raw cell structure (shows memory layout) */
void lr_debug_strucuture_cells(struct linked_ring *lr);

/* Verify the circular structure for a specific owner */
lr_result_t lr_debug_structure_circular(struct linked_ring *lr, lr_owner_t owner);

/* Debug the relinked structure after operations (shows free list) */
void lr_debug_structure_relinked(struct linked_ring *lr);

