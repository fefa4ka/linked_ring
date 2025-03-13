#include "lr_file.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

lr_result_t lr_file_open(struct linked_ring *buffer, char *path, size_t size)
{
    FILE           *file;
    size_t          buffer_size;
    size_t          line_no;
    size_t          line_no_readed;
    char           *symbol;
    char            line_buffer[BUFSIZ];
    struct lr_cell *cells;
    struct lr_cell *owner_cell;

    file = fopen(path, "r");
    if (file == NULL) {
        fprintf(stderr, "Error opening file %s\n", path);
        return LR_ERROR_UNKNOWN;
    }
    if (size) {
        buffer_size = size;

    } else {
        fseek(file, 0, SEEK_END);  // seek to end of file
        buffer_size = ftell(file); // get current file pointer
        fseek(file, 0, SEEK_SET);  // seek back to beginning of file
    }

    buffer_size += strlen(path);
    // TODO: coeff dependent on size
    buffer_size = buffer_size * 4;

    cells              = malloc(sizeof(struct lr_cell) * buffer_size);
    lr_result_t result = lr_init(buffer, buffer_size, cells);

    // Read filename
    symbol = path;
    while (*symbol) {
        lr_put(buffer, *(symbol++), 0);
    }

    line_no = 1;
    while (fgets(line_buffer, sizeof(line_buffer), file) != NULL) {
        // TODO:
        int len = strlen(line_buffer);
        if (len == sizeof(line_buffer)) {
            printf("Line bigger that buffer, fixme\n");
            exit(0);
        }
        // WARNING: replacing newline
        if (len > 1) {
            line_buffer[len - 1] = '\0';
            lr_put_string(buffer, line_buffer, lr_owner(line_no++));
            line_no_readed = line_no;

        } else {
            line_no++;
        }
    }


    /*if (--line_no != line_no_readed) {*/
    /*    //       lr_file_line_insert(buffer, line_no);*/
    /*    lr_put(buffer, '\n', lr_owner(line_no - 1));*/
    /*}*/

    fclose(file);

    return LR_OK;
}

lr_result_t lr_file_rebuild(struct linked_ring *buffer) { return LR_OK; }

lr_result_t lr_file_split(struct linked_ring *buffer, size_t line_no,
                          size_t index)
{
    lr_data_t       data;
    size_t          line_to_index = 0;
    struct lr_cell *owner_next_line
        = lr_owner_find(buffer, lr_owner(line_no + 1));

    /*printf("Splitting line %lu at index %lu\n", line_no, index);*/
    struct lr_cell *owner_cell;
    for (owner_cell = lr_last_cell(buffer); owner_cell >= buffer->owners;
         owner_cell--) {
        if (owner_cell->data > line_no
            || (owner_cell->data == line_no && index == 0)) {
            /*printf("Line %lu -> %lu\n", owner_cell->data, owner_cell->data + 1);*/
            owner_cell->data++;
        }
    }

    if (index == 0) {
        return LR_OK;
    }

    while (lr_file_pull(buffer, line_no, index, &data) == LR_OK) {
        /*printf("Move from %lu.%lu -> %lu.%lu = %c\n", line_no, index,*/
        /*       line_no + 1, line_to_index, data);*/
        lr_insert(buffer, data, lr_owner(line_no + 1), line_to_index++);
    }

    return LR_OK;
}

lr_result_t lr_file_line_merge(struct linked_ring *buffer, size_t line_no,
                               size_t merged_line_no)
{
    char           *symbol;
    lr_data_t       data;
    struct lr_cell *needle;
    needle = lr_owner_find(buffer, lr_owner(line_no));

    if (!line_no) {
        return LR_ERROR_BUFFER_EMPTY;
    }

    if (!needle) {
        lr_file_line_insert(buffer, line_no);
        needle = lr_owner_find(buffer, lr_owner(line_no));
    }

    while (lr_get(buffer, &data, lr_owner(merged_line_no)) == LR_OK) {
        lr_put(buffer, data, lr_owner(line_no));
    }

    for (needle = --needle; needle >= buffer->owners; needle--) {
        if (needle->data) {
            // 0 -- filename
            needle->data = lr_owner((size_t)needle->data - 1);
        }
    }


    return LR_OK;
}

lr_result_t lr_file_line_insert(struct linked_ring *buffer, size_t line_no)
{
    struct lr_cell *line           = NULL;
    struct lr_cell *new_line       = NULL;
    bool            is_line_exists = false;
    new_line                       = lr_owner_allocate(buffer);

    if (!new_line) {
        printf(" FIXME ERROR FILE LINE ALLOCATION\n");
        return LR_ERROR_UNKNOWN;
    }

    for (line = lr_last_cell(buffer); line > buffer->owners; --line) {
        if ((size_t)line->data >= line_no) {
            break;
        }
    }

    if ((size_t)line->data == 0) {
        line = line - 1;
    }


    if ((size_t)line->data == line_no) {
        is_line_exists = true;
    }


    struct lr_cell *needle = NULL;
    for (needle = new_line; needle < line; needle++) {
        *needle = *(needle + 1);
        if (needle->data && is_line_exists) {
            // 0 -- filename
            needle->data = lr_owner((size_t)needle->data + 1);
        }
    }


    line->data = lr_owner(line_no);
    line->next = NULL;

    buffer->owners = new_line;

    return LR_OK;
}

lr_result_t lr_file_line_pull(struct linked_ring *buffer, size_t line_no,
                              char *data)
{
    return LR_OK;
}

lr_result_t lr_file_rename(struct linked_ring *buffer, char *path)
{
    char     *symbol;
    lr_data_t data;

    while (lr_get(buffer, &data, 0) == LR_OK)
        ;
    symbol = path;
    while (*symbol) {
        lr_put(buffer, *(symbol++), 0);
    }

    return LR_OK;
}


lr_result_t lr_file_put(struct linked_ring *buffer, lr_data_t data)
{
    lr_put(buffer, data, (buffer->owners + 1)->data);
    return LR_OK;
}

lr_result_t lr_file_pull(struct linked_ring *buffer, size_t line_no,
                         size_t index, lr_data_t *data)
{

    return lr_pull(buffer, data, lr_owner(line_no), index);
}

lr_result_t lr_file_read(struct linked_ring *buffer, size_t line_no,
                         size_t index, lr_data_t *data)
{
    return LR_OK;
}

lr_result_t lr_file_read_line(struct linked_ring *buffer, size_t line_no,
                              char *data, size_t *length)
{
    return lr_read_string(buffer, data, length, lr_owner(line_no));
}

lr_result_t lr_file_write(struct linked_ring *buffer, size_t line_no,
                          size_t index, lr_data_t data)
{

    struct lr_cell *line;
    struct lr_cell *tail;
    line = lr_owner_find(buffer, lr_owner(line_no));

    if (!line) {
        lr_file_line_insert(buffer, line_no);
    }

    if (line && line->next->data == 0) {
        line->next->data = data;
        return LR_OK;
    }

    lr_insert(buffer, (char)data, lr_owner(line_no), index);

    return LR_OK;
}
lr_result_t lr_file_write_string(struct linked_ring *buffer, size_t line_no,
                                 size_t index, char *data)
{
    /*printf("\nline_no= %lu\n", line_no);*/
    while (*data) {
        if (*data == '\n') {
            /*printf("Before split: Owner of line %lu is %p\n", line_no,*/
            /*       lr_owner(line_no));*/
            // Ensure new lines are properly split and accounted for
            lr_file_split(buffer, line_no, index);

            line_no++; // Move to the next line
            index = 0;
        } else {
            lr_file_write(buffer, line_no, index++, (lr_data_t)*data);
        }
        data++;
    }

    return LR_OK;
}

lr_result_t lr_file_write_line(struct linked_ring *buffer, size_t line_no,
                               char *data)
{
    size_t index;
    index = 0;
    while (*data) {
        if (*data == '\n') {
            line_no += 1;
            index = 0;
        } else {
            lr_file_write(buffer, line_no, index++, *(data++));
        }
    }

    return LR_OK;
}

static int compare_lines(const void *a, const void *b)
{
    const size_t *line_a = (const size_t *)a;
    const size_t *line_b = (const size_t *)b;

    return (*line_a - *line_b);
}

lr_result_t lr_file_save(struct linked_ring *buffer, char *path)
{
    FILE           *file;
    char            line_buffer[BUFSIZ];
    size_t          lines_nr;
    struct lr_cell *line;
    size_t         *lines;

    file = fopen(path, "w");
    if (file == NULL) {
        fprintf(stderr, "Error opening file %s\n", path);
        return LR_ERROR_UNKNOWN;
    }

    lines_nr = lr_owners_count(buffer) - 1;
    lines    = malloc(sizeof(size_t) * lines_nr);

    lines_nr = 0;
    for (struct lr_cell *line = lr_last_cell(buffer); line >= buffer->owners;
         line--) {
        size_t line_nr = (size_t)line->data;
        if (line_nr > 0) {
            lines[lines_nr++] = line_nr;
        }
    }
    qsort(lines, lines_nr, sizeof(size_t), compare_lines);

    for (size_t entry = 0; entry < lines_nr; entry++) {
        size_t line_current = lines[entry];

        if (line_current > 1) {
            size_t prev_line = lines[entry - 1];
            size_t from, to;
            from = prev_line;
            to   = line_current;
            if (from && to - from > 1)
                for (size_t index = 0; index < to - from - 1; index++) {
                    fprintf(file, "\n");
                }
        }


        if (line_current > 0) {
            size_t length;
            lr_read_string(buffer, line_buffer, &length, lr_owner(line_current));
            fprintf(file, "%s\n", line_buffer);
        }
    }


    fclose(file);

    return LR_OK;
}
