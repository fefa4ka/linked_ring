# Linked Ring Buffer Library

This is a C library for implementing Linked Ring data structures, which provide a mechanism for allocating a single buffer for multiple consumers. Each consumer is associated with an owner (can be either a pointer or an enumerator value) and can access elements in the buffer that are associated with its owner.

Linked ring is a type of fixed-size buffer that stores data in a circular fashion. Linked ring buffers are useful for implementing queues, with elements being added to the end of the buffer and removed from the front.

The library provides functions for initializing and manipulating the Linked Ring structure, as well as functions for creating and manipulating elements in the buffer. It also provides convenience macros and typedefs for easy integration into existing code

## Use Cases

The Linked Ring data structure can be used in a variety of scenarios, such as:

-   Statically allocating a single buffer for multiple consumers
-   Allocating buffers for multiple tasks in an embedded system
-   Allocating a shared buffer between multiple threads
-   Storing data in a circular buffer without having to worry about buffer overflowo

The library uses a struct called `struct linked_ring` to represent a linked ring buffer. This struct contains pointers to the cells that make up the buffer, as well as information about the size of the buffer, the current read and write positions, and the owners of the elements in the buffer. The library also defines a struct called `struct lr_cell` to represent an element in the buffer, which consists of a data field and an owner field, as well as a pointer to the next element in the buffer.

The library provides a number of functions for initializing and manipulating linked ring buffers, such as `lr_init()`, which initializes a new linked ring buffer, `lr_put()`, which adds an element to the end of the buffer, and `lr_get()`, which removes an element from the front of the buffer. It also provides utility functions such as ` lr_count()`, which returns the number of elements in the buffer, and `lr_exists()`, which checks whether an element with a specific owner is present in the buffer.

### Performance

The performance of the Linked Ring data structure depends on the size of the buffer, and the number of elements stored in it. The larger the buffer, the more time it will take to add and remove elements from it. Adding and removing elements from a linked ring buffer is an O(1) operation, meaning that it has constant time performance regardless of the size of the buffer.

### Memory Consumption

The Linked Ring data structure requires a fixed amount of memory to store the buffer, which is determined by the size parameter passed to the `lr_init()` function. This size parameter determines the maximum number of elements that can be stored in the buffer.

The size of each element in the buffer is determined by the type of data stored in the `lr_data_t` and `lr_owner_t` fields. If a 64-bit system is used, the element will consume 8 bytes of memory, or 4 bytes in case of 32-bit system.

The total amount of memory consumed by the Linked Ring buffer and structure can be calculated as follows:

```
Memory Consumption = (size * (sizeof(lr_data_t) + sizeof(lr_owner_t) + sizeof(struct lr_cell))) + sizeof(struct linked_ring)
```

### Circular Buffer

Circular buffers and linked rings are both types of fixed-size buffers that are useful for storing and accessing data in a FIFO (first-in, first-out) manner. In a circular buffer, the data is stored in an array and the read and write pointers "wrap around" to the beginning of the array when they reach the end, so that the buffer can be used as a queue indefinitely. In a linked ring, the data is stored in a series of linked cells that form a circular chain, and the read and write pointers point to the cells that are currently being read from or written to.

There are a few key differences between circular buffers and linked rings that may influence when you would choose to use one or the other:

-   Memory usage: A circular buffer requires a contiguous block of memory to store the data, whereas a linked ring can store the data in non-contiguous cells that are linked together. This means that a linked ring may be more suitable if you need to allocate a buffer in situations where you cannot guarantee that you will have a contiguous block of memory available.
-   Data access: In a circular buffer, data can be accessed randomly, but accessing data in a linked ring requires following the pointers from one cell to the next. This means that a circular buffer may be more efficient if you need to access data randomly within the buffer.
-   Insertion and deletion: Both circular buffers and linked rings support insertion and deletion of data at the front and end of the buffer, but linked rings may be more efficient at inserting and deleting data in the middle of the buffer, since this does not require shifting the data around in memory as it does in a circular buffer.

The performance of Linked Ring Buffers is due to the fact that the buffer is only initialized once and all consumers can access the same buffer. This reduces the overhead of initializing and allocating multiple buffers, which is required with Circular Buffers.

The performance of Linked Ring Buffers also depends on the size of the buffer and the number of consumers. In general, the larger the buffer and the more consumers accessing it, the more performant the Linked Ring Buffer will be.

#### Memory Consumption

Linked Ring Buffers use more memory than Circular Buffers. The amount of memory saved by using Linked Ring depends on how many consumers can use the buffer.

In `N` multiple consumer case we allocate just one linked ring buffer and N circular buffers of BUFFER_SIZE size.

Linked ring buffer size in this case is equal to `BUFFER_SIZE*(sizeof(lr_data_t) + sizeof(lr_owner_t) + sizeof(struct lr_cell)) + sizeof(struct linked_ring)`

Therefore, memory consumption of Linked Ring Buffer in multiple consumer case is the same while memory consumption of Circular Buffer is equal to `N*BUFFER_SIZE*sizeof(lr_data_t) + sizeof(struct linked_ring)`

As we can see, Linked Ring Buffer uses less memory in multiple consumer case.

Example for 3 consumers and 256 elements buffer:

-   Linked Ring Buffer size: `256*(8 + 8 + 8) + 48 = 4608 bytes`
-   Circular Buffer size: `3*256*8 + 48 = 6144 bytes`

Example for 10 consumers and 64 elements buffer:

-   Linked Ring Buffer size = 256*(8 (sizeof lr_data_t) + 8 (sizeof lr_owner_t) + 8 (sizeof struct lr_cell *)) + 40 (sizeof struct linked_ring) = 5120 bytes
-   Circular Buffer size = 10*256*8 (sizeof lr_data_t) + 40 (sizeof struct linked_ring) = 20480 bytes

Difference is = 15360 bytes that is
15360 bytes / 5120 bytes = 3 times less memory.

### Linked Ring Based Data Structures

The Linked Ring data structure can be used to implement other data structures such as queues and stacks.

A queue can be implemented using a Linked Ring structure by simply adding elements to the end of the buffer, and removing them from the front.

A stack can be implemented using a Linked Ring structure by adding elements to the end of the buffer, and removing them from the same end. This allows elements to be "popped" off the stack in the same order they were "pushed" onto it.
