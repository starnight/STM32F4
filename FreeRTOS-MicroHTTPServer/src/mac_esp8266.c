#if 0
#include <string.h>
#include <stdio.h>
#include "bits/socket.h"
#include "bits/mac_esp8266.h"
#include "usart.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "gpio.h"

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
#define WIFICONNECTED	1
#define CONNECTED		2
#define RES_INITIAL		3
#define RES_DATA		4
#define RES_SENDDATA1	5
#define RES_SENDDATA2	6
#define RES_CLOSESOCKET	7
#define RES_END			8
#define REQ_INITIAL		9
#define REQ_DATA		10
#define REQ_END			11
#define CLOSED			12

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

void PopInRing(void) {
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

	GPIO_ResetBits(LEDS_GPIO_PORT, RED);
	for(r_len=0; (r_state == UNKNOW) && IsUSARTReadable(r_len); r_len++) {
		idx = (USART_rIdx + USART_READBUFLEN + r_len) % USART_READBUFLEN;
		if((USART_rBuf[idx] == ',') || (USART_rBuf[idx] == ':')) {
			/* Read byte is a splitting character. */
			num_spliter++;
		}
		if(USART_rBuf[idx] == 'W') {
			/* This must be connected to wifi message. */
			r_state = WIFICONNECTED;
			pPar = NULL;
			USART_rIdx = idx;
			GPIO_SetBits(LEDS_GPIO_PORT, BLUE);
			break;
		}
		else if(USART_rBuf[idx] == '+') {
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

void WifiConnected(void *pBuf) {
	uint16_t r_len;
	uint16_t idx;
	uint8_t num_spliter = 0;
	char wifi_str[] = "WIFI CONNECTED\r\nWIFI GOT IP\r\n";
	uint16_t l = strlen(wifi_str);

	for(r_len=0; (r_state == WIFICONNECTED) && IsUSARTReadable(r_len); r_len++) {
		idx = (USART_rIdx + USART_READBUFLEN + r_len) % USART_READBUFLEN;
		if(USART_rBuf[idx] == '\n')
			num_spliter++;
		if((num_spliter == 2) &&
			(USART_rBuf[idx] == '\n')) {
			/* Parse wifi connected message if message is completed. */
			if(CmpInRing(USART_rIdx+r_len-l+1, wifi_str, l - 1)) {
				USART_rIdx = idx;
				PopInRing();
				GPIO_SetBits(LEDS_GPIO_PORT, BLUE);
			}
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
		USART_rBuf[USART_wIdx] = USART_ReadByte(USART6);//usart);
#ifdef MIRRO_USART6
		USART_SendByte(USART2, USART_rBuf[USART_wIdx]);
#endif
		USART_wIdx++;
		/* Make sure the ESP8266 receive from USART hadler is working. */
		if(eTaskGetState(rTask) == eSuspended) {
			xTaskResumeFromISR(rTask);
		}
	}
#if 0
	USART_SendByte(USART2, USART_ReadByte(USART6));
#endif
}

/* ESP8266 UART receive task. */
void vESP8266RTask(void *__p) {
	while(1) {
		GPIO_ResetBits(LEDS_GPIO_PORT, ORANGE);
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
	BaseType_t xReturned;

	/* Zero client sockets' state. */
	for(i=0; i<MAX_CLIENT+1; i++) {
		clisock[i].state = 0;
	}
	/* Zero server socket's state. */
	svrsock.state = 0;
	/* Regist the callback ESP8266 for USART receive interrupt. */
	RegistUSART6OnReceive(OnUSARTReceive, USART6);
	/* Start USART for ESP8266. */
	setup_usart();

	r_state = UNKNOW;
	pr_state = UNKNOW;
	pPar = NULL;
	new_connects == NULL;

	xReturned = xTaskCreate(vESP8266RTask,
							"ESP8266 UART RX",
							1024,
							NULL,
							tskIDLE_PRIORITY,
							&rTask);
	if(xReturned == pdPASS)
		GPIO_SetBits(LEDS_GPIO_PORT, BLUE);
}

void BindTcpSocket(uint16_t port) {
	char mul_con[] = "AT+CIPMUX=1\r\n";
	char as_server[] = "AT+CIPSERVER=1,8001\r\n";
	TaskHandle_t task;

	USART_rIdx = 0;
	USART_wIdx = 0;
	USART_ovr = 0;

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
								,
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
#endif

//------------------------------------------------------------------------------
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

#define MAX_SOCKETBUFLEN	1024

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

#define ESP8266_NONE			0
#define ESP8266_LINKED			1
#define ESP8266_SEND_CMD_MODE	2
#define ESP8266_REQ_MODE		3

uint8_t ESP8266_state;

/* ESP8266 UART channel usage mutex. */
SemaphoreHandle_t xUSART_Mutex = NULL;

#define USART_RXBUFLEN	64

uint8_t USART_rBuf[USART_RXBUFLEN];
uint16_t USART_rIdx;
uint16_t USART_wIdx;

#define IsNumChar(c) (('0' <= c) && (c <= '9'))

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
					/* Notify os client socket has something to be read. */
					_SET_BIT(s->state, SOCKET_READABLE);
				}
			}
			else {
				/* Wait for read 1 byte from ESP8266 UART channel. */
				vTaskDelay(portTICK_PERIOD_MS);
			}
		}

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

	/* Have request header from ESP8266 UART channel. */
	USART_wIdx=0;
	do {
		if(USART_Read(USART6, &c, 1, NON_BLOCKING) > 0) {
			USART_rBuf[USART_wIdx] = c;
		}
		USART_wIdx++;
	} while(((c != '\n') || (c != ':')) && (USART_wIdx < USART_RXBUFLEN-1));
	USART_rBuf[USART_wIdx] = '\0';

	/* Try to parse request header. */
	USART_rIdx = 0;
	if(USART_wIdx < 4) {
		/* Useless message. */
	}
	if(IsNumChar(USART_rBuf[0])) {
		/* Number of ID part. */
		for(USART_rIdx++;
			IsNumChar(USART_rBuf[USART_rIdx]) &&
				(USART_rIdx < (USART_wIdx - strlen(",CONNECT\r\n")));
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
	else if(strncmp(USART_rBuf, "WIFI GOT IP", 11) == 0) {
		/* Change ESP8266 UART channel state is ready for internet usage. */
		ESP8266_state = ESP8266_LINKED;
		GPIO_SetBits(LEDS_GPIO_PORT, BLUE);
	}
	else if(strncmp(USART_rBuf, "WIFI CONNECTED", 14) == 0) {
		/* Ignore for now. */
	}
}

/* ESP8266 UART channel request parsing task. */
void vESP8266RTask(void *__p) {
	while(1) {
		/* Try to take ESP8266 UART channel usage mutex. */
		if(xSemaphoreTake(xUSART_Mutex, 0) == pdTRUE) {
			if(USART_Readable(USART6)) {
				/* There is a request from ESP8266 UART channel. */
				GetESP8266Request();
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
	ESP8266_state = ESP8266_NONE;

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
							tskIDLE_PRIORITY + 2,
							NULL);
	if(xReturned == pdPASS)
		GPIO_ResetBits(LEDS_GPIO_PORT, BLUE);
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
	char as_server[24];
	char res[7];
	TaskHandle_t task;
	ssize_t l, n;

	task = xTaskGetCurrentTaskHandle();
	if(new_connects == NULL)
		new_connects = xQueueCreate(MAX_CLIENT, sizeof(SOCKET));

	/* Make sure there is no pending message of ESP8266 RX data. */
	while(!USART_Readable(USART6)) {
		vTaskDelay(100);
	}
	/* Block to take ESP8266 UART channel usage mutex. */
	while(xSemaphoreTake(xUSART_Mutex, 0) != pdTRUE) {
		vTaskDelay(50);
	}
	/* Enable ESP8266 multiple connections. */
	USART_Send(USART6, mul_con, strlen(mul_con), NON_BLOCKING);
	l = 6;
	n = 0;
	do {
		n += USART_Read(USART6, res, l-n, BLOCKING);
	} while(n < l);
	res[l] = '\0';

	if(strncmp(res, "\r\nOK\r\n", 6) != 0) {
		/* Releas ESP8266 UART channel usage mutex. */
		xSemaphoreGive(xUSART_Mutex);
		return -1;
	}

	/* Set ESP8266 as server and listening on designated port. */
	snprintf(as_server, 24, "AT+CIPSERVER=1,%d\r\n", port);
	USART_Send(USART6, as_server, strlen(as_server), NON_BLOCKING);
	l = 6;
	n = 0;
	do {
		n += USART_Read(USART6, res, l-n, BLOCKING);
	} while(n < l);
	res[l] = '\0';

	/* Releas ESP8266 UART channel usage mutex. */
	xSemaphoreGive(xUSART_Mutex);

	if(strncmp(res, "\r\nOK\r\n", 6) == 0) {
		return 0;
	}
	else {
		errno = EBADF;
		return -1;
	}
}

SOCKET AcceptTcpSocket(void) {
	SOCKET s;

	if(xQueueReceive(new_connects, &s, 0) == pdTRUE) {
		/* Have a new connected socket. */
		/* Check is there still new clients from server socket. */
		if(uxQueueMessagesWaiting(new_connects) > 0)
			_SET_BIT(svrsock.state, SOCKET_READABLE);
		else
			_CLR_BIT(svrsock.state, SOCKET_READABLE);

		return s;
	}
	else {
		/* No new connected socket. */
		errno = ENODATA;
		return -1;
	}
}

ssize_t RecvSocket(SOCKET s, void *buf, size_t len, int f) {
	uint16_t id = Sock2ID(s);
	uint16_t i;
	uint8_t *pBuf;
	uint8_t c;

	pBuf = buf;

	for(i=0; i<len; i++) {
		if(xQueueReceive(clisock[id].rxQueue, &c, 0)) {
			pBuf[i] = c;
		}
		else {
			break;
		}
	}

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
	char send_header[22]; /* "AT+CIPSEND=id,len\r\n" */
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
		snprintf(send_header, 22, "AT+CIPSEND=%d,%d\r\n", id, len);
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
								tskIDLE_PRIORITY + 1,
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
		while(USART_Read(USART6, res+n, 1, BLOCKING) <= 0);
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
								tskIDLE_PRIORITY + 1,
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
