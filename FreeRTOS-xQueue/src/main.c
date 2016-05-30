/*
 * This program turns on the 4 leds of the stm32f4 discovery board
 * with pressed user button in order.
 */

/* Include STM32F4 and standard peripherals configuration headers. */
#include <stm32f4xx.h>
#include "stm32f4xx_conf.h"

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "usart.h"

#include <string.h>

QueueHandle_t Q;

/* Task 1. */
void main_task1() {
	int i = 0;
	char str[] = "Hello world!\r\n";
	while(1) {
		//while(USART_Send(USART6, str, strlen(str), NON_BLOCKING) < 0);
		if(xQueueSendToBack(Q, &str[i], 50) == pdPASS)
			i = (i + 1) % strlen(str);
	}
}

/* Task 2. */
void main_task2() {
	uint8_t c;
	USART_EnableRxPipe(USART6);
	while(1) {
		//if(xQueueReceive(Q, &c, 50))
		//	USART_SendByte(USART2, c);
		if(USART_Read(USART6, &c, 1, NON_BLOCKING))
			USART_SendByte(USART2, c);
	}
}

/* Main function, the entry point of this program.
 * The main function is called from the startup code in file
 * Libraries/CMSIS/Device/ST/STM32F4xx/Source/Templates/TrueSTUDIO/
 * startup_stm32f40_41xxx.s  (line 107)
 */
int main(void) {
    setup_usart2();
	setup_usart();
	Q = xQueueCreate(16, sizeof(uint8_t));
	USART_Printf(USART2, "Going to go!\r\n");
	/* Add the main task into FreeRTOS task scheduler. */
	//xTaskCreate(main_task1, "Main Task1", 512, NULL, tskIDLE_PRIORITY, NULL);
	xTaskCreate(main_task2, "Main Task2", 512, NULL, tskIDLE_PRIORITY, NULL);

	/* Start FreeRTOS task scheduler. */
	vTaskStartScheduler();

    return 0; // never returns actually
}
