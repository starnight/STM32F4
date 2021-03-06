This is a FreeRTOS USART TX-RX interrupts example for STM32F407, especially for STM32F4-Discovery.
It is [GPIO example](https://github.com/starnight/STM32F4/tree/master/GPIO) extended with FreeRTOS library.

### Tools and software requirements ###

* [GNU toolchain](https://launchpad.net/gcc-arm-embedded)
  for ARM Cortex-M.

* [stlink](https://github.com/texane/stlink) STM32 debug & flash utility
 written by texane for Linux.

* [STM32F4 library](http://www.st.com/web/catalog/tools/FM147/CL1794/SC961/SS1743/PF257901) STM32F4 DSP and standard peripherals library.

* [FreeRTOS](http://www.freertos.org/a00104.html?1) FreeRTOS library.

### Hardware ###

According to the schematic of [STM32F4-Discovery](http://www.st.com/st-web-ui/static/active/en/resource/technical/document/user_manual/DM00039084.pdf) Peripherals:

* LED4 (Green): PD12 connected to LD4

* LED3 (Orange): PD13 connected to LD3

* LED5 (Red): PD14 connected to LD5

* LED6 (Blue): PD15 connected to LD6

* User Button: PA0 connected to B1

* USART6 TX: PC6

* USART6 RX: PC7

### Usage ###

1. Edit the Makefile to modify the "STM\_DIR" to the path of STM32F4 DSP and
   standard peripherals library.

2. Edit the Makefile to modify the "FREERTOS\_DIR" to the path of FreeRTOS
   library.

3. Compile: ``` make ```

4. Flash to STM32F407: ``` make flash ```

5. Reset the power of STM32F4-Discovery.

6. Get a terminal connected with the right UART settings to USART6 on STM32F407. (115200 baud rate, 8 data bits, 1 stop bit, no parity check and non-flow control.)

7. Press any key and it will echo.
