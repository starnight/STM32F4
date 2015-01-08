/*
 * This program turns on the 4 leds of the stm32f4 discovery board
 * with pressed user button in order.
 */

/* Include STM32F4 and standard peripherals configuration headers. */
#include <stm32f4xx.h>
#include "stm32f4xx_conf.h"

/* Define the readable hardware memory address, according to the
 * schematic of STM32F4-Discovery.
 */
/* LEDs. */
#define GREEN    GPIO_Pin_12 // Green LED connects to PD12
#define ORANGE   GPIO_Pin_13 // Orange LED connects to PD13
#define RED      GPIO_Pin_14 // Red LED connects to PD14
#define BLUE     GPIO_Pin_15 // Blue LED connects to PD15
#define ALL_LEDS (GREEN | ORANGE | RED | BLUE) // all leds
#define LEDn     4 // 4 LEDs
#define LEDS_GPIO_PORT (GPIOD) // LEDs connect to Port D

/* User Button. */
#define USER_BUTTON GPIO_Pin_0 // User Button connects to PA0
#define BUTTON_GPIO_PORT (GPIOA) // User Button connects to Port A

/* The array stores the led order used to switch them on and off. */
static uint16_t leds[LEDn] = {GREEN, ORANGE, RED, BLUE};

/* This is how long we wait in the delay function. */
#define LED_LONG    1000000L
#define PAUSE_SHORT 10000L

/* A simple time comsuming function. */
static void delay(__IO uint32_t nCount) {
    while(nCount--)
        __asm("nop"); // do nothing
}

/* Initialize the GPIO port D for output LEDs. */
static void setup_leds(void) {
    /* Structure storing the information of GPIO Port D. */
    static GPIO_InitTypeDef GPIO_InitStructure;

    /* Enable the GPIOD peripheral clock. */
    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOD, ENABLE);
    /* Pin numbers of LEDs are mapped to bits number. */
    GPIO_InitStructure.GPIO_Pin   = ALL_LEDS;
    /* Pins in output mode. */
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_OUT;
    /* Clock speed rate for the selected pins. */
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_2MHz;
    /* Operating output type for the selected pins. */
    GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
    /* Operating Pull-up/Pull down for the selected pins. */
    GPIO_InitStructure.GPIO_PuPd  = GPIO_PuPd_NOPULL;

    /* Write this data into memory at the address
     * mapped to GPIO device port D, where the led pins
     * are connected */
    GPIO_Init(LEDS_GPIO_PORT, &GPIO_InitStructure);
}

/* Initialize the GPIO port A for input User Button. */
static void setup_button(void) {
    /* Structure storing the information of GPIO Port A. */
    static GPIO_InitTypeDef GPIO_InitStructure;

    /* Enable the GPIOA peripheral clock. */
    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOA, ENABLE);
    /* Pin number of User Button is mapped to a bit number. */
    GPIO_InitStructure.GPIO_Pin   = USER_BUTTON;
    /* Pin in input mode. */
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_IN;
    /* Clock speed rate for the selected pins. */
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_2MHz;
    /* Operating output type for the selected pins. */
    GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
    /* Operating Pull-up/Pull down for the selected pins. */
    GPIO_InitStructure.GPIO_PuPd  = GPIO_PuPd_DOWN;
    
    /* Write this data into memory at the address
     * mapped to GPIO device port A, where the led pins
     * are connected */
    GPIO_Init(BUTTON_GPIO_PORT, &GPIO_InitStructure);
}

/* Get the status of User Button.
 * 0: Not pressed.
 * 1: Pressed.
 */
static uint8_t read_button(void) {
    return GPIO_ReadInputDataBit(BUTTON_GPIO_PORT, USER_BUTTON);    
}

/* Turn all leds on and off 4 times. */
static void flash_all_leds(void) {
    int i;
    for (i = 0; i < 4; i++)
    {
        /* Turn on all user leds */
        GPIO_SetBits(LEDS_GPIO_PORT, ALL_LEDS);
        /* Wait a short time */
        delay(LED_LONG);
        /* Turn off all leds */
        GPIO_ResetBits(LEDS_GPIO_PORT, ALL_LEDS);
        /* Wait again before looping */
        delay(LED_LONG);
    }
}

/* Initial Timer 6 for timing. */
void setup_timer(void) {
    /* Structure storing the information of Timer 6. */
    TIM_TimeBaseInitTypeDef TIM_BaseStruct;
    
    /* Enable Timer 6 clock. */
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM6, ENABLE);

    /* Set the timing clock prescaler. */
    TIM_BaseStruct.TIM_Prescaler = 42000 - 1;
    /* Set the timer count up. */
    TIM_BaseStruct.TIM_CounterMode = TIM_CounterMode_Up;
    /* Set the timer's top counting value. */
    TIM_BaseStruct.TIM_Period = 500 - 1;
    /* Set the internal clock division. */
    TIM_BaseStruct.TIM_ClockDivision = TIM_CKD_DIV1;
    /* No repetition counter for Timer 6. */
    TIM_BaseStruct.TIM_RepetitionCounter = 0;

    /* Write this data into memory at the address mapped to Timer 6. */
    TIM_TimeBaseInit(TIM6, &TIM_BaseStruct);
    /* Enable Timer 6's counter. */
    TIM_Cmd(TIM6, ENABLE);
}

/* Check Timer 6 is timeout or not. */
#define is_timeout()    (TIM_GetFlagStatus(TIM6, TIM_FLAG_Update) == SET)

/* Reset the Timer 6 update flag which is for timeout. */
#define reset_timeout()    (TIM_ClearFlag(TIM6, TIM_FLAG_Update))

/* Main function, the entry point of this program.
 * The main function is called from the startup code in file
 * Libraries/CMSIS/Device/ST/STM32F4xx/Source/Templates/TrueSTUDIO/
 * startup_stm32f40_41xxx.s  (line 107)
 */
int main(void) {
    int8_t i = 0;
    int8_t step = 1;
    /* Current & previous state of User Button. */
    uint8_t nstate = 0, pstate = 0; // 0: Not pressed, 1: pressed.

    /* Setup input / output for User Button and LEDs. */
    setup_leds();
    setup_button();

    /* Setup timer to time. */
    setup_timer();

    /* Wellcome LEDs. */
    flash_all_leds();
    GPIO_SetBits(LEDS_GPIO_PORT, leds[i]);

    while (1) {
        nstate = read_button();
        /* Check the User Button is pressed or not. */
        if((nstate == 1) && (pstate == 0)) {
            /* If the User Button is pressed, reverse the direction. */
            step *= -1;
            /* Avoid button ocsillation. */
            delay(PAUSE_SHORT);
        }
        /* Save the current state by previous state. */
        pstate = nstate;
        /* Check it is timeput or not. */
        if(is_timeout()) {
            /* Clear the timeout flag. */
            reset_timeout();
            /* Turn off all LEDS. */
            GPIO_ResetBits(LEDS_GPIO_PORT, ALL_LEDS);
            /* Choose next LED and turn it on. */
            i = (i + step + LEDn) % LEDn;
            GPIO_SetBits(LEDS_GPIO_PORT, leds[i]);
        }
    }

    return 0; // never returns actually
}
