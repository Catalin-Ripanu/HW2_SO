/* SPDX-License-Identifier: BSD-3-Clause */

#pragma once

#include <errno.h>
#include <limits.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#define DIE(assertion, call_description)                       \
	do                                                         \
	{                                                          \
		if (assertion)                                         \
		{                                                      \
			fprintf(stderr, "(%s, %d): ", __FILE__, __LINE__); \
			perror(call_description);                          \
			exit(errno);                                       \
		}                                                      \
	} while (0)

/* Structure to hold memory block metadata */
struct block_meta
{
	size_t size;
	int status;
	struct block_meta *next;
};

struct block_meta *first_free_block;
struct block_meta *last_free_block;
int no_brk_alloc = 0;
int no_mmap_alloc = 0;

/* Block metadata status values */
#define STATUS_FREE 0
#define STATUS_ALLOC 1
#define STATUS_MAPPED 2
#define MMAP_TH 128 * 1024
#define ALIGNMENT 8
#define ALIGN(p) (((size_t)(p) + (ALIGNMENT - 1)) & ~0x7)
#define PAGE_SIZE 4 * 1024