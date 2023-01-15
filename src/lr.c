#include "lr.h"
#include <stdio.h>


/* lock the mutex if lock function provided, no op otherwise */
#define lock(lr, owner) do { \
  if (lr->lock != NULL) { \
    enum lr_result ret = lr->lock(lr->mutex_state, owner); \
    if (ret != LR_OK) { \
        return ret; \
    } \
   } \
} while (0)

/* unlock the mutex if unlock function provided and then return ret  */
#define unlock_and_return(lr, owner, ret) do { \
  if (lr->unlock != NULL) { \
    return lr->unlock(lr->mutex_state, lr_owner(owner)); \
   } \
   return ret; \
} while (0)

/* additional overload for returning success */
#define unlock_and_succeed(lr, owner) unlock_and_return(lr, owner, LR_OK)


/**
 * This function counts the number of elements owned by the specified owner in the linked ring buffer.
 * If limit is specified, it will stop counting after reaching the limit.
 * 
 * @param lr: pointer to the linked ring structure
 * @param limit: maximum number of elements to count (0 for no limit)
 * @param owner: the owner of the elements to count (0 for all owners)
 * 
 * @return the number of elements owned by the specified owner (up to the limit, if specified)
 */
uint16_t lr_count_limited_owned(struct linked_ring *lr, uint16_t limit,
                                lr_owner_t owner)
{
    lock(lr, owner);
    uint8_t         length  = 0;
    struct lr_cell *counter = lr->read;
    struct lr_cell *needle  = lr->write ? lr->write : lr->read;

    /* return 0 if the buffer is empty */
    if (lr->read == 0)
    {
        unlock_and_succeed(lr, owner);
    }

    /* return 0 if the owner is not present in the buffer */
    if (owner && (lr->owners & owner) != owner)
    {
        unlock_and_succeed(lr, owner);
    }

    /* full buffer */
    if (!owner && lr->write == 0)
    {
        unlock_and_return(lr, owner, lr->size);
    }

    /* iterate through the buffer and count the elements owned by the specified
     * owner */
    do {
        if (!owner || counter->owner == owner)
            length++;
        if (counter->next)
            counter = counter->next;
        else
            break;
    } while (counter != needle || (limit && limit == length));

    unlock_and_return(lr, owner, length);
}


/**
 * This function initializes a new linked ring buffer.
 * 
 * @param lr: pointer to the linked ring structure to be initialized
 * @param size: size of the buffer, in number of elements
 * @param cells: pointer to the array of cells that will make up the buffer
 * 
 * @return LR_OK: if the initialization was successful
 *         LR_ERROR_NOMEMORY: if the cells parameter is NULL or size is 0
 */
lr_result_t lr_init(struct linked_ring *lr, unsigned int size,
                    struct lr_cell *cells)
{
    lr->cells = cells;
    if (lr->cells == 0 || size <= 0) {
        return LR_ERROR_NOMEMORY;
    }

    lr->size   = size;
    lr->owners = 0;
    lr->read = 0;

    /* Set the write position to the first cell in the buffer */
    lr->write = lr->cells;

    /* Use lr_set_mutex to initialize these fields */
    lr->lock = NULL;
    lr->unlock = NULL;
    lr->mutex_state = NULL;

    return LR_OK;
}

/**
 * This function sets the mutex for a linked ring buffer.
 * 
 * @param lr: pointer to the linked ring structure to be initialized
 * @param attr: mutex attributes
 */
void lr_set_mutex(struct linked_ring *lr, struct lr_mutex_attr *attr)
{
    lr->lock = attr->lock;
    lr->unlock = attr->unlock;
    lr->mutex_state = attr->state;
}

/**
 * This function adds a new element to the linked ring buffer.
 * 
 * @param lr: pointer to the linked ring structure
 * @param data: the data to be added to the buffer
 * @param owner: the owner of the new element
 * 
 * @return LR_OK: if the element was successfully added
 *         LR_ERROR_BUFFER_FULL: if the buffer is full and the element could not be added
 */
lr_result_t lr_put(struct linked_ring *lr, lr_data_t data, lr_owner_t owner)
{
    lock(lr, owner);
    /* Check if the buffer is full */
    if (!lr->write && lr->read)
    {
        unlock_and_return(lr, owner, LR_ERROR_BUFFER_FULL);
    }


    /* Allocate a cell for the data */
    struct lr_cell *cell = lr->write ? lr->write : lr->cells;
    cell->data           = data;
    cell->owner          = owner;

    /* Update the owner bitmask */
    lr->owners |= owner;

    /* If there is no next cell, create one */
    if (!cell->next) {
        if ((cell - lr->cells) < (lr->size - 1)) {
            cell->next = cell + 1;
        } else {
            cell->next = lr->read;
        }
    }

    /* If the next cell is the read position, set the write position to 0
     * that mean filled buffer */
    if (cell->next == lr->read) {
        lr->write = 0;
    } else {
        /* Otherwise, set the write position to the next cell */
        lr->write = cell->next;
    }

    /* If the read position is not set, set it to the current cell */
    if (!lr->read)
        lr->read = cell;

    unlock_and_succeed(lr, owner);
}

/**
 * This function adds a new string element to the linked ring buffer.
 * 
 * @param lr: pointer to the linked ring structure
 * @param data: the string to be added to the buffer
 * @param owner: the owner of the new element
 * 
 * @return LR_OK: if the element was successfully added
 *         LR_ERROR_BUFFER_FULL: if the buffer is full and the element could not be added
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

/**
 * This function retrieves the next element from the linked ring buffer.
 * 
 * @param lr: pointer to the linked ring structure
 * @param data: pointer to the variable where the retrieved data will be stored
 * @param owner: the owner of the retrieved element
 * 
 * @return LR_OK: if the element was successfully retrieved
 *         LR_ERROR_BUFFER_EMPTY: if the buffer is empty and no element could be retrieved
 */
lr_result_t lr_get(struct linked_ring *lr, lr_data_t *data, lr_owner_t owner)
{
    lock(lr, owner);
    struct lr_cell *readable_cell = lr->read;
    struct lr_cell *previous_cell = 0;
    struct lr_cell *freed_cell    = 0;
/* Set the needle to the write position if it exists, otherwise set it to the read position */
    struct lr_cell *needle        = lr->write ? lr->write : lr->read;

    if (owner && (lr->owners & owner) != owner)
    {
        unlock_and_return(lr, owner, LR_ERROR_BUFFER_EMPTY);
    }

    /* Flush owners, and set again during buffer reading */
    lr->owners = 0;

    do {
        struct lr_cell *next_cell = readable_cell->next;

        if (!owner || readable_cell->owner == owner) {
            *data = readable_cell->data;
            /* For skipping next match set impossible owner */
            owner = lr_invalid_owner;

            /* Reassembly linking ring */
            if (previous_cell) {
                /* Link cells between */
                if (readable_cell->next == needle) {
                    readable_cell->next = lr->write ? lr->write : lr->read;
                    lr->write           = readable_cell;
                    unlock_and_succeed(lr, owner);
                } else {
                    previous_cell->next = readable_cell->next;
                }
            } else {
                /* If readed last available cell */
                if (readable_cell->next == needle) {
                    lr->read  = 0;
                    readable_cell->next = lr->write ? lr->write : lr->read;
                    lr->write = readable_cell;
                    unlock_and_succeed(lr, owner);
                } else {
                    /* Once case when read pointer changing
                     * If readed first cell */
                    lr->read = readable_cell->next;
                }
            }

            freed_cell = readable_cell;
            if (freed_cell->next) {
                if (lr->write)
                    freed_cell->next = lr->write;
                else
                    freed_cell->next = lr->read;
            }

        } else {
            lr->owners |= readable_cell->owner;
        }

        /* Cell iteration */
        previous_cell = readable_cell;
        readable_cell = next_cell;
    } while (readable_cell != needle && readable_cell);

    if (owner != lr_invalid_owner) {
        unlock_and_return(lr, owner, LR_ERROR_BUFFER_EMPTY);
    } else {
        /* Last iteration. Link freed cell as next */
        previous_cell->next = freed_cell;
        freed_cell->next = lr->write ? lr->write : lr->read;

        lr->write = freed_cell;
    }

    unlock_and_succeed(lr, owner);
}

lr_result_t lr_dump(struct linked_ring *lr)
{
    struct lr_cell *needle = lr->write ? lr->write : lr->read;
    if (lr_count(lr) == 0)
    {
        return LR_ERROR_BUFFER_EMPTY;
    }

    printf("\nLinked ring buffer dump\n");
    printf("=======================\n");
    printf("read    : %p\n", lr->read);
    printf("write   : %p\n", lr->write);
    printf("cells   : %p\n", lr->cells);
    printf("capacity: %d\n", lr->size);
    printf("size    : %d\n", lr_count(lr));
    printf("owners  : 0x%X\n", lr->owners);
    printf("\n");

    if(!lr->read)
        return LR_OK;

    struct lr_cell *readable_cell = lr->read;
    do {
        struct lr_cell *next_cell = readable_cell->next;

        printf("%x: %x", readable_cell->owner, readable_cell->data);
        readable_cell = next_cell;
        if(readable_cell && readable_cell != needle)
            printf(" | ");
    } while (readable_cell && readable_cell != needle);

    printf("%x\n", lr->write);
}
