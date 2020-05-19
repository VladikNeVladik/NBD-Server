// No Copyright. Vladislav Aleinik 2020
//===================================================================
// NBD-Server Negotiation
//===================================================================
// - Negotiation Phase
//===================================================================
#ifndef NBD_SERVER_NEGOTIATION_H_INCLUDED
#define NBD_SERVER_NEGOTIATION_H_INCLUDED

// htobe64() and the boys:
#include <endian.h>
// recv(), send():
#include <sys/types.h>
#include <sys/socket.h>

typedef char bool;	

struct OnWire_Server_Negotiation
{
	uint64_t init_passwd;
	uint64_t magic;
	uint16_t handshake_flags;
} __attribute__((packed));

struct OnWire_Client_Negotiation
{
	uint32_t handshake_flags;
} __attribute__((packed));

void perform_negotiation(int client_sock_fd, bool* no_zeroes, bool* fixed_newstyle)
{
	struct OnWire_Server_Negotiation server_says = 
	{
		.init_passwd     = htobe64(NBD_MAGIC_INIT_PASSWD),
		.magic           = htobe64(NBD_MAGIC_I_HAVE_OPT),
		.handshake_flags = htobe16(NBD_FLAG_FIXED_NEWSTYLE|NBD_FLAG_NO_ZEROES)
	};

	if (send(client_sock_fd, &server_says, sizeof(server_says), MSG_NOSIGNAL) != sizeof(server_says))
	{
		LOG_ERROR("[perform_negotiation] Unable to send() server negotiation");
		exit(EXIT_FAILURE);
	}

	struct OnWire_Client_Negotiation client_says;
	if (recv(client_sock_fd, &client_says, sizeof(client_says), MSG_WAITALL) != sizeof(client_says))
	{
		LOG_ERROR("[perform_negotiation] Unable to recv() client negotiation");
		exit(EXIT_FAILURE);
	}

	uint32_t client_handshake_flags = be32toh(client_says.handshake_flags);
	if (client_handshake_flags & NBD_FLAG_C_NO_ZEROES)
	{
		LOG("The client asked for no zeroes");
		*no_zeroes = 1;
	}
	else
	{
		LOG("The client is a 000_zeromaniac_000!");
		*no_zeroes = 1;
	}

	if (client_handshake_flags & NBD_FLAG_C_FIXED_NEWSTYLE)
	{
		LOG("The client is fixed newstyle");
		*fixed_newstyle = 1;
	}
	else
	{
		LOG("The client is non-fixed newstyle");
		*fixed_newstyle = 0;
	}

	if (client_handshake_flags & ~(NBD_FLAG_C_FIXED_NEWSTYLE|NBD_FLAG_C_NO_ZEROES))
	{
		LOG("Unrecognised client flags detected");
		LOG("Hard disconnect");
		exit(EXIT_SUCCESS);
	}

	LOG("Negotiation complete. Server flags: %04x. Client flags: %04x",
	    be16toh(server_says.handshake_flags), client_handshake_flags);
}

#endif // NBD_SERVER_NEGOTIATION_H_INCLUDED