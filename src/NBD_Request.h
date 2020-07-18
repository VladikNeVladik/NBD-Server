// No Copyright. Vladislav Aleinik 2020
//===================================================================
// NBD-Server NBD Request Management
//===================================================================
// NBD request management
//===================================================================
#ifndef NBD_SERVER_NBD_REQUEST_H_INCLUDED
#define NBD_SERVER_NBD_REQUEST_H_INCLUDED

#include "IO_Request.h"

#include <semaphore.h>

//=================
// Data Structures
//=================

const size_t MAX_NBD_REQUESTS = 16;

struct NBD_Request
{
	bool empty;

	uint32_t error;

	uint16_t type;
	uint64_t handle;
	uint64_t offset;
	uint32_t length;

	size_t io_reqs_pending;
};

struct NBD_RequestTable
{
	struct NBD_Request* nbd_reqs;

	sem_t sem;
};

//==============
// Init && Free
//==============

void init_nbd_table(struct NBD_RequestTable* nbd_table)
{
	// Allocate NBD request table:
	nbd_table->nbd_reqs = (struct NBD_Request*) malloc(MAX_NBD_REQUESTS * sizeof(*nbd_table->nbd_reqs));
	if (nbd_table->nbd_reqs == NULL)
	{
		LOG_ERROR("[init_nbd_table] Unable to allocate memory for NBD request table");
		exit(EXIT_FAILURE);
	}

	for (uint32_t i = 0; i < MAX_NBD_REQUESTS; ++i)
	{
		nbd_table->nbd_reqs[i].empty           = 1;
		nbd_table->nbd_reqs[i].io_reqs_pending = 0;
	}

	// Create a cell-guarding semaphore:
	if (sem_init(&nbd_table->sem, 0, MAX_NBD_REQUESTS) == -1)
	{
		LOG_ERROR("[init_nbd_table] Unable to initialise semaphore");
		exit(EXIT_FAILURE);
	}

	LOG("Initialised NBD-request table");
}

void free_nbd_table(struct NBD_RequestTable* nbd_table)
{
	free(nbd_table->nbd_reqs);

	// Destroy semaphore:
	if (sem_destroy(&nbd_table->sem) == -1)
	{
		LOG_ERROR("[free_nbd_table] Unable to destroy semaphore");
		exit(EXIT_FAILURE);
	}
}

//=================
// Cell Management
//=================

uint32_t get_nbd_req_cell(struct NBD_RequestTable* nbd_table)
{
	if (sem_wait(&nbd_table->sem) == -1)
	{
		LOG_ERROR("[get_nbd_req_cell] Unable to down a semaphore");
		exit(EXIT_FAILURE);
	}

	// Aquire a cell:
	uint32_t cell = -1;
	for (uint32_t i = 0; i < MAX_NBD_REQUESTS; ++i)
	{
		if (nbd_table->nbd_reqs[i].empty)
		{
			nbd_table->nbd_reqs[i].empty = 0;

			cell = i;
			break;
		}
	}

	BUG_ON(cell == -1, "[get_nbd_req_cell] Semaphore unlocked when shouldn't");

	LOG("NBD-request cell#%03u occupied", cell);

	return cell;
}

void free_nbd_req_cell(struct NBD_RequestTable* nbd_table, uint32_t nbd_req_cell)
{
	nbd_table->nbd_reqs[nbd_req_cell].empty = 1;

	// Free cell:
	if (sem_post(&nbd_table->sem) == -1)
	{
		LOG_ERROR("[free_nbd_req_cell] Unable to down a semaphore");
		exit(EXIT_FAILURE);
	}

	LOG("NBD-request cell#%03u free", nbd_req_cell);
}

bool no_infly_nbd_reqs(struct NBD_RequestTable* nbd_table)
{
	int sem_value;
	if (sem_getvalue(&nbd_table->sem, &sem_value) == -1)
	{
		LOG_ERROR("[free_nbd_req_cell] Unable to get a semaphore value");
		exit(EXIT_FAILURE);
	}

	// Note:
	// The actual value of the semaphore may be altered after the sem_getvalue() call
	return sem_value == MAX_NBD_REQUESTS;
}

//============
// Submission 
//============

void submit_nbd_request(struct IO_RequestTable* io_table, struct NBD_RequestTable* nbd_table, uint32_t nbd_cell)
{
	struct NBD_Request* nbd_req = &nbd_table->nbd_reqs[nbd_cell];

	if (nbd_req->error || nbd_req->type == NBD_CMD_DISC)
	{
		// In case of NBD_CMD_DISC it makes sure that the recv-thread wakes up:
		uint32_t io_cell = get_io_req_cell(io_table, nbd_cell);
		struct IO_Request* io_req = &io_table->io_reqs[io_cell];

		io_req->mother_cell = nbd_cell;
		io_req->opcode      = IORING_OP_NOP;
		io_req->offset      = -1;
		io_req->length      = nbd_req->length;
		io_req->error       = nbd_req->error;

		nbd_req->io_reqs_pending = 1;

		submit_io_request(&io_table->io_ring, io_req, io_cell);
	}
	else if (nbd_req->type == NBD_CMD_READ)
	{
		nbd_req->io_reqs_pending = nbd_req->length / READ_BLOCK_SIZE;
		if (nbd_req->length % READ_BLOCK_SIZE != 0)
		{
			nbd_req->io_reqs_pending += 1;
		}

		uint64_t off = nbd_req->offset;
		uint32_t len = nbd_req->length;

		while (1)
		{
			uint32_t io_cell = get_io_req_cell(io_table, nbd_cell);
			struct IO_Request* io_req = &io_table->io_reqs[io_cell];

			io_req->mother_cell = nbd_cell;
			io_req->opcode      = IORING_OP_READ_FIXED;
			io_req->offset      = off;
			io_req->length      = (len <= READ_BLOCK_SIZE)? len : READ_BLOCK_SIZE;
			io_req->error       = nbd_req->error;

			submit_io_request(&io_table->io_ring, io_req, io_cell);

			if (len <= READ_BLOCK_SIZE) break;

			off += READ_BLOCK_SIZE;
			len -= READ_BLOCK_SIZE;
		}
	}
	else
	{
		BUG_ON(1, "[submit_nbd_request] Forbidden request type");
	}

	LOG("Submitted NBD-request on cell#%03u", nbd_cell);
}

#endif // NBD_SERVER_NBD_REQUEST_H_INCLUDED