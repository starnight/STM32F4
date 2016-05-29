#ifndef __BITS_MAC_ESP8266_H__
#define __BITS_MAC_ESP8266_H__

#include <stdint.h>
#include "bits/socket.h"

#define SOCKET_READABLE		0	/* Socket is reading bit. */
#define SOCKET_WRITING		1	/* Socket is writing bit. */
#define SOCKET_READBUFOVR	2	/* Socket's read buffer is overflow bit. */
#define SOCKET_USING		7	/* Socket is used or not bit. */

#define _SET_BIT(r, b)	((r) |= (1 << (b)))
#define _CLR_BIT(r, b)	((r) &= ~(1 << (b)))
#define _ISBIT_SET(r, b)	(((r) & (1 << (b))) > 0)

#define MAX_CLIENT 			5

void InitESP8266(void);
SOCKET HaveTcpServerSocket(void);
int BindTcpSocket(uint16_t port);
SOCKET AcceptTcpSocket(void);
ssize_t SendSocket(SOCKET s, void *buf, size_t len, int f);
ssize_t RecvSocket(SOCKET s, void *buf, size_t len, int f);
int ShutdownSocket(SOCKET s, int how);
int IsSocketReady2Read(SOCKET s);
int IsSocketReady2Write(SOCKET s);

#endif
