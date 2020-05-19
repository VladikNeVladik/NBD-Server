// No Copyright. Vladislav Aleinik 2020
//===================================================================
// IO-Ring Management
//===================================================================
// - IO-userspace-ring Setup
// - Submission of IO-requests
// - Completion of IO-requests
//===================================================================
#ifndef NBD_SERVER_IO_USERSPACE_RING_H_INCLUDED
#define NBD_SERVER_IO_USERSPACE_RING_H_INCLUDED

// IO-userspace-ring
#include "vendor/io_uring.h"
// Everything:
#include <stdlib.h>
// sched_yield():
#include <sched.h>
// memset():
#include <string.h>
// close():
#include <unistd.h>
// syscall()
#include <sys/syscall.h>
// mmap()
#include <sys/mman.h>

#include "Logging.h"

//========================
// Constants And Typedefs
//========================

typedef char bool;

#define READ_BLOCK_SIZE (uint32_t) 4096

const long NR_io_uring_setup    = 425;
const long NR_io_uring_enter    = 426;
const long NR_io_uring_register = 427;

//=================
// Data Structures
//=================

struct IO_RingSQ
{
	unsigned* head;
	unsigned* tail;
	unsigned* ring_mask;
	unsigned* ring_entries;
	unsigned* flags;
	unsigned* dropped;

	struct io_uring_sqe* sq_entries;
	uint32_t* sq_ring;
};

struct IO_RingCQ
{
	unsigned* head;
	unsigned* tail;
	unsigned* ring_mask;
	unsigned* ring_entries;
	unsigned* overflow;
	unsigned* cqes;

	struct io_uring_cqe* cq_ring;
};

struct IO_Ring
{
	int fd;

	struct IO_RingSQ sq;
	struct IO_RingCQ cq;
};

struct IO_Request
{
	bool empty;

	uint32_t mother_cell;

	int fd;
	uint8_t  opcode;
	uint64_t offset;
	uint32_t length;
	uint32_t error;

	char buffer[READ_BLOCK_SIZE];
};

//=========================
// Init, Register And Free
//=========================

void init_io_ring(struct IO_Ring* io_ring, uint32_t num_entries)
{
	// Set IO-userspace-ring parameters to defaults:
	struct io_uring_params params;
	memset(&params, 0, sizeof(params));

	// Geta an IO-ring:
	int io_ring_fd = syscall(NR_io_uring_setup, num_entries, &params);
	if (io_ring_fd == -1)
	{
		LOG_ERROR("[init_io_ring] Unable to setup IO-ring");
		exit(EXIT_FAILURE);
	}

	io_ring->fd = io_ring_fd;

	// Map IO-ring submission queue:
	void* sq_ring_ptr = mmap(NULL, params.sq_off.array + num_entries * sizeof(uint32_t),
	                         PROT_READ|PROT_WRITE, MAP_SHARED|MAP_POPULATE,
	                         io_ring_fd, IORING_OFF_SQ_RING);
	if (sq_ring_ptr == MAP_FAILED)
	{
		LOG_ERROR("[init_io_ring] Unable to map IO-ring submission queue to userspace");
		exit(EXIT_FAILURE);
	}

	io_ring->sq.sq_ring = sq_ring_ptr;

	// Map submission entry array:
	void* sq_entries_ptr = mmap(NULL, num_entries * sizeof(struct io_uring_sqe),
	                            PROT_READ|PROT_WRITE, MAP_SHARED|MAP_POPULATE,
	                            io_ring_fd, IORING_OFF_SQES);
	if (sq_entries_ptr == MAP_FAILED)
	{
		LOG_ERROR("[init_io_ring] Unable to map IO-ring entry array to userspace");
		exit(EXIT_FAILURE);
	}

	io_ring->sq.sq_entries = sq_entries_ptr;

	// Map IO-ring completion queue:
	void* cq_ring_ptr = mmap(NULL, params.cq_off.cqes + num_entries * sizeof(struct io_uring_cqe),
	                       PROT_READ|PROT_WRITE, MAP_SHARED|MAP_POPULATE,
	                       io_ring_fd, IORING_OFF_CQ_RING);
	if (cq_ring_ptr == MAP_FAILED)
	{
		LOG_ERROR("[init_io_ring] Unable to map IO-ring completion queue to userspace");
		exit(EXIT_FAILURE);
	}

	io_ring->cq.cq_ring = cq_ring_ptr;

	// Calculate metainfo pointers:
	io_ring->sq.head         = sq_ring_ptr + params.sq_off.head;
	io_ring->sq.tail         = sq_ring_ptr + params.sq_off.tail;
	io_ring->sq.ring_mask    = sq_ring_ptr + params.sq_off.ring_mask;
	io_ring->sq.ring_entries = sq_ring_ptr + params.sq_off.ring_entries;
	io_ring->sq.flags        = sq_ring_ptr + params.sq_off.flags;
	io_ring->sq.dropped      = sq_ring_ptr + params.sq_off.dropped;

	io_ring->cq.head         = cq_ring_ptr + params.cq_off.head;
	io_ring->cq.tail         = cq_ring_ptr + params.cq_off.tail;
	io_ring->cq.ring_mask    = cq_ring_ptr + params.cq_off.ring_mask;
	io_ring->cq.ring_entries = cq_ring_ptr + params.cq_off.ring_entries;
	io_ring->cq.overflow     = cq_ring_ptr + params.cq_off.overflow;
	io_ring->cq.cqes         = cq_ring_ptr + params.cq_off.cqes;

	LOG("IO-userspace-ring initialised");
}

void register_io_buffers(struct IO_Ring* io_ring, struct iovec* buffers, uint32_t num_buffers)
{
	if (syscall(NR_io_uring_register, io_ring->fd, IORING_REGISTER_BUFFERS, buffers, num_buffers) == -1)
	{
		LOG_ERROR("[register_io_buffers] Unable to register IO buffers");
		exit(EXIT_FAILURE);
	}

	LOG("Some IO-buffers registered");
}

void free_io_ring(struct IO_Ring* io_ring)
{
	if (close(io_ring->fd) == -1)
	{
		LOG_ERROR("[free_io_ring] Unable to close IORing");
		exit(EXIT_FAILURE);
	}

	LOG("IO-userspace-ring freed");
}

//===============
// IO Submission
//===============

void submit_io_request(struct IO_Ring* io_ring, struct IO_Request* io_reqs, uint32_t io_req_cell)
{
	// Configure the IO submit operation:
	io_ring->sq.sq_entries[io_req_cell].opcode    =           io_reqs[io_req_cell].opcode;
	io_ring->sq.sq_entries[io_req_cell].flags     =           0;
	io_ring->sq.sq_entries[io_req_cell].ioprio    =           2 /*IOPRIO_CLASS_BE*/;
	io_ring->sq.sq_entries[io_req_cell].fd        =           io_reqs[io_req_cell].fd;
	io_ring->sq.sq_entries[io_req_cell].off       =           io_reqs[io_req_cell].offset;
	io_ring->sq.sq_entries[io_req_cell].addr      = (int64_t) io_reqs[io_req_cell].buffer;
	io_ring->sq.sq_entries[io_req_cell].len       =           io_reqs[io_req_cell].length;
	io_ring->sq.sq_entries[io_req_cell].user_data =           io_req_cell;

	io_ring->sq.sq_ring[*io_ring->sq.tail & *io_ring->sq.ring_mask] = io_req_cell;

	// ToDo: totally need a memory barrier here
	// It's a miracle that the program works without it!

	*io_ring->sq.tail += 1;

	int ios_submitted = syscall(NR_io_uring_enter, io_ring->fd, 1, 0, NULL);
	if (ios_submitted != 1)
	{
		LOG_ERROR("[submit_io_request] Unable to submit request to IO-ring submission queue");
		exit(EXIT_FAILURE);
	}

	LOG("IO-request on cell#%03u submitted: {off=%lu, len=%u, mother=%u}",
	    io_req_cell, io_reqs[io_req_cell].offset, io_reqs[io_req_cell].length, io_reqs[io_req_cell].mother_cell);
}

//================
// IO Completeion
//================

uint32_t get_io_request(struct IO_Ring* io_ring, struct IO_Request* io_reqs)
{
	// Wait for data to arrive (so sad you cannot block with io_uring):
	while (*io_ring->cq.head == *io_ring->cq.tail)
	{
		if (sched_yield() == -1)
		{
			LOG_ERROR("[get_io_request] Unable to yield the processor");
			exit(EXIT_FAILURE);
		}
	}

	// ToDO: yet another read barrier
	// Yet another miracle!

	uint32_t io_req_cell = io_ring->cq.cq_ring[*io_ring->cq.head & *io_ring->cq.ring_mask].user_data;

	if (io_ring->cq.cq_ring[*io_ring->cq.head & *io_ring->cq.ring_mask].res == -1)
	{
		LOG("An error occured during request on cell#%03u", io_req_cell);
		io_reqs[io_req_cell].error = NBD_EINVAL;
	}

	LOG("IO-request on cell#%03u is complete", io_req_cell);

	return io_req_cell;
}

#endif // NBD_SERVER_IO_USERSPACE_RING_H_INCLUDED
