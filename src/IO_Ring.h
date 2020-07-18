// No Copyright. Vladislav Aleinik 2020
//======================================
// IO-Ring Management
//======================================
// - IO-userspace-ring Setup
// - Submission of IO-requests
// - Completion of IO-requests
//======================================
#ifndef NBD_SERVER_IO_USERSPACE_RING_H_INCLUDED
#define NBD_SERVER_IO_USERSPACE_RING_H_INCLUDED

// Everything:
#include <stdlib.h>
// sched_yield():
#include <sched.h>
// memset():
#include <string.h>
// close():
#include <unistd.h>
// syscall():
#include <sys/syscall.h>
// mmap():
#include <sys/mman.h>
// sigfillset():
#include <signal.h>

// IO-userspace-ring
#include "vendor/io_uring.h"
// Memory barriers:
#include "vendor/barrier.h"

#include "Logging.h"

//========================
// Constants And Typedefs
//========================

typedef char bool;

const long NR_io_uring_setup    = 425;
const long NR_io_uring_enter    = 426;
const long NR_io_uring_register = 427;

//=================
// Data Structures
//=================

struct IO_Request
{
	bool empty;

	uint32_t mother_cell;

	int fd;
	uint8_t  opcode;
	uint64_t offset;
	uint32_t length;
	uint32_t error;

	char* buffer;
};

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

	struct io_uring_cqe* cq_ring;
};

struct IO_Ring
{
	int fd;

	struct IO_RingSQ sq;
	struct IO_RingCQ cq;
};

//=========================
// Init, Register And Free
//=========================

void init_io_ring(struct IO_Ring* io_ring, uint32_t num_entries)
{
	// Set IO-userspace-ring parameters to defaults:
	struct io_uring_params params;
	memset(&params, 0, sizeof(params));

	// Get an IO-ring:
	int io_ring_fd = syscall(NR_io_uring_setup, num_entries, &params);
	if (io_ring_fd == -1)
	{
		LOG_ERROR("[init_io_ring] Unable to setup IO-ring");
		exit(EXIT_FAILURE);
	}

	io_ring->fd = io_ring_fd;

	// Map IO-ring submission queue:
	void* sq_ring_ptr = mmap(NULL, params.sq_off.array + params.sq_entries * sizeof(uint32_t),
	                         PROT_READ|PROT_WRITE, MAP_SHARED|MAP_POPULATE,
	                         io_ring_fd, IORING_OFF_SQ_RING);
	if (sq_ring_ptr == MAP_FAILED)
	{
		LOG_ERROR("[init_io_ring] Unable to map IO-ring submission queue to userspace");
		exit(EXIT_FAILURE);
	}

	// Map submission entry array:
	void* sq_entries_ptr = mmap(NULL, params.sq_entries * sizeof(struct io_uring_sqe),
	                            PROT_READ|PROT_WRITE, MAP_SHARED|MAP_POPULATE,
	                            io_ring_fd, IORING_OFF_SQES);
	if (sq_entries_ptr == MAP_FAILED)
	{
		LOG_ERROR("[init_io_ring] Unable to map IO-ring entry array to userspace");
		exit(EXIT_FAILURE);
	}

	// Map IO-ring completion queue:
	void* cq_ring_ptr = mmap(NULL, params.cq_off.cqes + params.cq_entries * sizeof(struct io_uring_cqe),
	                         PROT_READ|PROT_WRITE, MAP_SHARED|MAP_POPULATE,
	                         io_ring_fd, IORING_OFF_CQ_RING);
	if (cq_ring_ptr == MAP_FAILED)
	{
		LOG_ERROR("[init_io_ring] Unable to map IO-ring completion queue to userspace");
		exit(EXIT_FAILURE);
	}

	// Calculate pointers:
	io_ring->sq.head         = sq_ring_ptr + params.sq_off.head;
	io_ring->sq.tail         = sq_ring_ptr + params.sq_off.tail;
	io_ring->sq.ring_mask    = sq_ring_ptr + params.sq_off.ring_mask;
	io_ring->sq.ring_entries = sq_ring_ptr + params.sq_off.ring_entries;
	io_ring->sq.flags        = sq_ring_ptr + params.sq_off.flags;
	io_ring->sq.sq_ring      = sq_ring_ptr + params.sq_off.array;
	io_ring->sq.dropped      = sq_ring_ptr + params.sq_off.dropped;

	io_ring->sq.sq_entries   = sq_entries_ptr;

	io_ring->cq.head         = cq_ring_ptr + params.cq_off.head;
	io_ring->cq.tail         = cq_ring_ptr + params.cq_off.tail;
	io_ring->cq.ring_mask    = cq_ring_ptr + params.cq_off.ring_mask;
	io_ring->cq.ring_entries = cq_ring_ptr + params.cq_off.ring_entries;
	io_ring->cq.overflow     = cq_ring_ptr + params.cq_off.overflow;
	io_ring->cq.cq_ring      = cq_ring_ptr + params.cq_off.cqes;

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

//==========================
// Compiler Memory Barriers
//==========================

#define WRITE_ONCE(var, val) (*((volatile __typeof(var) *)(&(var))) = (val))
#define READ_ONCE(var)       (*((volatile __typeof(var) *)(&(var))))

//=====================
// CPU Memory Barriers
//=====================

#if defined(__x86_64) || defined(__i386__)
#define memory_barrier() __asm__ __volatile__("":::"memory")

#else // In case of unknown arch perform a total memory barrier
#define memory_barrier() __sync_synchronize()
#endif

//===============
// IO Submission 
//===============

void submit_io_request(struct IO_Ring* io_ring, struct IO_Request* io_req, uint32_t io_req_cell)
{
	// Ensure the kernel updates to the SQ-ring have propagated to this CPU:
	memory_barrier();
	unsigned tail = READ_ONCE(*io_ring->sq.tail);

	// Configure the SQ-entry for submission:
	// Note: WRITE_ONCE forbids the compiler to optimize stores to be after the release:
	WRITE_ONCE(io_ring->sq.sq_entries[io_req_cell].opcode    ,           io_req->opcode);
	WRITE_ONCE(io_ring->sq.sq_entries[io_req_cell].flags     ,           0             );
	WRITE_ONCE(io_ring->sq.sq_entries[io_req_cell].ioprio    ,           0 /*default*/ );
	WRITE_ONCE(io_ring->sq.sq_entries[io_req_cell].fd        ,           io_req->fd    );
	WRITE_ONCE(io_ring->sq.sq_entries[io_req_cell].off       ,           io_req->offset);
	WRITE_ONCE(io_ring->sq.sq_entries[io_req_cell].addr      , (int64_t) io_req->buffer);
	WRITE_ONCE(io_ring->sq.sq_entries[io_req_cell].len       ,           io_req->length);
	WRITE_ONCE(io_ring->sq.sq_entries[io_req_cell].rw_flags  ,           0             );
	WRITE_ONCE(io_ring->sq.sq_entries[io_req_cell].user_data ,           io_req_cell   );
	WRITE_ONCE(io_ring->sq.sq_entries[io_req_cell].buf_index ,           io_req_cell   );

	WRITE_ONCE(io_ring->sq.sq_ring[tail & *io_ring->sq.ring_mask], io_req_cell);

	WRITE_ONCE(tail, tail + 1);

	// Ensure the kernel sees the sq-entries update before the tail update:
	memory_barrier();
	WRITE_ONCE(*io_ring->sq.tail, tail);
	memory_barrier();

	int ios_submitted = syscall(NR_io_uring_enter, io_ring->fd, 1, 0, NULL, _NSIG/8);
	if (ios_submitted != 1)
	{
		LOG_ERROR("[submit_io_request] Unable to submit request to IO-ring submission queue");
		exit(EXIT_FAILURE);
	}

	LOG("IO-request on cell#%03u submitted: {off=%lu, len=%u, mother=%u}",
	    io_req_cell, io_req->offset, io_req->length, io_req->mother_cell);
}

//===============
// IO Completion
//===============

uint32_t wait_for_io_completion(struct IO_Ring* io_ring)
{
	if (*io_ring->cq.head == *io_ring->cq.tail)
	{
		// Block connection hangup signal for correct IO waiting:
		sigset_t block_all_signals;
		if (sigfillset(&block_all_signals) == -1)
		{
			LOG_ERROR("[wait_for_io_completion] Unable to fill signal mask");
			exit(EXIT_FAILURE);
		}

		// Wait for IO:
		// Note: NSIG/8 is a size of sigset_t bitmask
		if (syscall(NR_io_uring_enter, io_ring->fd, 0, 1, IORING_ENTER_GETEVENTS, &block_all_signals, _NSIG/8) == -1)
		{
			LOG_ERROR("[wait_for_io_completion] Unable to wait for IO completion");
			exit(EXIT_FAILURE);
		}
	}

	// Ensure that the CQ-entries stores made by the kernel have propagated to this CPU:
	memory_barrier();
	unsigned head = READ_ONCE(*io_ring->cq.head);

	// Read the IO-request result:
	uint32_t io_req_cell = READ_ONCE(io_ring->cq.cq_ring[head & *io_ring->cq.ring_mask].user_data);

	// Ensure the head moves after the io_req_cell is read
	memory_barrier();
	WRITE_ONCE(*io_ring->cq.head, head + 1);
	
	return io_req_cell;
}

#endif // NBD_SERVER_IO_USERSPACE_RING_H_INCLUDED
