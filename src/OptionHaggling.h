// No Copyright. Vladislav Aleinik 2020
//===================================================================
// NBD-Server Option Haggling
//===================================================================
// - Option Haggling Phas
//===================================================================
#ifndef NBD_SERVER_OPTION_HAGGLING_H_INCLUDED
#define NBD_SERVER_OPTION_HAGGLING_H_INCLUDED

//================
// Recieve Option 
//================

struct OnWire_NBD_Option
{
	uint64_t magic;
	uint32_t option;
	uint32_t length;
} __attribute__((packed));

struct NBD_Option
{
	uint32_t option;
	uint32_t length;
	uint8_t* buffer;
};

void recv_option_header(int sock_fd, struct NBD_Option* opt, bool fixed_newstyle)
{
	struct OnWire_NBD_Option onwire_opt;

	int bytes_read = recv(sock_fd, &onwire_opt, sizeof(onwire_opt), MSG_WAITALL);
	if (bytes_read != sizeof(onwire_opt))
	{
		LOG_ERROR("[recv_option_header] Unable to recv() option header");
		exit(EXIT_FAILURE);
	}

	if (be64toh(onwire_opt.magic) != NBD_MAGIC_I_HAVE_OPT)
	{
		LOG("Unrecognised option magic");
		LOG("Hard disconnect");
		exit(EXIT_SUCCESS);
	}

	opt->option = be32toh(onwire_opt.option);
	opt->length = be32toh(onwire_opt.length);
	opt->buffer = NULL;

	LOG("Option header {type=%u, len=%u} recived", opt->option, opt->length);
}

void recv_option_data(int sock_fd, struct NBD_Option* opt)
{
	if (opt->length == 0) return;

	if (opt->buffer == NULL)
	{
		const char* TRASH_BIN = "/dev/null";
		int trash_bin = open(TRASH_BIN, 0);

		// Discard data from socket:
		int bytes_read = splice(sock_fd, NULL, trash_bin, NULL, opt->length, SPLICE_F_MOVE);
		if (bytes_read == -1)
		{
			LOG_ERROR("[recv_option_data] Unable to splice() option data");
			exit(EXIT_FAILURE);
		}

		close(trash_bin);
	}
	else
	{
		int bytes_read = recv(sock_fd, opt->buffer, opt->length, MSG_WAITALL);
		if (bytes_read != opt->length)
		{
			LOG_ERROR("[recv_option_data] Unable to recv() option data");
			exit(EXIT_FAILURE);
		}
	}

	LOG("Option data recieved");
}

//==============
// Send Replies
//==============

struct OnWire_NBD_Option_Reply
{
	uint64_t magic;
	uint32_t option;
	uint32_t option_reply;
	uint32_t length;
} __attribute__((packed));

struct NBD_Option_Reply
{
	uint32_t option;
	uint32_t option_reply;
	uint32_t length;
	uint8_t* buffer;
};

void send_option_reply(int sock_fd, struct NBD_Option_Reply* rep)
{
	struct OnWire_NBD_Option_Reply rep_header =
	{
		.magic        = htobe64(NBD_MAGIC_OPTION_REPLY),
		.option       = htobe32(rep->option),
		.option_reply = htobe32(rep->option_reply),
		.length       = htobe32(rep->length)
	};

	if (send(sock_fd, &rep_header, sizeof(rep_header), rep->length ? MSG_MORE : 0) != sizeof(rep_header))
	{
		LOG_ERROR("[send_option_reply] Unable to send() option reply header");
		exit(EXIT_FAILURE);
	}

	if (rep->length != 0)
	{
		if (send(sock_fd, rep->buffer, rep->length, 0) != rep->length)
		{
			LOG_ERROR("[send_option_reply] Unable to send() option data");
			exit(EXIT_FAILURE);
		}
	}

	LOG("Option %u reply sent", rep->option);
}

void send_unsupported_option_reply(int sock_fd, struct NBD_Option* opt)
{
	struct NBD_Option_Reply rep = 
	{
		.option       = opt->option,
		.option_reply = opt->length ? NBD_REP_ERR_INVALID : NBD_REP_ERR_UNSUP,
		.length       = 0,
		.buffer       = NULL
	};

	recv_option_data (sock_fd,  opt);
	send_option_reply(sock_fd, &rep);

	LOG("Sent reply to an unsopported option %u", opt->option);
}

struct OnWire_NBD_Option_ExportName_Reply
{
	uint64_t export_size;
	uint16_t transmission_flags;
} __attribute__((packed));

void send_option_export_name_reply(int sock_fd, uint64_t export_size, bool no_zeroes)
{
	struct OnWire_NBD_Option_ExportName_Reply onwire_rep =
	{
		.export_size        = htobe64(export_size),
		.transmission_flags = htobe16(NBD_FLAG_HAS_FLAGS|NBD_FLAG_READ_ONLY)
	};

	if (send(sock_fd, &onwire_rep, sizeof(onwire_rep), MSG_MORE) != sizeof(onwire_rep))
	{
		LOG_ERROR("[send_option_export_name_reply] Unable to send() option reply header");
		exit(EXIT_FAILURE);
	}

	if (!no_zeroes)
	{
		char zeroes[124];
		memset(zeroes, 0, 124);
		if (send(sock_fd, zeroes, 124, 0) != 124)
		{
			LOG_ERROR("[send_option_export_name_reply] Unable to send() zero-zero-zero-zero-zero-... (00__00)");
			exit(EXIT_FAILURE);
		}
	}

	LOG("Sent reply to NBD_OPT_EXPORT_NAME option");
}

//===========
// Option GO
//===========

struct OnWire_NBD_Info_Request
{
	uint16_t type;
} __attribute__((packed));

struct OnWire_NBD_Info_Export_Reply
{
	uint16_t type;
	uint64_t export_size;
	uint16_t transmission_flags;
} __attribute__((packed));

struct OnWire_NBD_Info_Block_Size_Reply
{
	uint16_t type;
	uint32_t minimum;
	uint32_t preferred;
	uint32_t maximum;
} __attribute__((packed));

void manage_option_go(int sock_fd, struct NBD_Option* opt, uint64_t export_size, uint32_t min_block_size)
{
	uint32_t export_name_length;
	int bytes_read = recv(sock_fd, &export_name_length, 4, MSG_WAITALL);
	if (bytes_read != 4)
	{
		// if (bytes_read == 0)
		// {
		// 	conn_hangup_handler();
		// }

		LOG_ERROR("[manage_option_go] Unable to recv() export name length");
		exit(EXIT_FAILURE);
	}
	export_name_length = be32toh(export_name_length);

	if (export_name_length > opt->length - 6)
	{
		LOG("Export name length too large");
		LOG("Hard disconnect");
		exit(EXIT_SUCCESS);
	}

	// Ignore requested export name:
	opt->length = export_name_length;
	recv_option_data(sock_fd, opt);

	uint16_t num_info_requests;
	bytes_read = recv(sock_fd, &num_info_requests, 2, MSG_WAITALL);
	if (bytes_read != 2)
	{
		// if (bytes_read == 0)
		// {
		// 	conn_hangup_handler();
		// }

		LOG_ERROR("[manage_option_go] Unable to recv() export name length");
		exit(EXIT_FAILURE);
	}
	num_info_requests = be16toh(num_info_requests);

	// Handle info requests:
	for (uint16_t i = 0; i < num_info_requests; ++i)
	{
		struct OnWire_NBD_Info_Request onwire_info_request;
		bytes_read = recv(sock_fd, &onwire_info_request, sizeof(onwire_info_request), MSG_WAITALL);
		if (bytes_read != sizeof(onwire_info_request))
		{
			// if (bytes_read == 0)
			// {
			// 	conn_hangup_handler();
			// }

			LOG_ERROR("[manage_option_go] Unable to recv() export info request");
			exit(EXIT_FAILURE);
		}

		uint16_t info_request_type = be16toh(onwire_info_request.type);
		switch (info_request_type)
		{
			case NBD_INFO_EXPORT:
			{
				// It is always sent anyway
				break;
			}
			case NBD_INFO_BLOCK_SIZE:
			{
				struct OnWire_NBD_Info_Block_Size_Reply onwire_info_reply = 
				{
					.type      = htobe16(NBD_INFO_BLOCK_SIZE),
					.minimum   = htobe32(min_block_size),
					.preferred = htobe32(4096),
					.maximum   = htobe32(1024 * 1024)
				};

				struct NBD_Option_Reply rep = 
				{
					.option       = opt->option,
					.option_reply = NBD_REP_INFO,
					.length       = sizeof(onwire_info_reply),
					.buffer       = (uint8_t*) &onwire_info_reply
				};

				send_option_reply(sock_fd, &rep);

				break;
			}
			default:
			{
				// Ignore it.
			}
		}
	}

	// Send the mandatory NBD_INFO_EXPORT reply:
	struct OnWire_NBD_Info_Export_Reply onwire_info_reply = 
	{
		.type               = htobe16(NBD_INFO_EXPORT),
		.export_size        = htobe64(export_size),
		.transmission_flags = htobe16(NBD_FLAG_HAS_FLAGS|NBD_FLAG_READ_ONLY)
	};

	struct NBD_Option_Reply rep = 
	{
		.option       = opt->option,
		.option_reply = NBD_REP_INFO,
		.length       = sizeof(onwire_info_reply),
		.buffer       = (uint8_t*) &onwire_info_reply
	};

	send_option_reply(sock_fd, &rep);

	// Acknowledge the option:
	struct NBD_Option_Reply ack = 
	{
		.option       = opt->option,
		.option_reply = NBD_REP_ACK,
		.length       = 0,
		.buffer       = NULL
	};

	send_option_reply(sock_fd, &ack);

	LOG("Sent reply to NBD_OPT_GO (or NBD_OPT_INFO) option");
}

#endif // NBD_SERVER_OPTION_HAGGLING_H_INCLUDED
