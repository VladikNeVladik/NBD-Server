// No Copyright. Vladislav Aleinik 2020
//===================================================================
// NBD-Server IO Request Management
//===================================================================
// Pending IO table management
//===================================================================
#ifndef NBD_SERVER_IO_REQUEST_H_INCLUDED
#define NBD_SERVER_IO_REQUEST_H_INCLUDED

#include "IO_Ring.h"

#include <semaphore.h>
#include <malloc.h>
#include <errno.h>

//========================
// Constants And Typedefs 
//========================

typedef char bool;

const size_t   MAX_IO_REQUESTS =   64;
const uint32_t READ_BLOCK_SIZE = 4096;

//=================
// Data Structures
//=================

struct IO_RequestTable
{
	struct IO_Request* io_reqs;

	sem_t sem;

	struct IO_Ring io_ring;

	uint32_t first_free;
};

//==============
// Init && Free 
//==============

void init_io_table(struct IO_RequestTable* io_table, int export_fd)
{
	// Init the IO-ring first:
	init_io_ring(&io_table->io_ring, MAX_IO_REQUESTS);

	// Allocate IO request table:
	io_table->io_reqs = (struct IO_Request*) malloc(MAX_IO_REQUESTS * sizeof(*io_table->io_reqs));
	if (io_table->io_reqs == NULL)
	{
		LOG_ERROR("[init_io_table] Unable to allocate memory for IO request table");
		exit(EXIT_FAILURE);
	}

	// Allocate alligned memory for buffers:
	char* aligned_buffers = (char*) aligned_alloc(READ_BLOCK_SIZE, MAX_IO_REQUESTS * READ_BLOCK_SIZE);
	if (aligned_buffers == NULL)
	{
		LOG_ERROR("[init_io_table] Unable to allocate aligned momory");
		exit(EXIT_FAILURE);
	}

	// Initialise and register all the IO-buffers:
	struct iovec* iovecs = (struct iovec*) malloc(MAX_IO_REQUESTS * sizeof(*iovecs));
	if (iovecs == NULL)
	{
		LOG_ERROR("[init_io_table] Unable to allocate iovec array");
		exit(EXIT_FAILURE);
	}

	for (uint32_t i = 0; i < MAX_IO_REQUESTS; ++i)
	{
		io_table->io_reqs[i].empty  = 1;
		io_table->io_reqs[i].cell   = i;
		io_table->io_reqs[i].buffer = &aligned_buffers[i * READ_BLOCK_SIZE];

		iovecs[i].iov_base = io_table->io_reqs[i].buffer;
		iovecs[i].iov_len  = READ_BLOCK_SIZE;
	}

	register_io_buffers(&io_table->io_ring, iovecs, MAX_IO_REQUESTS);

	free(iovecs);

	// Register the export file for IO-ring:
	register_files(&io_table->io_ring, &export_fd, 1);

	// Create a cell-guarding semaphore:
	if (sem_init(&io_table->sem, 0, MAX_IO_REQUESTS) == -1)
	{
		LOG_ERROR("[init_io_table] Unable to initialise semaphore");
		exit(EXIT_FAILURE);
	}

	// Set the first free cell:
	io_table->first_free = 0;

	LOG("Initialised IO-request table");
}

void free_io_table(struct IO_RequestTable* io_table)
{
	// Free IO-ring:
	free_io_ring(&io_table->io_ring);

	// Free memory:
	free(io_table->io_reqs[0].buffer);
	free(io_table->io_reqs);

	// Destroy semaphore:
	if (sem_destroy(&io_table->sem) == -1)
	{
		LOG_ERROR("[free_io_table] Unable to destroy semaphore");
		exit(EXIT_FAILURE);
	}

	LOG("Freed IO-request table");
}

//=================
// Cell Management
//=================

static uint32_t search_io_req_cell(struct IO_RequestTable* io_table)
{
	uint32_t cell = -1;
	for (uint32_t i = io_table->first_free; i < MAX_IO_REQUESTS; ++i)
	{
		if (io_table->io_reqs[i].empty)
		{
			io_table->io_reqs[i].empty = 0;

			cell = i;
			break;
		}
	}

	if (cell == -1)
	{
		for (uint32_t i = 0; i < io_table->first_free; ++i)
		{
			if (io_table->io_reqs[i].empty)
			{
				io_table->io_reqs[i].empty = 0;

				cell = i;
				break;
			}
		}
	}
	
	BUG_ON(cell == -1, "[seacrh_for_io_cell] Semaphore unlocked when shouldn't");

	io_table->first_free = cell + 1;

	return cell;
}

uint32_t get_io_req_cell(struct IO_RequestTable* io_table, uint32_t mother_cell)
{
	if (sem_wait(&io_table->sem) == -1)
	{
		LOG_ERROR("[get_io_req_cell] Unable to down a semaphore");
		exit(EXIT_FAILURE);
	}

	// Aquire a cell:
	uint32_t cell = search_io_req_cell(io_table);
	
	// Save corresponding nbd cell:
	io_table->io_reqs[cell].mother_cell = mother_cell;

	LOG("IO-request cell#%03u occupied", cell);

	return cell;
}

// The same as "get_io_req_cell", but instead of blocking it returns -1
uint32_t tryget_io_req_cell(struct IO_RequestTable* io_table, uint32_t mother_cell)
{
	if (sem_trywait(&io_table->sem) == -1)
	{
		if (errno == EAGAIN)
		{
			return -1;
		}

		LOG_ERROR("[tryget_io_req_cell] Unable to down a semaphore");
		exit(EXIT_FAILURE);	
	}

	// Aquire a cell:
	uint32_t cell = search_io_req_cell(io_table);

	// Save corresponding nbd cell:
	io_table->io_reqs[cell].mother_cell = mother_cell;

	LOG("IO-request cell#%03u occupied", cell);

	return cell;
}

void free_io_req_cell(struct IO_RequestTable* io_table, uint32_t io_req_cell)
{
	BUG_ON(io_req_cell >= MAX_IO_REQUESTS, "[free_io_req_cell] Invalid IO-cell");

	io_table->io_reqs[io_req_cell].empty = 1;

	// Free cell:
	if (sem_post(&io_table->sem) == -1)
	{
		LOG_ERROR("[get_io_req_cell] Unable to up a semaphore");
		exit(EXIT_FAILURE);
	}

	LOG("IO-request cell#%03u free", io_req_cell);
}

//===============
// IO Completion
//===============

uint32_t get_io_request(struct IO_RequestTable* io_table)
{
	struct IO_Ring* io_ring = &io_table->io_ring;

	uint32_t io_req_cell = wait_for_io_completion(io_ring);

	BUG_ON(io_req_cell >= MAX_IO_REQUESTS, "[get_io_request] Invalid IO-cell");

	if (io_ring->cq.cq_ring[*io_ring->cq.head & *io_ring->cq.ring_mask].res == -1)
	{
		LOG("An error occured during request on cell#%03u", io_req_cell);
		io_table->io_reqs[io_req_cell].error = NBD_EINVAL;
	}

	LOG("IO-request on cell#%03u is complete", io_req_cell);

	return io_req_cell;
}

#endif // NBD_SERVER_IO_REQUEST_H_INCLUDED