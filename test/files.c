#include <lr.h>      // include header for Linked Ring library
#include <lr_file.h> // include header for Linked Ring library
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


#define log_print(type, message, ...)                                          \
    printf(type "\t" message "\n", ##__VA_ARGS__)
#define log_debug(type, message, ...)                                          \
    log_print(type, message " (%s:%d)\n", ##__VA_ARGS__, __FILE__, __LINE__)
#define log_verbose(message, ...) log_print("VERBOSE", message, ##__VA_ARGS__)
#define log_info(message, ...)    log_print("INFO", message, ##__VA_ARGS__)
#define log_ok(message, ...)      log_print("OK", message, ##__VA_ARGS__)
#define log_error(message, ...)                                                \
    log_print("\e[1m\e[31mERROR\e[39m\e[0m", message " (%s:%d)\n",             \
              ##__VA_ARGS__, __FILE__, __LINE__)

#define test_assert(test, message, ...)                                        \
    if (!(test)) {                                                             \
        log_error(message, ##__VA_ARGS__);                                     \
        return LR_ERROR_UNKNOWN;                                               \
    } else {                                                                   \
        log_ok(message, ##__VA_ARGS__);                                        \
    }

struct linked_ring buffer; // declare a buffer for the Linked Ring

int main()
{
    struct linked_ring buffer        = {0};
    char              *one_line_path = "one_line.txt";
    char              *nofile_path   = "nofile.txt";
    lr_result_t        r;
    size_t             lines_nr;
    char               line[BUFSIZ];


    r = lr_file_open(&buffer, nofile_path, 0);
    test_assert(r == LR_ERROR_UNKNOWN,
                "There are no file `%s` in the directory", nofile_path);

    r = lr_file_open(&buffer, one_line_path, 0);
    test_assert(r == LR_OK, "File `%s` should be opened", one_line_path);
    lines_nr = lr_owners_count(&buffer) - 1;
    test_assert(lines_nr == 1, "File `%s` should have 1 line, have %lu",
                one_line_path, lines_nr);

    // File path from buffer
    r = lr_file_path(&buffer, line);
    test_assert(r == LR_OK && strcmp(one_line_path, line) == 0,
                "File `%s` should save path `%s`", one_line_path, line);

    r = lr_file_read_line(&buffer, 1, line);
    test_assert(r == LR_OK,
                "From file `%s` should read first line, response %d",
                one_line_path, r);

    r = lr_file_read_line(&buffer, 2, line);
    test_assert(r == LR_ERROR_BUFFER_EMPTY,
                "From file `%s` should not read second line, response %d",
                one_line_path, r);

    test_assert(strcmp(line, "HELLO WORLD") == 0,
                "First line from file `%s` should be `Hello world`, have `%s`",
                one_line_path, line);

    // Don't work
    /*lr_file_line_insert(&buffer,          2);*/

    // Add new line

    r = lr_file_write_string(&buffer, 2, 0, "2\n7\n");
    r = lr_file_write_string(&buffer, 2,        1, "\n3\n4\n5\n6");
    lr_dump(&buffer);
    char *new_name = "new_file.txt";
    r              = lr_file_rename(&buffer, new_name);
    test_assert(r == LR_OK, "File `%s` should be renamed to `%s`",
                one_line_path, new_name);
    r = lr_file_save(&buffer, new_name);
    test_assert(r == LR_OK, "File `%s` should be saved", new_name);

    return LR_OK;


    //    // Add char to line
    //    r = lr_file_write(&buffer, 1, 0, '-');
    //    test_assert(r == LR_OK, "Write `-` to first line of file `%s`",
    //                one_line_path);
    //    r = lr_file_read_line(&buffer, 1, line);
    //
    //
    //    test_assert(r == LR_OK && strcmp(line, "-HELLO WORLD") == 0,
    //                "First line from file `%s` should be `-Hello world`, have
    //                `%s`", one_line_path, line);
    //
    //    // Add char to new line
    //    r = lr_file_write(&buffer, 2, 0, '-');
    //    test_assert(r == LR_OK, "Write `-` to second line of file `%s`",
    //                one_line_path);
    //    r = lr_file_read_line(&buffer, 2, line);
    //    test_assert(r == LR_OK && strcmp(line, "-") == 0,
    //                "Second line from file `%s` should be `-`, have `%s`",
    //                one_line_path, line);
    //
    //    // Add string to existing line
    //    r = lr_file_write_string(&buffer, 2, 1, "BYE BYE\nEOF");
    //
    //    test_assert(r == LR_OK, "Write `BYE BYE` to second line of file `%s`",
    //                "HELLO WORLD", one_line_path);
    //
    //    r = lr_file_write_string(&buffer, 5, 1, "REAL EOF \n");
    //    /*r = lr_file_write_string(&buffer, 6, 0, "SHIT\n");*/
    //    r = lr_file_write_string(&buffer, 7, 0, "EPTA");
    //    /*return LR_OK;*/
    //
    //    /*lr_data_t data;*/
    //    /*lr_file_pull(&buffer, 7,                      3, &data);*/
    //    /*printf("Data: %c\n", data);*/
    //    /*lr_file_pull(&buffer, 7,                      3, &data);*/
    //    /*printf("Data: %c\n", data);*/
    //    /*r = lr_file_write_string(&buffer, 7, 1, "EBALA\nE");*/
    //
    //    r = lr_file_write_string(&buffer, 5, 4, "SHIT\nOH");
    //    /*lr_file_write(&buffer, 6,0, 'O');*/
    //    /*lr_file_write(&buffer, 6,       1, 'H');*/
    //    /*r = lr_file_read_line(&buffer, 2, line);*/
    //    /*test_assert(r == LR_OK && strcmp(line, "BYE BYE") == 0,*/
    //    /*            "Second line from file `%s` should be `BYE BYE`, have
    //    `%s`",*/
    //    /*            one_line_path, line);*/
    //    /**/
    //
    //    // Rename file
    //    char *new_name = "new_file.txt";
    //    r              = lr_file_rename(&buffer, new_name);
    //    test_assert(r == LR_OK, "File `%s` should be renamed to `%s`",
    //                one_line_path, new_name);
    //    r = lr_file_save(&buffer, new_name);
    //    test_assert(r == LR_OK, "File `%s` should be saved", new_name);
    //
    //
    //    return LR_OK;
}
