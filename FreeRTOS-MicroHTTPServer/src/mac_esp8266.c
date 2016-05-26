#include <string.h>
#include <stdio.h>
#include "bits/socket.h"
#include "bits/mac_esp8266.h"
#include "usart.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

/* FreeRTOS UART send and receive tasks handler. */
TaskHandle_t sTask, rTask;

#define MAX_SOCKETBUFLEN	1024

typedef struct _sock_struct {
	uint32_t peer_ip; /* Peer IP address. */
	uint16_t peer_port; /* Peer port. */
	uint16_t fd; /* File descriptor of the socket. */
	uint16_t slen; /* Length of the payload going to be sent to peer. */
	uint16_t rlen; /* Length of the payload coming form peer. */
	uint16_t rIdx, wIdx; /* Read and write index of socket read buffer. */
	uint8_t *sbuf; /* Points to the buffer going to be sent. */
	uint8_t rbuf[MAX_SOCKETBUFLEN]; /* Socket read buffer. */
	uint8_t ovr; /* Is the socket read buffer overflow or not. */
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

#define IsSocketReadable(s) (( \
	(((s)->ovr == 0) && ((s)->rIdx < (s)->wIdx)) || \
	(((s)->ovr == 1) && ((s)->rIdx < MAX_SOCKETBUFLEN)) || \
	(((s)->ovr == 1) && ((s)->rIdx < ((s)->wIdx + MAX_SOCKETBUFLEN))) \
	) ? 1 : 0)

#define USART_READBUFLEN	64

uint8_t USART_rBuf[USART_READBUFLEN];
uint16_t USART_rIdx = 0;
uint16_t USART_wIdx = 0;
uint8_t USART_ovr = 0;

#define UNKNOW			0
#define CONNECTED		1
#define RES_INITIAL		2
#define RES_DATA		3
#define RES_SENDDATA1	4
#define RES_SENDDATA2	5
#define RES_CLOSESOCKET	6
#define RES_END			7
#define REQ_INITIAL		8
#define REQ_DATA		9
#define REQ_END			10
#define CLOSED			11

/* On USART receive callback function type. */
typedef void (*CALLBACK)(void *);

uint8_t r_state;
uint8_t pr_state;
CALLBACK r_event[12];
void *pPar;

#define CONNECT_MAX_NUM_SPLITER 1
#define IPD_MAX_NUM_SPLITER	3	

#define IsUSARTReadable(ofs)	(( \
	((USART_ovr == 0) && ((USART_rIdx + ofs) < USART_wIdx)) || \
	((USART_ovr == 1) && ((USART_rIdx + ofs) < USART_READBUFLEN)) || \
	((USART_ovr == 1) && ((USART_rIdx + ofs) < (USART_wIdx + USART_READBUFLEN))) \
	) ? 1 : 0)

inline void PopInRing(void) {
	if((USART_ovr == 0) && (USART_rIdx < USART_wIdx)) {
		USART_rIdx = (USART_rIdx + 1 + USART_READBUFLEN) % USART_READBUFLEN;
	}
	else if ((USART_ovr == 1) && (USART_rIdx >= USART_READBUFLEN)) {
		USART_rIdx = 0;
		USART_ovr = 0;
	}
	else if ((USART_ovr == 1) && (USART_rIdx > USART_wIdx)) {
		USART_rIdx++;
	}
}

uint8_t CmpInRing(uint16_t s_idx, void *b, size_t l) {
	size_t i;
	uint8_t f = 1;
	uint8_t *m;

	m = b;

	for(i = 0; i < l; i++) {
		if(USART_rBuf[s_idx] != m[i]) {
			f = 0;
			break;
		}
		s_idx = (s_idx + 1 + USART_READBUFLEN) % USART_READBUFLEN;
	}

	return f;
}

void Clear2Unknow(void) {
	if(((r_state <= CONNECTED) || (r_state >= REQ_INITIAL)) &&
		(pr_state != UNKNOW) && (r_state != pr_state)) {
		r_state = pr_state;
	}
	else {
		r_state = UNKNOW;
		pPar = NULL;
	}
}

int8_t CheckHealth(void) {
	char c[] = "AT\r\n";

	while(pr_state != UNKNOW);
	USART_Send(USART6, c, strlen(c), NON_BLOCKING);
	pr_state = RES_DATA;
	sTask = xTaskGetCurrentTaskHandle();
	vTaskSuspend(NULL);

	return 1;
}

void GetUnknow(void *pBuf) {
	uint16_t r_len;
	uint16_t idx;
	uint8_t num_spliter = 0;

	for(r_len=0; (r_state == UNKNOW) && IsUSARTReadable(r_len); r_len++) {
		idx = (USART_rIdx + USART_READBUFLEN + r_len) % USART_READBUFLEN;
		if((USART_rBuf[idx] == ',') || (USART_rBuf[idx] == ':')) {
			/* Read byte is a splitting character. */
			num_spliter++;
		}
		if(USART_rBuf[idx] == '+') {
			/* This must be request header of message. */
			r_state = REQ_INITIAL;
			pPar = NULL;
			break;
		}
		else if((num_spliter == 1) && (USART_rBuf[idx] == 'O')) {
			/* This must be connected message. */
			r_state = CONNECTED;
			pPar = NULL;
			break;
		}
		else if((num_spliter == 1) && (USART_rBuf[idx] == 'L')) {
			/* This must be closed message. */
			r_state = CLOSED;
			pPar = NULL;
			break;
		}
		else if(USART_rBuf[idx] == '\r') {
			Clear2Unknow();
			break;
		}
	}
}

void GetConnentedHeader(void *pBuf) {
	uint16_t r_len;
	uint16_t idx;
	uint16_t id = 0;
	SOCKET s;
	char con_str[] = "CONNECT\r\n";
	uint8_t num_spliter = 0;
	uint16_t l = strlen(con_str);

	for(r_len=0; (r_state == CONNECTED) && IsUSARTReadable(r_len); r_len++) {
		idx = (USART_rIdx + USART_READBUFLEN + r_len) % USART_READBUFLEN;
		if(num_spliter == 0) {
			if(USART_rBuf[idx] != ',')
				id = id*10 + USART_rBuf[idx]-'0';
			else {
				num_spliter++;
				USART_rIdx = idx;
				r_len = 0;
			}
		}
		else if((num_spliter == 1) &&
				//(USART_rBuf[idx - 1] == '\r') &&
				(USART_rBuf[idx] == '\n')) {
			/* Parse connect message if message is completed. */
			if(CmpInRing(USART_rIdx+r_len-l+1, con_str, l - 1)) {
				USART_rIdx = idx;
				PopInRing();
				/* Notify os server socket connected. */
				s = ID2Sock(id);
				xQueueSend(new_connects, &s, 0);
				_SET_BIT(svrsock.state, SOCKET_READABLE);
				/* Clear state of the client sock. */
				clisock[id].fd = ID2Sock(id);
				clisock[id].rlen = 0;
				clisock[id].rIdx = 0;
				clisock[id].wIdx = 0;
				clisock[id].ovr = 0;
				clisock[id].state = 0;
				//_SET_BIT(clisock[id].state, SOCKET_USING);
			}
			Clear2Unknow();
			break;
		}
	}
}

void GetRequestHeader(void *pBuf) {
	uint16_t r_len;
	uint16_t idx;
	uint16_t id = 0;
	uint16_t len = 0;
	char req_start[] = "\r\n+IPD,";
	uint8_t l = strlen(req_start);
	uint8_t num_spliter = 0;

	for(r_len=0; (r_state == REQ_INITIAL) && IsUSARTReadable(r_len); r_len++) {
		idx = (USART_rIdx + USART_READBUFLEN + r_len) % USART_READBUFLEN; 
		if(num_spliter < IPD_MAX_NUM_SPLITER) {
			if((USART_rBuf[idx]==',') || (USART_rBuf[idx]==':')) {
				/* Get a spliter charater. */
				num_spliter++;
				if(USART_rBuf[idx]==',')
					continue;
			}
		}
		if(num_spliter == 1) {
			if(r_len == (l - 1)) {
				/* Check connect header. */
				if(!CmpInRing(USART_rIdx, req_start, l)) {
					/* It is not connect header. */
					Clear2Unknow();
					break;
				}
			}
			else {
				/* ID */
				id = id*10 + USART_rBuf[idx] - '0';
			}
		}
		else if(num_spliter == 2) {
			/* Payload length. */
			len = len*10 + USART_rBuf[idx] - '0';
		}
		else if(num_spliter == IPD_MAX_NUM_SPLITER) {
			if(id < MAX_CLIENT) {
				/* Parse request header finished. */
				USART_rIdx = idx;
				clisock[id].rlen = len;
				r_state = REQ_DATA;
				pPar = (clisock + id);
			}
			else {
				// To do: close the socket if id > MAX_CLIENT
			}
			break;
		}
	}
}

void GetRequestData(void *pBuf) {
	_sock *s;
	uint8_t f;

	s = pBuf;

	for(; s->rlen>0; s->rlen--) {
		if(IsUSARTReadable(0)) {
			f = 0;
			if((s->ovr == 0) && (s->wIdx < MAX_SOCKETBUFLEN)) {
				f = 1;
			}
			else if((s->ovr == 0) &&
				(s->wIdx >= MAX_SOCKETBUFLEN) &&
				(s->rIdx > 0)) {
				f = 1;
				s->wIdx = 0;
				s->ovr = 1;
			}
			else if((s->ovr == 1) && (s->wIdx < s->rIdx)) {
				f = 1;
			}
			if(f) {
				s->rbuf[s->wIdx] = USART_rBuf[USART_rIdx];
				PopInRing();
				s->wIdx++;
				/* Notify os client socket has something to be read. */
				_SET_BIT(s->state, SOCKET_READABLE);
			}
		}
		else {
			break;
		}
	}

	if(s->rlen == 0) {
		/* Already read from USART to socket buffer end. */
		r_state = REQ_END;
		pPar = NULL;
	}
}

void GetReqEnd(void *pBuf) {
	uint16_t r_len;
	uint16_t idx;
	char end_str[] = "\r\n";
	uint16_t l = strlen(end_str);

	for(r_len=0; (r_state == REQ_END) && IsUSARTReadable(r_len); r_len++) {
		idx = (USART_rIdx + USART_READBUFLEN + r_len) % USART_READBUFLEN;
		if(r_len == (l - 1)) {
			if(CmpInRing(USART_rIdx, end_str, l)) {
				/* Read end. */
				USART_rIdx = idx;
				PopInRing();
			}
			Clear2Unknow();
			break;
		}
	}
}

void GetResponseData(void *pBuf) {
	uint16_t r_len;
	uint16_t idx;
	char end_str[] = "\r\n";
	uint16_t l = strlen(end_str);

	for(r_len=0; (r_state == RES_DATA) && IsUSARTReadable(r_len); r_len++) {
		idx = (USART_rIdx + USART_READBUFLEN + r_len) % USART_READBUFLEN;
		if(r_len == (l - 1)) {
			if(CmpInRing(USART_rIdx, end_str, l)) {
				/* Read end. */
				USART_rIdx = idx;
				PopInRing();
				r_state = RES_END;
			}
			else {
				Clear2Unknow();
			}
			break;
		}
	} 
}

void GetResponseSendData1(void *pBuf) {
	uint16_t r_len;
	uint16_t idx;
	char send1_str[] = "\r\nOK\r\n>";
	uint16_t l = strlen(send1_str);

	for(r_len=0; (r_state == RES_SENDDATA1) && IsUSARTReadable(r_len); r_len++) {
		idx = (USART_rIdx + USART_READBUFLEN + r_len) % USART_READBUFLEN;
		if(r_len == (l - 1)) {
			if(CmpInRing(USART_rIdx, send1_str, l)) {
				/* Read end. */
				USART_rIdx = idx;
				PopInRing();
				if(sTask != NULL) {
					/* Resume suspended task if needed. */
					vTaskResume(sTask);
					sTask = NULL;
				}
				pr_state = UNKNOW;
			}
			Clear2Unknow();
			break;
		}
	}
}

void GetResponseSendData2(void *pBuf) {
	uint16_t r_len;
	uint16_t idx;
	uint16_t l = 0;
	uint16_t idx2;
	char send1_str[] = "Recv ";
	char send2_str[] = "bytes\r\n\r\nSEND ";
	uint16_t num_spliter = 0;
	uint16_t l1 = strlen(send1_str);
	uint16_t l2 = strlen(send2_str);

	for(r_len=0; (r_state == RES_SENDDATA2) && IsUSARTReadable(r_len); r_len++) {
		idx = (USART_rIdx + USART_READBUFLEN + r_len) % USART_READBUFLEN;
		if(USART_rBuf[idx] == ' ') {
			num_spliter++;
		}
		if(r_len == (l1 - 1)) {
			/* Read 1st string end. */
			if(!CmpInRing(USART_rIdx, send1_str, l)) {
				Clear2Unknow();
				break;
			}
		}
		else if((r_len >= l1) && (num_spliter == 1)) {
			/* Read number of sent bytes. */
			l = l*10 + USART_rBuf[idx] - '0';
		}
		else if((num_spliter == 2) && (USART_rBuf[idx] == ' ')) {
			/* Read number of sent bytes which is finished. */
			idx2 = idx + 1;
		}
		else if((num_spliter == 3) && (USART_rBuf[idx] == ' ')) {
			/* Read 2nd string end. */
			if(CmpInRing(idx2, send2_str, l2 - 1)) {
				USART_rIdx = idx;
				PopInRing();
				r_state = RES_END;
			}
			else {
				Clear2Unknow();
			}
			break;
		}
	}
}

void GetResponseCloseSocket(void *pBuf) {
	uint16_t r_len;
	uint16_t idx;
	uint16_t id = 0;
	char cls_str[] = "CLOSED\r\n";
	uint8_t num_spliter = 0;
	uint16_t l = strlen(cls_str);

	for(r_len=0; (r_state == CLOSED) && IsUSARTReadable(r_len); r_len++) {
		idx = (USART_rIdx + USART_READBUFLEN + r_len) % USART_READBUFLEN;
		if(num_spliter == 0) {
			if(USART_rBuf[idx] != ',')
				id = id*10 + USART_rBuf[idx]-'0';
			else {
				num_spliter++;
				USART_rIdx = idx;
				r_len = 0;
			}
		}
		else if((num_spliter == 1) &&
				//(USART_rBuf[idx - 1] == '\r') &&
				(USART_rBuf[idx] == '\n')) {
			/* Parse close message if message is completed. */
			if(CmpInRing(USART_rIdx+r_len-l+1, cls_str, l - 1)) {
				USART_rIdx = idx;
				PopInRing();
				r_state = RES_END;
				// To do: Notify os socket closed.

			}
			else {
				Clear2Unknow();
			}
			break;
		}
	}
}

void GetResponseEnd(void *pBuf) {
	uint16_t r_len;
	uint16_t idx;
	char end_str[] = "OK\r\n";
	uint16_t l = strlen(end_str);

	for(r_len=0; (r_state == RES_END) && IsUSARTReadable(r_len); r_len++) {
		idx = (USART_rIdx + USART_READBUFLEN + r_len) % USART_READBUFLEN;
		if(r_len == (l - 1)) {
			if(CmpInRing(USART_rIdx, end_str, l)) {
				/* Read end. */
				USART_rIdx = idx;
				PopInRing();
				if(sTask != NULL) {
					/* Resume suspended task if needed. */
					vTaskResume(sTask);
					sTask = NULL;
				}
				pr_state = UNKNOW;
			}
			Clear2Unknow();
			break;
		}
	}
}

void GetClosedHeader(void *pBuf) {
	uint16_t r_len;
	uint16_t idx;
	uint16_t id = 0;
	char cls_str[] = "CLOSED\r\n";
	uint8_t num_spliter = 0;
	uint16_t l = strlen(cls_str);

	for(r_len=0; (r_state == CLOSED) && IsUSARTReadable(r_len); r_len++) {
		idx = (USART_rIdx + USART_READBUFLEN + r_len) % USART_READBUFLEN;
		if(num_spliter == 0) {
			if(USART_rBuf[idx] != ',')
				id = id*10 + USART_rBuf[idx]-'0';
			else {
				num_spliter++;
				USART_rIdx = idx;
				r_len = 0;
			}
		}
		else if((num_spliter == 1) &&
				//(USART_rBuf[idx - 1] == '\r') &&
				(USART_rBuf[idx] == '\n')) {
			/* Parse close message if message is completed. */
			if(CmpInRing(USART_rIdx+r_len-l+1, cls_str, l - 1)) {
				USART_rIdx = idx;
				PopInRing();
				// To do: Notify os socket closed.

			}
			Clear2Unknow();
			break;
		}
	}
}

void OnUSARTReceive(void *pBuf) {
	uint8_t f = 0;
	USART_TypeDef *usart;

	usart = pBuf;

	if((USART_ovr == 0)
		&& (USART_wIdx < USART_READBUFLEN)) {
		f = 1;
	}
	else if((USART_ovr == 0)
		&& (USART_wIdx >= USART_READBUFLEN)
		&& (0 < USART_rIdx)) {
		f = 1;
		USART_wIdx = 0;
		USART_ovr = 1;
	}
	else if((USART_ovr == 1)
		&& (USART_wIdx < USART_rIdx)) {
		f = 1;
	}
	if(f) {
		/* Read a byte. */
		USART_rBuf[USART_wIdx] = USART_ReadByte(usart);
		USART_wIdx++;
		/* Make sure the ESP8266 receive from USART hadler is working. */
		if(eTaskGetState(rTask) == eSuspended) {
			xTaskResumeFromISR(rTask);
		}
	}
}

/* ESP8266 UART receive task. */
void vESP8266RTask(void *__p) {
	while(1) {
		if(IsUSARTReadable(0)) {
			r_event[r_state](pPar);
		}
		else {
			vTaskSuspend(NULL);
		}
	}
}

void InitESP8266(void) {
	uint16_t i;

	/* Zero client sockets' state. */
	for(i=0; i<MAX_CLIENT+1; i++) {
		clisock[i].state = 0;
	}
	/* Zero server socket's state. */
	svrsock.state = 0;
	/* Start USART for ESP8266. */
	setup_usart();
	/* Regist the callback ESP8266 for USART receive interrupt. */
	RegistUSART6OnReceive(OnUSARTReceive, USART6);

	r_state = UNKNOW;
	pr_state = UNKNOW;
	pPar = NULL;
	new_connects == NULL;

	xTaskCreate(vESP8266RTask,
				"ESP8266 Driver UART receive",
				128,
				NULL,
				tskIDLE_PRIORITY,
				&rTask);
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
		return -1;
	}
}

void BindTcpSocket(uint16_t port) {
	char mul_con[] = "AT+CIPMUX=1\r\n";
	char as_server[] = "AT+CIPSERVER=1,8001\r\n";
	TaskHandle_t task;

	task = xTaskGetCurrentTaskHandle();
	if(new_connects == NULL)
		new_connects = xQueueCreate(MAX_CLIENT, sizeof(SOCKET));

	while(pr_state != UNKNOW); /* Block to wait USART send finished. */
	pr_state = RES_DATA;
	USART_Send(USART6, mul_con, strlen(mul_con), NON_BLOCKING);
	sTask = task;
	vTaskSuspend(NULL);

	while(pr_state != UNKNOW); /* Block to wait USART send finished. */
	pr_state = RES_DATA;
	USART_Send(USART6, as_server, strlen(as_server), NON_BLOCKING);
	sTask = task;
	vTaskSuspend(NULL);
}

SOCKET AcceptTcpSocket(void) {
	SOCKET s;

	if(xQueueReceive(new_connects, &s, 0) == pdTRUE) {
		clisock[Sock2ID(s)].state = 0;
		_SET_BIT(clisock[Sock2ID(s)].state, SOCKET_USING);
		
		if(uxQueueMessagesWaiting(new_connects) > 0)
			_SET_BIT(svrsock.state, SOCKET_READABLE);
		else
			_CLR_BIT(svrsock.state, SOCKET_READABLE);

		return s;
	}
	else {
		return -1;
	}
}

void vSendSocketTask(void *__p) {
	SOCKET *ps;
	TaskHandle_t task;
	uint16_t id;
	char send_header[22]; /* "AT+CIPSEND=id,len\r\n" */

	ps = (SOCKET *)__p;
	id = Sock2ID(*ps);
	snprintf(send_header,
				strlen(send_header),
				"AT+CIPSEND=%d,%d\r\n",
				id,
				clisock[id].slen);
	task = xTaskGetCurrentTaskHandle();

	/* Send socket send command. */
	while(pr_state != UNKNOW); /* Block to wait USART send finished. */
	pr_state = RES_SENDDATA1;
	USART_Send(USART6, send_header, strlen(send_header), NON_BLOCKING);
	sTask = task;
	vTaskSuspend(NULL);

	/* Send socket payload. */
	while(pr_state != UNKNOW); /* Block to wait USART send finished. */
	pr_state = RES_SENDDATA2;
	USART_Send(USART6, clisock[id].sbuf, clisock[id].slen, NON_BLOCKING);
	sTask = task;
	vTaskSuspend(NULL);

	/* Finish writing and clear the socket is writing now. */
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
								&s,
								tskIDLE_PRIORITY,
								NULL);
	}
	else {
		/* The socket is busy in writing now. */
	}

	return -1;
}

ssize_t RecvSocket(SOCKET s, void *buf, size_t len, int f) {
	uint16_t id = Sock2ID(s);
	uint16_t rlen;
	uint16_t idx;
	uint8_t *pBuf;

	pBuf = buf;

	for(rlen=0; (rlen<len) && IsSocketReadable(clisock + id); rlen++) {
		if((clisock[id].ovr == 0) &&
			(clisock[id].rIdx < clisock[id].wIdx)) {
			pBuf[rlen] = clisock[id].rbuf[clisock[id].rIdx];
			clisock[id].rIdx++;
		}
		else if((clisock[id].ovr == 1) &&
				(clisock[id].rIdx < MAX_SOCKETBUFLEN)) {
			pBuf[rlen] = clisock[id].rbuf[clisock[id].rIdx];
			clisock[id].rIdx++;
		}
		else if((clisock[id].ovr == 1) &&
				(clisock[id].rIdx < clisock[id].wIdx + MAX_SOCKETBUFLEN)) {
			clisock[id].rIdx = 0;
			clisock[id].ovr = 0;
			pBuf[rlen] = clisock[id].rbuf[clisock[id].rIdx];
		}
	}

	/* Check there are still more bytes to be read. */
	if(IsSocketReadable(clisock + id))
		_SET_BIT(clisock[id].state, SOCKET_READABLE);
	else
		_CLR_BIT(clisock[id].state, SOCKET_READABLE);

	return rlen;
}

void vCloseSocketTask(void *__p) {
	SOCKET *ps;
	uint16_t id;
	char sd_sock[18]; /* Going to be "AT+CIPCLOSE=id\r\n" */

	ps = (SOCKET *)__p;
	id = Sock2ID(*ps);

	/* ID will be 0~4 in this practice.
	 * Should use itoa liked function to asign ID string in real world.
	 */
	snprintf(sd_sock, sizeof(sd_sock), "AT+CIPCLOSE=%d\r\n", id);

	while(pr_state != UNKNOW); /* Block to wait USART send finished. */
	pr_state = RES_CLOSESOCKET;
	USART_Send(USART6, sd_sock, strlen(sd_sock), NON_BLOCKING);
	sTask = xTaskGetCurrentTaskHandle();
	vTaskSuspend(NULL);

	clisock[id].state = 0;

	/* Delete close socket task after the socket is closed. */
	vTaskDelete(NULL);
}

int ShutdownSocket(SOCKET s, int how) {
	uint16_t id = Sock2ID(s);
	BaseType_t xReturned;

	if(_ISBIT_SET(clisock[id].state, SOCKET_USING)) {
		xReturned = xTaskCreate(vCloseSocketTask,
								"Close Socket Task",
								128,
								&s,
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
