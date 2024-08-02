// SPDX-License-Identifier: BSD-3-Clause

#include "osmem.h"
#include "printf.h"
#include "block_meta.h"


struct block_meta *head_brk;
struct block_meta *head_mmap;

void coalesce_free_blocks(void)
{
	struct block_meta *current = head_brk;

	while (current != NULL) {
		if (current->status == 0 && current->next != NULL && current->next->status == 0) {
			current->size += current->next->size;
			current->next = current->next->next;
		}
		current = current->next;
	}
}

void *verify_size(struct block_meta *current, size_t size, void *ptr, size_t copy_size)
{
	if (current->size + current->next->size >= size + sizeof(struct block_meta)) {
		// Set the size of the current block to the size of the block we want to allocate
		current->size = current->size + current->next->size;

		// Set the next of the current block to the next of the next of the current block
		current->next = current->next->next;

		// Set the status of the current block to allocated
		current->status = STATUS_ALLOC;

		// Verify if there is space for another block
		if (current->size >= 2 * sizeof(struct block_meta) + size + 8) {
			// Initialize the second block
			struct block_meta *second_block = (struct block_meta *)((void *)current + size + sizeof(struct block_meta));

			// Initialize the size of the second block
			second_block->size = current->size - size - sizeof(struct block_meta);

			// Set the status of the second block to free
			second_block->status = STATUS_FREE;

			// Set the prev of the second block to the current block
			second_block->prev = current;

			// Set the next of the second block to the next of the current block
			second_block->next = current->next;

			// Set the next of the current block to the second block
			current->next = second_block;

			// Set the size of the current block to the size of the block we want to allocate
			current->size = size + sizeof(struct block_meta);

			// Set the status of the current block to allocated
			current->status = STATUS_ALLOC;

			// Return the pointer to the allocated memory
			return (void *)current + sizeof(struct block_meta);

		// If there is no space for another block
		} else {
			// Return the pointer to the allocated memory
			return (void *)current + sizeof(struct block_meta);
		}
	} else {
		void *new_ptr = os_malloc(size);

		memcpy(new_ptr, ptr, copy_size + sizeof(struct block_meta));

		os_free(ptr);

		return new_ptr;
	}
}

size_t min(size_t a, size_t b)
{
	if (a < b)
		return a;
	return b;
}

void *os_malloc(size_t size)
{
	//If the size is 0, return NULL
	if (size == 0)
		return NULL;

	// Align the size
	if (size % ALIGNMENT != 0)
		size += (ALIGNMENT - (size % ALIGNMENT));

	//Verify if the first block was allocated and if the size is smaller than the threshold
	if (head_brk == NULL && size < MMAP_THRESHHOLD - sizeof(struct block_meta)) {
		// Initialize the first block
		void *ptr = sbrk(MMAP_THRESHHOLD);

		DIE(ptr == (void *)-1, "sbrk failed");

		// Initialize the first block
		struct block_meta *block = (struct block_meta *)ptr;

		// Initialize the size and status of the first block
		block->size = size + sizeof(struct block_meta);

		// Set the status of the first block to allocated
		block->status = STATUS_ALLOC;

		// Set the prev and next of the first block to NULL
		block->prev = NULL;
		block->next = NULL;

		// Set the head_brk to the first block
		head_brk = block;

		//Verify if in the allocated memory there is enough space for another block
		if (size < 131000) { // poate sa fie <= 131000
			// Initialize the second block
			struct block_meta *second_block = head_brk + size + sizeof(struct block_meta);

			// Initialize the size
			second_block->size = MMAP_THRESHHOLD - size - sizeof(struct block_meta);

			// Set the status of the second block to free
			second_block->status = STATUS_FREE;

			// Set the prev to the first block
			second_block->prev = head_brk;

			// Set the next to NULL
			second_block->next = NULL;

			// Set the next of the first block to the second block
			head_brk->next = second_block;
		}
		// Return the pointer to the allocated memory
		return (void *)block + sizeof(struct block_meta); // Return the pointer to the allocated memory
	}

	// Verify if the first block was allocated and if there is enough space for another block
	if (head_brk != NULL && size < MMAP_THRESHHOLD - sizeof(struct block_meta)) {
		//Coalesce the free blocks
		coalesce_free_blocks();

		// Initialize the current block
		struct block_meta *current = head_brk;

		// Initialize the best fit block
		struct block_meta *best_fit = NULL;

		//Going through the allocated memory to find the best fit block
		while (current != NULL) {
			// Verify if the current block is free and if the size of the block is bigger/equal
			// than the size of the block we want to allocate
			if (current->status == STATUS_FREE && current->size >= size + sizeof(struct block_meta)) {
				// Verify if the best fit block is NULL
				if (best_fit == NULL) {
					best_fit = current;
					// Or if the size of the current block is smaller
				} else if (current->size < best_fit->size) {
					best_fit = current;
				}
			}

			// Go to the next block
			current = current->next;
		}

		// Verify if the best fit block exists
		if (best_fit != NULL) {
			// Verify if there is space for another block
			if (best_fit->size >= 2 * sizeof(struct block_meta) + size + 8) {
				// Initialize the second block
				struct block_meta *second_block = (struct block_meta *)((void *)best_fit + size + sizeof(struct block_meta));

				// Initialize the size of the second block
				second_block->size = best_fit->size - size - sizeof(struct block_meta);

				// Set the status of the second block to free
				second_block->status = STATUS_FREE;

				// Set the prev of the second block to the best fit block
				second_block->prev = best_fit;

				// Set the next of the second block to the next of the best fit block
				second_block->next = best_fit->next;

				// Set the next of the best fit block to the second block
				best_fit->next = second_block;

				// Set the size of the best fit block to the size of the block we want to allocate
				best_fit->size = size + sizeof(struct block_meta);

				// Set the status of the best fit block to allocated
				best_fit->status = STATUS_ALLOC;

				// Return the pointer to the allocated memory
				return (void *)best_fit + sizeof(struct block_meta);

			// If there is no space for another block
			} else {
				// Set the status of the best fit block to allocated
				best_fit->status = STATUS_ALLOC;

				// Return the pointer to the allocated memory
				return (void *)best_fit + sizeof(struct block_meta);
			}

		// If the best_fit block does not exist
		} else {
			// Initialize the current block
			struct block_meta *current = head_brk;

			// Going through the allocated memory to find the last block
			while (current->next != NULL)
				current = current->next;

			// Verify if the last block is free
			if (current->status == STATUS_FREE) {
				// Verify if the size of the last block is smaller than the size of the block we want to allocate
				if (size + sizeof(struct block_meta) > current->size) {
					// Use sbrk to allocate memory
					void *ptr = sbrk(size + sizeof(struct block_meta) - current->size);

					// Verify if sbrk failed
					DIE(ptr == (void *)-1, "sbrk failed");

					// Set the size of the last block to the size of the block we want to allocate
					current->size = size + sizeof(struct block_meta);

					// Set the status of the last block to allocated
					current->status = STATUS_ALLOC;

					// Return the pointer to the allocated memory
					return (void *)current + sizeof(struct block_meta);
				}
			} else if (current->status == STATUS_ALLOC) {
				// Use sbrk to allocate memory
				void *ptr = sbrk(size + sizeof(struct block_meta));

				// Verify if sbrk failed
				DIE(ptr == (void *)-1, "sbrk failed");

				// Initialize the block
				struct block_meta *block = (struct block_meta *)ptr;

				// Initialize the size of the block
				block->size = size + sizeof(struct block_meta);

				// Set the status of the block to allocated
				block->status = STATUS_ALLOC;

				// Set the prev of the block to the current block
				block->prev = current;

				// Set the next of the block to
				block->next = current->next;

				// Set the next of the current block to the block
				current->next = block;

				// Return the pointer to the allocated memory
				return (void *)block + sizeof(struct block_meta);
			}
		}
	}

	// Need to use mmap for allocation
	if (size >= MMAP_THRESHHOLD) {
		// Verify if the head is initialized
		if (head_mmap == NULL) {
			// Use mmap to allocate memory
			void *ptr = mmap(NULL, size + sizeof(struct block_meta), PROT_READ
							 | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

			// Verify if mmap failed
			DIE(ptr == (void *)-1, "mmap failed");

			// Initialize the block
			struct block_meta *block = (struct block_meta *)ptr;

			// Initialize the size of the block
			block->size = size + sizeof(struct block_meta);

			// Set the status of the block to mapped
			block->status = STATUS_MAPPED;

			// Set the prev of the block to NULL
			block->prev = NULL;

			// Set the next of the block to NULL
			block->next = NULL;

			// Set the head_mmap to the block
			head_mmap = block;

			// Return the pointer to the allocated memory
			return (void *)block + sizeof(struct block_meta);

		// If there is at least one block of memory mapped
		} else if (head_mmap != NULL) {
			void *ptr = mmap(NULL, size + sizeof(struct block_meta), PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

			// Verify if mmap failed
			DIE(ptr == (void *)-1, "mmap failed");

			// Initialize the block
			struct block_meta *block = (struct block_meta *)ptr;

			// Initialize the size of the block
			block->size = size + sizeof(struct block_meta);

			// Set the status of the block to mapped
			block->status = STATUS_MAPPED;

			// Go to the last block
			struct block_meta *current = head_mmap;

			while (current->next != NULL)
				current = current->next;

			// Set the prev of the block to the current block
			block->prev = current;

			// Set the next of the block to NULL
			block->next = NULL;

			// Set the next of the current block to the block
			current->next = block;

			// Return the pointer to the allocated memory
			return (void *)block + sizeof(struct block_meta);
		}
	}

	return NULL;
}

void os_free(void *ptr)
{
	// Verify if the pointer is NULL
	if (ptr == NULL)
		return;
	// Use a pointer to go through the allocated memory with brk
	struct block_meta *current_brk = head_brk;

	//Parse the allocated memory in brk
	while (current_brk != NULL) {
		// Verify if the pointer is the same as the pointer to the current block
		if ((void *)current_brk + sizeof(struct block_meta) == ptr) {
			// Set the status of the current block to free
			current_brk->status = STATUS_FREE;

			coalesce_free_blocks();
			return;
		}

		// Go to the next block
		current_brk = current_brk->next;
	}

	// Use a pointer to go through the allocated memory with mmap
	struct block_meta *current_mmap = head_mmap;


	//Parse the allocated memory in mmap
	while (current_mmap != NULL) {
		// Verify if the pointer is the same as the pointer to the current block
		if ((void *)current_mmap + sizeof(struct block_meta) == ptr) {
			// Set the status of the current block to free
			current_mmap->status = STATUS_FREE;

			// If the current block is the head, set the head to the next block
			if (current_mmap == head_mmap)
				head_mmap = current_mmap->next;

			// If the current block is not the head, set the next of the previous block to the next of the current block
			if (current_mmap->prev != NULL)
				current_mmap->prev->next = current_mmap->next;

			// If the current block is not the last block, set the prev of the next block to the prev of the current block
			if (current_mmap->next != NULL)
				current_mmap->next->prev = current_mmap->prev;

			// Use munmap to free the memory
			munmap(current_mmap, current_mmap->size);

			return;
		}
		current_mmap = current_mmap->next;
	}
}

void *os_calloc(size_t nmemb, size_t size)
{
	size_t total_size = nmemb * size;

	size_t page_size = getpagesize();
	//If the size is 0, return NULL
	if (total_size == 0)
		return NULL;

	// Align the size
	if (total_size % ALIGNMENT != 0)
		total_size += (ALIGNMENT - (total_size % ALIGNMENT));

	//Verify if the first block was allocated and if the size is smaller than the threshold
	if (head_brk == NULL && total_size < page_size - sizeof(struct block_meta)) {
		// Initialize the first block
		void *ptr = sbrk(MMAP_THRESHHOLD);

		DIE(ptr == (void *)-1, "sbrk failed");

		// Initialize the first block
		struct block_meta *block = (struct block_meta *)ptr;

		// Initialize the size and status of the first block
		block->size = total_size + sizeof(struct block_meta);

		// Set the status of the first block to allocated
		block->status = STATUS_ALLOC;

		// Set the prev and next of the first block to NULL
		block->prev = NULL;
		block->next = NULL;

		// Set the head_brk to the first block
		head_brk = block;

		//Verify if in the allocated memory there is enough space for another block
		if (total_size <= MMAP_THRESHHOLD - 2 * sizeof(struct block_meta) - 8) {
			// Initialize the second block
			struct block_meta *second_block = head_brk + total_size + sizeof(struct block_meta);

			// Initialize the size
			second_block->size = MMAP_THRESHHOLD - total_size - sizeof(struct block_meta);

			// Set the status of the second block to free
			second_block->status = STATUS_FREE;

			// Set the prev to the first block
			second_block->prev = head_brk;

			// Set the next to NULL
			second_block->next = NULL;

			// Set the next of the first block to the second block
			head_brk->next = second_block;
		}

		// Set the memory to 0
		memset((void *)block + sizeof(struct block_meta), 0, total_size);
		// Return the pointer to the allocated memory
		return (void *)block + sizeof(struct block_meta); // Return the pointer to the allocated memory
	}

	// Verify if the first block was allocated and if there is enough space for another block
	if (head_brk != NULL && total_size < page_size - sizeof(struct block_meta)) {
		//Coalesce the free blocks
		coalesce_free_blocks();

		// Initialize the current block
		struct block_meta *current = head_brk;

		// Initialize the best fit block
		struct block_meta *best_fit = NULL;

		//Going through the allocated memory to find the best fit block
		while (current != NULL) {
			// Verify if the current block is free and if the size of the block is bigger/equal
			// than the size of the block we want to allocate
			if (current->status == STATUS_FREE && current->size >= total_size + sizeof(struct block_meta)) {
				// Verify if the best fit block is NULL
				if (best_fit == NULL) {
					best_fit = current;
					// Or if the size of the current block is smaller
				} else if (current->size < best_fit->size) {
					best_fit = current;
				}
			}

			// Go to the next block
			current = current->next;
		}

		// Verify if the best fit block exists
		if (best_fit != NULL) {
			// Verify if there is space for another block
			if (best_fit->size >= 2 * sizeof(struct block_meta) + total_size + 8) {
				// Initialize the second block
				struct block_meta *second_block = (struct block_meta *)((void *)best_fit + total_size + sizeof(struct block_meta));

				// Initialize the size of the second block
				second_block->size = best_fit->size - total_size - sizeof(struct block_meta);

				// Set the status of the second block to free
				second_block->status = STATUS_FREE;

				// Set the prev of the second block to the best fit block
				second_block->prev = best_fit;

				// Set the next of the second block to the next of the best fit block
				second_block->next = best_fit->next;

				// Set the next of the best fit block to the second block
				best_fit->next = second_block;

				// Set the size of the best fit block to the size of the block we want to allocate
				best_fit->size = total_size + sizeof(struct block_meta);

				// Set the status of the best fit block to allocated
				best_fit->status = STATUS_ALLOC;

				// Set the memory to 0
				memset((void *)best_fit + sizeof(struct block_meta), 0, total_size);

				// Return the pointer to the allocated memory
				return (void *)best_fit + sizeof(struct block_meta);

			// If there is no space for another block
			} else {
				// Set the status of the best fit block to allocated
				best_fit->status = STATUS_ALLOC;

				// Set the memory to 0
				memset((void *)best_fit + sizeof(struct block_meta), 0, total_size);

				// Return the pointer to the allocated memory
				return (void *)best_fit + sizeof(struct block_meta);
			}

		// If the best_fit block does not exist
		} else {
			// Initialize the current block
			struct block_meta *current = head_brk;

			// Going through the allocated memory to find the last block
			while (current->next != NULL)
				current = current->next;

			// Verify if the last block is free
			if (current->status == STATUS_FREE) {
				// Verify if the size of the last block is smaller than the size of the block we want to allocate
				if (total_size + sizeof(struct block_meta) > current->size) {
					// Use sbrk to allocate memory
					void *ptr = sbrk(total_size + sizeof(struct block_meta) - current->size);

					// Verify if sbrk failed
					DIE(ptr == (void *)-1, "sbrk failed");

					// Set the size of the last block to the size of the block we want to allocate
					current->size = total_size + sizeof(struct block_meta);

					// Set the status of the last block to allocated
					current->status = STATUS_ALLOC;

					// Set the memory to 0
					memset((void *)current + sizeof(struct block_meta), 0, total_size);

					// Return the pointer to the allocated memory
					return (void *)current + sizeof(struct block_meta);
				}
			} else if (current->status == STATUS_ALLOC) {
				// Use sbrk to allocate memory
				void *ptr = sbrk(total_size + sizeof(struct block_meta));

				// Verify if sbrk failed
				DIE(ptr == (void *)-1, "sbrk failed");

				// Initialize the block
				struct block_meta *block = (struct block_meta *)ptr;

				// Initialize the size of the block
				block->size = total_size + sizeof(struct block_meta);

				// Set the status of the block to allocated
				block->status = STATUS_ALLOC;

				// Set the prev of the block to the current block
				block->prev = current;

				// Set the next of the block to
				block->next = current->next;

				// Set the next of the current block to the block
				current->next = block;

				// Set the memory to 0
				memset((void *)block + sizeof(struct block_meta), 0, total_size);

				// Return the pointer to the allocated memory
				return (void *)block + sizeof(struct block_meta);
			}
		}
	}

	// Need to use mmap for allocation
	if (total_size + sizeof(struct block_meta) >= page_size) {
		// Verify if the head is initialized
		if (head_mmap == NULL) {
			// Use mmap to allocate memory
			void *ptr = mmap(NULL, total_size + sizeof(struct block_meta),
							 PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

			// Verify if mmap failed
			DIE(ptr == (void *)-1, "mmap failed");

			// Initialize the block
			struct block_meta *block = (struct block_meta *)ptr;

			// Initialize the size of the block
			block->size = total_size + sizeof(struct block_meta);

			// Set the status of the block to mapped
			block->status = STATUS_MAPPED;

			// Set the prev of the block to NULL
			block->prev = NULL;

			// Set the next of the block to NULL
			block->next = NULL;

			// Set the head_mmap to the block
			head_mmap = block;

			// Set the memory to 0
			memset((void *)block + sizeof(struct block_meta), 0, total_size);

			// Return the pointer to the allocated memory
			return (void *)block + sizeof(struct block_meta);

		// If there is at least one block of memory mapped
		} else if (head_mmap != NULL) {
			// Use mmap to allocate memory
			void *ptr = mmap(NULL, total_size + sizeof(struct block_meta),
							 PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

			// Verify if mmap failed
			DIE(ptr == (void *)-1, "mmap failed");

			// Initialize the block
			struct block_meta *block = (struct block_meta *)ptr;

			// Initialize the size of the block
			block->size = total_size + sizeof(struct block_meta);

			// Set the status of the block to mapped
			block->status = STATUS_MAPPED;

			// Go to the last block
			struct block_meta *current = head_mmap;

			while (current->next != NULL)
				current = current->next;

			// Set the prev of the block to the current block
			block->prev = current;

			// Set the next of the block to NULL
			block->next = NULL;

			// Set the next of the current block to the block
			current->next = block;

			// Set the memory to 0
			memset((void *)block + sizeof(struct block_meta), 0, total_size);

			// Return the pointer to the allocated memory
			return (void *)block + sizeof(struct block_meta);
		}
	}
	return NULL;
}

void *os_realloc(void *ptr, size_t size)
{
	//Verify if the pointer is NULL
	if (ptr == NULL)
		return os_malloc(size);

	//Verify if the size is 0
	if (size == 0) {
		os_free(ptr);
		return NULL;
	}

	// Align the size
	if (size % ALIGNMENT != 0)
		size += (ALIGNMENT - (size % ALIGNMENT));

	struct block_meta *old_ptr = (struct block_meta *)(ptr - sizeof(struct block_meta));

	size_t old_size = old_ptr->size - sizeof(struct block_meta);

	size_t copy_size = min(old_size, size);

	struct block_meta *current = head_brk;

	// Use a pointer to go through the allocated memory with brk
	current = head_brk;

	// Parse the allocated memory in brk
	while (current != NULL) {
		// Verify if the pointer is the same as the pointer to the current block
		if (current == old_ptr) {
			if (current->status == STATUS_FREE)
				return NULL;

			// Verify if the size of the current block is bigger/equal than the size of the block we want to allocate
			if (current->size >= size + sizeof(struct block_meta)) {
				// Verify if there is space for another block
				if (current->size >= 2 * sizeof(struct block_meta) + size + 8) {
					// Initialize the second block
					struct block_meta *second_block = (struct block_meta *)((void *)current + size + sizeof(struct block_meta));

					// Initialize the size of the second block
					second_block->size = current->size - size - sizeof(struct block_meta);

					// Set the status of the second block to free
					second_block->status = STATUS_FREE;

					// Set the prev of the second block to the current block
					second_block->prev = current;

					// Set the next of the second block to the next of the current block
					second_block->next = current->next;

					if (current->next != NULL)
						current->next->prev = second_block;

					// Set the next of the current block to the second block
					current->next = second_block;

					// Set the size of the current block to the size of the block we want to allocate
					current->size = size + sizeof(struct block_meta);

					// Set the status of the current block to allocated
					current->status = STATUS_ALLOC;

					// Return the pointer to the allocated memory
					return (void *)current + sizeof(struct block_meta);

				// If there is no space for another block
				} else {
					// Set the status of the current block to allocated
					current->status = STATUS_ALLOC;

					// Return the pointer to the allocated memory
					return (void *)current + sizeof(struct block_meta);
				}
			} else if (current->next != NULL) {
				// Verify if the next block is free
				if (current->next->status == STATUS_FREE) {
					// Verify if the size of the current block and the next block is bigger/equal
					// than the size of the block we want to allocate
					return verify_size(current, size, ptr, copy_size);

				} else {
					void *new_ptr = os_malloc(size);

					memcpy(new_ptr, ptr, copy_size + sizeof(struct block_meta));

					os_free(ptr);

					return new_ptr;
				}

			} else {
				void *new_ptr = sbrk(size + sizeof(struct block_meta) - current->size);

				// Verify if sbrk failed
				DIE(new_ptr == (void *)-1, "sbrk failed");

				// Set the size of the current block to the size of the block we want to allocate
				current->size = size + sizeof(struct block_meta);

				// Set the status of the current block to allocated
				current->status = STATUS_ALLOC;

				// Return the pointer to the allocated memory
				return (void *)current + sizeof(struct block_meta);
			}
		}
		current = current->next;
	}

	// Use a pointer to go through the allocated memory with mmap
	struct block_meta *current_mmap = head_mmap;

	while (current_mmap != NULL) {
		if (current_mmap == old_ptr) {
			void *new_ptr = os_malloc(size);

			memcpy(new_ptr, ptr, copy_size);

			os_free(ptr);

			return new_ptr;
		}
		current_mmap = current_mmap->next;
	}

	return NULL;
}
