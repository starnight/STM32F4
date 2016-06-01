#include <string.h>
#include <stdio.h>
#include <errno.h>
#include "bits/socket.h"
#include "bits/mac_esp8266.h"
#include "usart.h"
#include "gpio.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"

#define MAX_SOCKETBUFLEN	2048

typedef struct _sock_struct {
	uint32_t peer_ip; /* Peer IP address. */
	uint16_t peer_port; /* Peer port. */
	uint16_t fd; /* File descriptor of the socket. */
	uint16_t slen; /* Length of the payload going to be sent to peer. */
	uint16_t rlen; /* Length of the payload going to receive from peer. */
	QueueHandle_t rxQueue; /* Buffer for receive from client. */
	uint8_t *sbuf; /* Points to the buffer going to be sent. */
	uint8_t state; /* Socket status. */
} _sock;

_sock clisock[MAX_CLIENT];
_sock svrsock;

/* Set server socket's ID being last socket ID. */
#define SERVER_SOCKET_ID	MAX_CLIENT

/* Sockets pool queue handler. */
QueueHandle_t new_connects;

/* Socket and ID mapping macroes. */
#define ID2Sock(id)	((SOCKET)(id + SOCKET_BASE))
#define Sock2ID(s)	(s - SOCKET_BASE)

uint8_t _ESP8266_state;

/* ESP8266 UART channel usage mutex. */
SemaphoreHandle_t xUSART_Mutex = NULL;

#define USART_RXBUFLEN	64

uint8_t USART_rBuf[USART_RXBUFLEN];
uint16_t USART_rIdx;
uint16_t USART_wIdx;

#define IsNumChar(c) (('0' <= c) && (c <= '9'))

/* Get the status of ESP8266. */
uint8_t GetESP8266State(void) {
	return _ESP8266_state;
}

/* Try to parse connected by a new client. */
void GetClientConnented(void) {
	uint16_t id;
	SOCKET s;

	if(sscanf(USART_rBuf, "%d,CONNECT\r\n", &id) > 0) {
		/* Notify os server socket connected. */
		s = ID2Sock(id);
		xQueueSend(new_connects, &s, 0);
		_SET_BIT(svrsock.state, SOCKET_READABLE);
		/* Set initial state of the client sock. */
		clisock[id].fd = ID2Sock(id);
		clisock[id].rlen = 0;
		clisock[id].slen = 0;
		xQueueReset(clisock[Sock2ID(s)].rxQueue);
		clisock[id].state = 0;
		_SET_BIT(clisock[id].state, SOCKET_USING);
	}
}

/* Try to parse request from a client. */
void GetClientRequest(void) {
	uint16_t id;
	uint16_t len;
	uint16_t n;
	_sock *s;
	uint8_t c;

	if(sscanf(USART_rBuf, "+IPD,%d,%d:", &id, &len) == 2) {
		clisock[id].rlen = len;
		if(len > MAX_SOCKETBUFLEN) {
			len = MAX_SOCKETBUFLEN;
		}

		n = 0;
		while(n < len) {
			s = clisock + id;
			if(USART_Read(USART6, &c, 1, NON_BLOCKING) > 0) {
				/* Read 1 byte from ESP8266 UART channel and
				 * send it to related scoket. */
				if(xQueueSendToBack(clisock[id].rxQueue, &c, 0) == pdTRUE) {
					n++;
				}
			}
			else {
				/* Wait for read 1 byte from ESP8266 UART channel. */
				vTaskDelay(portTICK_PERIOD_MS);
			}
		}
		/* Notify client socket there is something to be read 
		 * after receive a frame. */
		_SET_BIT(s->state, SOCKET_READABLE);

		clisock[id].rlen -= n;
	}
}

/* Try to parse closed by a client. */
void GetClientClosed(void) {
	uint16_t id;
	SOCKET s;

	if(sscanf(USART_rBuf, "%d,CLOSED\r\n", &id) > 0) {
		/* Notify os server socket close. */
		s = ID2Sock(id);
		/* Clear state of the client sock. */
		xQueueReset(clisock[id].rxQueue);
		clisock[id].state = 0;
	}
}

/* Try to dispatch the request from ESP8266 UART channel. */
void GetESP8266Request(void) {
	ssize_t n;
	uint8_t c;
	char s[8];

	/* Have request header from ESP8266 UART channel. */
	USART_wIdx=0;
	do {
		//USART_Printf(USART2, "Ding ");
		if(USART_Read(USART6, &c, 1, NON_BLOCKING) > 0) {
			//USART_Printf(USART2, "Doom ");
			//snprintf(s, 8, "%d\r\n", (int)c);
			//USART_Printf(USART2, s);
			USART_rBuf[USART_wIdx] = c;
			USART_wIdx++;
			GPIO_SetBits(LEDS_GPIO_PORT, GREEN);
		}
		else {
			vTaskDelay(50);
		}
	} while(((c != '\n') && (c != ':')) && (USART_wIdx < USART_RXBUFLEN-1));
	USART_rBuf[USART_wIdx] = '\0';

	//USART_Printf(USART2, "\r\nRepeat: ");
	//USART_Printf(USART2, USART_rBuf);
	/* Try to parse request header. */
	USART_rIdx = 0;
	if(USART_wIdx < 4) {
		/* Useless message. */
	}
	else if(strncmp(USART_rBuf, "WIFI GOT IP", 11) == 0) {
		/* Change ESP8266 UART channel state is ready for internet usage. */
		_ESP8266_state = ESP8266_LINKED;
		USART_Printf(USART2, "Wifi enabled.\r\n");
		//GPIO_SetBits(LEDS_GPIO_PORT, BLUE);
	}
	else if(IsNumChar(USART_rBuf[0])) {
		/* Number of ID part. */
		for(USART_rIdx++;
			IsNumChar(USART_rBuf[USART_rIdx]) &&
				(USART_rIdx < (USART_wIdx - strlen(",CLOSED\r\n")));
			USART_rIdx++);
		if(USART_rBuf[USART_rIdx+2] == 'O') {
			/* Go parse new ID connected. */
			GetClientConnented();
		}
		else if(USART_rBuf[USART_rIdx+2] == 'L') {
			/* Go parse an ID closed. */
			GetClientClosed();
		}
	}
	else if(strncmp(USART_rBuf, "+IPD,", 5) == 0) {
		/* Go parse ID request. */
		GetClientRequest();
	}
	else if(strncmp(USART_rBuf, "WIFI CONNECTED", 14) == 0) {
		/* Ignore for now. */
	}
}

/* ESP8266 UART channel request parsing task. */
void vESP8266RTask(void *__p) {
	char s[8];
	
	/* Enable the pipe with ESP8266 UART channel. */
	USART_Printf(USART2, "Going to enable RX pipe.\r\n");
	USART_EnableRxPipe(USART6);
	USART_Printf(USART2, "RX pipe enabled.\r\n");

	while(1) {
		/* Try to take ESP8266 UART channel usage mutex. */
		if(xSemaphoreTake(xUSART_Mutex, 0) == pdTRUE) {
			if(USART_Readable(USART6)) {
				//snprintf(s, 8, "%d\r\n", USART_Readable(USART6));
				//USART_Printf(USART2, s);
				/* There is a request from ESP8266 UART channel. */
				GetESP8266Request();
			}
			else {
				GPIO_ResetBits(LEDS_GPIO_PORT, GREEN);
			}
			/* Parse finished and releas ESP8266 UART channel usage mutex. */
			xSemaphoreGive(xUSART_Mutex);
		}
		else {
			/* Wait for ESP8266 UART channel usage mutex. */
			vTaskDelay(portTICK_PERIOD_MS);
		}
	}
}

void InitESP8266(void) {
	uint16_t i;
	BaseType_t xReturned;

	/* Zero ESP8266 state. */
	_ESP8266_state = ESP8266_NONE;

	/* Zero client sockets' state. */
	for(i=0; i<MAX_CLIENT+1; i++) {
		clisock[i].state = 0;
		clisock[i].rxQueue = xQueueCreate(MAX_SOCKETBUFLEN, sizeof(uint8_t));
	}
	/* Zero server socket's state. */
	svrsock.state = 0;

	/* Create ESP8266 UART channel usage mutex. */
	xUSART_Mutex = xSemaphoreCreateMutex();
	/* Create the new socket client connection queue. */
	new_connects = xQueueCreate(MAX_CLIENT, sizeof(SOCKET));

	/* Start USART for ESP8266. */
	setup_usart();

	/* Create ESP8266 parsing request from USART RX task. */
	xReturned = xTaskCreate(vESP8266RTask,
							"ESP8266 Parse Req",
							1024,
							NULL,
							tskIDLE_PRIORITY,
							NULL);
	//if(xReturned == pdPASS)
	//	GPIO_ResetBits(LEDS_GPIO_PORT, BLUE);
}

int HaveInterfaceIP(uint32_t *pip) {
	char get_ip[] = "AT+CIFSR\r\n";
	char res[40];
	uint8_t ip[4];
	ssize_t l, n;

	char debug[80];

	USART_Printf(USART2, "\tCheck there is still more bytes in RX queue\r\n");
	/* Make sure there is no pending message of ESP8266 RX data. */
	while(USART_Readable(USART6)) {
		snprintf(debug, 80, "\tStill %d bytes in RX queue\r\n", USART_Readable(USART6));
		USART_Printf(USART2, debug);
		vTaskDelay(100);
	}
	USART_Printf(USART2, "\tTry to get ESP8266 channel mutex\r\n");
	/* Block to take ESP8266 UART channel usage mutex. */
	while(xSemaphoreTake(xUSART_Mutex, 0) != pdTRUE) {
		USART_Printf(USART2, "\tStill can't get EXP8266 channel mutex\r\n");
		vTaskDelay(50);
	}
	/* Get ESP8266 station IP. */
	USART_Printf(USART2, "\tGoing to send AT+CIFSR command\r\n");
	USART_Send(USART6, get_ip, strlen(get_ip), NON_BLOCKING);
	l = strlen(get_ip) + 1;
	n = 0;
	do {
		n += USART_Read(USART6, res, l-n, BLOCKING);
		snprintf(debug, 80, "\tIn send get ip response.  Get %d bytes\r\n", n);
		USART_Printf(USART2, debug);
	} while(n < l);
	res[n] = '\0';

	if(strncmp(res, "AT+CIFSR\r\r\n", 11) != 0) {
		for(n=0; n<l; n++) {
			snprintf(debug, 80, "\t\t%d\r\n", res[n]);
			USART_Printf(USART2, debug);
		}
		/* Releas ESP8266 UART channel usage mutex. */
		xSemaphoreGive(xUSART_Mutex);
		return -1;
	}

	/* Read IPs and MACs. */
	USART_Printf(USART2, "\tGoing to get IP and MAC\r\n");
	for(l=0; l<4; l++) { // Need 4 '\n'
		for(n=0; res[n] != '\n'; n++) {
			while(USART_Read(USART6, &res[n], 1, BLOCKING) < 1) {
				USART_Printf(USART2, "\t\tWait IP and MAC message\r\n");
				vTaskDelay(100);
			}
		}
		if(strncmp(res, "+CIFSR:STAIP,", 13) == 0) {
			USART_Printf(USART2, res);
			sscanf(res, "+CIFSR:STAIP,\"%d.%d.%d.%d\"", &ip[0], &ip[1], &ip[2], &ip[3]);
			*pip = ip[0] << 24 + ip[1] << 16 + ip[2] << 8 + ip[3];
			break;
		}
	}

	l = strlen("\r\nOK\r\n");
	n = 0;
	do {
		n += USART_Read(USART6, res, l-n, BLOCKING);
		snprintf(debug, 80, "\tGet IP response.  Get %d bytes\r\n", n);
		USART_Printf(USART2, debug);
	} while(n < l);
	res[n] = '\0';

	/* Releas ESP8266 UART channel usage mutex. */
	xSemaphoreGive(xUSART_Mutex);

	if(strncmp(res, "\r\nOK\r\n", 6) == 0) {
		USART_Printf(USART2, "\tGet ip ok!\r\n");
		return 0;
	}
	else {
		for(n=0; n<l; n++) {
			snprintf(debug, 80, "\t\t%d\r\n", res[n]);
			USART_Printf(USART2, debug);
		}
		USART_Printf(USART2, "\tGet ip failed!\r\n");
		errno = EBADF;
		return -1;
	}
}

SOCKET HaveTcpServerSocket(void) {
	if(!_ISBIT_SET(svrsock.state, SOCKET_USING)) {
		/* First time to have server socket.
		 * There is only one server socket should be. */
		svrsock.fd = ID2Sock(SERVER_SOCKET_ID);
		_SET_BIT(svrsock.state, SOCKET_USING);
		return svrsock.fd;
	}
	else {
		/* No more server socket should be. */
		errno = ENFILE;
		return -1;
	}
}

int BindTcpSocket(uint16_t port) {
	char mul_con[] = "AT+CIPMUX=1\r\n";
	char as_server[30];
	char res[30];
	TaskHandle_t task;
	ssize_t l, n;

	char debug[80];

	//task = xTaskGetCurrentTaskHandle();
	if(new_connects == NULL)
		new_connects = xQueueCreate(MAX_CLIENT, sizeof(SOCKET));

	USART_Printf(USART2, "\tCheck there is still more bytes in RX queue\r\n");
	/* Make sure there is no pending message of ESP8266 RX data. */
	while(USART_Readable(USART6)) {
		snprintf(debug, 80, "\tStill %d bytes in RX queue\r\n", USART_Readable(USART6));
		USART_Printf(USART2, debug);
		vTaskDelay(100);
	}
	USART_Printf(USART2, "\tTry to get ESP8266 channel mutex\r\n");
	/* Block to take ESP8266 UART channel usage mutex. */
	while(xSemaphoreTake(xUSART_Mutex, 0) != pdTRUE) {
		USART_Printf(USART2, "\tStill can't get EXP8266 channel mutex\r\n");
		vTaskDelay(50);
	}
	/* Enable ESP8266 multiple connections. */
	USART_Printf(USART2, "\tGoing to send MUX=1 AT command\r\n");
	USART_Send(USART6, mul_con, strlen(mul_con), NON_BLOCKING);
	l = strlen(mul_con) + strlen("\r\nOK\r\n") + 1;
	n = 0;
	do {
		n += USART_Read(USART6, res, l-n, BLOCKING);
		snprintf(debug, 80, "\tIn set MUX=1 response.  Get %d bytes\r\n", n);
		USART_Printf(USART2, debug);
	} while(n < l);
	res[n] = '\0';

	if(strncmp(res, "AT+CIPMUX=1\r\r\n\r\nOK\r\n", 20) != 0) {
		for(n=0; n<l; n++) {
			snprintf(debug, 80, "\t\t%d\r\n", res[n]);
			USART_Printf(USART2, debug);
		}
		/* Releas ESP8266 UART channel usage mutex. */
		xSemaphoreGive(xUSART_Mutex);
		return -1;
	}

	/* Set ESP8266 as server and listening on designated port. */
	snprintf(as_server, 30, "AT+CIPSERVER=1,%d\r\n", port);
	USART_Printf(USART2, "\tGoing to send ");
	USART_Printf(USART2, as_server);
	USART_Send(USART6, as_server, strlen(as_server), NON_BLOCKING);
	l = strlen(as_server) + strlen("\r\nOK\r\n") + 1;
	n = 0;
	do {
		n += USART_Read(USART6, res, l-n, BLOCKING);
		snprintf(debug, 80, "\tIn set server listening on %d response.  Get %d bytes\r\n", port, n);
		USART_Printf(USART2, debug);
	} while(n < l);
	res[n] = '\0';

	/* Releas ESP8266 UART channel usage mutex. */
	xSemaphoreGive(xUSART_Mutex);

	snprintf(as_server, 30, "AT+CIPSERVER=1,%d\r\r\n\r\nOK\r\n", port);
	if(strncmp(res, as_server, strlen(as_server)) == 0) {
		USART_Printf(USART2, "\tBind socket ok!\r\n");
		return 0;
	}
	else {
		for(n=0; n<l; n++) {
			snprintf(debug, 80, "\t\t%d\r\n", res[n]);
			USART_Printf(USART2, debug);
		}
		USART_Printf(USART2, "\tBind socket failed!\r\n");
		errno = EBADF;
		return -1;
	}
}

SOCKET AcceptTcpSocket(void) {
	SOCKET s;

	char debug[80];

	if(xQueueReceive(new_connects, &s, 0) == pdTRUE) {
		/* Have a new connected socket. */
		/* Check is there still new clients from server socket. */
		snprintf(debug, 80, "\tGet a client socket %d from accept pool\r\n", s);
		USART_Printf(USART2, debug);
		if(uxQueueMessagesWaiting(new_connects) > 0)
			_SET_BIT(svrsock.state, SOCKET_READABLE);
		else
			_CLR_BIT(svrsock.state, SOCKET_READABLE);

		return s;
	}
	else {
		/* No new connected socket. */
		USART_Printf(USART2, "\tGet a client socket from accept poll failed\r\n");
		errno = ENODATA;
		return -1;
	}
}

ssize_t RecvSocket(SOCKET s, void *buf, size_t len, int f) {
	uint16_t id = Sock2ID(s);
	uint16_t i;
	uint8_t *pBuf;
	uint8_t c;

	char debug[30];

	pBuf = buf;

	for(i=0; i<len; i++) {
		if(xQueueReceive(clisock[id].rxQueue, &c, 0)) {
			pBuf[i] = c;
		}
		else {
			break;
		}
	}
	snprintf(debug, 30, "\tReceive %d bytes to task\r\n", i);
	USART_Printf(USART2, debug);
	/* Check there are still more bytes to be read. */
	if(uxQueueMessagesWaiting(clisock[id].rxQueue) > 0)
		_SET_BIT(clisock[id].state, SOCKET_READABLE);
	else
		_CLR_BIT(clisock[id].state, SOCKET_READABLE);

	return i;
}

void vSendSocketTask(void *__p) {
	SOCKET *ps;
	uint16_t id;
	uint16_t len;
	char send_header[30]; /* "AT+CIPSEND=id,len\r\n" */
	char res[20];
	ssize_t l, n;

#define MAX_SOCKETSENDBUFLEN	2048

	ps = (SOCKET *)__p;
	id = Sock2ID(*ps);

	/* Make sure there is no pending message of ESP8266 RX data. */
	while(!USART_Readable(USART6)) {
		vTaskDelay(100);
	}
	/* Block to take ESP8266 UART channel usage mutex. */
	while(xSemaphoreTake(xUSART_Mutex, 0) != pdTRUE) {
		/* Wait for ESP8266 UART channel usage mutex. */
		vTaskDelay(50);
	}

	while(clisock[id].slen > 0) {
		/* Split going to send packet into frame size. */
		if(clisock[id].slen > MAX_SOCKETSENDBUFLEN) {
			len = MAX_SOCKETSENDBUFLEN;
		}
		else {
			len = clisock[id].slen;
		}

		/* Have send frame header which is send command. */
		snprintf(send_header, 30, "AT+CIPSEND=%d,%d\r\n", id, len);
		/* Send socket send command to ESP8266. */
		USART_Send(USART6, send_header, strlen(send_header), NON_BLOCKING);
		/* Have ESP8266 response message. */
		l = 7;
		n = 0;
		do {
			n += USART_Read(USART6, res, l-n, BLOCKING);
		} while(n < l);
		res[l] = '\0';

		if(strncmp(res, "\r\nOK\r\n>", 7) != 0) {
			break;
		}

		/* Send socket payload to ESP8266. */
		USART_Send(USART6, clisock[id].sbuf, len, NON_BLOCKING);
		/* Have ESP8266 response message. */
		for(n=0; (n < 19); n++) {
			while(USART_Read(USART6, res+n, 1, BLOCKING) <= 0);
			if(res[n] == '\n') {
				n++;
				break;
			}
		}
		res[n] = 0;

		if(sscanf(res, "Recv %d bytes\r\n", &len) > 0) {
			clisock[id].slen -= len;
			clisock[id].sbuf += len;
		}
		else {
			break;
		}
	}

	/* Releas ESP8266 UART channel usage mutex. */
	xSemaphoreGive(xUSART_Mutex);
	/* Finish writing and clear the socket is not writing now. */
	_CLR_BIT(clisock[id].state, SOCKET_WRITING);
	/* Delete send socket task after it is finished. */
	vTaskDelete(NULL);
}

ssize_t SendSocket(SOCKET s, void *buf, size_t len, int f) {
	uint16_t id = Sock2ID(s);
	BaseType_t xReturned;

	if(!_ISBIT_SET(clisock[id].state, SOCKET_WRITING)) {
		/* The socket is writeable. */
		/* Set the socket is busy in writing now. */
		_SET_BIT(clisock[id].state, SOCKET_WRITING);
		clisock[id].sbuf = buf;
		clisock[id].slen = len;
		xReturned = xTaskCreate(vSendSocketTask,
								"Socket Send Task",
								128,
								&(clisock[id].fd),
								tskIDLE_PRIORITY,
								NULL);
		errno = EAGAIN;
	}
	else {
		/* The socket is busy in writing now. */
		errno = EBUSY;
	}

	return -1;
}

void vCloseSocketTask(void *__p) {
	SOCKET *ps;
	uint16_t id;
	char sd_sock[18]; /* Going to be "AT+CIPCLOSE=id\r\n" */
	char res[20];
	uint8_t n;
	uint8_t num_spliter;

	char debug[30];

	ps = (SOCKET *)__p;
	id = Sock2ID(*ps);

	/* Have close socket send command. */
	snprintf(sd_sock, sizeof(sd_sock), "AT+CIPCLOSE=%d\r\n", id);

	/* Make sure there is no pending message of ESP8266 RX data. */
	while(!USART_Readable(USART6)) {
		vTaskDelay(100);
	}
	/* Block to take ESP8266 UART channel usage mutex. */
	while(xSemaphoreTake(xUSART_Mutex, 0) != pdTRUE) {
		/* Wait for ESP8266 UART channel usage mutex. */
		vTaskDelay(50);
	}
	/* Send close socket command to ESP8266. */
	USART_Send(USART6, sd_sock, strlen(sd_sock), NON_BLOCKING);
	/* Have ESP8266 response message. */
	num_spliter = 0;
	for(n=0; n<20; n++) {
		while(USART_Read(USART6, res+n, 1, BLOCKING) <= 0) {
			vTaskDelay(100);
		}
		if(res[n] == '\n')
			num_spliter++;
		if(num_spliter == 3) {
			n++;
			break;
		}
	}
	res[n] = 0;

	/* Releas ESP8266 UART channel usage mutex. */
	xSemaphoreGive(xUSART_Mutex);
	snprintf(debug, 30, "\tReceive %d bytes to task\r\n", n);
	USART_Printf(USART2, debug);

	if(sscanf(res, "%d,CLOSED\r\n\r\nOK\r\n", &id) > 0) {
		clisock[id].state = 0;
	}
	/* Delete close socket task after the socket is closed. */
	vTaskDelete(NULL);
}

int ShutdownSocket(SOCKET s, int how) {
	uint16_t id = Sock2ID(s);
	BaseType_t xReturned;

	if(_ISBIT_SET(clisock[id].state, SOCKET_USING)) {
		xReturned = xTaskCreate(vCloseSocketTask,
								"Close Socket Task",
								configMINIMAL_STACK_SIZE,
								&(clisock[id].fd),
								tskIDLE_PRIORITY,
								NULL);
	}
	return 0;
}

int IsSocketReady2Read(SOCKET s) {
	uint16_t id = Sock2ID(s);
	uint8_t mask = (1 << SOCKET_USING) | (1 << SOCKET_READABLE);

	if((0 <= id) && (id < MAX_CLIENT))
		return (clisock[id].state & mask) > 0;
	else
		return 0;
}

int IsSocketReady2Write(SOCKET s) {
	uint16_t id = Sock2ID(s);
	uint8_t mask = (1 << SOCKET_USING) | (1 << SOCKET_WRITING);

	if((0 <= id) && (id < MAX_CLIENT))
		return (clisock[id].state & mask) == (1 << SOCKET_USING);
	else
		return 0;
}
