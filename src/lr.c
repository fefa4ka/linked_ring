/*
 * Linked Ring Buffer - Implementation
 *
 * This file contains the implementation of the linked ring buffer data structure.
 * The code is organized into the following sections:
 *
 * 1. Includes and Macros
 * 2. Buffer Initialization and Configuration
 * 3. Owner Management Functions
 * 4. Cell Management Functions
 * 5. Data Insertion Operations
 * 6. Data Retrieval Operations
 * 7. Buffer Information Functions
 * 8. Debugging Functions
 *
 * Implementation Challenges:
 * - Circular Structure: The buffer maintains a complex circular linked list structure
 *   with separate tracking for data elements and owner cells.
 * - Owner Management: Owner cells are stored at the end of the buffer in reverse order,
 *   which is not immediately intuitive.
 * - Head/Tail Relationship: Each owner cell's 'next' pointer points to its tail element,
 *   not its head. The head is found through the previous owner's cell.
 * - Cell Reuse: When elements are removed, cells are returned to a free list tracked
 *   by the 'write' pointer for efficient memory reuse.
 * - Thread Safety: The buffer uses function pointers for mutex operations to support
 *   different threading models.
 */

/* ========================================================================== */
/* 1. INCLUDES AND MACROS                                                     */
/* ========================================================================== */

#include "lr.h"
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>

/* Thread safety macros */

/* Lock the mutex if lock function provided, no op otherwise */
#define lock(lr)                                                               \
    do {                                                                       \
        if (lr->lock != NULL) {                                                \
            enum lr_result ret = lr->lock(lr->mutex_state);                    \
            if (ret != LR_OK) {                                                \
                return ret;                                                    \
            }                                                                  \
        }                                                                      \
    } while (0)

#define unlock(lr)                                                             \
    if (lr->lock != NULL) {                                                    \
        lr->unlock(lr->mutex_state);                                           \
    }

/* Unlock the mutex if unlock function provided and then return ret  */
#define unlock_and_return(lr, ret)                                             \
    do {                                                                       \
        unlock(lr);                                                            \
        return ret;                                                            \
    } while (0)

/* Additional overload for returning success */
#define unlock_and_succeed(lr) unlock_and_return(lr, LR_OK)

/* ========================================================================== */
/* 2. BUFFER INITIALIZATION AND CONFIGURATION                                 */
/* ========================================================================== */

/**
 * Initialize a new linked ring buffer.
 *
 * @param lr Pointer to the linked ring structure to be initialized
 * @param size Size of the buffer, in number of elements
 * @param cells Pointer to the array of cells that will make up the buffer
 *
 * @return LR_OK if the initialization was successful
 * @return LR_ERROR_NOMEMORY if the cells parameter is NULL or size is 0
 * 
 * @note Not thread-safe. Should be called before any threads access the buffer.
 */
lr_result_t lr_init(struct linked_ring *lr, size_t size, struct lr_cell *cells)
{
    if (cells == NULL || size == 0) {
        return LR_ERROR_NOMEMORY;
    }

    lr->cells  = cells;
    lr->size   = size;
    lr->owners = NULL;

    /* Set the write position to the first cell in the buffer */
    lr->write = lr->cells;

    /* Initialize the cells */
    /* Link the cells in a ring */
    for (size_t idx = 0; idx < lr->size - 1; ++idx) {
        lr->cells[idx].next
            = &lr->cells[idx + 1]; /* Every cell points to the next */
    }

    /* Use lr_set_mutex to initialize these fields */
    lr->lock        = NULL;
    lr->unlock      = NULL;
    lr->mutex_state = NULL;

    return LR_OK;
}

/**
 * Set the mutex for thread-safe access to the linked ring.
 *
 * @param lr Pointer to the linked ring structure
 * @param attr Mutex attributes
 *
 * @note Not thread-safe. Should be called before any threads access the buffer.
 */
void lr_set_mutex(struct linked_ring *lr, struct lr_mutex_attr *attr)
{
    lr->lock        = attr->lock;
    lr->unlock      = attr->unlock;
    lr->mutex_state = attr->state;
}

/**
 * Custom memory copy function for internal use.
 *
 * @param dest Destination memory address
 * @param src Source memory address
 * @param n Number of bytes to copy
 *
 * @return Pointer to the destination memory
 */
void *lr_memcpy(void *restrict dest, const void *restrict src, size_t n)
{
    char *csrc  = (char *)src;
    char *cdest = (char *)dest;

    for (size_t i = 0; i < n; i++)
        cdest[i] = csrc[i];

    return dest;
}

/**
 * Resize the linked ring buffer.
 *
 * Moves the data from the old cells array to the new cells array, and updates
 * the pointers to the cells array.
 *
 * @param lr Pointer to the linked ring structure to be resized
 * @param size New size of the buffer, in number of elements
 * @param cells Pointer to the new array of cells
 *
 * @return LR_OK if the resize was successful
 * @return LR_ERROR_NOMEMORY if the cells parameter is NULL or size is 0
 *
 * @note Not thread-safe. All threads must stop accessing the buffer
 *       before calling this function.
 * 
 * @note Implementation challenge: Resizing requires:
 *       1. Copying all data cells while preserving their relationships
 *       2. Recreating owner cells in the new array
 *       3. Updating all pointers to reference the new array
 *       4. Maintaining the circular structure
 *       This is one of the most complex operations as it must rebuild
 *       the entire buffer structure while preserving all relationships.
 */
lr_result_t lr_resize(struct linked_ring *lr, size_t size,
                      struct lr_cell *cells)
{
    struct lr_cell *data_needle  = NULL;
    struct lr_cell *owner_needle = NULL;
    size_t          owner_nr     = 0;
    size_t          data_nr      = 0;

    if (cells == NULL || size == 0) {
        return LR_ERROR_NOMEMORY;
    }
    owner_nr = lr_owners_count(lr);
    // Copy all data from old cells to new cells, without owner cells
    data_nr     = (lr->size - owner_nr);
    data_needle = cells;
    for (size_t i = 0; i < data_nr; i++) {
        struct lr_cell *lr_needle = lr->cells + i;
        size_t          data_pos  = (size_t)(lr_needle->next - lr->cells);

        data_needle->data = lr_needle->data;
        data_needle->next = cells + data_pos;
        data_needle++;
    }

    // Create owner cells and map them to the new data
    owner_needle = cells + size - owner_nr;
    for (size_t i = 0; i < owner_nr; i++) {
        struct lr_cell *lr_needle = lr->owners + i;
        size_t          data_pos  = (size_t)(lr_needle->next - lr->cells);

        owner_needle->data = lr_needle->data;
        owner_needle->next = cells + data_pos;
        owner_needle++;
    }

    // Update buffer properties
    lr->cells = cells;
    lr->size  = size;
    lr->write = --data_needle; // Reset write pointer to beginning of new buffer
    lr->owners = owner_nr > 0 ? cells + size - owner_nr : NULL;
    // Link write pointer to first owner cell in new buffer
    for (struct lr_cell *lr_needle = data_needle; lr_needle < lr->owners;
         lr_needle++) {
        lr_needle->next = lr_needle + 1;
        // Last cell should point to NULL
        if (lr_needle == lr->owners - 1) {
            lr_needle->next = NULL;
        }
    }

    return LR_OK;
}

/* ========================================================================== */
/* 3. OWNER MANAGEMENT FUNCTIONS                                              */
/* ========================================================================== */


/**
 * Find an owner cell in the buffer.
 *
 * @param lr Pointer to the linked ring structure
 * @param owner The owner ID to find
 *
 * @return Pointer to the owner cell if found, NULL otherwise
 *
 * @note Not thread-safe. This function does not acquire the mutex
 *       and should only be called from within functions that have already acquired
 *       the mutex or in single-threaded contexts.
 * 
 * @note Implementation challenge: Owner cells are stored at the end of the buffer
 *       in reverse order, which makes this traversal non-intuitive.
 */
struct lr_cell *lr_owner_find(struct linked_ring *lr, lr_data_t owner)
{
    /* Traverse through each owner in the owner array */
    for (struct lr_cell *owner_cell = lr->owners;
         owner_cell < lr->owners + lr_owners_count(lr); owner_cell++) {
        /* Check if owner of the current cell matches with given owner */
        if (owner_cell->data == owner) {
            return owner_cell;
        }
    }

    return NULL;
}

/**
 * Get the head cell for an owner.
 *
 * @param lr Pointer to the linked ring structure
 * @param owner_cell Pointer to the owner cell
 *
 * @return Pointer to the head cell
 *
 * @note Not thread-safe. This function does not acquire the mutex
 *       and should only be called from within functions that have already acquired
 *       the mutex or in single-threaded contexts.
 * 
 * @note Implementation challenge: The head cell for an owner is not directly
 *       referenced by the owner cell. Instead, it's found through the previous
 *       owner's cell, which points to its tail, which points to the next owner's
 *       head. This indirect relationship makes the code more complex.
 */
struct lr_cell *lr_owner_head(struct linked_ring *lr,
                              struct lr_cell     *owner_cell)
{
    struct lr_cell *head;
    struct lr_cell *prev_owner;
    struct lr_cell *last_cell = lr_last_cell(lr);

    if (owner_cell == last_cell) {
        /* If the provided owner is first, then last added owner is used
         * to link with owner_cell head
         */
        if (lr->owners != NULL && lr->owners->next != NULL) {
            return lr->owners->next->next;
        } else {
            prev_owner = lr->owners + 1; /* Owners stored in reverse order */
        }
    } else {
        /* For any other cell, the prev owner is used for head linkage */
        prev_owner = owner_cell + 1; /* Owners stored in reverse order */
    }

    while (prev_owner->next == NULL && prev_owner < last_cell) {
        prev_owner += 1;
    }

    if (prev_owner->next == NULL) {
        /* No valid head found */
        return NULL;
    }

    head = prev_owner->next->next;

    return head;
}

/**
 * Allocate a new owner cell.
 *
 * @param lr Pointer to the linked ring structure
 *
 * @return Pointer to the allocated owner cell if successful, NULL otherwise
 *
 * @note Not thread-safe. This function does not acquire the mutex
 *       and should only be called from within functions that have already acquired
 *       the mutex or in single-threaded contexts.
 */
struct lr_cell *lr_owner_allocate(struct linked_ring *lr)
{
    struct lr_cell *owner_cell;
    struct lr_cell *needle;
    struct lr_cell *head;
    size_t          owners_nr;

    /* Calculate the number of owners in the linked ring buffer */
    owners_nr = lr_owners_count(lr);

    /* Allocate the owner cell at the appropriate position in the cells array */
    owner_cell = &lr->cells[lr->size - owners_nr - 1];

    /* If the owners array is not empty, check if the owner cell already exists
     */
    if (lr->owners) {
        /* Look up the owner cell starting from the next cell of the owners
         * array */
        struct lr_cell *owner_needle;

        owner_needle = lr->owners;
        while (owner_needle->next == NULL) {
            owner_needle += 1;
        }
        needle = lr_cell_lookup(lr, owner_needle->next, owner_cell);

        /* If the owner cell is found, return it */
        if (needle)
            return needle;
    }

    /* If the owner cell is the same as the write position, update the write
     * position and return it */
    if (owner_cell == lr->write) {
        lr->write = lr->write->next;

        return owner_cell;
    }

    /* Look for the owner cell in the free pool */
    head   = lr->write;
    needle = head;
    while (needle->next != head && needle->next != owner_cell) {
        needle = needle->next;
    }

    /* If the owner cell is found, update the next pointer to skip it */
    if (needle->next == owner_cell) {
        needle->next = needle->next->next;

        return owner_cell;
    }

    return NULL;
}

/**
 * Get or create an owner cell.
 *
 * @param lr Pointer to the linked ring structure
 * @param owner The owner ID
 *
 * @return Pointer to the owner cell if found or created, NULL otherwise
 *
 * @note Not thread-safe. This function does not acquire the mutex
 *       and should only be called from within functions that have already acquired
 *       the mutex or in single-threaded contexts.
 */
struct lr_cell *lr_owner_get(struct linked_ring *lr, lr_data_t owner)
{
    struct lr_cell *owner_cell = NULL;

    /* Find the owner cell in the linked ring buffer */
    owner_cell = lr_owner_find(lr, owner);
    if (owner_cell)
        return owner_cell;

    /* If the write position is empty, we can't allocate a new owner cell */
    if (!lr->write->next)
        return NULL;

    /* Allocate a new owner cell and update the owners array */
    owner_cell       = lr_owner_allocate(lr);
    lr->owners       = owner_cell;
    owner_cell->data = owner;
    owner_cell->next = NULL;

    return owner_cell;
}

/* ========================================================================== */
/* 4. CELL MANAGEMENT FUNCTIONS                                               */
/* ========================================================================== */

/**
 * Swap the provided cell with the cell at the write position in the linked ring
 * buffer.
 *
 * @param lr: pointer to the linked ring structure
 * @param cell: pointer to the cell to be swapped
 *
 * @return pointer to the swapped cell
 */
struct lr_cell *lr_cell_swap(struct linked_ring *lr, struct lr_cell *cell)
{
    struct lr_cell *swap;

    /* Store the cell at the write position in the swap variable */
    swap = lr->write;

    /* Update the write position to the next cell */
    if (swap->next) {
        lr->write = swap->next;
    } else if (swap->next == NULL) {
        lr->write = NULL;
    }

    /* Copy the data and next pointer from the provided cell to the swap cell */
    swap->data = cell->data;
    swap->next = cell->next;

    /* Update any pointers that were pointing to the cell to now point to swap
     */
    for (struct lr_cell *owner_swap = lr->cells;
         owner_swap < (lr->cells + lr->size); owner_swap++) {
        if (owner_swap->next == cell) {
            owner_swap->next = swap;
        }
    }


    return swap;
}

/**
 * Lookup a cell in the linked ring buffer starting from the provided head cell.
 * If the cell is found, it is swapped with the cell at the write position and
 * the head cell is updated accordingly.
 *
 * @param lr: pointer to the linked ring structure
 * @param head: pointer to the head cell from where the lookup should start
 * @param cell: pointer to the cell to be looked up
 *
 * @return pointer to the looked up cell if found, NULL otherwise
 */
struct lr_cell *lr_cell_lookup(struct linked_ring *lr, struct lr_cell *head,
                               struct lr_cell *cell)
{
    struct lr_cell *swap;
    struct lr_cell *needle;

    /* Traverse through the linked ring buffer starting from the head cell */
    needle = head;
    while (needle->next != head && needle->next != cell) {
        needle = needle->next;
    }

    /* If the cell is found, swap it with the cell at the write position */
    if (needle->next == cell) {
        swap         = lr_cell_swap(lr, cell);
        needle->next = swap;

        return cell;
    }

    return NULL;
}


/* ========================================================================== */
/* 5. DATA INSERTION OPERATIONS                                               */
/* ========================================================================== */

/**
 * Add a new element to the head of the linked ring buffer.
 *
 * @param lr Pointer to the linked ring structure
 * @param data The data to be added to the buffer
 * @param owner The owner of the new element
 *
 * @return LR_OK if the element was successfully added
 * @return LR_ERROR_BUFFER_FULL if the buffer is full
 * @return LR_ERROR_LOCK if mutex acquisition failed
 *
 * @note Thread-safe with mutex. Acquires and releases mutex internally.
 * 
 * @note Implementation challenge: This function must handle multiple cases:
 *       1. Adding to an existing owner's list
 *       2. Creating a new owner
 *       3. Maintaining the circular structure
 *       4. Updating the owner's tail pointer
 *       Each case requires careful pointer manipulation to preserve the
 *       circular structure of the buffer.
 */

lr_result_t lr_put(struct linked_ring *lr, lr_data_t data, lr_data_t owner)
{
    struct lr_cell *tail;
    struct lr_cell *cell;
    struct lr_cell *chain;
    struct lr_cell *owner_cell;
    struct lr_cell *prev_owner;

    if (lr == NULL) {
        return LR_ERROR_NOMEMORY;
    }

    /* Debug: Print original cell structure before any changes */

    lock(lr);

    /* Check if we have space to add a new element */
    if (lr->write == NULL) {
        unlock_and_return(lr, LR_ERROR_BUFFER_FULL);
    }

    /* Find or create owner cell */
    owner_cell = lr_owner_get(lr, owner);

    if (owner_cell == NULL) {
        unlock_and_return(lr, LR_ERROR_BUFFER_FULL);
    }

    /* Get the tail for this owner */
    tail = lr_owner_tail(owner_cell);

    /* Allocate a cell for the new data */
    cell = lr->write;

    /* Update write pointer to next available cell */
    lr->write = lr->write->next;

    /* Store the data */
    cell->data = data;

    if (tail) {
        /* If owner already exists, insert after tail while preserving the
         * circular structure */
        cell->next = tail->next;
        tail->next = cell;
    } else {
        /* If new owner */
        if (owner_cell < lr_last_cell(lr)) {
            /* If prev owner exists */
            prev_owner = owner_cell + 1;

            while (prev_owner->next == NULL && prev_owner < lr_last_cell(lr)) {
                prev_owner += 1;
            }

            if (prev_owner->next != NULL) {
                /* Insert into the existing circular list */
                chain                  = prev_owner->next->next;
                cell->next             = chain;
                prev_owner->next->next = cell;
            } else {
                /* If no valid previous owner found, create a self-referential
                 * loop */
                cell->next = cell;
            }
        } else {
            /* If first owner, create a self-referential loop */
            cell->next = cell;
        }
    }

    /* Update owner's tail pointer to the new cell */
    owner_cell->next = cell;

    unlock_and_return(lr, LR_OK);
}

/**
 * Add a new element to the tail of the linked ring buffer.
 *
 * @param lr Pointer to the linked ring structure
 * @param data The data to be added to the buffer
 * @param owner The owner of the new element
 *
 * @return LR_OK if the element was successfully added
 * @return LR_ERROR_BUFFER_FULL if the buffer is full
 * @return LR_ERROR_LOCK if mutex acquisition failed
 *
 * @note Thread-safe with mutex. Acquires and releases mutex internally.
 */
lr_result_t lr_push(struct linked_ring *lr, lr_data_t data, lr_owner_t owner)
{
    struct lr_cell *tail;
    struct lr_cell *cell;
    struct lr_cell *owner_cell;

    if (lr == NULL) {
        return LR_ERROR_NOMEMORY;
    }

    lock(lr);

    /* Check if we have space to add a new element */
    if (lr->write == NULL) {
        unlock_and_return(lr, LR_ERROR_BUFFER_FULL);
    }

    /* Find or create owner cell */
    owner_cell = lr_owner_get(lr, owner);
    if (owner_cell == NULL) {
        unlock_and_return(lr, LR_ERROR_BUFFER_FULL);
    }

    /* Get the tail for this owner */
    tail = lr_owner_tail(owner_cell);

    /* Allocate a cell for the new data */
    cell = lr->write;

    /* Update write pointer to next available cell */
    lr->write = lr->write->next;

    /* Store the data */
    cell->data = data;

    if (tail) {
        /* If owner already exists, insert after tail while preserving the
         * circular structure */
        cell->next = tail->next;
        tail->next = cell;
    } else {
        /* If this is a new owner, we need to set up the circular structure */
        struct lr_cell *prev_owner;
        struct lr_cell *last_cell = lr_last_cell(lr);

        if (owner_cell < last_cell) {
            /* Find a valid previous owner with a next pointer */
            prev_owner = owner_cell + 1;
            while (prev_owner->next == NULL && prev_owner < last_cell) {
                prev_owner += 1;
            }

            if (prev_owner->next != NULL) {
                /* Insert into the existing circular list */
                cell->next             = prev_owner->next->next;
                prev_owner->next->next = cell;
            } else {
                /* If no valid previous owner found, create a self-referential
                 * loop */
                cell->next = cell;
            }
        } else {
            /* If first owner, create a self-referential loop */
            cell->next = cell;
        }
    }

    /* Update owner's tail pointer to the new cell */
    owner_cell->next = cell;

    unlock_and_return(lr, LR_OK);
}

/**
 * Insert a new element after a specific cell.
 *
 * @param lr Pointer to the linked ring structure
 * @param data The data to be added to the buffer
 * @param needle The cell after which to insert the new element
 *
 * @return LR_OK if the element was successfully added
 * @return LR_ERROR_BUFFER_FULL if the buffer is full
 * @return LR_ERROR_LOCK if mutex acquisition failed
 *
 * @note Thread-safe with mutex.
 */
lr_result_t lr_insert_next(struct linked_ring *lr, lr_data_t data,
                           struct lr_cell *needle)
{
    struct lr_cell *cell;


    lock(lr);

    if (lr->write == NULL) {
        unlock_and_return(lr, LR_ERROR_BUFFER_FULL);
    }

    cell      = lr->write;
    lr->write = lr->write->next;

    cell->data = data;

    cell->next = needle->next;

    needle->next = cell;

    unlock_and_return(lr, LR_OK);
}

/**
 * Insert a new element at the specified index of the linked ring buffer.
 *
 * @param lr Pointer to the linked ring structure
 * @param data The data to be added to the buffer
 * @param owner The owner of the new element
 * @param index The index at which to insert the element
 *
 * @return LR_OK if the element was successfully added
 * @return LR_ERROR_BUFFER_FULL if the buffer is full
 * @return LR_ERROR_LOCK if mutex acquisition failed
 *
 * @note Thread-safe with mutex.
 */
lr_result_t lr_insert(struct linked_ring *lr, lr_data_t data, lr_data_t owner,
                      size_t index)
{
    size_t          cell_index;
    struct lr_cell *head;
    struct lr_cell *needle;
    struct lr_cell *tail;
    struct lr_cell *last_cell;
    struct lr_cell *cell;
    struct lr_cell *owner_cell;
    struct lr_cell *prev_owner;

    lock(lr);

    owner_cell = lr_owner_find(lr, owner);
    if (lr->write == NULL) {
        unlock_and_return(lr, LR_ERROR_BUFFER_FULL);
    }

    owner_cell = lr_owner_get(lr, owner);

    if (owner_cell == NULL) {
        unlock_and_return(lr, LR_ERROR_BUFFER_FULL);
    }

    last_cell = lr_last_cell(lr);
    if (owner_cell == last_cell) {
        prev_owner = lr->owners;
    } else {
        prev_owner = owner_cell + 1;
    }
    while (prev_owner->next == NULL) {
        prev_owner += 1;
    }
    head = prev_owner->next->next;
    tail = lr_owner_tail(owner_cell);

    if (!tail) {
        unlock(lr);
        return lr_put(lr, data, owner);
    }

    cell      = lr->write;
    lr->write = lr->write->next;

    cell->data = data;

    if (index == 0) {
        needle = prev_owner->next;
        /* When inserting at the beginning */
        cell->next             = head;
        prev_owner->next->next = cell;

        unlock_and_return(lr, LR_OK);
    } else {
        needle     = head;
        cell_index = 1;
        while (cell_index != index && needle != tail) {
            needle = needle->next;
            cell_index++;
        }
    }

    if (needle == tail) {
        owner_cell->next = cell;
    }

    cell->next   = needle->next;
    needle->next = cell;

    unlock_and_return(lr, LR_OK);
}

/**
 * Add a string to the linked ring buffer.
 *
 * @param lr Pointer to the linked ring structure
 * @param data The string to be added to the buffer
 * @param owner The owner of the new elements
 *
 * @return LR_OK if the string was successfully added
 * @return LR_ERROR_BUFFER_FULL if the buffer is full
 * @return LR_ERROR_LOCK if mutex acquisition failed
 *
 * @note Thread-safe with mutex. Acquires and releases mutex internally
 *       for each character, but not atomic for the entire string.
 */
lr_result_t lr_put_string(struct linked_ring *lr, unsigned char *data,
                          lr_owner_t owner)
{
    /* Loop through each character in the string */
    while (*data) {
        /* Add character to the buffer */
        if (lr_put(lr, *(data++), owner) == LR_ERROR_BUFFER_FULL)
            /* If the buffer is full, return an error */
            return LR_ERROR_BUFFER_FULL;
    };

    /* If all characters were added successfully */
    return LR_OK;
}

/* ========================================================================== */
/* 6. DATA RETRIEVAL OPERATIONS                                               */
/* ========================================================================== */


/**
 * Retrieve and remove the next element from the linked ring buffer.
 *
 * @param lr Pointer to the linked ring structure
 * @param data Pointer to store the retrieved data
 * @param owner The owner of the element to retrieve
 *
 * @return LR_OK if the element was successfully retrieved
 * @return LR_ERROR_BUFFER_EMPTY if the buffer is empty for this owner
 * @return LR_ERROR_LOCK if mutex acquisition failed
 *
 * @note Thread-safe with mutex. Acquires and releases mutex internally.
 * 
 * @note Implementation challenge: This function must handle:
 *       1. Removing an element while preserving the circular structure
 *       2. Special case when removing the last element for an owner
 *       3. Returning the cell to the free list
 *       4. Potentially removing the owner cell if it has no more data
 *       These operations require careful pointer manipulation to maintain
 *       the integrity of the buffer structure.
 */
lr_result_t lr_get(struct linked_ring *lr, lr_data_t *data, lr_owner_t owner)
{
    struct lr_cell *head;
    struct lr_cell *last_cell;
    struct lr_cell *tail;
    struct lr_cell *prev_owner;
    struct lr_cell *owner_cell;

    if (lr == NULL || data == NULL) {
        return LR_ERROR_NOMEMORY;
    }

    lock(lr);

    /* Find the owner cell */
    owner_cell = lr_owner_find(lr, owner);
    if (owner_cell == NULL) {
        unlock_and_return(lr, LR_ERROR_BUFFER_EMPTY);
    }

    /* Find the previous owner to get the head of our list */
    last_cell = lr_last_cell(lr);
    if (owner_cell == last_cell) {
        prev_owner = lr->owners;
    } else {
        prev_owner = owner_cell + 1;
    }

    /* Find a valid previous owner with a next pointer */
    while (prev_owner->next == NULL && prev_owner < last_cell) {
        prev_owner += 1;
    }

    /* If we couldn't find a valid previous owner, return empty */
    if (prev_owner->next == NULL) {
        unlock_and_return(lr, LR_ERROR_BUFFER_EMPTY);
    }

    /* Get the head of the list and update the circular structure */
    head                   = prev_owner->next->next;
    prev_owner->next->next = head->next;

    /* Extract the data */
    *data = head->data;

    /* Get the tail for this owner */
    tail = lr_owner_tail(owner_cell);

    if (head == tail) {
        /* If this was the last cell for this owner */
        /* Delete and shorten the list, put a new link to lr->owners */
        for (struct lr_cell *owner_swap = owner_cell; owner_swap > lr->owners;
             owner_swap--) {
            struct lr_cell *next_owner = owner_swap - 1;
            *owner_swap                = *next_owner;
        }

        /* Return the owner cell to the free list */
        if (lr->write == NULL) {
            lr->write       = lr->owners;
            lr->write->next = NULL;
        } else {
            lr->owners->next = lr->write;
            lr->write        = lr->owners;
        }

        /* Update owners pointer */
        if (lr->owners == last_cell) {
            lr->owners = NULL;
        } else {
            lr->owners += 1;
        }
    }

    /* Return the cell to the free list */
    head->next = lr->write;
    lr->write  = head;

    lr->write->data = 0;
    unlock_and_return(lr, LR_OK);
}

/**
 * Retrieve and remove the last element from the linked ring buffer.
 *
 * @param lr Pointer to the linked ring structure
 * @param data Pointer to store the retrieved data
 * @param owner The owner of the element to retrieve
 *
 * @return LR_OK if the element was successfully retrieved
 * @return LR_ERROR_BUFFER_EMPTY if the buffer is empty for this owner
 * @return LR_ERROR_LOCK if mutex acquisition failed
 *
 * @note Thread-safe with mutex. Acquires and releases mutex internally.
 */
lr_result_t lr_pop(struct linked_ring *lr, lr_data_t *data, lr_owner_t owner)
{
    struct lr_cell *head;
    struct lr_cell *needle;
    struct lr_cell *last_cell;
    struct lr_cell *tail;
    struct lr_cell *prev_owner;
    struct lr_cell *owner_cell;

    lock(lr);

    owner_cell = lr_owner_find(lr, owner);
    if (owner_cell == NULL) {
        return LR_ERROR_BUFFER_EMPTY;
    }

    last_cell = lr_last_cell(lr);
    if (owner_cell == last_cell) {
        prev_owner = lr->owners;
    } else {
        prev_owner = owner_cell + 1;
    }
    while (prev_owner->next == NULL) {
        prev_owner += 1;
    }
    head  = prev_owner->next->next;
    tail  = lr_owner_tail(owner_cell);
    *data = tail->data;

    if (head == tail) {
        /* If this is the last cell for this owner, remove the owner */
        for (struct lr_cell *owner_swap = owner_cell; owner_swap > lr->owners;
             owner_swap--) {
            struct lr_cell *next_owner = owner_swap - 1;
            *owner_swap                = *next_owner;
        }

        if (prev_owner != owner_cell)
            prev_owner->next->next = tail->next;

        lr->owners->next = lr->write;
        lr->write        = lr->owners;

        if (lr->owners == last_cell) {
            lr->owners = NULL;
        } else {
            lr->owners += 1;
        }
    }


    needle = head;
    while (needle != tail) {
        if (needle->next == tail) {
            owner_cell->next = needle;
            needle->next     = tail->next;
            needle           = tail;
        } else {
            needle = needle->next;
        }
    }

    tail->next = lr->write;
    lr->write  = tail;

    unlock_and_return(lr, LR_OK);
}

/**
 * Retrieve and remove an element at a specific index.
 *
 * @param lr Pointer to the linked ring structure
 * @param data Pointer to store the retrieved data
 * @param owner The owner of the element to retrieve
 * @param index The index of the element to retrieve
 *
 * @return LR_OK if the element was successfully retrieved
 * @return LR_ERROR_BUFFER_EMPTY if the buffer is empty for this owner or index is invalid
 * @return LR_ERROR_LOCK if mutex acquisition failed
 *
 * @note Thread-safe with mutex. Acquires and releases mutex internally.
 */
lr_result_t lr_pull(struct linked_ring *lr, lr_data_t *data, lr_owner_t owner,
                    size_t index)
{
    size_t          needle_index;
    struct lr_cell *head;
    struct lr_cell *needle;
    struct lr_cell *selected;
    struct lr_cell *last_cell;
    struct lr_cell *tail;
    struct lr_cell *prev_owner;
    struct lr_cell *owner_cell;

    if (lr == NULL || data == NULL) {
        return LR_ERROR_NOMEMORY;
    }

    lock(lr);

    /* Find the owner cell */
    owner_cell = lr_owner_find(lr, owner);
    if (owner_cell == NULL) {
        unlock_and_return(lr, LR_ERROR_BUFFER_EMPTY);
    }

    /* Find the previous owner to get the head of our list */
    last_cell = lr_last_cell(lr);
    if (owner_cell == last_cell) {
        prev_owner = lr->owners;
    } else {
        prev_owner = owner_cell + 1;
    }

    /* Find a valid previous owner with a next pointer */
    while (prev_owner->next == NULL && prev_owner < last_cell) {
        prev_owner += 1;
    }

    if (prev_owner->next == NULL) {
        unlock_and_return(lr, LR_ERROR_BUFFER_EMPTY);
    }

    /* Get the head and tail of the list */
    head = prev_owner->next->next;
    tail = lr_owner_tail(owner_cell);

    /* Validate head and tail */
    if (head == NULL || tail == NULL) {
        unlock_and_return(lr, LR_ERROR_BUFFER_EMPTY);
    }

    // Handle case where there's only one element for this owner
    if (head == tail) {
        if (index == 0) {
            *data = head->data;

            /* Remove this owner since it's the last cell */
            for (struct lr_cell *owner_swap = owner_cell;
                 owner_swap > lr->owners; owner_swap--) {
                struct lr_cell *next_owner = owner_swap - 1;
                *owner_swap                = *next_owner;
            }

            /* Update the circular structure */
            if (prev_owner != owner_cell && prev_owner->next != NULL) {
                prev_owner->next->next = tail->next;
            }

            /* Return the owner cell to the free list */
            if (lr->write == NULL) {
                lr->write = lr->owners;
            } else {
                lr->owners->next = lr->write;
                lr->write        = lr->owners;
            }

            /* Update owners pointer */
            if (lr->owners == last_cell) {
                lr->owners = NULL;
            } else {
                lr->owners += 1;
            }

            /* Return the cell to the free list */
            head->next = lr->write;
            lr->write  = head;

            unlock_and_return(lr, LR_OK);
        } else {
            unlock_and_return(lr, LR_ERROR_BUFFER_EMPTY);
        }
    }

    // Handle index 0 specially
    if (index == 0) {
        selected = head;
        *data    = selected->data;

        /* Update the circular structure */
        if (prev_owner->next != NULL) {
            prev_owner->next->next = selected->next;
        }

        if (selected == tail) {
            owner_cell->next = NULL;
        }
    } else {
        // Find the element at the specified index
        needle       = head;
        needle_index = 0;

        // Find the element before the one we want to pull
        while (needle_index < index - 1 && needle->next != tail
               && needle->next != head) {
            needle = needle->next;
            needle_index++;
        }

        // If we couldn't reach the index
        if (needle_index < index - 1) {
            unlock_and_return(lr, LR_ERROR_BUFFER_EMPTY);
        }

        selected = needle->next;
        *data    = selected->data;

        // Update the linked list to skip the pulled element
        needle->next = selected->next;

        // If we're removing the tail, update the owner's tail pointer
        if (selected == tail) {
            owner_cell->next = needle;
        }
    }

    // Return the cell to the free list
    selected->next = lr->write;
    lr->write      = selected;

    unlock_and_return(lr, LR_OK);
}

/**
 * Read data from the head without removing it (peek operation).
 *
 * @param lr Pointer to the linked ring structure
 * @param data Pointer to store the read data
 * @param owner The owner of the data to read
 *
 * @return LR_OK if the data was successfully read
 * @return LR_ERROR_BUFFER_EMPTY if the buffer is empty for this owner
 * @return LR_ERROR_LOCK if mutex acquisition failed
 *
 * @note Thread-safe with mutex. Acquires and releases mutex internally.
 */
lr_result_t lr_read(struct linked_ring *lr, lr_data_t *data, lr_owner_t owner)
{
    return lr_read_at(lr, data, owner, 0);
}

/**
 * Read a string from the buffer without removing it.
 *
 * @param lr Pointer to the linked ring structure
 * @param data Buffer to store the string
 * @param length Pointer to store the length of the string
 * @param owner The owner of the string to read
 *
 * @return LR_OK if the string was successfully read
 * @return LR_ERROR_BUFFER_EMPTY if the buffer is empty for this owner
 * @return LR_ERROR_LOCK if mutex acquisition failed
 *
 * @note Thread-safe with mutex. Acquires and releases mutex internally.
 */
lr_result_t lr_read_string(struct linked_ring *lr, unsigned char *data,
                           size_t *length, lr_owner_t owner)
{
    struct lr_cell *needle;
    struct lr_cell *tail;
    struct lr_cell *owner_cell;

    lock(lr);

    *length    = 0;
    owner_cell = lr_owner_find(lr, owner);
    if (owner_cell == NULL) {
        unlock_and_return(lr, LR_ERROR_BUFFER_EMPTY);
    }

    needle = lr_owner_head(lr, owner_cell);
    tail   = lr_owner_tail(owner_cell);

    // Handle empty line case
    if (needle == tail && tail->data == 0) {
        *data = '\0';
        unlock_and_return(lr, LR_OK);
    }

    do {
        *data++ = needle->data;
        needle  = needle->next;
        *length += 1;
    } while (needle != tail->next);
    *data = '\0';

    unlock_and_return(lr, LR_OK);
}

/**
 * Read a character at a specific index without removing it.
 *
 * @param lr Pointer to the linked ring structure
 * @param data Pointer to store the read character
 * @param owner The owner of the character to read
 * @param index The index of the character to read
 *
 * @return LR_OK if the character was successfully read
 * @return LR_ERROR_BUFFER_EMPTY if the buffer is empty for this owner
 * @return LR_ERROR_INVALID_INDEX if the index is out of bounds
 * @return LR_ERROR_LOCK if mutex acquisition failed
 *
 * @note Thread-safe with mutex. Acquires and releases mutex internally.
 */
lr_result_t lr_read_at(struct linked_ring *lr, lr_data_t *data,
                       lr_owner_t owner, size_t index)
{
    struct lr_cell *head;
    struct lr_cell *tail;
    struct lr_cell *needle;
    struct lr_cell *owner_cell;
    size_t          count = 0;

    lock(lr);

    /* Find the owner cell */
    owner_cell = lr_owner_find(lr, owner);
    if (owner_cell == NULL) {
        unlock_and_return(lr, LR_ERROR_BUFFER_EMPTY);
    }

    /* Get the head of the owner's data */
    head = lr_owner_head(lr, owner_cell);
    tail = lr_owner_tail(owner_cell);
    if (head == NULL || tail == NULL) {
        unlock_and_return(lr, LR_ERROR_BUFFER_EMPTY);
    }

    /* Traverse to the specified index */
    needle = head;
    while (count < index) {
        if (needle == tail) {
            /* Reached the end before finding the index */
            unlock_and_return(lr, LR_ERROR_INVALID_INDEX);
        }
        needle = needle->next;
        count++;
    }

    /* Return the data at the specified index */
    *data = needle->data;

    unlock_and_return(lr, LR_OK);
}

/* ========================================================================== */
/* 7. BUFFER INFORMATION FUNCTIONS                                            */
/* ========================================================================== */

/**
 * Count elements owned by a specific owner, with an optional limit.
 *
 * @param lr Pointer to the linked ring structure
 * @param limit Maximum number of elements to count (0 for no limit)
 * @param owner The owner ID
 *
 * @return Number of elements owned by the specified owner (up to the limit if specified)
 *
 * @note Thread-safe with mutex. Acquires and releases mutex internally.
 */
size_t lr_count_limited_owned(struct linked_ring *lr, size_t limit,
                              lr_owner_t owner)
{
    size_t          length;
    struct lr_cell *head;
    struct lr_cell *needle;
    struct lr_cell *tail;
    struct lr_cell *owner_cell;


    lock(lr);

    length     = 0;
    owner_cell = lr_owner_find(lr, owner);
    if (owner_cell == NULL) {
        unlock_and_return(lr, length);
    }

    head = lr_owner_head(lr, owner_cell);
    tail = lr_owner_tail(owner_cell);

    needle = head;
    length = 1;
    while (needle != tail && (limit == 0 || length < limit)) {
        needle = needle->next;
        length += 1;
    }

    unlock_and_return(lr, length);
}

/**
 * Count the total number of elements in the buffer.
 *
 * @param lr Pointer to the linked ring structure
 *
 * @return Total number of elements in the buffer
 *
 * @note Thread-safe with mutex. Acquires and releases mutex internally.
 */
size_t lr_count(struct linked_ring *lr)
{
    struct lr_cell *head;
    struct lr_cell *needle;
    size_t          length;

    lock(lr);

    length = 0;
    if (lr->owners == NULL) {
        unlock_and_return(lr, length);
    }


    struct lr_cell *owner_cell = lr->owners;
    while (owner_cell->next == NULL) {
        owner_cell++;
    }

    head = owner_cell->next->next;

    length = 1;
    needle = head;
    while (needle->next != head) {
        needle = needle->next;
        length += 1;
    }

    unlock_and_return(lr, length);
}


/* ========================================================================== */
/* 8. DEBUGGING FUNCTIONS                                                     */
/* ========================================================================== */

/**
 * Print detailed information about buffer contents by owner.
 *
 * @param lr Pointer to the linked ring structure
 *
 * @return LR_OK if successful, error code otherwise
 *
 * @note Thread-safe with mutex. Acquires and releases mutex internally.
 */
lr_result_t lr_print(struct linked_ring *lr)
{
    struct lr_cell *head;
    struct lr_cell *needle;
    struct lr_cell *tail;
    struct lr_cell *owner_cell;
    size_t          owner_count = 0;

    lock(lr);

    if (lr->owners == NULL) {
        printf("\033[33mNo owners found in buffer\033[0m\n");
        unlock_and_return(lr, LR_ERROR_BUFFER_EMPTY);
    }

    for (owner_cell = lr_last_cell(lr); owner_cell >= lr->owners;
         owner_cell--) {
        owner_count++;

        // Special handling for owner 0 (typically file path in file operations)
        if (owner_cell->data == 0) {
            printf("\n\033[1;36mOwner: %lu (File Path)\033[0m\n",
                   owner_cell->data);
        } else {
            printf("\n\033[1;32mOwner: %lu\033[0m\n", owner_cell->data);
        }

        if (owner_cell->next == NULL) {
            printf("\033[31mERROR: Owner cell is empty\033[0m\n");
            continue;
        }

        head = lr_owner_head(lr, owner_cell);
        tail = lr_owner_tail(owner_cell);

        // Print a nice table header for the data
        printf("┌───────┬─────────┬────────────────────────┐\n");
        printf("│ Index │ Value   │ Representation         │\n");
        printf("├───────┼─────────┼────────────────────────┤\n");

        needle       = head;
        size_t index = 0;

        while (needle != tail) {
            // Print data in multiple formats for better understanding
            if (needle->data > 31 && needle->data < 127) {
                printf("│ %5lu │ 0x%04lx  │ '%c' (ASCII printable)  │\n", index,
                       needle->data, (char)needle->data);
            } else if (needle->data <= 31) {
                printf("│ %5lu │ 0x%04lx  │ CTRL (ASCII control)   │\n", index,
                       needle->data);
            } else if (needle->data == 127) {
                printf("│ %5lu │ 0x%04lx  │ DEL (ASCII control)    │\n", index,
                       needle->data);
            } else if (needle->data > 127 && needle->data <= 255) {
                printf("│ %5lu │ 0x%04lx  │ Extended ASCII         │\n", index,
                       needle->data);
            } else {
                printf("│ %5lu │ 0x%04lx  │ Binary data            │\n", index,
                       needle->data);
            }

            needle = needle->next;
            index++;
        }

        // Print the last element (tail)
        if (needle->data > 31 && needle->data < 127) {
            printf("│ %5lu │ 0x%04lx  │ '%c' (ASCII printable)  │\n", index,
                   needle->data, (char)needle->data);
        } else if (needle->data <= 31) {
            printf("│ %5lu │ 0x%04lx  │ CTRL (ASCII control)   │\n", index,
                   needle->data);
        } else if (needle->data == 127) {
            printf("│ %5lu │ 0x%04lx  │ DEL (ASCII control)    │\n", index,
                   needle->data);
        } else if (needle->data > 127 && needle->data <= 255) {
            printf("│ %5lu │ 0x%04lx  │ Extended ASCII         │\n", index,
                   needle->data);
        } else {
            printf("│ %5lu │ 0x%04lx  │ Binary data            │\n", index,
                   needle->data);
        }

        printf("└───────┴─────────┴────────────────────────┘\n");
    }

    printf("\n\033[1mTotal owners: %lu\033[0m\n", owner_count);
    unlock_and_return(lr, LR_OK);
}

/**
 * Dump the contents of the buffer for debugging.
 *
 * @param lr Pointer to the linked ring structure
 *
 * @return LR_OK if successful, error code otherwise
 *
 * @note Thread-safe with mutex. Acquires and releases mutex internally.
 *       However, the buffer state may change between acquiring and releasing the mutex
 *       for different operations within this function.
 */
lr_result_t lr_dump(struct linked_ring *lr)
{
    struct lr_cell *head;
    size_t          buffer_usage_percent;

    lock(lr);
    head = NULL;
    if (lr->owners) {
        struct lr_cell *owner_cell = lr->owners;
        while (owner_cell->next == NULL) {
            owner_cell++;
        }

        head = owner_cell->next->next;
    }

    // Calculate buffer usage percentage
    size_t total_elements = lr_count(lr);
    size_t total_owners   = lr_owners_count(lr);
    size_t available      = lr_available(lr);
    buffer_usage_percent  = (total_elements + total_owners) * 100 / lr->size;

    // Print header with box drawing characters
    printf("\n┌───────────────────────────────────────────┐\n");
    printf("│         \033[1mLinked Ring Buffer Status\033[0m         │\n");
    printf("├─────────────────────────┬─────────────────┤\n");
    printf("│ Memory Addresses        │      Values     │\n");
    printf("├─────────────────────────┼─────────────────┤\n");
    printf("│ Head pointer            │ %15p │\n", (void *)head);
    printf("│ Write pointer           │ %15p │\n", (void *)lr->write);
    printf("│ Cells array             │ %15p │\n", (void *)lr->cells);
    printf("├─────────────────────────┼─────────────────┤\n");
    printf("│ Buffer Metrics          │      Values     │\n");
    printf("├─────────────────────────┼─────────────────┤\n");
    printf("│ Total capacity (cells)  │   %13d │\n", lr->size);
    printf("│ Elements in buffer      │   %13ld │\n", total_elements);
    printf("│ Owner count             │   %13ld │\n", total_owners);
    printf("│ Available space         │   %13ld │\n", available);
    printf("│ Buffer usage            │   %12ld%% │\n", buffer_usage_percent);
    printf("└─────────────────────────┴─────────────────┘\n");

    // If buffer is empty, return early
    if (total_elements == 0) {
        printf("\n\033[33mBuffer is empty - no data to display\033[0m\n\n");
        unlock_and_return(lr, LR_ERROR_BUFFER_EMPTY);
    }

    // Print buffer contents with improved formatting
    printf("\n┌─────────────────────────────────────────┐\n");
    printf("│         \033[1mBuffer Contents by Owner\033[0m        │\n");
    printf("└─────────────────────────────────────────┘\n");

    lr_print(lr);

    unlock_and_return(lr, LR_OK);
}

/**
 * Debug the cell structure.
 *
 * @param lr Pointer to the linked ring structure
 *
 * @note Not thread-safe. This function does not acquire the mutex
 *       and should only be called in single-threaded contexts or when no other
 *       threads are accessing the buffer.
 */
void lr_debug_strucuture_cells(struct linked_ring *lr)
{
    if (lr == NULL || lr->cells == NULL) {
        printf("\033[31mERROR: Cannot debug NULL buffer or cells\033[0m\n");
        return;
    }

    printf("\n\033[1;35m=== Original Cells Structure ===\033[0m\n");
    printf("Buffer size: %u, Owners count: %zu\n", lr->size,
           lr_owners_count(lr));
    printf("Cells array address: %p\n", (void *)lr->cells);
    printf("Write pointer: %p\n", (void *)lr->write);

    printf("\n\033[1mCell array contents:\033[0m\n");
    printf("┌───────┬─────────────────┬────────────┬─────────────────┐\n");
    printf("│ Index │     Address     │ Data Value │    Next Addr    │\n");
    printf("├───────┼─────────────────┼────────────┼─────────────────┤\n");

    for (unsigned int i = 0; i < lr->size; i++) {
        struct lr_cell *cell = &lr->cells[i];
        printf("│ %5u │ %15p │ %10lu │ %15p │\n", i, (void *)cell, cell->data,
               (void *)cell->next);
    }

    printf("└───────┴─────────────────┴────────────┴─────────────────┘\n");
}

/**
 * Debug the circular structure for a specific owner.
 *
 * @param lr Pointer to the linked ring structure
 * @param owner The owner ID
 *
 * @return LR_OK if successful, error code otherwise
 *
 * @note Not thread-safe. This function does not acquire the mutex
 *       and should only be called in single-threaded contexts or when no other
 *       threads are accessing the buffer.
 */
lr_result_t lr_debug_structure_circular(struct linked_ring *lr,
                                        lr_owner_t          owner)
{
    struct lr_cell *owner_cell = lr_owner_find(lr, owner);
    if (owner_cell == NULL) {
        printf("\033[31mERROR: Owner %lu not found in buffer\033[0m\n", owner);
        return LR_ERROR_UNKNOWN;
    }

    struct lr_cell *head    = lr_owner_head(lr, owner_cell);
    struct lr_cell *tail    = lr_owner_tail(owner_cell);
    struct lr_cell *current = head;
    size_t          count   = 0;
    size_t          expected_count = lr_count_owned(lr, owner);

    printf(
        "\n\033[1;36m=== Circular Structure Debug for Owner %lu ===\033[0m\n",
        owner);
    printf("Owner cell address: %p, data: %lu\n", (void *)owner_cell,
           owner_cell->data);
    printf("Head address: %p\n", (void *)head);
    printf("Tail address: %p\n", (void *)tail);
    printf("Tail->next address: %p\n", (void *)tail->next);
    printf("Expected element count: %zu\n", expected_count);

    printf("\n\033[1mTracing owner's elements:\033[0m\n");
    printf("┌───────┬─────────────────┬────────────┬─────────────────┐\n");
    printf("│ Index │     Address     │ Data Value │    Next Addr    │\n");
    printf("├───────┼─────────────────┼────────────┼─────────────────┤\n");

    // First trace just this owner's elements
    do {
        printf("│ %5zu │ %15p │ %10lu │ %15p │\n", count, (void *)current,
               current->data, (void *)current->next);
        
        if (current == tail) {
            break; // Stop when we reach the tail
        }
        
        current = current->next;
        count++;

        /* Safety check to prevent infinite loops during debugging */
        if (count > lr->size) {
            printf(
                "└───────┴─────────────────┴────────────┴─────────────────┘\n");
            printf("\033[31mWARNING: Possible infinite loop detected after %zu "
                   "elements\033[0m\n",
                   count);
            return LR_ERROR_UNKNOWN;
        }
    } while (current != head && count < expected_count);

    printf("└───────┴─────────────────┴────────────┴─────────────────┘\n");

    // Verify element count
    if (count + 1 != expected_count) { // +1 because we didn't count the tail
        printf("\033[33mWARNING: Expected %zu elements, found %zu\033[0m\n", 
               expected_count, count + 1);
    } else {
        printf("\033[32mElement count verified: %zu elements\033[0m\n", expected_count);
    }

    // Now check the global circular structure
    printf("\n\033[1mVerifying global circular structure:\033[0m\n");
    current = head;
    count = 0;
    bool found_head_again = false;
    
    printf("Starting at head (%p) and following next pointers...\n", (void*)head);
    
    do {
        current = current->next;
        count++;
        
        if (current == head) {
            found_head_again = true;
            break;
        }
        
        /* Safety check to prevent infinite loops */
        if (count > lr->size) {
            printf("\033[31mWARNING: Could not complete the global circle within %zu steps\033[0m\n", 
                   count);
            return LR_ERROR_UNKNOWN;
        }
    } while (current != NULL);
    
    if (found_head_again) {
        printf("\033[32mGlobal circular structure verified: Found way back to head in %zu steps\033[0m\n", 
               count);
    } else {
        printf("\033[31mWARNING: Global circular structure is broken - could not find way back to head\033[0m\n");
    }

    return LR_OK;
}

/**
 * Debug the relinked structure after operations.
 *
 * @param lr Pointer to the linked ring structure
 *
 * @note Not thread-safe. This function does not acquire the mutex
 *       and should only be called in single-threaded contexts or when no other
 *       threads are accessing the buffer.
 */
void lr_debug_structure_relinked(struct linked_ring *lr)
{
    if (lr == NULL || lr->cells == NULL) {
        printf("\033[31mERROR: Cannot debug NULL buffer or cells\033[0m\n");
        return;
    }

    printf("\n\033[1;33m=== Relinked Structure After Operations ===\033[0m\n");

    // Print free list (write chain)
    struct lr_cell *free_cell  = lr->write;
    size_t          free_count = 0;

    printf("\n\033[1mFree cells chain (write pointer chain):\033[0m\n");
    printf("Write pointer: %p\n", (void *)lr->write);

    if (free_cell == NULL) {
        printf("No free cells available (buffer full)\n");
    } else {
        printf("┌───────┬─────────────────┬────────────┬─────────────────┐\n");
        printf("│ Index │     Address     │ Data Value │    Next Addr    │\n");
        printf("├───────┼─────────────────┼────────────┼─────────────────┤\n");

        while (free_cell != NULL && free_count < lr->size) {
            printf("│ %5zu │ %15p │ %10lu │ %15p │\n", free_count,
                   (void *)free_cell, free_cell->data, (void *)free_cell->next);

            if (free_cell->next == NULL)
                break;

            // Check for self-referential loop in free list
            if (free_cell->next == free_cell) {
                printf("│ %5zu │ %15p │ %10lu │ \033[31mSELF-REFERENCE!\033[0m "
                       "│\n",
                       free_count, (void *)free_cell, free_cell->data);
                printf("└───────┴─────────────────┴────────────┴───────────────"
                       "──┘\n");
                printf("\033[31mWARNING: Self-referential loop detected in "
                       "free list\033[0m\n");
                break;
            }

            free_cell = free_cell->next;
            free_count++;

            // Safety check
            if (free_count >= lr->size) {
                printf("└───────┴─────────────────┴────────────┴───────────────"
                       "──┘\n");
                printf("\033[31mWARNING: Possible loop in free list\033[0m\n");
                break;
            }
        }
        printf("└───────┴─────────────────┴────────────┴─────────────────┘\n");
        printf("Total free cells: %zu\n", free_count + 1);
    }

    // Print owners information
    size_t owner_count = lr_owners_count(lr);
    printf("\n\033[1mOwners (%zu total):\033[0m\n", owner_count);

    if (owner_count == 0) {
        printf("No owners in buffer\n");
    } else {
        printf("┌───────┬─────────────────┬────────────┬─────────────────┐\n");
        printf("│ Index │     Address     │  Owner ID  │    Tail Addr    │\n");
        printf("├───────┼─────────────────┼────────────┼─────────────────┤\n");

        for (size_t i = 0; i < owner_count; i++) {
            struct lr_cell *owner_cell = lr->owners + i;
            printf("│ %5zu │ %15p │ %10lu │ %15p │\n", i, (void *)owner_cell,
                   owner_cell->data, (void *)owner_cell->next);
        }
        printf("└───────┴─────────────────┴────────────┴─────────────────┘\n");
    }
}
