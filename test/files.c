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

// Function declarations for test cases
lr_result_t test_file_open_nonexistent();
lr_result_t test_file_open_and_read();
lr_result_t test_file_split_operations();
lr_result_t test_file_write_operations();
lr_result_t test_file_rename_and_save();
lr_result_t run_all_tests();

int main()
{
    lr_result_t result = run_all_tests();
    return result == LR_OK ? 0 : 1;
}

// Run all test cases
lr_result_t run_all_tests()
{
    lr_result_t result;
    
    log_info("=== Testing file operations ===");
    
    result = test_file_open_nonexistent();
    if (result != LR_OK) return result;
    
    result = test_file_open_and_read();
    if (result != LR_OK) return result;
    
    result = test_file_split_operations();
    if (result != LR_OK) return result;
    
    result = test_file_write_operations();
    if (result != LR_OK) return result;
    
    result = test_file_rename_and_save();
    if (result != LR_OK) return result;
    
    log_info("=== All tests passed ===");
    return LR_OK;
}
// Test opening a nonexistent file
lr_result_t test_file_open_nonexistent()
{
    struct linked_ring buffer = {0};
    char *nofile_path = "nofile.txt";
    lr_result_t r;
    
    log_info("Testing opening nonexistent file");
    r = lr_file_open(&buffer, nofile_path, 0);
    test_assert(r == LR_ERROR_UNKNOWN,
                "Opening nonexistent file '%s' should return error", nofile_path);
    
    return LR_OK;
}

// Test opening and reading from a file
lr_result_t test_file_open_and_read()
{
    struct linked_ring buffer = {0};
    char *one_line_path = "one_line.txt";
    lr_result_t r;
    size_t lines_nr;
    char line[BUFSIZ];
    size_t length;
    
    log_info("Testing opening and reading from file");
    
    // Test file opening
    r = lr_file_open(&buffer, one_line_path, 0);
    test_assert(r == LR_OK, "File '%s' should be opened successfully", one_line_path);
    
    // Test line count
    lines_nr = lr_owners_count(&buffer) - 1;
    test_assert(lines_nr == 720, "File '%s' should have 720 lines, has %lu",
                one_line_path, lines_nr);
    
    // Test retrieving file path
    r = lr_file_path(&buffer, line, &length);
    test_assert(r == LR_OK && strcmp(one_line_path, line) == 0,
                "File path should be '%s', got '%s'", one_line_path, line);
    
    // Test reading first line
    r = lr_file_read_line(&buffer, 1, line, &length);
    test_assert(r == LR_OK, "Reading first line should succeed");
    test_assert(strcmp(line, "HELLO WORLD") == 0,
                "First line should be 'HELLO WORLD', got '%s'", line);
    
    // Test reading nonexistent line
    r = lr_file_read_line(&buffer, 2, line, &length);
    test_assert(r == LR_ERROR_BUFFER_EMPTY,
                "Reading nonexistent line should return empty buffer error");
    
    return LR_OK;
}

// Test line splitting operations
lr_result_t test_file_split_operations()
{
    struct linked_ring buffer = {0};
    char *one_line_path = "one_line.txt";
    lr_result_t r;
    char line[BUFSIZ];
    size_t length;
    
    log_info("Testing line splitting operations");
    
    // Open the file
    r = lr_file_open(&buffer, one_line_path, 0);
    test_assert(r == LR_OK, "File '%s' should be opened successfully", one_line_path);
    
    // Test splitting a line at different positions
    // Split at beginning (should create empty line)
    r = lr_file_split(&buffer, 1, 0);
    test_assert(r == LR_OK, "Splitting line at beginning should succeed");
    
    // Read the lines to verify split
    r = lr_file_read_line(&buffer, 1, line, &length);
    test_assert(r == LR_OK && length == 0, 
                "Reading first line after split should succeed and be empty");
    
    r = lr_file_read_line(&buffer, 2, line, &length);
    test_assert(r == LR_OK && strcmp(line, "HELLO WORLD") == 0,
                "Second line after split should be 'HELLO WORLD', got '%s'", line);
    
    // Split in the middle
    r = lr_file_split(&buffer, 2, 5);
    test_assert(r == LR_OK, "Splitting line in the middle should succeed");
    
    // Read the lines to verify split
    r = lr_file_read_line(&buffer, 2, line, &length);
    test_assert(r == LR_OK && strcmp(line, "HELLO") == 0,
                "Second line after middle split should be 'HELLO', got '%s'", line);
    
    r = lr_file_read_line(&buffer, 3, line, &length);
    test_assert(r == LR_OK && strcmp(line, " WORLD") == 0,
                "Third line after middle split should be ' WORLD', got '%s'", line);
    
    // Test edge case: splitting at end of line
    r = lr_file_split(&buffer, 3, strlen(" WORLD"));
    test_assert(r == LR_OK, "Splitting at end of line should succeed");
    
    // Test edge case: splitting an empty line
    r = lr_file_line_insert(&buffer, 5);
    test_assert(r == LR_OK, "Inserting empty line should succeed");
    r = lr_file_split(&buffer, 5, 0);
    test_assert(r == LR_OK, "Splitting an empty line should succeed");
    
    return LR_OK;
}

// Test writing operations
lr_result_t test_file_write_operations()
{
    struct linked_ring buffer = {0};
    char *one_line_path = "one_line.txt";
    lr_result_t r;
    char line[BUFSIZ];
    size_t length;
    
    log_info("Testing writing operations");
    
    // Open the file
    r = lr_file_open(&buffer, one_line_path, 0);
    test_assert(r == LR_OK, "File '%s' should be opened successfully", one_line_path);
    
    // Test writing a character
    r = lr_file_write(&buffer, 1, 0, '-');
    test_assert(r == LR_OK, "Writing a character should succeed");
    
    r = lr_file_read_line(&buffer, 1, line, &length);
    test_assert(r == LR_OK && strcmp(line, "-HELLO WORLD") == 0,
                "Line after writing character should be '-HELLO WORLD', got '%s'", line);
    
    // Test writing to a new line
    r = lr_file_write(&buffer, 2, 0, '+');
    test_assert(r == LR_OK, "Writing to a new line should succeed");
    
    r = lr_file_read_line(&buffer, 2, line, &length);
    test_assert(r == LR_OK && strcmp(line, "+") == 0,
                "New line after writing should be '+', got '%s'", line);
    
    // Test writing a string with newlines
    r = lr_file_write_string(&buffer, 2, 1, "TEST\nMULTI\nLINE");
    test_assert(r == LR_OK, "Writing a multi-line string should succeed");
    
    // Verify the lines
    r = lr_file_read_line(&buffer, 2, line, &length);
    test_assert(r == LR_OK && strcmp(line, "+TEST") == 0,
                "Line 2 after string write should be '+TEST', got '%s'", line);
    
    r = lr_file_read_line(&buffer, 3, line, &length);
    test_assert(r == LR_OK && strcmp(line, "MULTI") == 0,
                "Line 3 after string write should be 'MULTI', got '%s'", line);
    
    r = lr_file_read_line(&buffer, 4, line, &length);
    test_assert(r == LR_OK && strcmp(line, "LINE") == 0,
                "Line 4 after string write should be 'LINE', got '%s'", line);
    
    // Test edge case: writing an empty string
    r = lr_file_write_string(&buffer, 5, 0, "");
    test_assert(r == LR_OK, "Writing an empty string should succeed");
    
    // Test edge case: writing a string with multiple consecutive newlines
    r = lr_file_write_string(&buffer, 6, 0, "MULTIPLE\n\n\nNEWLINES");
    test_assert(r == LR_OK, "Writing string with multiple newlines should succeed");
    
    // Verify the lines
    r = lr_file_read_line(&buffer, 6, line, &length);
    test_assert(r == LR_OK && strcmp(line, "MULTIPLE") == 0,
                "Line 6 should be 'MULTIPLE', got '%s'", line);
    
    r = lr_file_read_line(&buffer, 9, line, &length);
    test_assert(r == LR_OK && strcmp(line, "NEWLINES") == 0,
                "Line 9 should be 'NEWLINES', got '%s'", line);
    
    // Test edge case: writing to a very high line number
    r = lr_file_write_string(&buffer, 100, 0, "HIGH LINE");
    test_assert(r == LR_OK, "Writing to a high line number should succeed");
    
    r = lr_file_read_line(&buffer, 100, line, &length);
    test_assert(r == LR_OK && strcmp(line, "HIGH LINE") == 0,
                "Line 100 should contain 'HIGH LINE', got '%s'", line);
    
    return LR_OK;
}

// Test renaming and saving
lr_result_t test_file_rename_and_save()
{
    struct linked_ring buffer = {0};
    char *one_line_path = "one_line.txt";
    char *new_name = "new_file.txt";
    lr_result_t r;
    char line[BUFSIZ];
    size_t length;
    
    log_info("Testing file renaming and saving");
    
    // Open the file
    r = lr_file_open(&buffer, one_line_path, 0);
    test_assert(r == LR_OK, "File '%s' should be opened successfully", one_line_path);
    
    // Make some changes
    r = lr_file_write_string(&buffer, 1, 0, "MODIFIED ");
    test_assert(r == LR_OK, "Modifying content should succeed");
    
    // Rename the file
    r = lr_file_rename(&buffer, new_name);
    test_assert(r == LR_OK, "Renaming file to '%s' should succeed", new_name);
    
    // Verify the path was updated
    r = lr_file_path(&buffer, line, &length);
    test_assert(r == LR_OK && strcmp(new_name, line) == 0,
                "File path should be updated to '%s', got '%s'", new_name, line);
    
    // Save the file
    r = lr_file_save(&buffer, new_name);
    test_assert(r == LR_OK, "Saving file as '%s' should succeed", new_name);
    
    // Reopen the saved file to verify contents
    struct linked_ring verify_buffer = {0};
    r = lr_file_open(&verify_buffer, new_name, 0);
    test_assert(r == LR_OK, "Reopening saved file '%s' should succeed", new_name);
    
    r = lr_file_read_line(&verify_buffer, 1, line, &length);
    test_assert(r == LR_OK && strncmp(line, "MODIFIED ", 9) == 0,
                "First line of saved file should start with 'MODIFIED ', got '%s'", line);
    
    // Clean up the test file
    remove(new_name);
    
    return LR_OK;
}
