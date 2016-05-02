This is a simple USART with interrupts example for STM32F407, especially for STM32F4-Discovery.
It will flash the LEDs and turn the LEDs on / off periodically in order with the
Timer.  It will also print Hello world string and get message through/from USART6 with interrupts.

### Tools and software requirements ###

* [GNU toolchain](https://launchpad.net/gcc-arm-embedded)
  for ARM Cortex-M.

* [stlink](https://github.com/texane/stlink) STM32 debug & flash 
  utility written by texane for Linux.

* [library](http://www.st.com/web/catalog/tools/FM147/CL1794/SC961/SS1743/PF257901) STM32F4 DSP and standard peripherals library.

* Make sure the **HSE\_VALUE** in stm32f4xx\_conf.h and stm32f4xx.h is set with 8000000 and **PLL\_M** in system\_stm32f4xx.c is set with 8, because of the crystal oscillator on STM32F4-Discovery is 8MHz.

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

1. Edit the Makefile to modify the "STM\_DIR" to the path of STM32F4 
   DSP and standard peripherals library.

2. Compile: ``` make ```

3. Flash to STM32F407: ``` make flash ```

4. Reset the power of STM32F4-Discovery.

5. Get a terminal connected with the right UART settings to USART6 on STM32F407. (115200 baud rate, 8 data bits, 1 stop bit, no parity check and non-flow control.)

6. Press any key and it will echo.
