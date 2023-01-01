#pragma once

#include <stdint.h>

/* `lr_data_t` is a typedef for the `uintptr_t` type, which is an unsigned
 * integer type that is large enough to hold a pointer value. It is used to
 * store the data for each element in the Linked Ring buffer.  */
#define lr_data_t uintptr_t
/* The `lr_data` macro is provided as a convenience for casting a pointer to the
 * `lr_data_t` type. This macro is useful for ensuring that the data is stored
 * as an unsigned integer, rather than a pointer, which may be necessary for
 * certain operations on the data. */
#define lr_data(ptr) (uintptr_t) ptr

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
#define lr_owner(ptr) (uintptr_t) ptr

typedef enum lr_result {
    LR_OK = 0,
    LR_ERROR_UNKNOWN,
    LR_ERROR_NOMEMORY,
    LR_ERROR_BUFFER_FULL,
    LR_ERROR_BUFFER_EMPTY,
    LR_ERROR_BUFFER_BUSY
} lr_result_t;

/* Representation of an element in the Linked Ring buffer */
struct lr_cell {
    lr_data_t       data;  // The data for the element.
    lr_owner_t      owner; // The owner or user associated with the element.
    struct lr_cell *next;  // A pointer to the next element in the Linked Ring
                           // buffer. It allows the elements to be linked
                           // together in a circular fashion.
};

struct linked_ring {
    struct lr_cell *cells; // Allocated array of cellsin the buffer
    unsigned int    size;  // Maximum number of elements that can be stored

    lr_owner_t owners; // Bitfield of owners currently present

    struct lr_cell *read;  // Cell that is currently being read from
    struct lr_cell *write; // Cell that is currently being written to
    struct lr_cell *tail;  // Last cell in the buffer
};

uint16_t lr_count_limited_owned(struct linked_ring *, uint16_t limit,
                                lr_owner_t owner);

#define lr_count(lr)              lr_count_limited_owned(lr, 0, 0)
#define lr_exists(lr, owner)      lr_count_limited_owned(lr, 1, owner)
#define lr_count_owned(lr, owner) lr_count_limited_owned(lr, 0, owner)

lr_result_t lr_init(struct linked_ring *lr, unsigned int size,
                    struct lr_cell *cells);
lr_result_t lr_put(struct linked_ring *lr, lr_data_t data, lr_owner_t owner);
lr_result_t lr_put_list(struct linked_ring *lr, unsigned char *data,
                        lr_owner_t owner);
lr_result_t lr_get(struct linked_ring *, lr_data_t *, lr_owner_t);

lr_result_t lr_dump(struct linked_ring *lr);
