# Linked Ring Buffer

A high-performance, thread-safe circular buffer implementation in C designed for embedded systems and other memory-constrained environments.

## Overview

The Linked Ring Buffer provides an efficient way to manage data in a circular buffer with multiple "owners" (data sources or consumers). It's particularly useful for:

- Embedded systems with limited memory
- Communication between different components or threads
- Buffering data streams with multiple producers and consumers
- Real-time data processing applications

## Features

- Thread-safe operations with customizable mutex implementation
- Support for multiple data owners/streams in a single buffer
- Efficient memory usage with configurable buffer size
- FIFO (First-In-First-Out) data handling with `lr_put()` and `lr_get()`
- LIFO (Last-In-First-Out) data handling with `lr_push()` and `lr_pop()`
- String operations for text processing
- Buffer resizing capability
- Comprehensive debugging tools
- Circular structure verification

## Building

The project uses CMake for building:

```bash
mkdir build
cd build
cmake ..
make
```

## Testing

The project includes several test suites to verify functionality:

```bash
# Build and run all tests
cd build
make test

# Run specific tests
./test/basic
./test/circular_test
./test/push_pop_test
./test/multi_thread
./test/edge_cases
./test/resize_test
```

## Basic Usage

```c
#include <lr.h>

// Initialize a buffer
struct lr_cell cells[10];
struct linked_ring buffer;
lr_init(&buffer, 10, cells);

// Add data to the buffer (owner 1)
lr_put(&buffer, 42, 1);
lr_put(&buffer, 43, 1);

// Add data to the buffer (owner 2)
lr_put(&buffer, 100, 2);

// Retrieve data for a specific owner
lr_data_t data;
lr_get(&buffer, &data, 1);  // Gets 42 from owner 1

// Add a string to the buffer
lr_put_string(&buffer, (unsigned char*)"Hello", 1);

// Use stack-like operations
lr_push(&buffer, 99, 3);  // Add to tail
lr_pop(&buffer, &data, 3);  // Get from tail (99)

// Print buffer contents
lr_dump(&buffer);

// Debug circular structure
lr_debug_structure_circular(&buffer, 1);
```

## Thread Safety

For thread-safe operation, provide a mutex implementation:

```c
// Define mutex functions
lr_result_t my_lock(void *state) {
    pthread_mutex_t *mutex = (pthread_mutex_t *)state;
    return pthread_mutex_lock(mutex) == 0 ? LR_OK : LR_ERROR_LOCK;
}

lr_result_t my_unlock(void *state) {
    pthread_mutex_t *mutex = (pthread_mutex_t *)state;
    return pthread_mutex_unlock(mutex) == 0 ? LR_OK : LR_ERROR_UNLOCK;
}

// Set up mutex
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
struct lr_mutex_attr mutex_attr = {
    .state = &mutex,
    .lock = my_lock,
    .unlock = my_unlock
};

// Apply mutex to buffer
lr_set_mutex(&buffer, &mutex_attr);
```

## License

This project is available under the MIT License.
