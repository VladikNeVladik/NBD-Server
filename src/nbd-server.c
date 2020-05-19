// No Copyright. Vladislav Aleinik 2020
//===================================================================
// NBD Server
//===================================================================
// - Implementation of Network Block Device protocol baseline
// - Actually, even less than the baseline (^_^) - no NBD_CMD_WRITE
//===================================================================

#ifndef _LARGEFILE64_SOURCE
#define _LARGEFILE64_SOURCE
#endif

// Logging Facility:
#define LOG_TO_STDOUT
#include "Logging.h"

// NBD flags and constants:
#include "NBD.h"

// Request Tables:
#include "RequestManagement.h"

#include <stdlib.h>
// Sockets API:
#include <sys/types.h>
#include <sys/socket.h>
// splice() and fcntl():
#include <fcntl.h>
// open():
#include <sys/stat.h>
// close(), read():
#include <unistd.h>
// memset():
#include <string.h>
// POSIX-threads:
#include <pthread.h>
// sockaddr_in:
#include <netdb.h>
// errno:
#include <errno.h>
// nanosleep():
#include <time.h>

//=================
// Data Structures
//=================

typedef char bool;

struct ServerHandle
{
	// Export info:
	const char* export_name;
	int         export_fd;
	uint64_t    export_size;

	// Established connection:
	int client_sock_fd;

	// Negotiation phase:
	bool fixed_newstyle;
	bool no_zeroes;

	// Option haggling:
	bool structured_replies;

	// Transmission phase:
	struct IO_RequestTable io_table;

	struct NBD_RequestTable nbd_table;

	size_t nbd_reqs_pending;
	bool shutdown;
};

//==========================
// Connection establishment
//==========================

const uint16_t NBD_IANA_RESERVED_PORT = 10809;

int establish_connection()
{
	int accept_sock_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (accept_sock_fd == -1)
	{
		LOG_ERROR("[init_connection] Unable to get socket()");
		exit(EXIT_FAILURE);
	}

	// Acquire address:
	struct sockaddr_in server_addr;
	server_addr.sin_family      = AF_INET;
	server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	server_addr.sin_port        = htons(NBD_IANA_RESERVED_PORT);

	bool logged_sock_addr_in_use = 0;
	while (bind(accept_sock_fd, &server_addr, sizeof(server_addr)) == -1)
	{
		if (errno != EADDRINUSE)
		{
			LOG_ERROR("[init_connection] Unable to bind()");
			exit(EXIT_FAILURE);
		}

		if (!logged_sock_addr_in_use)
		{
			LOG("Sock Address In Use. Waiting For It To Get Free");
			logged_sock_addr_in_use = 1;
		}

		// Sleep for socket to become active:
		struct timespec sleep_request = {0, 100000000}; // 100ms
		nanosleep(&sleep_request, NULL);
	}
	

	// Ask socket to automatically detect disconnection:
	int setsockopt_yes = 1;
	if (setsockopt(accept_sock_fd, SOL_SOCKET, SO_KEEPALIVE, &setsockopt_yes, sizeof(setsockopt_yes)) == -1)
	{
		LOG_ERROR("[init_connection] Unable to set SO_KEEPALIVE socket option");
		exit(EXIT_FAILURE);
	}

	// Disable the TIME-WAIT state of a socket:
	if (setsockopt(accept_sock_fd, SOL_SOCKET, SO_REUSEADDR, &setsockopt_yes, sizeof(setsockopt_yes)) == -1)
	{
		LOG_ERROR("[init_connection] Unable to set SO_REUSEADDR socket option");
		exit(EXIT_FAILURE);
	}

	// Listen for incoming connection (only 1 connection is supported):
	if (listen(accept_sock_fd, 1) == -1)
	{
		LOG_ERROR("[init_connection] Unable to listen() on a socket");
		exit(EXIT_FAILURE);
	}

	// Wait for client:
	LOG("Waiting for client");

	int sock_fd = accept(accept_sock_fd, NULL, NULL);
	if (sock_fd == -1)
	{
		LOG_ERROR("[init_connection] Unable to accept() a connection");
		exit(EXIT_FAILURE);
	}

	if (close(accept_sock_fd) == -1)
	{
		LOG_ERROR("[init_connection] Unable to close() accept-socket");
		exit(EXIT_FAILURE);
	}

	LOG("Connection established");

	return sock_fd;
}

//===================
// Export Management
//===================

void open_export_file(struct ServerHandle* handle)
{
	handle->export_fd = open(handle->export_name, O_RDONLY|O_DIRECT|O_NONBLOCK|O_LARGEFILE);
	if (handle->export_fd == -1)
	{
		LOG_ERROR("[open_export_file] Unable to open() export file");
		exit(EXIT_FAILURE);
	}

	handle->export_size = lseek64(handle->export_fd, 0, SEEK_END);
	if (handle->export_size == -1)
	{
		LOG_ERROR("[open_export_file] Unable to lseek64() for SEEK_END");
		exit(EXIT_FAILURE);
	}

	LOG("Export file \"%s\" opened", handle->export_name);
}

//=============
// Negotiation
//=============

#include "Negotiation.h"

//=================
// Option Haggling
//=================

#include "OptionHaggling.h"

void manage_options(struct ServerHandle* handle)
{
	struct NBD_Option opt;
	struct NBD_Option_Reply rep;
	int sock_fd = handle->client_sock_fd;

	while (1)
	{
		recv_option_header(sock_fd, &opt, handle->fixed_newstyle);

		// Prepare default reply:
		rep.option       = opt.option;
		rep.option_reply = NBD_REP_ACK;
		rep.length       = 0;
		rep.buffer       = NULL;

		switch (opt.option)
		{
			case NBD_OPT_EXPORT_NAME:
			{
				// Ignore the export name:
				recv_option_data(sock_fd, &opt);

				send_option_export_name_reply(sock_fd, handle->export_size, handle->no_zeroes);
				return;
			}
			case NBD_OPT_ABORT:
			{
				// Ignore the export name:
				recv_option_data(sock_fd, &opt);

				send_option_reply(sock_fd, &rep);
				LOG("Client sent option NBD_OPT_ABORT");
				LOG("Hard disconnect");
				exit(EXIT_SUCCESS);
			}
			case NBD_OPT_LIST:
			{
				// Ignore option data:
				recv_option_data(sock_fd, &opt);

				send_option_reply(sock_fd, &rep);
				break;
			}
			case NBD_OPT_INFO:
			{
				manage_option_go(sock_fd, &opt, handle->export_size);
				// Do not return
			}
			case NBD_OPT_GO:
			{
				manage_option_go(sock_fd, &opt, handle->export_size);
				return;
			}
			case NBD_OPT_STRUCTURED_REPLY:
			{
				recv_option_data(sock_fd, &opt);

				rep.option_reply           = opt.length ? NBD_REP_ERR_INVALID : NBD_REP_ACK;
				handle->structured_replies = (opt.length != 0);

				send_option_reply(sock_fd, &rep);

				break;
			}
			default:
			{
				send_unsupported_option_reply(sock_fd, &opt);
				break;
			}
		}
	}

	LOG("Finished option-haggling");
}

//=====================
// Simple Transmission
//=====================

#include "Transmission.h"

struct OnWire_NBD_Reply_Simple
{
	uint32_t magic;
	uint32_t error;
	uint64_t handle;
} __attribute__((packed));

void send_nbd_simple_reply_header(int sock_fd, struct NBD_Request* req, bool more)
{
	struct OnWire_NBD_Reply_Simple onwire_reply =
	{
		.magic  = htobe32(NBD_MAGIC_SIMPLE_REPLY),
		.error  = htobe32(req->error),
		.handle = htobe64(req->handle)
	};

	if (send(sock_fd, &onwire_reply, sizeof(onwire_reply), MSG_NOSIGNAL|(more? MSG_MORE : 0)) != sizeof(onwire_reply))
	{
		LOG_ERROR("[send_nbd_simple_reply_header] Unable to send() simple reply header");
		exit(EXIT_FAILURE);
	}

	LOG("Sent simple reply header");
}

void simple_transmission_eventloop(struct ServerHandle* handle)
{
	LOG("Running eventloop for simple replies");

	struct NBD_Request req;
	int sock_fd = handle->client_sock_fd;

	char* export = mmap(NULL, handle->export_size, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_POPULATE, handle->export_fd, 0);
	if (export == MAP_FAILED)
	{
		LOG_ERROR("[simple_transmission_eventloop] Unable to mmap() export");
		exit(EXIT_FAILURE);
	}

	while (1)
	{
		recv_nbd_request(sock_fd, &req);

		send_nbd_simple_reply_header(sock_fd, &req, req.error != 0 && req.type == NBD_CMD_READ);

		if (req.error != 0)
		{
			LOG("Parse Error Detected: %d", req.error);
		}
		else if (req.type == NBD_CMD_READ)
		{
			// Send reply:
			if (send(sock_fd, export + req.offset, req.length, MSG_NOSIGNAL) != req.length)
			{
				LOG_ERROR("[simple_transmission_eventloop] Unable to send() data to peer");
				exit(EXIT_FAILURE);
			}

			LOG("Reply to request sent");
		}
		else if (req.type == NBD_CMD_DISC)
		{
			LOG("Disconnect requested");
			LOG("Soft disconnect");
			return;
		}
		else
		{
			BUG_ON(1, "[simple_transmission_eventloop] Forbidden request type");
		}
	}

	if (munmap(export, handle->export_size) == -1)
	{
		LOG_ERROR("[simple_transmission_eventloop] Unable to unmap() export");
		exit(EXIT_FAILURE);
	}
}

//=========================
// Structured Transmission
//=========================

void init_structured_transmission(struct ServerHandle* handle)
{
	handle->shutdown = 0;
	handle->nbd_reqs_pending = 0;

	init_req_tables(&handle->io_table, &handle->nbd_table);

	LOG("Structured transmission initialised");
}

void finish_structured_transmission(struct ServerHandle* handle)
{
	free_req_tables(&handle->io_table, &handle->nbd_table);

	LOG("Structured transmission finished");
}

void* structured_transmission_recv_eventloop(void* arg)
{
	LOG("Running recv-eventloop for structured replies");

	struct ServerHandle* handle = arg;

	while (1)
	{
		uint32_t nbd_cell = get_nbd_req_cell(&handle->nbd_table);
		handle->nbd_reqs_pending += 1;

		struct NBD_Request* nbd_req = &handle->nbd_table.nbd_reqs[nbd_cell];

		recv_nbd_request(handle->client_sock_fd, nbd_req);

		if (nbd_req->type == NBD_CMD_DISC)
		{
			handle->shutdown = 1;
			break;
		}

		submit_nbd_request(&handle->io_table, &handle->nbd_table, nbd_cell);
	}

	return NULL;
}

void structured_transmission_send_eventloop(struct ServerHandle* handle)
{
	LOG("Running send-eventloop for structured replies");

	while (1)
	{
		// Wait for IO-ring event:
		uint32_t io_cell = complete_io_request(&handle->io_table);
		uint32_t nbd_cell = handle->io_table.io_reqs[io_cell].mother_cell;

		struct  IO_Request*  io_req = &handle-> io_table. io_reqs[ io_cell];
		struct NBD_Request* nbd_req = &handle->nbd_table.nbd_reqs[nbd_cell];

		if (nbd_req->type == NBD_CMD_READ)
		{
			send_nbd_read_reply(handle->client_sock_fd, nbd_req, io_req);
		}

		if (nbd_req->io_reqs_pending == 0)
		{
			send_nbd_final_read_reply(handle->client_sock_fd, nbd_req);

			free_nbd_req_cell(&handle->nbd_table, nbd_cell);
			handle->nbd_reqs_pending -= 1;
		}

		if (handle->shutdown && handle->nbd_reqs_pending == 0)
		{
			LOG("Soft disconnect finished");
			return;
		}
	}
}

//======
// Main
//======

int main(int argc, char* argv[])
{
	if (argc != 2)
	{
		fprintf(stderr, "Usage: nbd-server export-filename");
		exit(EXIT_FAILURE);
	}

	struct ServerHandle server_handle;

	// Open export file for reading:
	server_handle.export_name = argv[1];
	open_export_file(&server_handle);

	// Establish TCP connection:
	LOG("Establishing connection");
	server_handle.client_sock_fd = establish_connection();

	// Fixed-newsyle negotiation:
	LOG("Entering negotiation phase");
	perform_negotiation(server_handle.client_sock_fd, &server_handle.no_zeroes, &server_handle.fixed_newstyle);

	// Option haggling:
	LOG("Entering option haggling phase");
	manage_options(&server_handle);

	// Transmission:
	LOG("Entering transmission phase");

	if (server_handle.structured_replies)
	{
		init_structured_transmission(&server_handle);

		pthread_t recv_thread;
		if (pthread_create(&recv_thread, NULL, structured_transmission_recv_eventloop, &server_handle) != 0)
		{
			LOG_ERROR("[main] Unable to start recv-eventloop");
			exit(EXIT_FAILURE);
		}

		structured_transmission_send_eventloop(&server_handle);

		finish_structured_transmission(&server_handle);
	}
	else
	{
		simple_transmission_eventloop(&server_handle);
	}

	LOG("Export successful!");

	return EXIT_SUCCESS;
}
