#pragma once
#include "lr.h"

/**
 * File handling extension for the Linked Ring buffer library
 * 
 * This module provides file operations using the linked ring buffer structure.
 * In this implementation:
 * - owner == 0 is reserved for storing the filename
 * - owner > 0 represents line numbers in the file (1-based indexing)
 */

/**
 * Opens a file and loads its contents into the linked ring buffer.
 *
 * @param buffer: pointer to the linked ring buffer to store the file contents
 * @param path: path to the file to be opened
 * @param size: size of the buffer to allocate (0 for automatic sizing based on file size)
 *
 * @return LR_OK if successful, error code otherwise
 */
lr_result_t lr_file_open(struct linked_ring *buffer, char *path, size_t size);

/**
 * Rebuilds the internal structure of the file buffer after modifications.
 *
 * @param buffer: pointer to the linked ring buffer
 *
 * @return LR_OK if successful, error code otherwise
 */
lr_result_t lr_file_rebuild(struct linked_ring *buffer); 

/**
 * Retrieves the file path stored in the buffer.
 * The path is stored with owner ID 0.
 */
#define lr_file_path(buffer, path, length) lr_read_string(buffer, path, length, 0);

/**
 * Renames the file associated with the buffer.
 *
 * @param buffer: pointer to the linked ring buffer
 * @param path: new path/name for the file
 *
 * @return LR_OK if successful, error code otherwise
 */
lr_result_t lr_file_rename(struct linked_ring *buffer, char *path);

/**
 * Adds a single character to the current line in the buffer.
 *
 * @param buffer: pointer to the linked ring buffer
 * @param data: character to add
 *
 * @return LR_OK if successful, error code otherwise
 */
lr_result_t lr_file_put(struct linked_ring *buffer, lr_data_t data);

/**
 * Retrieves and removes a character from a specific position in a line.
 *
 * @param buffer: pointer to the linked ring buffer
 * @param line_no: line number (1-based)
 * @param index: character position within the line (0-based)
 * @param data: pointer to store the retrieved character
 *
 * @return LR_OK if successful, error code otherwise
 */
lr_result_t lr_file_pull(struct linked_ring *buffer, size_t line_no, size_t index, lr_data_t *data);

/**
 * Merges two lines in the buffer.
 *
 * @param buffer: pointer to the linked ring buffer
 * @param line_no: destination line number
 * @param merged_line_no: source line number to merge into destination
 *
 * @return LR_OK if successful, error code otherwise
 */
lr_result_t lr_file_line_merge(struct linked_ring *buffer, size_t line_no, size_t merged_line_no); 

/**
 * Splits a line at the specified position, creating a new line.
 *
 * This function divides a line into two parts at the given index position.
 * All characters from the index position to the end of the line are moved
 * to a new line that is inserted after the current line.
 *
 * @param buffer: pointer to the linked ring buffer
 * @param line_no: line number to split (1-based)
 * @param index: character position within the line where to split (0-based)
 *
 * @return LR_OK if successful, error code otherwise
 */
lr_result_t lr_file_split(struct linked_ring *buffer, size_t line_no,
                          size_t index);

/**
 * Inserts a new empty line at the specified position.
 *
 * @param buffer: pointer to the linked ring buffer
 * @param line_no: line number where to insert the new line
 *
 * @return LR_OK if successful, error code otherwise
 */
lr_result_t lr_file_line_insert(struct linked_ring *buffer, size_t line_no);

/**
 * Reads a character from a specific position in a line without removing it.
 *
 * @param buffer: pointer to the linked ring buffer
 * @param line_no: line number (1-based)
 * @param index: character position within the line (0-based)
 * @param data: pointer to store the read character
 *
 * @return LR_OK if successful, error code otherwise
 */
lr_result_t lr_file_read(struct linked_ring *buffer, size_t line_no, size_t index, lr_data_t *data);

/**
 * Reads an entire line from the buffer.
 *
 * @param buffer: pointer to the linked ring buffer
 * @param line_no: line number to read (1-based)
 * @param data: buffer to store the line content
 * @param length: pointer to store the length of the read line
 *
 * @return LR_OK if successful, error code otherwise
 */
lr_result_t lr_file_read_line(struct linked_ring *buffer, size_t line_no, char *data, size_t *length); 

/**
 * Writes a single character to a specific position in a line.
 *
 * @param buffer: pointer to the linked ring buffer
 * @param line_no: line number (1-based)
 * @param index: character position within the line (0-based)
 * @param data: character to write
 *
 * @return LR_OK if successful, error code otherwise
 */
lr_result_t lr_file_write(struct linked_ring *buffer, size_t line_no, size_t index, lr_data_t data);

/**
 * Writes a string as a complete line.
 *
 * @param buffer: pointer to the linked ring buffer
 * @param line_no: line number to write to (1-based)
 * @param data: string to write
 *
 * @return LR_OK if successful, error code otherwise
 */
lr_result_t lr_file_write_line(struct linked_ring *buffer, size_t line_no, char *data); 

/**
 * Writes a string to a specific position in a line.
 * Handles newline characters by splitting into multiple lines.
 *
 * @param buffer: pointer to the linked ring buffer
 * @param line_no: line number to start writing at (1-based)
 * @param index: character position within the line to start writing (0-based)
 * @param data: string to write
 *
 * @return LR_OK if successful, error code otherwise
 */
lr_result_t lr_file_write_string(struct linked_ring *buffer, size_t line_no, size_t index, char *data); 

/**
 * Saves the buffer contents to a file.
 *
 * @param buffer: pointer to the linked ring buffer
 * @param path: path where to save the file
 *
 * @return LR_OK if successful, error code otherwise
 */
lr_result_t lr_file_save(struct linked_ring *buffer, char *path);

/**
 * Closes the file and frees associated resources.
 *
 * @param buffer: pointer to the linked ring buffer
 *
 * @return LR_OK if successful, error code otherwise
 */
lr_result_t lr_file_close(struct linked_ring *buffer);
