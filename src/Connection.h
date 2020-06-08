// No Copyright. Vladislav Aleinik 2020
//=====================================================
// Connection
//=====================================================
// Utilities to detect connection loss or client death
//=====================================================
#ifndef NBD_SERVER_CONNECTION_HPP_INCLUDED
#define NBD_SERVER_CONNECTION_HPP_INCLUDED

#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#endif

#include "Logging.h"

// Sockets API:
#include <sys/types.h>
#include <sys/socket.h>
// splice() and fcntl():
#include <fcntl.h>
// Network byte order:
#include <arpa/inet.h>
// TCP keepalive options:
#include <netinet/in.h>
#include <netinet/tcp.h>

const uint16_t NBD_IANA_RESERVED_PORT = 10809;

// TCP-keepalive attributes:
const int TCP_KEEPALIVE_IDLE_TIME  = 1; // sec
const int TCP_KEEPALIVE_INTERVAL   = 1; // sec
const int TCP_KEEPALIVE_NUM_PROBES = 4;

// TCP user-timeout:
const unsigned int TCP_NO_SEND_ACKS_TIMEOUT = 5000; // ms

int establish_connection()
{
	//--------------------
	// Acquire connection
	//--------------------

	int accept_sock_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (accept_sock_fd == -1)
	{
		LOG_ERROR("[establish_connection] Unable to get socket()");
		exit(EXIT_FAILURE);
	}

	// Acquire address:
	struct sockaddr_in server_addr;
	server_addr.sin_family      = AF_INET;
	server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	server_addr.sin_port        = htons(NBD_IANA_RESERVED_PORT);

	if (bind(accept_sock_fd, &server_addr, sizeof(server_addr)) == -1)
	{
		LOG_ERROR("[establish_connection] Unable to bind()");
	}

	// Listen for incoming connection (only 1 connection is supported):
	if (listen(accept_sock_fd, 1) == -1)
	{
		LOG_ERROR("[establish_connection] Unable to listen() on a socket");
		exit(EXIT_FAILURE);
	}

	// Wait for client:
	LOG("Waiting for client");

	int sock_fd = accept(accept_sock_fd, NULL, NULL);
	if (sock_fd == -1)
	{
		LOG_ERROR("[establish_connection] Unable to accept() a connection");
		exit(EXIT_FAILURE);
	}

	if (close(accept_sock_fd) == -1)
	{
		LOG_ERROR("[establish_connection] Unable to close() accept-socket");
		exit(EXIT_FAILURE);
	}

	//-----------------------
	// Configure TCP options
	//-----------------------

	// Ask socket to automatically detect disconnection:
	int setsockopt_yes = 1;
	if (setsockopt(sock_fd, SOL_SOCKET, SO_KEEPALIVE, &setsockopt_yes, sizeof(setsockopt_yes)) == -1)
	{
		LOG_ERROR("[establish_connection] Unable to set SO_KEEPALIVE socket option");
		exit(EXIT_FAILURE);
	}

	if (setsockopt(sock_fd, IPPROTO_TCP, TCP_KEEPIDLE,
	              &TCP_KEEPALIVE_IDLE_TIME, sizeof(TCP_KEEPALIVE_IDLE_TIME)) == -1)
	{
		LOG_ERROR("[establish_connection] Unable to set TCP_KEEPIDLE socket option");
		exit(EXIT_FAILURE);
	}

	if (setsockopt(sock_fd, IPPROTO_TCP, TCP_KEEPINTVL,
	               &TCP_KEEPALIVE_INTERVAL, sizeof(TCP_KEEPALIVE_INTERVAL)) == -1)
	{
		LOG_ERROR("[establish_connection] Unable to set TCP_KEEPINTVL socket option");
		exit(EXIT_FAILURE);
	}

	if (setsockopt(sock_fd, IPPROTO_TCP, TCP_KEEPCNT,
	               &TCP_KEEPALIVE_NUM_PROBES, sizeof(TCP_KEEPALIVE_NUM_PROBES)) == -1)
	{
		LOG_ERROR("[establish_connection] Unable to set TCP_KEEPCNT socket option");
		exit(EXIT_FAILURE);
	}

	// Set timeout to wait for unaknowledged sends:
	if (setsockopt(sock_fd, IPPROTO_TCP, TCP_USER_TIMEOUT,
	               &TCP_NO_SEND_ACKS_TIMEOUT, sizeof(TCP_NO_SEND_ACKS_TIMEOUT)) == -1)
	{
		LOG_ERROR("[establish_connection] Unable to set TCP_USER_TIMEOUT socket option");
		exit(EXIT_FAILURE);
	}

	// Disable socket lingering:
	struct linger linger_params =
	{
		.l_onoff  = 1,
		.l_linger = 0
	};
	if (setsockopt(sock_fd, SOL_SOCKET, SO_LINGER, &linger_params, sizeof(linger_params)) == -1)
	{
		LOG_ERROR("[establish_connection] Unable to disable SO_LINGER socket option");
		exit(EXIT_FAILURE);
	}

	int setsockopt_arg = 0;
	if (setsockopt(sock_fd, IPPROTO_TCP, TCP_LINGER2, &setsockopt_arg, sizeof(setsockopt_arg)) == -1)
	{
		LOG_ERROR("[establish_connection] Unable to disable TCP_LINGER2 socket option");
		exit(EXIT_FAILURE);
	}

	LOG("Connection established");

	return sock_fd;
}

#endif // NBD_SERVER_CONNECTION_HPP_INCLUDED
