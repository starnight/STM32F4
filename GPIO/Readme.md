This is a GPIO example for STM32F407, especially for 
STM32F4-Discovery, referenced from 
[blinky](https://github.com/Malkavian/tuts/tree/master/stm/blinky) 
project.  It is ported from using STM32F4-Discovery Library to 
STM32F4 DSP and standard peripherals library.

### Tools and software requirements ###

* [GNU toolchain](https://launchpad.net/gcc-arm-embedded)
  for ARM Cortex-M.

* [stlink](https://github.com/texane/stlink) STM32 debug & flash 
  utility written by texane for Linux.

* [library](http://www.st.com/web/catalog/tools/FM147/CL1794/SC961/SS1743/PF257901) STM32F4 DSP and standard peripherals library.

### Usage ###

1. Edit the Makefile to modify the "STM\_DIR" to the path of STM32F4 
   DSP and standard peripherals library.

2. Compile ``` make ```

3. Flash to STM32F407 ``` make flash ```

4. Reset the power of STM32F4-Discovery.
