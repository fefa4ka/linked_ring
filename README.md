# Linked Ring Buffer

A high-performance, thread-safe circular buffer implementation in C designed for embedded systems and other memory-constrained environments.

For a detailed explanation of the implementation, see [One Size Fits All: Linked Ring Instead of a Ring Buffer](https://blog.devgenius.io/one-size-fits-all-linked-ring-instead-of-a-ring-buffer-d1d69a12bf73).

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

## Memory Efficiency

The Linked Ring Buffer provides significant memory savings compared to traditional ring buffers when dealing with multiple producers/consumers:

### Traditional Approach
With traditional ring buffers, each producer-consumer pair typically requires its own dedicated buffer:

```
N pairs × Buffer Size = Total Memory Required

Example: 5 pairs with 100-element buffers = 500 elements
```

### Linked Ring Approach
With the Linked Ring Buffer, a single buffer can be shared among all producers and consumers:

```
1 buffer + N owner cells = Total Memory Required

Example: 5 pairs with a 100-element shared buffer = 100 + 5 = 105 elements
```

### Memory Savings Calculation
```
Memory Savings = (N × Buffer Size) - (Buffer Size + N)
                = Buffer Size × (N - 1) - N
                
Example: With 5 pairs and 100-element buffers
         Savings = 100 × (5 - 1) - 5 = 395 elements (79% reduction)
```

The memory savings increase with:
1. More producer-consumer pairs
2. Larger buffer sizes
3. Uneven usage patterns (some producers are idle while others are active)

For embedded systems with limited memory, these savings can be crucial.

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
ctest
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

## API Reference

### Buffer Management
- `lr_init(lr, size, cells)` - Initialize a new buffer - O(n)
- `lr_resize(lr, size, cells)` - Resize an existing buffer - O(n)
- `lr_set_mutex(lr, attr)` - Set mutex for thread-safe operations - O(1)

### Data Operations (FIFO)
- `lr_put(lr, data, owner)` - Add data to head of buffer - O(1)
- `lr_get(lr, &data, owner)` - Get data from head of buffer - O(1)
- `lr_read(lr, &data, owner)` - Read data without removing it - O(1)

### Data Operations (LIFO)
- `lr_push(lr, data, owner)` - Add data to tail of buffer - O(1)
- `lr_pop(lr, &data, owner)` - Get data from tail of buffer - O(n)

### String Operations
- `lr_put_string(lr, data, owner)` - Add string to buffer - O(m) where m is string length
- `lr_read_string(lr, data, &length, owner)` - Read string from buffer - O(m) where m is string length

### Advanced Operations
- `lr_insert(lr, data, owner, index)` - Insert data at specific index - O(n)
- `lr_pull(lr, &data, owner, index)` - Remove data from specific index - O(n)

### Owner Management
- `lr_owner_find(lr, owner)` - Find owner cell - O(k) where k is number of owners
- `lr_count_owned(lr, owner)` - Count elements for an owner - O(n)
- `lr_count(lr)` - Count all elements in buffer - O(n)

### Debugging
- `lr_dump(lr)` - Print buffer contents - O(n)
- `lr_debug_structure_circular(lr, owner)` - Verify circular structure - O(n)
- `lr_debug_strucuture_cells(lr)` - Debug cell structure - O(n)
- `lr_debug_structure_relinked(lr)` - Debug relinked structure - O(n)

## License

This project is available under the MIT License.
