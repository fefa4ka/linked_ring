#pragma once

#include <stdint.h>
#include <stdio.h>


/* `lr_data_t` is a typedef for the `uintptr_t` type, which is an unsigned
 * integer type that is large enough to hold a pointer value. It is used to
 * store the data for each element in the Linked Ring buffer.  */
#define lr_data_t uintptr_t
/* The `lr_data` macro is provided as a convenience for casting a pointer to the
 * `lr_data_t` type. This macro is useful for ensuring that the data is stored
 * as an unsigned integer, rather than a pointer, which may be necessary for
 * certain operations on the data. */
#define lr_data(ptr) (uintptr_t)ptr

/* `lr_owner_t` is a typedef for the `uintptr_t` type, which is an unsigned
 * integer type that is large enough to hold a pointer value. It is used to
 * store the owner or user associated with each element in the Linked Ring
 * buffer. The `lr_owner` macro is provided as a convenience for casting a
 * pointer to the `lr_owner_t` type. */
#define lr_owner_t uintptr_t
/* This macro is useful for ensuring that the owner is stored as an unsigned
 * integer, rather than a pointer, which may be necessary for certain operations
 * on the owner. The `lr_owner_t` type can be used to store either a pointer or
 * an enumerator value, depending on the needs of the application.
 *
 * It allows users to associate each element in the buffer with an "owner" or
 * user, which could be represented by either a pointer or an enumerator value.
 * This can be useful for organizing and controlling access to the elements in
 * the buffer, depending on the specific needs of the application. */
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
    lr_data_t       data; // The data for the element.
    struct lr_cell *next; // A pointer to the next element in the Linked Ring
                          // buffer. It allows the elements to be linked
                          // together in a circular fashion.
};

struct linked_ring {
    struct lr_cell *cells; // Allocated array of cells in the buffer
    unsigned int    size;  // Maximum number of elements that can be stored
                           // Buffer size = size - N_owners

    struct lr_cell *write;  // Cell that is currently being written to
    struct lr_cell *owners; // Cell from which data about owners in buffer
                            // stored N_owners = cells + size - owners

    enum lr_result (*lock)(void *state); // used to make operations thread-safe
    enum lr_result (*unlock)(void *state);

    void *mutex_state;
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
lr_result_t lr_debug_circular_structure(struct linked_ring *lr, lr_owner_t owner);
