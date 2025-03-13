# Linked Ring Buffer

A high-performance, thread-safe circular buffer implementation in C designed for embedded systems and other memory-constrained environments.

## Overview

The Linked Ring Buffer provides an efficient way to manage data in a circular buffer with multiple "owners" (data sources or consumers). It's particularly useful for:

- Embedded systems with limited memory
- Communication between different components or threads
- Buffering data streams with multiple producers and consumers

## Features

- Thread-safe operations with customizable mutex implementation
- Support for multiple data owners/streams in a single buffer
- Efficient memory usage with configurable buffer size
- FIFO (First-In-First-Out) data handling
- String operations for text processing

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

# Run a specific test
./test_basic
./test_files
./test_multi_thread
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

// Print buffer contents
lr_dump(&buffer);
```

## License

This project is available under the MIT License.
