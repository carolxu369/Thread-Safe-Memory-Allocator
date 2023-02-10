#include "my_malloc.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>

// define the head and tail of the free linked list used in normal case 
block * head = NULL;
block * tail = NULL;

// define the head and tail of the free linked list used in thread local storage case
__thread block * head_thread = NULL;
__thread block * tail_thread = NULL;

// initialize the pthread_mutex_t lock
pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

// declare to record the entire heap memory and size of the "free list"
unsigned long data_segment_size = 0;
unsigned long data_segment_free_space_size = 0;

// case when there is no free block with enough space for malloc, we need to use sbrk() to increase the heap
block * new_space(size_t size) {
  size_t new_size = sizeof(block) + size;
  block * new_block = sbrk(new_size);

  // the case when sbrk() failed
  if (new_block == (void *)-1) {
    perror("The sbrk() failed.\n");
    exit(EXIT_FAILURE);
  }

  new_block->size = size;
  new_block->next = NULL;
  new_block->prev = NULL;
  data_segment_size += new_size;

  return (block *)((char *)new_block + sizeof(block));
}

// declare another new_space lock version for tls case
block * new_space_lock(size_t size) {
  size_t new_size = sizeof(block) + size;

  pthread_mutex_lock(&lock);
  block * new_block = sbrk(new_size);
  pthread_mutex_unlock(&lock);

  // the case when sbrk() failed
  if (new_block == (void *)-1) {
    perror("The sbrk() failed.\n");
    exit(EXIT_FAILURE);
  }

  new_block->size = size;
  new_block->next = NULL;
  new_block->prev = NULL;
  data_segment_size += new_size;

  return (block *)((char *)new_block + sizeof(block));
}

// case when there is free block with enough space for malloc, we find the first fit free block for malloc and split the block
block * ff_split(block * ff_block, size_t size, block ** head, block ** tail) {
  size_t curr_size = sizeof(block) + size;
  block * malloc_block = ff_block;
  block * new_block = (block *)((char *)ff_block + curr_size);

  new_block->size = ff_block->size - size - sizeof(block);
  new_block->prev = ff_block->prev;
  new_block->next = ff_block->next;
  malloc_block->size = size;
  malloc_block->prev = NULL;
  malloc_block->next = NULL;

  data_segment_free_space_size -= curr_size;

  // add the new block back to the free linked list and remove the malloc block
  if (ff_block == *head && *head == *tail) {
    *head = new_block;
    *tail = new_block;
  }
  else if (ff_block == *head) {
    *head = new_block;
    new_block->next->prev = new_block;
  }
  else if (ff_block == *tail) {
    *tail = new_block;
    new_block->prev->next = new_block;
  }
  else {
    new_block->prev->next = new_block;
    new_block->next->prev = new_block;
  }

  ff_block->next = NULL;
  ff_block->prev = NULL;

  return (block *)((char *)malloc_block + sizeof(block));
}

// remove the malloc block from the free linked list
void remove_malloc(block * malloc_block, block ** head, block ** tail) {
  if (malloc_block == *head && *head == *tail) {
    *head = NULL;
    *tail = NULL;
  }
  else if (malloc_block == *head) {
    *head = malloc_block->next;
    malloc_block->next->prev = NULL;
  }
  else if (malloc_block == *tail) {
    *tail = malloc_block->prev;
    malloc_block->prev->next = NULL;
  }
  else {
    malloc_block->prev->next = malloc_block->next;
    malloc_block->next->prev = malloc_block->prev;
  }
  malloc_block->prev = NULL;
  malloc_block->next = NULL;
}

// add free block back
void add_free(block * free_block, block ** head, block ** tail) {
  // check whether we have the head node
  if (*head == NULL) {
    *head = free_block;
    *tail = free_block;
  }
  else {
    // insert first
    block * curr_block = *head;

    while (curr_block && curr_block <= free_block) {
      if (curr_block->next && free_block <= curr_block->next) {
        break;
      }
      curr_block = curr_block->next;
    }

    if (curr_block == NULL) {
      (*tail)->next = free_block;
      free_block->prev = *tail;
      free_block->next = NULL;
      *tail = free_block;
    }
    else if (curr_block == *head && curr_block > free_block) {
      free_block->next = *head;
      (*head)->prev = free_block;
      free_block->prev = NULL;
      *head = free_block;
    }
    else {
      free_block->next = curr_block->next;
      free_block->prev = curr_block;
      curr_block->next->prev = free_block;
      curr_block->next = free_block;
    }

    // merge if needed
    if (free_block->prev && free_block->next) {
      if ((char *)free_block + sizeof(block) + free_block->size ==
              (char *)free_block->next &&
          (char *)free_block->prev + sizeof(block) + free_block->prev->size ==
              (char *)free_block) {
        free_block->prev->size +=
            sizeof(block) + sizeof(block) + free_block->size + free_block->next->size;
        free_block->prev->next = free_block->next->next;
        if (free_block->next->next == NULL) {
          *tail = free_block->prev;
        }
        else {
          free_block->next->next->prev = free_block->prev;
        }
      }
      else if ((char *)free_block + sizeof(block) + free_block->size ==
               (char *)free_block->next) {
        free_block->size += sizeof(block) + free_block->next->size;
        if (free_block->next->next == NULL) {
          *tail = free_block;
          free_block->next = NULL;
        }
        else {
          free_block->next->next->prev = free_block;
          free_block->next = free_block->next->next;
        }
      }
      else if ((char *)free_block->prev + sizeof(block) + free_block->prev->size ==
               (char *)free_block) {
        free_block->prev->size += sizeof(block) + free_block->size;
        free_block->prev->next = free_block->next;
        free_block->next->prev = free_block->prev;
        free_block->next = NULL;
        free_block->prev = NULL;
      }
    }
    else if (free_block->prev) {
      if ((char *)free_block->prev + sizeof(block) + free_block->prev->size ==
          (char *)free_block) {
        free_block->prev->size += sizeof(block) + free_block->size;
        free_block->prev->next = NULL;
        *tail = free_block->prev;
        free_block->next = NULL;
        free_block->prev = NULL;
      }
    }
    else if (free_block->next) {
      if ((char *)free_block + sizeof(block) + free_block->size ==
          (char *)free_block->next) {
        free_block->size += sizeof(block) + free_block->next->size;
        if (free_block->next->next == NULL) {
          *tail = free_block;
          free_block->next = NULL;
        }
        else {
          free_block->next->next->prev = free_block;
          free_block->next = free_block->next->next;
        }
      }
    }
  }
}

// // first fit malloc
// void * ff_malloc(size_t size) {
//   block * curr_block = head;

//   // find the first block whose size is equal or larger than the required malloc size
//   while (curr_block && curr_block->size < size) {
//     curr_block = curr_block->next;
//   }

//   if (curr_block != NULL) {
//     if (curr_block->size > sizeof(block) + size) {
//       return ff_split(curr_block, size);
//     }
//     else {
//       remove_malloc(curr_block);
//       data_segment_free_space_size -= sizeof(block) + curr_block->size;
//       return (block *)((char *)curr_block + sizeof(block));
//     }
//   }
//   else {
//     return new_space(size);
//   }
// }

// // first fit free
// void ff_free(void * ptr) {
//   if (ptr == NULL) {
//     return;
//   }
//   block * free_block = (block *)((char *)ptr - sizeof(block));
//   data_segment_free_space_size += sizeof(block) + free_block->size;

//   // adding the new free block back to the free linked list
//   add_free(free_block);
// }

// best fit malloc, modify this to count for the thread local storage case
void * bf_malloc(size_t size, block ** head, block ** tail, int sbrk_lock) {
  block * curr_block = *head;
  block * bf_block = NULL;
  size_t curr_size = ULONG_MAX;

  while (curr_block) {
    if (curr_block->size == size) {
      remove_malloc(curr_block, head, tail);
      data_segment_free_space_size -= sizeof(block) + curr_block->size;
      return (block *)((char *)curr_block + sizeof(block));
    }
    if (curr_block->size >= size) {
      if (curr_block->size < curr_size) {
        curr_size = curr_block->size;
        bf_block = curr_block;
      }
    }
    curr_block = curr_block->next;
  }

  if (bf_block != NULL) {
    if (bf_block->size > sizeof(block) + size) {
      return ff_split(bf_block, size, head, tail);
    }
    else {
      remove_malloc(bf_block, head, tail);
      data_segment_free_space_size -= sizeof(block) + bf_block->size;
      return (block *)((char *)bf_block + sizeof(block));
    }
  }
  else {
    if (sbrk_lock == 1){
        return new_space_lock(size);
    }
    else{
        return new_space(size);
    }
  }
}

//best fit free
void bf_free(void * ptr, block ** head, block ** tail) {
  if (ptr == NULL) {
    return;
  }
  block * free_block = (block *)((char *)ptr - sizeof(block));
  data_segment_free_space_size += sizeof(block) + free_block->size;

  // adding the new free block back to the free linked list
  add_free(free_block, head, tail);
}

// Thread safe malloc and free: locking version
void * ts_malloc_lock(size_t size){
    pthread_mutex_lock(&lock);
    void * result = bf_malloc(size, &head, &tail, 0);
    pthread_mutex_unlock(&lock);
    return result;
}

void ts_free_lock(void * ptr){
    pthread_mutex_lock(&lock);
    bf_free(ptr, &head, &tail);
    pthread_mutex_unlock(&lock);
}

// Thread safe malloc and free: non-locking version
void * ts_malloc_nolock(size_t size){
    void * result = bf_malloc(size, &head_thread, &tail_thread, 1);
    return result;
}

void ts_free_nolock(void * ptr){
    bf_free(ptr, &head_thread, &tail_thread);
}


unsigned long get_data_segment_size() {
  return data_segment_size;
}

unsigned long get_data_segment_free_space_size() {
  return data_segment_free_space_size;
}
