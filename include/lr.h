/**
 * @file lr.h
 * @brief Linked Ring Buffer - A circular buffer with ownership tracking
 *
 * The Linked Ring Buffer is a data structure that provides a circular buffer
 * with ownership tracking. It allows multiple owners to share the same buffer
 * while maintaining separate views of the data.
 */

#pragma once

#include <stdint.h>
#include <stdio.h>

/**
 * @defgroup Types Basic Types
 * @{
 * 
 * @note Thread Safety:
 * Functions in this library are generally not thread-safe unless a mutex is provided
 * via lr_set_mutex(). Functions that acquire and release the mutex internally are
 * marked as "Thread-safe with mutex". Functions that don't use the mutex or require
 * external synchronization are marked as "Not thread-safe".
 */

/** Type for storing data in the linked ring buffer */
#define lr_data_t uintptr_t

/** Cast a pointer to lr_data_t type */
#define lr_data(ptr) (uintptr_t)ptr

/** Type for identifying owners in the linked ring buffer */
#define lr_owner_t uintptr_t

/** Cast a pointer to lr_owner_t type */
#define lr_owner(ptr) (uintptr_t)ptr

/**
 * @brief Result codes for linked ring operations
 */
typedef enum lr_result {
    LR_OK = 0,               /**< Operation completed successfully */
    LR_ERROR_UNKNOWN,        /**< Unknown error occurred */
    LR_ERROR_NOMEMORY,       /**< Memory allocation failed */
    LR_ERROR_LOCK,           /**< Failed to acquire lock */
    LR_ERROR_UNLOCK,         /**< Failed to release lock */
    LR_ERROR_BUFFER_FULL,    /**< Buffer is full */
    LR_ERROR_BUFFER_EMPTY,   /**< Buffer is empty */
    LR_ERROR_BUFFER_BUSY     /**< Buffer is currently in use */
} lr_result_t;

/** @} */ /* end of Types group */

/**
 * @defgroup DataStructures Data Structures
 * @{
 */

/**
 * @brief Representation of an element in the Linked Ring buffer
 */
struct lr_cell {
    lr_data_t       data; /**< The data stored in this cell */
    struct lr_cell *next; /**< Pointer to the next cell in the ring */
};

/**
 * @brief The main Linked Ring buffer structure
 */
struct linked_ring {
    struct lr_cell *cells;  /**< Allocated array of cells in the buffer */
    unsigned int    size;   /**< Maximum number of elements that can be stored */
    struct lr_cell *write;  /**< Cell that is currently being written to */
    struct lr_cell *owners; /**< First owner cell in the buffer */

    /** Thread safety lock function */
    enum lr_result (*lock)(void *state);
    
    /** Thread safety unlock function */
    enum lr_result (*unlock)(void *state);
    
    /** State for mutex operations */
    void *mutex_state;
};

/**
 * @brief Provides a mechanism for thread-safe access to the linked ring
 */
struct lr_mutex_attr {
    /** Shared state for lock and unlock functions */
    void *state;

    /**
     * Blocks until the mutex is acquired
     * @param state The mutex state
     * @return LR_OK on success, error code otherwise
     * @note Is a no-op if NULL
     */
    enum lr_result (*lock)(void *state);

    /**
     * Release the mutex
     * @param state The mutex state
     * @return LR_OK on success, error code otherwise
     * @note Is a no-op if NULL
     */
    enum lr_result (*unlock)(void *state);
};

/** @} */ /* end of DataStructures group */


/**
 * @defgroup Macros Utility Macros
 * @{
 */

/**
 * @brief Get the number of available cells in the buffer
 * @param lr Pointer to the linked ring buffer
 * @return Number of available cells
 */
#define lr_available(lr) ((lr)->size - lr_count(lr) - lr_owners_count(lr))

/**
 * @brief Get the size of the buffer
 * @param lr Pointer to the linked ring buffer
 * @return Size of the buffer
 */
#define lr_size(lr) (lr->cells - lr->owners)

/**
 * @brief Get the number of owners in the buffer
 * @param lr Pointer to the linked ring buffer
 * @return Number of owners
 */
#define lr_owners_count(lr) \
    ((lr)->owners == NULL ? 0 : (lr)->cells + (lr)->size - (lr)->owners)

/**
 * @brief Check if an owner exists in the buffer
 * @param lr Pointer to the linked ring buffer
 * @param owner Owner ID to check
 * @return Non-zero if owner exists, 0 otherwise
 */
#define lr_exists(lr, owner) lr_count_limited_owned(lr, 1, owner)

/**
 * @brief Count the number of elements owned by a specific owner
 * @param lr Pointer to the linked ring buffer
 * @param owner Owner ID
 * @return Number of elements owned by the specified owner
 */
#define lr_count_owned(lr, owner) lr_count_limited_owned(lr, 0, owner)

/**
 * @brief Get the last cell in the buffer
 * @param lr Pointer to the linked ring buffer
 * @return Pointer to the last cell
 */
#define lr_last_cell(lr) ((lr)->cells + (lr)->size - 1)

/**
 * @brief Get the tail cell for an owner
 * @param owner_cell Pointer to the owner cell
 * @return Pointer to the tail cell
 */
#define lr_owner_tail(owner_cell) owner_cell->next

/** @} */ /* end of Macros group */

/**
 * @defgroup BufferManagement Buffer Management Functions
 * @{
 */

/**
 * @brief Initialize a new linked ring buffer
 * 
 * @param lr Pointer to the linked ring structure to be initialized
 * @param size Size of the buffer, in number of elements
 * @param cells Pointer to the array of cells that will make up the buffer
 * 
 * @return LR_OK if the initialization was successful
 * @return LR_ERROR_NOMEMORY if the cells parameter is NULL or size is 0
 * 
 * @note Thread safety: Not thread-safe. Should be called before any threads access the buffer.
 */
lr_result_t lr_init(struct linked_ring *lr, size_t size, struct lr_cell *cells);

/**
 * @brief Resize the linked ring buffer
 * 
 * Moves the data from the old cells array to the new cells array, and updates
 * the pointers to the cells array.
 * 
 * @param lr Pointer to the linked ring structure to be resized
 * @param size New size of the buffer, in number of elements
 * @param cells Pointer to the new array of cells
 * 
 * @return LR_OK if the resize was successful
 * @return LR_ERROR_NOMEMORY if the cells parameter is NULL or size is 0
 * 
 * @note Thread safety: Not thread-safe. All threads must stop accessing the buffer
 *       before calling this function.
 */
lr_result_t lr_resize(struct linked_ring *lr, size_t size, struct lr_cell *cells);

/**
 * @brief Set the mutex for thread-safe access to the linked ring
 * 
 * @param lr Pointer to the linked ring structure
 * @param attr Mutex attributes
 * 
 * @note Thread safety: Not thread-safe. Should be called before any threads access the buffer.
 */
void lr_set_mutex(struct linked_ring *lr, struct lr_mutex_attr *attr);

/** @} */ /* end of BufferManagement group */

/**
 * @defgroup DataOperations Data Operations
 * @{
 */

/**
 * @brief Add a new element to the head of the linked ring buffer
 * 
 * @param lr Pointer to the linked ring structure
 * @param data The data to be added to the buffer
 * @param owner The owner of the new element
 * 
 * @return LR_OK if the element was successfully added
 * @return LR_ERROR_BUFFER_FULL if the buffer is full
 * @return LR_ERROR_LOCK if mutex acquisition failed
 * 
 * @note Thread safety: Thread-safe with mutex. Acquires and releases mutex internally.
 */
lr_result_t lr_put(struct linked_ring *lr, lr_data_t data, lr_owner_t owner);

/**
 * @brief Add a new element to the tail of the linked ring buffer
 * 
 * @param lr Pointer to the linked ring structure
 * @param data The data to be added to the buffer
 * @param owner The owner of the new element
 * 
 * @return LR_OK if the element was successfully added
 * @return LR_ERROR_BUFFER_FULL if the buffer is full
 * @return LR_ERROR_LOCK if mutex acquisition failed
 * 
 * @note Thread safety: Thread-safe with mutex. Acquires and releases mutex internally.
 */
lr_result_t lr_push(struct linked_ring *lr, lr_data_t data, lr_owner_t owner);

/**
 * @brief Insert a new element at the specified index of the linked ring buffer
 * 
 * @param lr Pointer to the linked ring structure
 * @param data The data to be added to the buffer
 * @param owner The owner of the new element
 * @param index The index at which to insert the element
 * 
 * @return LR_OK if the element was successfully added
 * @return LR_ERROR_BUFFER_FULL if the buffer is full
 * @return LR_ERROR_LOCK if mutex acquisition failed
 * 
 * @note Thread safety: Thread-safe with mutex. Acquires and releases mutex internally.
 */
lr_result_t lr_insert(struct linked_ring *lr, lr_data_t data, lr_owner_t owner, size_t index);

/**
 * @brief Insert a new element after a specific cell
 * 
 * @param lr Pointer to the linked ring structure
 * @param data The data to be added to the buffer
 * @param needle The cell after which to insert the new element
 * 
 * @return LR_OK if the element was successfully added
 * @return LR_ERROR_BUFFER_FULL if the buffer is full
 * @return LR_ERROR_LOCK if mutex acquisition failed
 * 
 * @note Thread safety: Thread-safe with mutex. Acquires and releases mutex internally.
 */
lr_result_t lr_insert_next(struct linked_ring *lr, lr_data_t data, struct lr_cell *needle);

/**
 * @brief Add a string to the linked ring buffer
 * 
 * @param lr Pointer to the linked ring structure
 * @param data The string to be added to the buffer
 * @param owner The owner of the new elements
 * 
 * @return LR_OK if the string was successfully added
 * @return LR_ERROR_BUFFER_FULL if the buffer is full
 * @return LR_ERROR_LOCK if mutex acquisition failed
 * 
 * @note Thread safety: Thread-safe with mutex. Acquires and releases mutex internally
 *       for each character, but not atomic for the entire string.
 */
lr_result_t lr_put_string(struct linked_ring *lr, unsigned char *data, lr_owner_t owner);

/**
 * @brief Retrieve and remove the next element from the linked ring buffer
 * 
 * @param lr Pointer to the linked ring structure
 * @param data Pointer to store the retrieved data
 * @param owner The owner of the element to retrieve
 * 
 * @return LR_OK if the element was successfully retrieved
 * @return LR_ERROR_BUFFER_EMPTY if the buffer is empty for this owner
 * @return LR_ERROR_LOCK if mutex acquisition failed
 * 
 * @note Thread safety: Thread-safe with mutex. Acquires and releases mutex internally.
 */
lr_result_t lr_get(struct linked_ring *lr, lr_data_t *data, lr_owner_t owner);

/**
 * @brief Retrieve and remove the last element from the linked ring buffer
 * 
 * @param lr Pointer to the linked ring structure
 * @param data Pointer to store the retrieved data
 * @param owner The owner of the element to retrieve
 * 
 * @return LR_OK if the element was successfully retrieved
 * @return LR_ERROR_BUFFER_EMPTY if the buffer is empty for this owner
 * @return LR_ERROR_LOCK if mutex acquisition failed
 * 
 * @note Thread safety: Thread-safe with mutex. Acquires and releases mutex internally.
 */
lr_result_t lr_pop(struct linked_ring *lr, lr_data_t *data, lr_owner_t owner);

/**
 * @brief Retrieve and remove an element at a specific index
 * 
 * @param lr Pointer to the linked ring structure
 * @param data Pointer to store the retrieved data
 * @param owner The owner of the element to retrieve
 * @param index The index of the element to retrieve
 * 
 * @return LR_OK if the element was successfully retrieved
 * @return LR_ERROR_BUFFER_EMPTY if the buffer is empty for this owner or index is invalid
 * @return LR_ERROR_LOCK if mutex acquisition failed
 * 
 * @note Thread safety: Thread-safe with mutex. Acquires and releases mutex internally.
 */
lr_result_t lr_pull(struct linked_ring *lr, lr_data_t *data, lr_owner_t owner, size_t index);

/**
 * @brief Read the next element without removing it
 * 
 * @param lr Pointer to the linked ring structure
 * @param data Pointer to store the read data
 * @param owner The owner of the element to read
 * 
 * @return LR_OK if the element was successfully read
 * @return LR_ERROR_BUFFER_EMPTY if the buffer is empty for this owner
 * @return LR_ERROR_LOCK if mutex acquisition failed
 * 
 * @note Thread safety: Thread-safe with mutex. Acquires and releases mutex internally.
 */
lr_result_t lr_read(struct linked_ring *lr, lr_data_t *data, lr_owner_t owner);

/**
 * @brief Read a string from the buffer without removing it
 * 
 * @param lr Pointer to the linked ring structure
 * @param data Buffer to store the string
 * @param length Pointer to store the length of the string
 * @param owner The owner of the string to read
 * 
 * @return LR_OK if the string was successfully read
 * @return LR_ERROR_BUFFER_EMPTY if the buffer is empty for this owner
 * @return LR_ERROR_LOCK if mutex acquisition failed
 * 
 * @note Thread safety: Thread-safe with mutex. Acquires and releases mutex internally.
 */
lr_result_t lr_read_string(struct linked_ring *lr, unsigned char *data, size_t *length, lr_owner_t owner);

/**
 * @brief Read a character at a specific index without removing it
 * 
 * @param lr Pointer to the linked ring structure
 * @param data Pointer to store the read character
 * @param owner The owner of the character to read
 * @param index The index of the character to read
 * 
 * @return LR_OK if the character was successfully read
 * @return LR_ERROR_BUFFER_EMPTY if the buffer is empty for this owner
 * @return LR_ERROR_INVALID_INDEX if the index is out of bounds
 * @return LR_ERROR_LOCK if mutex acquisition failed
 * 
 * @note Thread safety: Thread-safe with mutex. Acquires and releases mutex internally.
 */
lr_result_t lr_read_at(struct linked_ring *lr, lr_data_t *data, lr_owner_t owner, size_t index);

/** @} */ /* end of DataOperations group */

/**
 * @defgroup OwnerManagement Owner Management Functions
 * @{
 */

/**
 * @brief Find an owner cell in the buffer
 * 
 * @param lr Pointer to the linked ring structure
 * @param owner The owner ID to find
 * 
 * @return Pointer to the owner cell if found, NULL otherwise
 * 
 * @note Thread safety: Not thread-safe. This function does not acquire the mutex
 *       and should only be called from within functions that have already acquired
 *       the mutex or in single-threaded contexts.
 */
struct lr_cell *lr_owner_find(struct linked_ring *lr, lr_data_t owner);

/**
 * @brief Allocate a new owner cell
 * 
 * @param lr Pointer to the linked ring structure
 * 
 * @return Pointer to the allocated owner cell if successful, NULL otherwise
 * 
 * @note Thread safety: Not thread-safe. This function does not acquire the mutex
 *       and should only be called from within functions that have already acquired
 *       the mutex or in single-threaded contexts.
 */
struct lr_cell *lr_owner_allocate(struct linked_ring *lr);

/**
 * @brief Get the head cell for an owner
 * 
 * @param lr Pointer to the linked ring structure
 * @param owner_cell Pointer to the owner cell
 * 
 * @return Pointer to the head cell
 * 
 * @note Thread safety: Not thread-safe. This function does not acquire the mutex
 *       and should only be called from within functions that have already acquired
 *       the mutex or in single-threaded contexts.
 */
struct lr_cell *lr_owner_head(struct linked_ring *lr, struct lr_cell *owner_cell);

/**
 * @brief Count elements owned by a specific owner, with an optional limit
 * 
 * @param lr Pointer to the linked ring structure
 * @param limit Maximum number of elements to count (0 for no limit)
 * @param owner The owner ID
 * 
 * @return Number of elements owned by the specified owner (up to the limit if specified)
 * 
 * @note Thread safety: Thread-safe with mutex. Acquires and releases mutex internally.
 */
size_t lr_count_limited_owned(struct linked_ring *lr, size_t limit, lr_owner_t owner);

/**
 * @brief Count the total number of elements in the buffer
 * 
 * @param lr Pointer to the linked ring structure
 * 
 * @return Total number of elements in the buffer
 * 
 * @note Thread safety: Thread-safe with mutex. Acquires and releases mutex internally.
 */
size_t lr_count(struct linked_ring *lr);

/** @} */ /* end of OwnerManagement group */

/**
 * @defgroup Debug Debug Functions
 * @{
 */

/**
 * @brief Dump the contents of the buffer for debugging
 * 
 * @param lr Pointer to the linked ring structure
 * 
 * @return LR_OK if successful, error code otherwise
 * 
 * @note Thread safety: Thread-safe with mutex. Acquires and releases mutex internally.
 *       However, the buffer state may change between acquiring and releasing the mutex
 *       for different operations within this function.
 */
lr_result_t lr_dump(struct linked_ring *lr);

/**
 * @brief Debug the cell structure
 * 
 * @param lr Pointer to the linked ring structure
 * 
 * @note Thread safety: Not thread-safe. This function does not acquire the mutex
 *       and should only be called in single-threaded contexts or when no other
 *       threads are accessing the buffer.
 */
void lr_debug_strucuture_cells(struct linked_ring *lr);

/**
 * @brief Debug the circular structure for a specific owner
 * 
 * @param lr Pointer to the linked ring structure
 * @param owner The owner ID
 * 
 * @return LR_OK if successful, error code otherwise
 * 
 * @note Thread safety: Not thread-safe. This function does not acquire the mutex
 *       and should only be called in single-threaded contexts or when no other
 *       threads are accessing the buffer.
 */
lr_result_t lr_debug_structure_circular(struct linked_ring *lr, lr_owner_t owner);

/**
 * @brief Debug the relinked structure after operations
 * 
 * @param lr Pointer to the linked ring structure
 * 
 * @note Thread safety: Not thread-safe. This function does not acquire the mutex
 *       and should only be called in single-threaded contexts or when no other
 *       threads are accessing the buffer.
 */
void lr_debug_structure_relinked(struct linked_ring *lr);

/** @} */ /* end of Debug group */

