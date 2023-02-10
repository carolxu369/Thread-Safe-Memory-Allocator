#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

// we first define a struct to represent the memory space of the heap, where we need to allocate or free memory
// according to malloc and free function, we use linked list in this case
struct linked_list {
  size_t size;  // the size of each block of the linked list
  struct linked_list *
      next;  // next pointer of the linked list pointing to the next block in the linkedlist
  struct linked_list *
      prev;  // prev pointer of the linked list pointing to the prev block in the linkedlist
};
typedef struct linked_list block;

// first fit malloc and free
// void * ff_malloc(size_t size);
// void ff_free(void * ptr);

// best fit malloc and free
void * bf_malloc(size_t size, block ** head, block ** tail, int sbrk_lock);
void bf_free(void * ptr, block ** head, block ** tail);

// Thread safe malloc and free: locking version
void * ts_malloc_lock(size_t size);
void ts_free_lock(void * ptr);

// Thread safe malloc and free: non-locking version
void * ts_malloc_nolock(size_t size);
void ts_free_nolock(void * ptr);

// case when there is no free block with enough space for malloc, we need to use sbrk() to increase the heap
block * new_space(size_t size);
block * new_space_lock(size_t size);

// case when there is free block with enough space for malloc, we find the first fit free block for malloc and split the block
block * ff_split(block * ff_block, size_t size, block ** head, block ** tail);

// add free block back
void add_free(block * free_block, block ** head, block ** tail);

// remove the malloc block from the free linked list
void remove_malloc(block * malloc_block, block ** head, block ** tail);

// perfromance measure
unsigned long get_data_segment_size();
unsigned long get_data_segment_free_space_size();