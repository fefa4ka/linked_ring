# Linked Ring Buffer

> **The Ultimate Memory-Efficient Buffer Solution for Embedded Systems**

Linked Ring Buffer is a high-performance, thread-safe circular buffer implementation designed specifically for embedded systems and memory-constrained environments where every byte counts.

For a detailed explanation of the implementation, see [One Size Fits All: Linked Ring Instead of a Ring Buffer](https://blog.devgenius.io/one-size-fits-all-linked-ring-instead-of-a-ring-buffer-d1d69a12bf73).

[![MIT License](https://img.shields.io/badge/License-MIT-blue.svg)](https://opensource.org/licenses/MIT)

## 🚀 Key Features

- **Multi-Owner Architecture** - Manage multiple data streams in a single buffer
- **Thread-Safe Operations** - Built-in customizable mutex support
- **Zero-Copy Design** - Store data pointers directly for maximum efficiency
- **Deterministic Performance** - Constant-time operations for predictable behavior
- **Flexible Access Patterns** - Both FIFO and LIFO operations supported
- **Memory Efficient** - Fixed memory footprint with no dynamic allocation
- **Embedded-Ready** - No dependencies on standard library functions

## 💡 Why Choose Linked Ring Buffer?

### Perfect for Resource-Constrained Systems
Designed from the ground up for environments where memory is precious and performance is critical. Our buffer implementation ensures you get the most out of limited hardware.

### Ideal Use Cases
- **IoT Devices** - Manage sensor data efficiently
- **Real-Time Systems** - Deterministic performance for time-critical applications
- **Communication Stacks** - Handle multiple data streams with owner-based access
- **Embedded Applications** - Reliable operation with minimal resource usage

## 🔧 Quick Start

### Installation

```bash
mkdir build
cd build
cmake ..
make
```

### Basic Usage

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

// Print buffer contents
lr_dump(&buffer);
```

## 📚 API Overview

### Buffer Management
- **Initialize & Configure** - `lr_init()`, `lr_resize()`, `lr_set_mutex()`

### Data Operations
- **Queue-like (FIFO)** - `lr_put()`, `lr_get()`, `lr_read()`
- **Stack-like (LIFO)** - `lr_push()`, `lr_pop()`
- **Advanced Access** - `lr_insert()`, `lr_pull()`, `lr_read_at()`

### String Handling
- **Text Processing** - `lr_put_string()`, `lr_read_string()`

### Debugging Tools
- **Visualization** - `lr_dump()`, `lr_debug_structure_circular()`

## 🧪 Testing

```bash
cd build
make test
ctest
```

## 📋 Technical Considerations

While Linked Ring Buffer offers significant advantages, it's important to consider:

- Buffer size must be determined at initialization
- Optimized for head/tail operations rather than random access
- Owner tracking adds a small memory overhead

## 📄 License

MIT License - Free for commercial and personal use
