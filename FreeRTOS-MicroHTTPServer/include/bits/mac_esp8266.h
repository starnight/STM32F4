#ifndef __MAC_ESP8266_H__
#define __MAC_ESP8266_H__

#define SOCKET_READABLE		0	/* Socket is reading bit. */
#define SOCKET_WRITING		1	/* Socket is writing bit. */
#define SOCKET_READBUFOVR	2	/* Socket's read buffer is overflow bit. */
#define SOCKET_USING		7	/* Socket is used or not bit. */

#define SET_BIT(r, b)	((r) |= (1 << (b)))
#define CLR_BIT(r, b)	((r) &= ~(1 << (b)))
#define ISBIT_SET(r, b)	(((r) & (1 << (b))) > 0)

#endif
