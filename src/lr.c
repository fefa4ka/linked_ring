#include "lr.h"
#include <stdio.h>

uint16_t lr_count_limited_owned(struct linked_ring *lr, uint16_t limit,
                                lr_owner_t owner)
{
    uint8_t         length  = 0;
    struct lr_cell *counter = lr->read;
    struct lr_cell *needle  = lr->write ? lr->write : lr->read;

    /* Return 0 if the buffer is empty */
    if (lr->read == 0)
        return 0;

    /* Return 0 if the owner is not present in the buffer */
    if (owner && (lr->owners & owner) != owner)
        return 0;

    /* Full buffer */
    if (!owner && lr->write == 0)
        return lr->size;

    /* Iterate through the buffer and count the elements owned by the specified
     * owner */
    do {
        if (!owner || counter->owner == owner)
            length++;
        if (counter->next)
            counter = counter->next;
        else
            break;
    } while (counter != needle || (limit && limit == length));

    return length;
}

/* This function initializes a new linked ring buffer.
 * Parameters:
 *     lr: pointer to the linked ring structure to be initialized
 *     size: size of the buffer, in number of elements
 *     cells: pointer to the array of cells that will make up the buffer
 * Returns:
 *     LR_OK: if the initialization was successful
 *     LR_ERROR_NOMEMORY: if the cells parameter is NULL or size is 0
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

    // Set the write position to the first cell in the buffer
    lr->write = lr->cells;

    return LR_OK;
}

lr_result_t lr_put(struct linked_ring *lr, lr_data_t data, lr_owner_t owner)
{
    /* Check if the buffer is full */
    if (!lr->write && lr->read)
        return LR_ERROR_BUFFER_FULL;

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

    return LR_OK;
}

lr_result_t lr_put_string(struct linked_ring *lr, unsigned char *data,
                           lr_owner_t owner)
{
    while (*data) {
        if (lr_put(lr, *(data++), owner) == LR_ERROR_BUFFER_FULL)
            return LR_ERROR_BUFFER_FULL;
    };

    return LR_OK;
}

lr_result_t lr_get(struct linked_ring *lr, lr_data_t *data, lr_owner_t owner)
{
    struct lr_cell *readable_cell = lr->read;
    struct lr_cell *previous_cell = 0;
    struct lr_cell *freed_cell    = 0;
    struct lr_cell *needle        = lr->write ? lr->write : lr->read;

    if (lr_count_owned(lr, owner) == 0)
        return LR_ERROR_BUFFER_EMPTY;

    /* Flush owners, and set again during buffer reading */
    lr->owners = 0;

    do {
        struct lr_cell *next_cell = readable_cell->next;

        if (!owner || readable_cell->owner == owner) {
            *data = readable_cell->data;
            /* For skipping next match set impossible owner */
            // TODO: max lr_owner_t
            owner = 0xFFFF;

            /* Reassembly linking ring */
            if (previous_cell) {
                /* Link cells between */
                if (readable_cell->next == needle) {
                    readable_cell->next = lr->write ? lr->write : lr->read;
                    lr->write           = readable_cell;

                    return LR_OK;
                } else {
                    previous_cell->next = readable_cell->next;
                }
            } else {
                /* If readed last available cell */
                if (readable_cell->next == needle) {
                    lr->read  = 0;
                    readable_cell->next = lr->write ? lr->write : lr->read;
                    lr->write = readable_cell;

                    return LR_OK;
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

    if (owner != 0xFFFF) {
        return LR_ERROR_BUFFER_EMPTY;
    } else {
        /* Last iteration. Link freed cell as next */
        previous_cell->next = freed_cell;
        freed_cell->next = lr->write ? lr->write : lr->read;

        lr->write = freed_cell;
    }

    return LR_OK;
}

lr_result_t lr_dump(struct linked_ring *lr)
{
    struct lr_cell *needle = lr->write ? lr->write : lr->read;
    if (lr_count(lr) == 0)
        return LR_ERROR_BUFFER_EMPTY;

    printf("\nLinked ring buffer dump\n");
    printf("=======================\n");
    printf("read    : %p\n", lr->read);
    printf("write   : %p\n", lr->write);
    printf("cells   : %p\n", lr->cells);
    printf("capacity: %d\n", lr->size);
    printf("size    : %d\n", lr_count(lr));
    printf("owners  : 0x%X\n", lr->owners);
    printf("\n");

    struct lr_cell *readable_cell = lr->read;
    do {
        struct lr_cell *next_cell = readable_cell->next;

        printf("%x: %x", readable_cell->owner, readable_cell->data);
        readable_cell = next_cell;
        if(readable_cell && readable_cell != needle)
            printf(" | ");
    } while (readable_cell && readable_cell != needle);

    printf("%x\n", lr->write);

    return LR_OK;
}
