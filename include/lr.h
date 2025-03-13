#pragma once

#include <stdint.h>
#include <stdio.h>


/* Type for storing data in the linked ring buffer */
#define lr_data_t uintptr_t
/* Cast a pointer to lr_data_t type */
#define lr_data(ptr) (uintptr_t)ptr

/* Type for identifying owners in the linked ring buffer */
#define lr_owner_t uintptr_t
/* Cast a pointer to lr_owner_t type */
#define lr_owner(ptr) (uintptr_t)ptr


typedef enum lr_result {
    LR_OK = 0,
    LR_ERROR_UNKNOWN,
    LR_ERROR_NOMEMORY,
    LR_ERROR_LOCK,
    LR_ERROR_UNLOCK,
    LR_ERROR_BUFFER_FULL,
    LR_ERROR_BUFFER_EMPTY,
    LR_ERROR_BUFFER_BUSY
} lr_result_t;


/* Representation of an element in the Linked Ring buffer */
struct lr_cell {
    lr_data_t       data; // The data for the element
    struct lr_cell *next; // Pointer to the next element
};

struct linked_ring {
    struct lr_cell *cells;  // Allocated array of cells in the buffer
    unsigned int    size;   // Maximum number of elements that can be stored
    struct lr_cell *write;  // Cell that is currently being written to
    struct lr_cell *owners; // First owner cell in the buffer

    enum lr_result (*lock)(void *state);   // Thread safety lock function
    enum lr_result (*unlock)(void *state); // Thread safety unlock function
    void *mutex_state;                     // State for mutex operations
};

/* Provides a mechanism for a thread to exclusively access the linked ring.
 */
struct lr_mutex_attr {
    /* Shared state for lock and unlock functions. */
    void *state;

    /* Blocks until the mutex is acquired.
     * If a thread attempts to acquire a mutex that it has already locked, an
     * error shall be returned. Is a no op if NULL.
     */
    enum lr_result (*lock)(void *state);

    /* Release the mutex.
     * Returns LR_OK on success and an error code otherwise.
     * If a thread attempts to release a mutex that it has not acquired or a
     * mutex which is unlocked, an error shall be returned. Is a no op if NULL.
     */
    enum lr_result (*unlock)(void *state);
};


size_t lr_count_limited_owned(struct linked_ring *, size_t limit,
                              lr_owner_t owner);

size_t lr_count(struct linked_ring *lr);

#define lr_available(lr) ((lr)->size - lr_count(lr) - lr_owners_count(lr))
#define lr_size(lr)      (lr->cells - lr->owners)
#define lr_owners_count(lr)                                                    \
    ((lr)->owners == NULL ? 0 : (lr)->cells + (lr)->size - (lr)->owners)
#define lr_exists(lr, owner)      lr_count_limited_owned(lr, 1, owner)
#define lr_count_owned(lr, owner) lr_count_limited_owned(lr, 0, owner)
#define lr_last_cell(lr)          ((lr)->cells + (lr)->size - 1)

lr_result_t lr_init(struct linked_ring *lr, size_t size, struct lr_cell *cells);
lr_result_t lr_resize(struct linked_ring *lr, size_t size,
                      struct lr_cell *cells);

void lr_set_mutex(struct linked_ring *lr, struct lr_mutex_attr *attr);

lr_result_t lr_get(struct linked_ring *, lr_data_t *,
                   lr_owner_t requested_owner);
lr_result_t lr_pop(struct linked_ring *lr, lr_data_t *data, lr_owner_t owner);
lr_result_t lr_pull(struct linked_ring *lr, lr_data_t *data, lr_owner_t owner,
                    size_t index);

/* Insert a new element to head of the linked ring buffer. */
lr_result_t lr_put(struct linked_ring *lr, lr_data_t data, lr_owner_t owner);
/* Insert a new element to the tail of the linked ring buffer. */
lr_result_t lr_push(struct linked_ring *lr, lr_data_t data,
                        struct lr_cell *needle);
/* Insert a new element to the specified index of the linked ring buffer. */
lr_result_t lr_insert(struct linked_ring *lr, lr_data_t data, lr_owner_t owner,
                      size_t index);
lr_result_t lr_insert_next(struct linked_ring *lr, lr_data_t data,
                           struct lr_cell *needle);

// lr_result_t lr_put_next(struct linked_ring *lr, lr_cell *cell, lr_data_t
// data, lr_owner_t owner);
lr_result_t lr_put_string(struct linked_ring *lr, unsigned char *data,
                          lr_owner_t owner);

lr_result_t lr_read(struct linked_ring *, lr_data_t *,
                    lr_owner_t requested_owner);
lr_result_t lr_read_string(struct linked_ring *lr, unsigned char *data,
                           size_t *length, lr_owner_t owner);

lr_result_t lr_get(struct linked_ring *, lr_data_t *,
                   lr_owner_t requested_owner);

struct lr_cell *lr_owner_find(struct linked_ring *lr, lr_data_t owner);
struct lr_cell *lr_owner_allocate(struct linked_ring *lr);

/* Cursor */
struct lr_cell *lr_owner_head(struct linked_ring *lr,
                              struct lr_cell     *owner_cell);
#define lr_owner_tail(owner_cell) owner_cell->next

lr_result_t lr_owner_head_get(struct linked_ring *lr, lr_data_t owner,
                              struct lr_cell *cell);
lr_result_t lr_owner_tail_get(struct linked_ring *lr, lr_data_t owner,
                              struct lr_cell *cell);
lr_result_t lr_owner_cell_get(struct linked_ring *lr, lr_data_t owner,
                              size_t index, struct lr_cell *cell);
lr_result_t lr_owner_cell_next(struct linked_ring *lr, lr_data_t owner,
                               struct lr_cell *cell, struct lr_cell *next_cell);

/* not thread-safe */
lr_result_t lr_dump(struct linked_ring *lr);
void lr_debug_cells_structure(struct linked_ring *lr);
lr_result_t lr_debug_circular_structure(struct linked_ring *lr, lr_owner_t owner);
void lr_debug_relinked_structure(struct linked_ring *lr);

/**
 * Check and fix the integrity of the linked ring buffer.
 * This function detects and repairs common structural issues like self-referential loops.
 *
 * @param lr: pointer to the linked ring structure
 * @return LR_OK if the buffer is valid or was successfully repaired,
 *         error code otherwise
 */
lr_result_t lr_check_integrity(struct linked_ring *lr);
