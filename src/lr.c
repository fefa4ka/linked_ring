#include "lr.h"
#include <stdio.h>

uint16_t lr_count_limited_owned(struct linked_ring *lr, uint16_t limit,
                                lr_owner_t owner)
{
    uint8_t         length  = 0;
    struct lr_cell *counter = lr->read;
    struct lr_cell *needle  = lr->write ? lr->write : lr->read;

    /* Empty buffer */
    if (lr->read == 0)
        return 0;

    if (owner && (lr->owners & owner) != owner)
        return 0;

    /* Full buffer */
    if (!owner && lr->write == 0)
        return lr->size;

    /* Iterate from read to cell with *next = &lr.write */
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

lr_result_t lr_init(struct linked_ring *lr, unsigned int size,
                    struct lr_cell *cells)
{
    lr->cells = cells;
    if (lr->cells == 0 || size <= 0) {
        return LR_ERROR_NOMEMORY;
    }

    // Initialize the size and owners fields
    lr->size   = size;
    lr->owners = 0;

    // Initialize the read, write, and tail pointers
    lr->read  = 0;
    lr->write = lr->cells;

    return LR_OK;
}

lr_result_t lr_put(struct linked_ring *lr, lr_data_t data, lr_owner_t owner)
{
    struct lr_cell *recordable_cell;

    /* Check if the buffer is full */
    if (!lr->write && lr->read)
        return LR_ERROR_BUFFER_FULL;

    recordable_cell = lr->write;

    /* Set the data and owner fields of the recordable_cell element, and update
     * the owners field of the linked_ring structure */
    recordable_cell->data  = data;
    recordable_cell->owner = owner;

    lr->owners |= owner;

    /* If the next field of the recordable_cell element is not set, set it to
     * point to the next element in the buffer */
    if (!recordable_cell->next) {
        if ((recordable_cell - lr->cells) < (lr->size - 1)) {
            recordable_cell->next = recordable_cell + 1;
        }
    }

    if (recordable_cell->next == lr->read) {
        /* If the next field points to the read position, set the write pointer
         * to 0 to indicate that the buffer is full */
        lr->write = 0;
    } else
        /* If the next field points to any other element, set the write pointer
         * to point to that element */
        lr->write = recordable_cell->next;

    /* If the read pointer is not set, set it to point to the recordable_cell
     * element */
    if (!lr->read)
        lr->read = recordable_cell;

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
    /* O(n) = buffer_length */
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
                    lr->write           = readable_cell;
                    previous_cell->next = readable_cell;

                    return LR_OK;
                } else {
                    previous_cell->next = readable_cell->next;

                }
            } else {
                /* If readed last available cell */
                if (readable_cell->next == needle) {
                    lr->read  = 0;
                    lr->write = readable_cell;

                    return LR_OK;
                } else {
                    /* Once case when read pointer changing
                     * If readed first cell */
                    lr->read = readable_cell->next;
                    /* Link new read with last available write cell */
                }
            }

            freed_cell = readable_cell;
            if (freed_cell->next) {
                if (lr->write)
                    /* Add cell on top of the buffer */
                    freed_cell->next = lr->write;
                else
                    freed_cell->next = lr->read;
            }

        } else {
            /* All cells owners digest */
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

        lr->write = freed_cell;
    }

    return LR_OK;
}

lr_result_t lr_put_list(struct linked_ring *lr, unsigned char *data,
                        lr_owner_t owner)
{
    while (*data) {
        if (lr_put(lr, *(data++), owner) == LR_ERROR_BUFFER_FULL)
            return LR_ERROR_BUFFER_FULL;
    };

    return LR_OK;
}

void print_element_box(struct lr_cell *this, lr_data_t data, lr_owner_t owner,
                       struct lr_cell *next)
{
    // Calculate the length of the data and owner strings
    int data_len  = snprintf(NULL, 0, "0x%x", data);
    int owner_len = snprintf(NULL, 0, "0x%x", owner);

    // Calculate the maximum length of the strings
    int max_len = data_len > owner_len ? data_len : owner_len;

    // Print the top line of the box
    printf("0x%-*x\n", max_len, this);
    putchar('+');
    for (int i = 0; i < max_len + 4; i++) {
        putchar('-');
    }
    putchar('+');
    putchar('\n');

    // Print the data value
    printf("| D:%-*x |\n", max_len, data);

    // Print the owner value
    printf("| O:%-*x |\n", max_len, owner);

    // Print the bottom line of the box
    putchar('+');
    for (int i = 0; i < max_len + 4; i++) {
        putchar('-');
    }
    putchar('+');

    // Print the next value
    printf("\n0x%-*x\n", max_len, next);
    printf("|\nv\n");
}

lr_result_t lr_dump(struct linked_ring *lr)
{
    struct lr_cell *needle  = lr->write ? lr->write : lr->read;
    if (lr_count(lr) == 0)
        return LR_ERROR_BUFFER_EMPTY;

    printf("size=%d\n", lr_count(lr));
    struct lr_cell *readable_cell = lr->read;
    do {
        struct lr_cell *next_cell = readable_cell->next;

        // printf("%x: %x", readable_cell->owner, readable_cell->data);
        print_element_box(readable_cell, readable_cell->data,
                          readable_cell->owner, readable_cell->next);
        readable_cell = next_cell;
    } while (readable_cell && readable_cell != needle);

    printf("%x\n", lr->write);

    return LR_OK;
}
