#pragma once
#include "lr.h"

// lr 
// owner == 0 -- filename
// owner > 0 -- line

lr_result_t lr_file_open(struct linked_ring *buffer, char *path, size_t size);

lr_result_t lr_file_rebuild(struct linked_ring *buffer); 
#define lr_file_path(buffer, path) lr_read_string(buffer, path, 0);
lr_result_t lr_file_rename(struct linked_ring *buffer, char *path);

lr_result_t lr_file_put(struct linked_ring *buffer, lr_data_t data);

lr_result_t lr_file_pull(struct linked_ring *buffer, size_t line_no, size_t index, lr_data_t *data);

lr_result_t lr_file_line_merge(struct linked_ring *buffer, size_t line_no, size_t merged_line_no); 
lr_result_t lr_file_line_insert(struct linked_ring *buffer, size_t line_no);

lr_result_t lr_file_read(struct linked_ring *buffer, size_t line_no, size_t index, lr_data_t *data);
lr_result_t lr_file_read_line(struct linked_ring *buffer, size_t line_no, char *data); 
lr_result_t lr_file_write(struct linked_ring *buffer, size_t line_no, size_t index, lr_data_t data);
lr_result_t lr_file_write_line(struct linked_ring *buffer, size_t line_no, char *data); 
lr_result_t lr_file_write_string(struct linked_ring *buffer, size_t line_no, size_t index, char *data); 

lr_result_t lr_file_save(struct linked_ring *buffer, char *path);
lr_result_t lr_file_close(struct linked_ring *buffer);
