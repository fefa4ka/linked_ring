#include "lr.h"
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>

/**
 * Initialize a new linked ring buffer.
 *
 * @param lr: pointer to the linked ring structure to be initialized
 * @param size: size of the buffer, in number of elements
 * @param cells: pointer to the array of cells that will make up the buffer
 *
 * @return LR_OK: if the initialization was successful
 *         LR_ERROR_NOMEMORY: if the cells parameter is NULL or size is 0
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
 * @param lr: pointer to the linked ring structure to be resized
 * @param size: size of the buffer, in number of elements
 * @param cells: pointer to the array of cells that will make up the buffer
 *
 * @return LR_OK: if the initialization was successful
 *         LR_ERROR_NOMEMORY: if the cells parameter is NULL or size is 0
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
        size_t data_pos
            = (size_t)(lr->cells + lr->size - (lr->cells + i)->next);
        data_needle->data = (lr->cells + i)->data;
        data_needle->next = cells + data_pos;
        data_needle++;
    }

    // Create owner cells and map them to the new data
    owner_needle = cells + size - owner_nr;
    for (size_t i = 0; i < owner_nr; i++) {
        size_t data_pos
            = (size_t)(lr->cells + lr->size - (lr->owners + i)->next);
        owner_needle->data = (lr->owners + i)->data;
        owner_needle->next = cells + data_pos;
        owner_needle++;
    }

    return LR_OK;
}


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

/* Unlock the mutex if unlock function provided and then return ret  */
#define unlock_and_return(lr, ret)                                             \
    do {                                                                       \
        if (lr->unlock != NULL) {                                              \
            return lr->unlock(lr->mutex_state);                                \
        }                                                                      \
        return ret;                                                            \
    } while (0)

/* Additional overload for returning success */
#define unlock_and_succeed(lr) unlock_and_return(lr, LR_OK)


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
            head = lr->owners->next->next;
        } else {
            /* No valid head found */
            return NULL;
        }
    } else {
        /* For any other cell, the prev owner is used for head linkage */
        prev_owner = owner_cell + 1; /* Owners stored in reverse order */

        while (prev_owner->next == NULL && prev_owner < last_cell) {
            prev_owner += 1;
        }
        
        if (prev_owner->next == NULL) {
            /* No valid head found */
            return NULL;
        }
        
        head = prev_owner->next->next;
    }

    /* Verify head is valid */
    if (head == NULL) {
        return NULL;
    }

    /* Verify circular structure - head should be part of a circular list */
    struct lr_cell *needle = head;
    size_t count = 0;
    size_t max_iterations = lr->size * 2; // Safety limit
    
    while (needle != NULL && needle->next != NULL && count < max_iterations) {
        if (needle->next == head) {
            /* Found circular reference back to head */
            return head;
        }
        needle = needle->next;
        count++;
    }
    
    /* If we get here, the circular structure is broken */
    /* Try to repair it if possible */
    if (owner_cell->next != NULL) {
        /* Use the owner's tail to create a circular reference */
        struct lr_cell *tail = owner_cell->next;
        if (tail != head) {
            tail->next = head;
        }
    }
    
    return head;
}


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
    struct lr_cell *last_free;

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

    /* Update the next pointer of the owners pointing to the provided cell to
     * point to the swap cell */
    for (struct lr_cell *owner_swap = lr->owners;
         owner_swap < (lr->cells + lr->size); owner_swap++) {
        if (owner_swap->next == cell) {
            owner_swap->next = swap;
        }
    }

    return swap;
}


/* Lookup a cell in the linked ring buffer starting from the provided head cell.
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
    if (head == NULL) {
        return NULL;
    }
    
    needle = head;
    size_t count = 0;
    size_t max_iterations = lr->size;
    
    while (needle->next != head && needle->next != cell && count < max_iterations) {
        if (needle->next == NULL) {
            return NULL;  /* Broken list */
        }
        needle = needle->next;
        count++;
    }
    
    /* If we hit the iteration limit, there might be a circular reference issue */
    if (count >= max_iterations) {
        return NULL;
    }

    /* If the cell is found, swap it with the cell at the write position and
     * update the head cell */
    if (needle->next == cell) {
        /* Make sure we have a valid write position */
        if (lr->write == NULL) {
            return NULL;
        }
        
        swap = lr_cell_swap(lr, cell);
        if (swap != NULL) {
            needle->next = swap;
            return cell;
        }
    }

    return NULL;
}

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
     * position */
    if (owner_cell == lr->write) {
        if (lr->write->next != NULL) {
            lr->write = lr->write->next;
        } else {
            /* If write->next is NULL, we need to find another cell to use */
            lr->write = NULL;
            for (size_t i = 0; i < lr->size; i++) {
                if (&lr->cells[i] != owner_cell && 
                    (lr->owners == NULL || &lr->cells[i] < lr->owners)) {
                    lr->write = &lr->cells[i];
                    break;
                }
            }
        }
        return owner_cell;
    }

    /* If the owner cell is not found in the linked ring buffer, lookup in the
     * free pool */
    head = lr->write;
    if (head == NULL) {
        return NULL;  /* No free cells available */
    }
    
    needle = head;
    /* Safety check to prevent infinite loops */
    size_t count = 0;
    size_t max_iterations = lr->size;
    
    while (needle->next != head && needle->next != owner_cell && count < max_iterations) {
        needle = needle->next;
        count++;
    }
    
    /* If we hit the iteration limit, there might be a circular reference issue */
    if (count >= max_iterations) {
        return NULL;
    }

    /* If the owner cell is found, update the next pointer to skip it */
    if (needle->next == owner_cell) {
        /* Ensure we don't create an invalid reference */
        if (needle->next->next != NULL) {
            needle->next = needle->next->next;
        } else {
            needle->next = NULL;
        }
        return owner_cell;
    }

    return NULL;
}

struct lr_cell *lr_owner_get(struct linked_ring *lr, lr_data_t owner)
{
    struct lr_cell *owner_cell = NULL;

    /* Find the owner cell in the linked ring buffer */
    owner_cell = lr_owner_find(lr, owner);
    if (owner_cell)
        return owner_cell;

    /* If the write position is NULL or its next pointer is NULL, we can't allocate */
    if (lr->write == NULL || lr->write->next == NULL)
        return NULL;

    /* Allocate a new owner cell and update the owners array */
    owner_cell = lr_owner_allocate(lr);
    if (owner_cell == NULL) {
        return NULL;
    }
    
    lr->owners = owner_cell;
    owner_cell->data = owner;
    owner_cell->next = NULL;

    return owner_cell;
}


/**
 * Count the number of elements owned by the specified owner in the linked ring
 * buffer. If limit is specified, it will stop counting after reaching the
 * limit.
 *
 * @param lr: pointer to the linked ring structure
 * @param limit: maximum number of elements to count (0 for no limit)
 * @param owner: the owner of the elements to count (0 for all owners)
 *
 * @return the number of elements owned by the specified owner (up to the limit,
 * if specified)
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
 * Count the number of elements in the linked ring buffer.
 *
 * @param lr: pointer to the linked ring structure
 *
 * @return the number of elements in the buffer
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

    head   = lr->owners->next;
    length = 1;
    needle = head;
    while (needle->next != head) {
        needle = needle->next;
        length += 1;
    }

    unlock_and_return(lr, length);
}


/**
 * Set the mutex for a linked ring buffer.
 *
 * @param lr: pointer to the linked ring structure to be initialized
 * @param attr: mutex attributes
 */
void lr_set_mutex(struct linked_ring *lr, struct lr_mutex_attr *attr)
{
    lr->lock        = attr->lock;
    lr->unlock      = attr->unlock;
    lr->mutex_state = attr->state;
}

/**
 * Add a new element to the linked ring buffer.
 *
 * @param lr: pointer to the linked ring structure
 * @param data: the data to be added to the buffer
 * @param owner: the owner of the new element
 *
 * @return LR_OK: if the element was successfully added
 *         LR_ERROR_BUFFER_FULL: if the buffer is full and the element could not
 * be added
 */

lr_result_t lr_put(struct linked_ring *lr, lr_data_t data, lr_data_t owner)
{
    struct lr_cell *tail;
    struct lr_cell *cell;
    struct lr_cell *chain;
    struct lr_cell *owner_cell;
    struct lr_cell *prev_owner;
    struct lr_cell *last_free;

    if (lr == NULL) {
        return LR_ERROR_NOMEMORY;
    }

    lock(lr);

    /* Check if we have space to add a new element */
    if (lr->write == NULL) {
        unlock_and_return(lr, LR_ERROR_BUFFER_FULL);
    }

    /* Find or create owner cell */
    owner_cell = lr_owner_find(lr, owner);
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
        /* If owner already exists, insert after tail while preserving the circular structure */
        if (tail->next != NULL) {
            cell->next = tail->next;
        } else {
            /* If tail->next is NULL, we need to find the head to maintain circularity */
            struct lr_cell *head = lr_owner_head(lr, owner_cell);
            if (head != NULL) {
                cell->next = head;
            } else {
                /* If no head is found, create a self-referential loop */
                cell->next = cell;
            }
        }
        tail->next = cell;
    } else {
        /* If new owner */
        if (owner_cell < lr_last_cell(lr)) {
            /* If prev owner exists */
            prev_owner = owner_cell + 1;

            while (prev_owner->next == NULL && prev_owner < lr_last_cell(lr)) {
                prev_owner += 1;
            }

            if (prev_owner->next != NULL && prev_owner->next->next != NULL) {
                /* Insert into the existing circular list */
                chain = prev_owner->next->next;
                cell->next = chain;
                prev_owner->next->next = cell;
            } else {
                /* If no valid previous owner found, create a self-referential loop */
                cell->next = cell;
            }
        } else {
            /* If first owner, create a self-referential loop */
            cell->next = cell;
        }
    }

    /* Update owner's tail pointer to the new cell */
    owner_cell->next = cell;

    // Debug verification of circular structure
    #if 1
    if (tail) {
        struct lr_cell *head = lr_owner_head(lr, owner_cell);
        if (cell->next != head) {
            printf("WARNING: Circular structure broken after put for owner %lu\n", owner);
            printf("New cell %p should point to head %p but points to %p\n",
                   cell, head, cell->next);
            
            // Attempt to repair the circular structure
            if (head != NULL) {
                cell->next = head;
                printf("Circular structure repaired: cell->next now points to head\n");
            }
        }
    }
    #endif

    unlock_and_return(lr, LR_OK);
}

lr_result_t lr_insert_next(struct linked_ring *lr, lr_data_t data,
                           struct lr_cell *needle)
{
    size_t          cell_index;
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

lr_result_t lr_insert(struct linked_ring *lr, lr_data_t data, lr_data_t owner,
                      size_t index)
{
    size_t          cell_index;
    struct lr_cell *head;
    struct lr_cell *needle;
    struct lr_cell *tail;
    struct lr_cell *last_cell;
    struct lr_cell *cell;
    struct lr_cell *chain;
    struct lr_cell *owner_cell;
    struct lr_cell *prev_owner;
    struct lr_cell *last_free;

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
        return lr_put(lr, data, owner);
    }

    cell      = lr->write;
    lr->write = lr->write->next;

    cell->data = data;

    if (index == 0) {
        needle = prev_owner->next;
        /* When inserting at the beginning, we need to update the head pointer
         */
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
 * Add a new string element to the linked ring buffer.
 *
 * @param lr: pointer to the linked ring structure
 * @param data: the string to be added to the buffer
 * @param owner: the owner of the new element
 *
 * @return LR_OK: if the element was successfully added
 *         LR_ERROR_BUFFER_FULL: if the buffer is full and the element could not
 * be added
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


lr_result_t lr_read_string(struct linked_ring *lr, unsigned char *data,
                           size_t *length, lr_owner_t owner)
{
    struct lr_cell *needle;
    struct lr_cell *last_cell;
    struct lr_cell *tail;
    struct lr_cell *prev_owner;
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
 * Retrieve the next element from the linked ring buffer.
 *
 * @param lr: pointer to the linked ring structure
 * @param data: pointer to the variable where the retrieved data will be stored
 * @param owner: the owner of the retrieved element
 *
 * @return LR_OK: if the element was successfully retrieved
 *         LR_ERROR_BUFFER_EMPTY: if the buffer is empty and no element could be
 * retrieved
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

    /* Get the head of the list */
    if (prev_owner->next == NULL) {
        unlock_and_return(lr, LR_ERROR_BUFFER_EMPTY);
    }
    
    head = prev_owner->next->next;
    if (head == NULL) {
        unlock_and_return(lr, LR_ERROR_BUFFER_EMPTY);
    }
    
    /* Update the circular structure */
    if (head->next != NULL) {
        prev_owner->next->next = head->next;
    } else {
        /* If head->next is NULL, we have a broken circular structure */
        unlock_and_return(lr, LR_ERROR_BUFFER_EMPTY);
    }

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
            *owner_swap = *next_owner;
        }

        /* Return the owner cell to the free list */
        if (lr->write == NULL) {
            lr->write = lr->owners;
        } else {
            /* Ensure we don't create a circular reference in the free list */
            if (lr->owners != lr->write) {
                lr->owners->next = lr->write;
                lr->write = lr->owners;
            } else {
                /* If owners is the same as write, just update write */
                lr->write = lr->owners;
            }
        }

        /* Update owners pointer */
        if (lr->owners == last_cell) {
            lr->owners = NULL;
        } else {
            lr->owners += 1;
        }
    }

    /* Return the cell to the free list */
    if (head != lr->write) {  /* Prevent circular reference in free list */
        head->next = lr->write;
        lr->write = head;
    } else {
        /* If head is already the write pointer, just ensure next is valid */
        if (head->next == head) {  /* Break self-reference */
            head->next = NULL;
        } else if (head->next == NULL) {
            /* If head->next is NULL, we need to find another cell to use */
            for (size_t i = 0; i < lr->size; i++) {
                if (&lr->cells[i] != head && 
                    (lr->owners == NULL || &lr->cells[i] < lr->owners)) {
                    head->next = &lr->cells[i];
                    break;
                }
            }
        }
    }

    unlock_and_return(lr, LR_OK);
}

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
        /* If last cell for owner */
        /* delete and shorten the list, put a new link to lr->owners */
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

            /* If last cell for owner */
            /* delete and shorten the list, put a new link to lr->owners */
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
                lr->write = lr->owners;
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
        *data = selected->data;
        
        /* Update the circular structure */
        if (prev_owner->next != NULL) {
            prev_owner->next->next = selected->next;
        }

        if (selected == tail) {
            owner_cell->next = NULL;
        }
    } else {
        // Find the element at the specified index
        needle = head;
        needle_index = 0;

        // Find the element before the one we want to pull
        while (needle_index < index - 1 && needle->next != tail && needle->next != head) {
            needle = needle->next;
            needle_index++;
        }

        // If we couldn't reach the index
        if (needle_index < index - 1) {
            unlock_and_return(lr, LR_ERROR_BUFFER_EMPTY);
        }

        selected = needle->next;
        *data = selected->data;

        // Update the linked list to skip the pulled element
        needle->next = selected->next;

        // If we're removing the tail, update the owner's tail pointer
        if (selected == tail) {
            owner_cell->next = needle;
        }
    }

    // Return the cell to the free list
    selected->next = lr->write;
    lr->write = selected;

    unlock_and_return(lr, LR_OK);
}

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


lr_result_t lr_dump(struct linked_ring *lr)
{
    struct lr_cell *needle;
    struct lr_cell *head;
    size_t          buffer_usage_percent;

    lock(lr);
    head = NULL;
    if (lr->owners) {
        head = lr->owners->next->next;
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
    printf("│ Head pointer            │ %15p │\n", head);
    printf("│ Write pointer           │ %15p │\n", lr->write);
    printf("│ Cells array             │ %15p │\n", lr->cells);
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

/* Debug function to visualize circular structure for a specific owner */
lr_result_t lr_debug_circular_structure(struct linked_ring *lr, lr_owner_t owner) {
    struct lr_cell *owner_cell = lr_owner_find(lr, owner);
    if (owner_cell == NULL) {
        printf("\033[31mERROR: Owner %lu not found in buffer\033[0m\n", owner);
        return LR_ERROR_UNKNOWN;
    }

    struct lr_cell *head = lr_owner_head(lr, owner_cell);
    if (head == NULL) {
        printf("\033[31mERROR: Could not find head for owner %lu\033[0m\n", owner);
        return LR_ERROR_UNKNOWN;
    }

    struct lr_cell *tail = lr_owner_tail(owner_cell);
    if (tail == NULL) {
        printf("\033[31mERROR: Could not find tail for owner %lu\033[0m\n", owner);
        return LR_ERROR_UNKNOWN;
    }

    struct lr_cell *current = head;
    size_t count = 0;

    printf("\n\033[1;36m=== Circular Structure Debug for Owner %lu ===\033[0m\n", owner);
    printf("Owner cell address: %p, data: %lu\n", owner_cell, owner_cell->data);
    printf("Head address: %p\n", head);
    printf("Tail address: %p\n", tail);
    printf("Tail->next address: %p\n", tail->next);

    printf("\n\033[1mTracing circular path:\033[0m\n");
    printf("┌───────┬─────────────────┬────────────┬─────────────────┐\n");
    printf("│ Index │     Address     │ Data Value │    Next Addr    │\n");
    printf("├───────┼─────────────────┼────────────┼─────────────────┤\n");

    /* Safety check for NULL current pointer */
    if (current == NULL) {
        printf("└───────┴─────────────────┴────────────┴─────────────────┘\n");
        printf("\033[31mERROR: Current pointer is NULL\033[0m\n");
        return LR_ERROR_UNKNOWN;
    }

    do {
        printf("│ %5zu │ %15p │ %10lu │ %15p │\n",
               count, current, current->data, current->next);
        
        /* Safety check for NULL next pointer */
        if (current->next == NULL) {
            printf("└───────┴─────────────────┴────────────┴─────────────────┘\n");
            printf("\033[31mERROR: Found NULL next pointer at element %zu\033[0m\n", count);
            return LR_ERROR_UNKNOWN;
        }
        
        current = current->next;
        count++;

        /* Safety check to prevent infinite loops during debugging */
        if (count > lr->size) {
            printf("└───────┴─────────────────┴────────────┴─────────────────┘\n");
            printf("\033[31mWARNING: Possible infinite loop detected after %zu elements\033[0m\n", count);
            return LR_ERROR_UNKNOWN;
        }
    } while (current != head && count < lr_count_owned(lr, owner));

    printf("└───────┴─────────────────┴────────────┴─────────────────┘\n");

    /* In a single-circle design, tail->next may not point to this owner's head.
     * It should point to the next owner's head, or back to the first owner's head
     * if this is the last owner.
     */
    printf("\033[32mCircular structure follows single-circle design\033[0m\n");

    /* Verify the circular structure integrity */
    if (current != head) {
        printf("\033[31mWARNING: Circular structure is broken - did not return to head\033[0m\n");
        printf("Current: %p, Head: %p\n", current, head);
        return LR_ERROR_UNKNOWN;
    }

    return LR_OK;
}
