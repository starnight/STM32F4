/*
 * This program turns on the 4 leds of the stm32f4 discovery board
 * with pressed user button in order.
 */

/* Include STM32F4 and standard peripherals configuration headers. */
#include <stm32f4xx.h>
#include "stm32f4xx_conf.h"

#include "FreeRTOS.h"
#include "task.h"

#include "usart.h"

#include <string.h>

/* Task 1. */
void main_task1() {
	int i = 0;
	char str[] = "Hello world! i\r\n";
	while(1) {
		str[13] = i + '0';
		//USART_Printf(USART6, str);
		while(USART_Send(USART6, str, strlen(str), NON_BLOCKING) < 0);
		i = (i + 1) % 10;
	}
}

/* Task 2. */
void main_task2() {
	int i = 0;
	char str[] = "Go Go i! Then ...\r\n";
	while(1) {
		str[6] = i + '0';
		//USART_Printf(USART6, str);
		while(USART_Send(USART6, str, strlen(str), NON_BLOCKING) < 0);
		i = (i + 1) % 10;
	}
}

/* Main function, the entry point of this program.
 * The main function is called from the startup code in file
 * Libraries/CMSIS/Device/ST/STM32F4xx/Source/Templates/TrueSTUDIO/
 * startup_stm32f40_41xxx.s  (line 107)
 */
int main(void) {
    setup_usart();
	USART_Printf(USART6, "Hello world!\r\n");
	/* Add the main task into FreeRTOS task scheduler. */
	xTaskCreate(main_task1, "Main Task1", 512, NULL, tskIDLE_PRIORITY, NULL);
	xTaskCreate(main_task2, "Main Task2", 512, NULL, tskIDLE_PRIORITY, NULL);

	/* Start FreeRTOS task scheduler. */
	vTaskStartScheduler();

    return 0; // never returns actually
}
