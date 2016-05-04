#include <ctype.h>
#include <string.h>
#include "usart.h"

/* Define the structure of stream going to be written out. */
typedef struct _STREAM {
	uint8_t *pBuf;
	size_t BufLen;
} STREAM;


/* Global varables to hold the state of the streams going to be send. */
/* USART streams pool. */
STREAM usart_stream[MAX_USART_STREAM];
/* Index of current working stream. */
uint8_t usart_stream_idx;

/* Clear USART stream macro. */
#define ClearStream(stream) {(stream)->pBuf = NULL; (stream)->BufLen = 0;}

/* Initialize the USART6. */
void setup_usart(void) {
	USART_InitTypeDef USART_InitStructure;
	GPIO_InitTypeDef GPIO_InitStructure;

	/* Enable the GPIOA peripheral clock. */
	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOC, ENABLE);

	/* Make PC6, PC7 as alternative function of USART6. */
	GPIO_PinAFConfig(GPIOC, GPIO_PinSource6, GPIO_AF_USART6);
	GPIO_PinAFConfig(GPIOC, GPIO_PinSource7, GPIO_AF_USART6);

	/* Initialize PC6, PC7.  */
	GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
	GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_UP;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF;
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_6 | GPIO_Pin_7;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_100MHz;
	GPIO_Init(GPIOC, &GPIO_InitStructure);

	/* Enable the USART6 peripheral clock. */
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_USART6, ENABLE);

	/* Initialize USART6 with
	 * 115200 buad rate,
	 * 8 data bits,
	 * 1 stop bit,
	 * no parity check,
	 * none flow control.
	 */
	USART_InitStructure.USART_BaudRate = 115200;
	USART_InitStructure.USART_WordLength = USART_WordLength_8b;
	USART_InitStructure.USART_StopBits = USART_StopBits_1;
	USART_InitStructure.USART_Parity = USART_Parity_No;
	USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
	USART_InitStructure.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;
	USART_Init(USART6, &USART_InitStructure);

	/* Enable USART6. */
	USART_Cmd(USART6, ENABLE);

	/* Initial the USART streams. */
	for(usart_stream_idx = MAX_USART_STREAM;
		usart_stream_idx > 0;
		usart_stream_idx--)
		ClearStream(usart_stream + usart_stream_idx - 1);

	/* Enable USART6 RX interrupt. */
	USART_ITConfig(USART6, USART_IT_RXNE, ENABLE);
	/* Enable USART6 in NVIC vector. */
	NVIC_EnableIRQ(USART6_IRQn);
}

/* Send bytes array with designated length through USART. */
ssize_t USART_Send(USART_TypeDef *USARTx, void *buf, ssize_t l, uint8_t flags) {
	ssize_t i = 0;
	uint8_t idx;
	uint8_t *pBuf;

	pBuf = buf;

	/* Send with blocking mode. */
	if(flags == BLOCKING) {
		for(i=0; i<l; i++) {
			while(USART_GetFlagStatus(USARTx, USART_FLAG_TC) == RESET);
			USART_SendByte(USARTx, (uint16_t)(pBuf[i]));
		}
	}
	/* Send with non-blocking mode. */
	else if(flags == NON_BLOCKING) {
		for(i=0; i<MAX_USART_STREAM; i++) {
			idx = (i + usart_stream_idx) % MAX_USART_STREAM;
			if(usart_stream[idx].pBuf == NULL) {
				usart_stream[idx].pBuf = pBuf;
				usart_stream[idx].BufLen = l;
				/* Enable USART6 TX interrupt. */
				USART_ITConfig(USART6, USART_IT_TXE, ENABLE);
				break;
			}
		}
		if(i >= MAX_USART_STREAM)
			i = -1;
	}

	return i;
}

/* Print the string through USART with blocking. */
void USART_Printf(USART_TypeDef* USARTx, char *str) {
	USART_Send(USARTx, str, strlen(str), BLOCKING);
}

/* USART6 IRQ handler. */
void USART6_IRQHandler(void) {
	char c[] = "\0\r\n\0";
	uint8_t i;

	/* USART6 RX interrupt. */
	if(USART6->SR & USART_SR_RXNE) {
		c[0] = USART_ReadByte(USART6);

		USART_Printf(USART6, "Pressed key: ");
		USART_Printf(USART6, c);
	}

	/* USART6 TX interrupt. */
	if(USART6->SR & USART_SR_TXE) {
		if(usart_stream[usart_stream_idx].BufLen > 0) {
			USART_SendByte(USART6,
						   toupper(*usart_stream[usart_stream_idx].pBuf));
			usart_stream[usart_stream_idx].pBuf++;
			usart_stream[usart_stream_idx].BufLen--;
		}
		else {
			/* Current USART streaming is finished. */
			/* Release and clear current USART stream. */
			ClearStream(usart_stream + usart_stream_idx);
			/* Try to stream next USART stream which should be stream. */
			usart_stream_idx++;
			for(i=0; i<MAX_USART_STREAM; i++) {
				usart_stream_idx = (usart_stream_idx + i) % MAX_USART_STREAM;
				if(usart_stream[usart_stream_idx].pBuf != NULL)
					break;
			}
			/* Disable USART6 TX interrupt after all streams are finished. */
			if(i >= MAX_USART_STREAM)
				USART_ITConfig(USART6, USART_IT_TXE, DISABLE);
		}
	}
}
