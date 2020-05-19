// No Copyright. Vladislav Aleinik 2020
//===================================================================
// NBD-Server Request Transmission
//===================================================================
// - Request recieving and parsing
// - Structured reply transmission
//===================================================================
#ifndef NBD_SERVER_TRANSMISSION_H_INCLUDED
#define NBD_SERVER_TRANSMISSION_H_INCLUDED

#include "RequestManagement.h"

// recv(), send():
#include <sys/types.h>
#include <sys/socket.h>
// open():
#include <sys/stat.h>
// htobe64() and the boys:
#include <endian.h>

struct OnWire_NBD_Request
{
	uint32_t request_magic;
	uint16_t command_flags;
	uint16_t type;
	uint64_t handle;
	uint64_t offset;
	uint32_t length;
} __attribute__((packed));

struct OnWire_NBD_Reply
{
	uint32_t reply_magic;
	uint16_t flags;
	uint16_t type;
	uint64_t handle;
	uint32_t length;
} __attribute__((packed));

void recv_nbd_request(int sock_fd, struct NBD_Request* nbd_req)
{
	struct OnWire_NBD_Request onwire_req;

	nbd_req->error = 0;

	// Recv request:
	if (recv(sock_fd, &onwire_req, sizeof(onwire_req), MSG_WAITALL) != sizeof(onwire_req))
	{
		LOG_ERROR("[recv_nbd_request] Unable to recv() NBD request");
		exit(EXIT_FAILURE);
	}

	// Fix endianness:
	nbd_req->type   = be16toh(onwire_req.type);
	nbd_req->handle = be64toh(onwire_req.handle);
	nbd_req->offset = be64toh(onwire_req.offset);
	nbd_req->length = be32toh(onwire_req.length);

	// Error-check:
	if (be32toh(onwire_req.request_magic) != NBD_MAGIC_REQUEST)
	{
		LOG("[NBD-SERVER] Incorrect request magic");
		LOG("[NBD-SERVER] Hard disconnect");
		exit(EXIT_SUCCESS);
	}

	if (be16toh(onwire_req.command_flags) != 0)
	{
		LOG("[NBD-SERVER] Client sent unsoppurted command flags");
		nbd_req->error = NBD_EINVAL;
	}

	if (nbd_req->type != NBD_CMD_READ && nbd_req->type != NBD_CMD_DISC)
	{
		LOG("[NBD-SERVER] Client sent unsoppurted request type");
		nbd_req->error = NBD_EINVAL;
	}

	// Discard spare data:
	if (nbd_req->type != NBD_CMD_READ && nbd_req->length != 0)
	{
		LOG("[NBD-SERVER] Client sent non-zero request data length");
		nbd_req->error = NBD_EINVAL;

		const char* TRASH_BIN = "/dev/null";
		int trash_bin = open(TRASH_BIN, 0);

		if (splice(sock_fd, NULL, trash_bin, NULL, nbd_req->length, SPLICE_F_MOVE) == -1)
		{
			LOG_ERROR("[recv_nbd_request] Unable to splice() option data");
			exit(EXIT_FAILURE);
		}

		close(trash_bin);
	}

	LOG("[NBD-SERVER] Recieved NBD Request: {type=%x, hdl=%lu, off=%lu, len=%u}",
		nbd_req->type,
		nbd_req->handle,
		nbd_req->offset,
		nbd_req->length);
}

void send_nbd_read_reply(int sock_fd, struct NBD_Request* nbd_req, struct IO_Request* io_req)
{
	struct OnWire_NBD_Reply onwire_reply =
	{
		.reply_magic = htobe32(NBD_MAGIC_STRUCTURED_REPLY),
		.flags       = htobe16(0),
		.type        = htobe16(io_req->error ? NBD_REPLY_TYPE_ERROR_OFFSET : NBD_REPLY_TYPE_OFFSET_DATA),
		.handle      = htobe64(nbd_req->handle),
		.length      = htobe32(io_req->length)
	};

	if (io_req->error == 0)
	{
		if (send(sock_fd, &onwire_reply, sizeof(onwire_reply), MSG_MORE|MSG_NOSIGNAL) != sizeof(onwire_reply))
		{
			LOG_ERROR("[send_nbd_read_reply] Unable to send() reply header");
			exit(EXIT_FAILURE);
		}

		uint64_t onwire_off = htobe64(io_req->offset);
		if (send(sock_fd, &onwire_off, 8, MSG_MORE|MSG_NOSIGNAL) != 8)
		{
			LOG_ERROR("[send_nbd_read_reply] Unable to send() offset");
			exit(EXIT_FAILURE);
		}

		if (send(sock_fd, io_req->buffer, io_req->length, MSG_NOSIGNAL) != io_req->length)
		{
			LOG_ERROR("[send_nbd_read_reply] Unable to send() data");
			exit(EXIT_FAILURE);
		}
	}
	else
	{
		if (send(sock_fd, &onwire_reply, sizeof(onwire_reply), MSG_MORE|MSG_NOSIGNAL) != sizeof(onwire_reply))
		{
			LOG_ERROR("[send_nbd_read_reply] Unable to send() reply header");
			exit(EXIT_FAILURE);
		}

		uint32_t error = htobe32(io_req->error);
		if (send(sock_fd, &error, 4, MSG_MORE|MSG_NOSIGNAL) != 4)
		{
			LOG_ERROR("[send_nbd_read_reply] Unable to send() error");
			exit(EXIT_FAILURE);
		}

		uint32_t length = htobe32(0);
		if (send(sock_fd, &length, 4, MSG_MORE|MSG_NOSIGNAL) != 4)
		{
			LOG_ERROR("[send_nbd_read_reply] Unable to send() length");
			exit(EXIT_FAILURE);
		}

		uint64_t off = htobe64(io_req->offset);
		if (send(sock_fd, &off, 8, MSG_NOSIGNAL) != 8)
		{
			LOG_ERROR("[send_nbd_read_reply] Unable to send() offset");
			exit(EXIT_FAILURE);
		}
	}
	
	LOG("[NBD-SERVER] Send NBD_CMD_READ Structured Reply: {hdl=%lu, off=%lu, len=%u}",
		nbd_req->handle,
		 io_req->offset,
		 io_req->length);
}

void send_nbd_final_read_reply(int sock_fd, struct NBD_Request* nbd_req)
{
	struct OnWire_NBD_Reply onwire_reply =
	{
		.reply_magic = htobe32(NBD_MAGIC_STRUCTURED_REPLY),
		.flags       = htobe16(NBD_REPLY_FLAG_DONE),
		.type        = htobe16(NBD_REPLY_TYPE_NONE),
		.handle      = htobe64(nbd_req->handle),
		.length      = htobe32(0)
	};

	if (send(sock_fd, &onwire_reply, sizeof(onwire_reply), MSG_MORE|MSG_NOSIGNAL) != sizeof(onwire_reply))
	{
		LOG_ERROR("[send_nbd_reply] Unable to send() final read reply");
		exit(EXIT_FAILURE);
	}

	LOG("[NBD-SERVER] Send NBD_CMD_READ Final Reply: {hdl=%lu, off=%lu, len=%u}",
		nbd_req->handle,
		nbd_req->offset,
		nbd_req->length);
}

#endif // NBD_SERVER_TRANSMISSION_H_INCLUDED
