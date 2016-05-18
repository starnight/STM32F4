#include <string.h>
#include "usart.h"
#include "bits/socket.h"
#include "sys/socket.h"

#define MAX_NUM_SOCKET 64

uint64_t sock_slot = 0;

uint32_t htonl(uint32_t hostlong) {
	return hostlong;
}

uint16_t htons(uint16_t hostshort) {
	return hostshort;
}


/* Create a new socket of type TYPE in domain DOMAIN, using
   protocol PROTOCOL.  If PROTOCOL is zero, one is chosen automatically.
   Returns a file descriptor for the new socket, or -1 for errors.  */
int socket (int __domain, int __type, int __protocol) {
	int i = -1;
	
	if(__domain == AF_INET && __type == SOCK_STREAM && __protocol == 0) {
		for(i=0; i<MAX_NUM_SOCKET; i++) {
			if((sock_slot & (1 << i)) == 0) {
				sock_slot |= (1 << i);
				i += SOCKET_BASE;
				break;
			}
		}
	}

	return i;
}

/* Give the socket FD the local address ADDR (which is LEN bytes long).  */
int bind (int __fd, __CONST_SOCKADDR_ARG __addr, socklen_t __len) {
	/*
	char mul_con[] = "AT+CIPMUX=1\r\n";
	char as_server[] = "AT+CIPSERVER=1,8001\r\n";

	USART_Send(USART6, mul_con, strlen(mul_con), BLOCKING);
	// To do: Check OK
	
	USART_Send(USART6, as_server, strlen(as_server), BLOCKING);
	// To do: Check OK
	*/
	return 0;
}

/* Send N bytes of BUF to socket FD.  Returns the number sent or -1.  */
ssize_t send (int __fd, __const void *__buf, size_t __n, int __flags) {
}

/* Read N bytes into BUF from socket FD.
   Returns the number read or -1 for errors.  */
ssize_t recv (int __fd, void *__buf, size_t __n, int __flags) {
}

/* Set socket FD's option OPTNAME at protocol level LEVEL
   to *OPTVAL (which is OPTLEN bytes long).
   Returns 0 on success, -1 for errors.  */
int setsockopt (int __fd, int __level, int __optname,
		       const void *__optval, socklen_t __optlen) {
}

/* Prepare to accept connections on socket FD.
   N connection requests will be queued before further requests are refused.
   Returns 0 on success, -1 for errors.  */
int listen (int __fd, int __n) {
}

/* Await a connection on socket FD.
   When a connection arrives, open a new socket to communicate with it,
   set *ADDR (which is *ADDR_LEN bytes long) to the address of the connecting
   peer and *ADDR_LEN to the address's actual length, and return the
   new socket's descriptor, or -1 for errors.  */
int accept (int __fd, __SOCKADDR_ARG __addr, socklen_t *__restrict __addr_len) {
}

/* Shut down all or part of the connection open on socket FD.
   HOW determines what to shut down:
     SHUT_RD   = No more receptions;
     SHUT_WR   = No more transmissions;
     SHUT_RDWR = No more receptions or transmissions.
   Returns 0 on success, -1 for errors.  */
int shutdown (int __fd, int __how) {
}

