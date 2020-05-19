// No Copyright. Vladislav Aleinik 2020
//===================================================================
// NBD-Server Request Management
//===================================================================
// All of the folowing is implemented in this file 
// - Request table management
// - Pending IO table management
//===================================================================
#ifndef NBD_SERVER_REQUEST_MANAGEMENT_H_INCLUDED
#define NBD_SERVER_REQUEST_MANAGEMENT_H_INCLUDED

// System V semaphores:
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/sem.h>

#include "IO_Ring.h"

//===========
// Constants
//===========

const size_t MAX_NBD_REQUESTS = 16;
const size_t MAX_IO_REQUESTS  = 256;

//=================
// Data Structures
//=================

struct IO_RequestTable
{
	struct IO_Request* io_reqs;
	uint32_t num_pending_io_reqs;

	int semset_id;

	struct IO_Ring io_ring;
};

struct NBD_Request
{
	bool empty;

	uint32_t error;

	int fd;
	uint16_t type;
	uint64_t handle;
	uint64_t offset;
	uint32_t length;

	size_t io_reqs_pending;
};

struct NBD_RequestTable
{
	struct NBD_Request* nbd_reqs;
	uint32_t num_pending_nbd_reqs;

	int semset_id;
};

enum {
	SEM_NUM_FREE_IO_CELLS   = 0,
	SEM_NUM_FREE_NBD_CELLS  = 1
};

//=====================
// Init && Free Tables
//=====================

void init_req_tables(struct IO_RequestTable* io_table, struct NBD_RequestTable* nbd_table)
{
	// Init the IO-table's IO-ring first:
	init_io_ring(&io_table->io_ring, MAX_IO_REQUESTS);

	// Allocate IO request table:
	io_table->io_reqs = (struct IO_Request*) calloc(MAX_IO_REQUESTS, sizeof(*io_table->io_reqs));
	if (io_table->io_reqs == NULL)
	{
		LOG_ERROR("[init_req_tables] Unable to allocate memory for IO request table");
		exit(EXIT_FAILURE);
	}

	// Initialise and register all the IO-buffers:
	struct iovec* iovecs = (struct iovec*) calloc(MAX_IO_REQUESTS, sizeof(*iovecs));
	if (iovecs == NULL)
	{
		LOG_ERROR("[init_req_tables] Unable to allocate iovec array");
		exit(EXIT_FAILURE);
	}

	for (uint32_t i = 0; i < MAX_IO_REQUESTS; ++i)
	{
		io_table->io_reqs[i].empty = 1;

		iovecs[i].iov_base = io_table->io_reqs[i].buffer;
		iovecs[i].iov_len  = READ_BLOCK_SIZE;
	}

	register_io_buffers(&io_table->io_ring, iovecs, MAX_IO_REQUESTS);

	// Allocate NBD request table:
	nbd_table->nbd_reqs = (struct NBD_Request*) calloc(MAX_NBD_REQUESTS, sizeof(*nbd_table->nbd_reqs));
	if (nbd_table->nbd_reqs == NULL)
	{
		LOG_ERROR("[init_req_tables] Unable to allocate memory for NBD request table");
		exit(EXIT_FAILURE);
	}

	for (uint32_t i = 0; i < MAX_NBD_REQUESTS; ++i)
	{
		nbd_table->nbd_reqs[i].empty = 1;
	}

	// Setup a semaphore set:
	const char* KEYSEED_FILE = "/var/tmp/nbd-server-keyseed-file";

	int semset_key = ftok(KEYSEED_FILE, 0);
	if (semset_key == -1)
	{
		LOG_ERROR("[init_req_tables] Unable to get semaphore key out of the keyseed file");
		exit(EXIT_FAILURE);
	}

	int semset_id = semget(semset_key, 2, IPC_CREAT|0600);
	if (semset_id == -1)
	{
		LOG_ERROR("[init_req_tables] Unable to allocate shared memory");
		exit(EXIT_FAILURE);
	}

	io_table ->semset_id = semset_id;
	nbd_table->semset_id = semset_id;	

	// Transactionally set semaphore set initial values:
	struct sembuf set_initial_values[2] =
	{
		{SEM_NUM_FREE_IO_CELLS , MAX_IO_REQUESTS , 0},
		{SEM_NUM_FREE_NBD_CELLS, MAX_NBD_REQUESTS, 0}
	};

	if (semop(semset_id, set_initial_values, 2) == -1)
	{
		LOG_ERROR("[init_req_tables] Unable to set semaphore initial values");
		exit(EXIT_FAILURE);
	}

	LOG("Initialised NBD- and IO-request tables");
}

void free_req_tables(struct IO_RequestTable* io_table, struct NBD_RequestTable* nbd_table)
{
	// Free IO-ring:
	free_io_ring(&io_table->io_ring);

	// Free memory:
	free(io_table->io_reqs);
	free(nbd_table->nbd_reqs);

	// Close semaphore:
	if (semctl(io_table->semset_id, 2, IPC_RMID) == -1)
	{
		LOG_ERROR("[free_req_tables] Unable to delete semaphore set");
		exit(EXIT_FAILURE);
	}

	LOG("NBD- and IO-request tables freed");
}

//=============================
// Pending IO Table Management
//=============================

uint32_t get_io_req_cell(struct IO_RequestTable* io_table, uint32_t mother_cell)
{
	// Block until a cell gets free:
	struct sembuf occupy_cell[1] =
	{
		{SEM_NUM_FREE_IO_CELLS, -1, 0}
	};
	if (semop(io_table->semset_id, occupy_cell, 1) == -1)
	{
		fprintf(stderr, "[get_io_req_cell] Unable to acquire cell");
		exit(EXIT_FAILURE);
	}

	// Aquire a cell:
	uint32_t cell = -1;
	for (uint32_t i = 0; i < MAX_IO_REQUESTS; ++i)
	{
		if (io_table->io_reqs[i].empty)
		{
			io_table->io_reqs[i].empty = 0;

			cell = i;
			break;
		}
	}

	BUG_ON(cell == -1, "[get_io_req_cell] Semaphore unlocked when shouldn't");

	io_table->io_reqs[cell].mother_cell = mother_cell;

	LOG("IO-request cell#%03u occupied", cell);

	return cell;
}

void free_io_req_cell(struct IO_RequestTable* io_table, uint32_t io_req_cell)
{
	io_table->io_reqs[io_req_cell].empty = 1;

	// Free cell:
	struct sembuf free_cell[1] =
	{
		{SEM_NUM_FREE_IO_CELLS, +1, 0}
	};
	if (semop(io_table->semset_id, free_cell, 1) == -1)
	{
		fprintf(stderr, "[free_io_req_cell] Unable to free cell");
		exit(EXIT_FAILURE);
	}

	LOG("IO-request cell#%03u free", io_req_cell);
}

//==============================
// NBD Request Table Management
//==============================

uint32_t get_nbd_req_cell(struct NBD_RequestTable* nbd_table)
{
	// Block until a cell gets free:
	struct sembuf occupy_cell[1] =
	{
		{SEM_NUM_FREE_NBD_CELLS, -1, 0}
	};
	if (semop(nbd_table->semset_id, occupy_cell, 1) == -1)
	{
		fprintf(stderr, "[get_nbd_req_cell] Unable to acquire cell");
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
	struct sembuf free_cell[1] =
	{
		{SEM_NUM_FREE_IO_CELLS, +1, 0}
	};
	if (semop(nbd_table->semset_id, free_cell, 1) == -1)
	{
		fprintf(stderr, "[free_nbd_req_cell] Unable to free cell");
		exit(EXIT_FAILURE);
	}

	LOG("NBD-request cell#%03u free", nbd_req_cell);
}

//========================
// NBD Request Submission
//========================

void submit_nbd_request(struct IO_RequestTable* io_table, struct NBD_RequestTable* nbd_table, uint32_t nbd_cell)
{
	struct NBD_Request* nbd_req = &nbd_table->nbd_reqs[nbd_cell];

	if (nbd_req->error || nbd_req->type == NBD_CMD_DISC)
	{
		// In case of NBD_CMD_DISC it makes sure that the recv-thread wakes up
		nbd_req->io_reqs_pending = 1;

		uint32_t io_cell = get_io_req_cell(io_table, nbd_cell);
		struct IO_Request* io_req = &io_table->io_reqs[io_cell];

		io_req->mother_cell = nbd_cell;
		io_req->fd          = 0;
		io_req->opcode      = IORING_OP_NOP;
		io_req->offset      = 0;
		io_req->length      = nbd_req->length;
		io_req->error       = nbd_req->error;

		submit_io_request(&io_table->io_ring, io_req, io_cell);
	}
	else if (nbd_req->type == NBD_CMD_READ)
	{
		nbd_req->io_reqs_pending = nbd_req->length / READ_BLOCK_SIZE + (nbd_req->length % READ_BLOCK_SIZE != 0);

		uint64_t off = nbd_req->offset;
		uint32_t len = nbd_req->length;
		
		while (1)
		{
			uint32_t io_cell = get_io_req_cell(io_table, nbd_cell);
			struct IO_Request* io_req = &io_table->io_reqs[io_cell];

			io_req->mother_cell = nbd_cell;
			io_req->fd          = nbd_req->fd;
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

//========================
// IO Request Completion
//========================

uint32_t complete_io_request(struct IO_RequestTable* io_table)
{
	return get_io_request(&io_table->io_ring, io_table->io_reqs);
}

#endif // NBD_SERVER_REQUEST_MANAGEMENT_H_INCLUDED
