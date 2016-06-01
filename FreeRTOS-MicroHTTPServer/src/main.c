/*
 * This program turns on the 4 leds of the stm32f4 discovery board
 * with pressed user button in order.
 */

/* Include STM32F4 and standard peripherals configuration headers. */
#include <stm32f4xx.h>
#include "stm32f4xx_conf.h"

#include "FreeRTOS.h"
#include "task.h"

#include "gpio.h"
#include "usart.h"
#include "bits/mac_esp8266.h"
#include "server.h"
#include "middleware.h"
#include "app.h"

/* Micro HTTP Server. */
void MicroHTTPServer_task() {
	HTTPServer srv;
	uint32_t ip;

	/* Make sure the internet is worked. */
	while(GetESP8266State() != ESP8266_LINKED) {
		vTaskDelay(5000 * portTICK_PERIOD_MS);
	}

	GPIO_ResetBits(LEDS_GPIO_PORT, GREEN);
	HaveInterfaceIP(&ip);
	
	AddRoute(HTTP_GET, "/", HelloPage);
	USART_Printf(USART2, "Going to start Micro HTTP Server.\r\n");
	HTTPServerInit(&srv, MTS_PORT);
	USART_Printf(USART2, "Micro HTTP Server started and listening.\r\n");
	HTTPServerRunLoop(&srv, Dispatch);
	HTTPServerClose(&srv);

	vTaskDelete(NULL);
}

/* Main function, the entry point of this program.
 * The main function is called from the startup code in file
 * Libraries/CMSIS/Device/ST/STM32F4xx/Source/Templates/TrueSTUDIO/
 * startup_stm32f40_41xxx.s  (line 107)
 */
int main(void) {
	BaseType_t xReturned;

	delay(10000000L);
	/* Initial LEDs. */
	setup_leds();
	//GPIO_SetBits(LEDS_GPIO_PORT, ALL_LEDS);

#ifdef MIRROR_USART6
	setup_usart2();
	USART_Printf(USART2, "USART2 initialized.\r\n");
#endif
	/* Initial wifi network interface ESP8266. */
	InitESP8266();
	USART_Printf(USART2, "USART6 initialized.\r\n");
	//GPIO_ResetBits(LEDS_GPIO_PORT, ALL_LEDS);
	//GPIO_SetBits(LEDS_GPIO_PORT, GREEN);

	/* Add the task into FreeRTOS task scheduler. */
	/* Add Micro HTTP Server. */
	xReturned = xTaskCreate(MicroHTTPServer_task,
							"Micro HTTP Server",
							4096,
							NULL,
							tskIDLE_PRIORITY,
							NULL);
	if(xReturned == pdPASS)
		GPIO_SetBits(LEDS_GPIO_PORT, ORANGE);

	/* Start FreeRTOS task scheduler. */
	vTaskStartScheduler();

    return 0; // never returns actually
}
