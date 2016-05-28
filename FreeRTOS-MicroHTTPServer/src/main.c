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
	
	AddRoute(HTTP_GET, "/", HelloPage);
	HTTPServerInit(&srv, MTS_PORT);
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
	/* Initial LEDs. */
	setup_leds();
	GPIO_SetBits(LEDS_GPIO_PORT, ALL_LEDS);

	/* Initial wifi network interface ESP8266. */
	InitESP8266();
	GPIO_ResetBits(LEDS_GPIO_PORT, ALL_LEDS);
	GPIO_SetBits(LEDS_GPIO_PORT, GREEN);

	/* Add the main task into FreeRTOS task scheduler. */
	//xTaskCreate(main_task1, "Main Task1", 512, NULL, tskIDLE_PRIORITY, NULL);
	/* Add Micro HTTP Server. */
	xTaskCreate(MicroHTTPServer_task,
				"Micro HTTP Server",
				4096,
				NULL,
				tskIDLE_PRIORITY,
				NULL);
	GPIO_SetBits(LEDS_GPIO_PORT, ORANGE);

	/* Start FreeRTOS task scheduler. */
	vTaskStartScheduler();

    return 0; // never returns actually
}
