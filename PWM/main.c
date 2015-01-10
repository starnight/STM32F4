/*
 * This program set PWM channel (Timer 4) on the 4 leds of the stm32f4 discovery
 * board in order with Timer 6.  Change the lighting direction by pressing 
 * user button.
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

/* Check Timer 6 is timeout or not. */
#define is_timeout()    (TIM_GetFlagStatus(TIM6, TIM_FLAG_Update) == SET)

/* Reset the Timer 6 update flag which is for timeout. */
#define reset_timeout() (TIM_ClearFlag(TIM6, TIM_FLAG_Update))

/* This is how long we wait in the delay function. */
#define PWM_LONG    1000L
#define PAUSE_SHORT 20L

/* The delay counters for specific purpose. */
int16_t pwm_long = PWM_LONG;
int16_t pause_short = PAUSE_SHORT;

/* The period of PWM, also the max lightness. */
#define MAX_LIGHTNESS 500L

/* Set the Capture Compare1 Register value */
#define set_compare(TIMx, i, compare) (*((&(TIMx->CCR1)) + i) = compare)

/* A simple time comsuming function. */
static void delay(void) {
    /* Check it is timeput or not. */
    while(is_timeout()) {
        /* Clear the timeout flag. */
        reset_timeout();
        /* Decrease counters. */
        pwm_long--;
        pause_short--;
    }
}

/* Initial Timer 6 for timing. */
void setup_timer(void) {
    /* Structure storing the information of Timer 6. */
    TIM_TimeBaseInitTypeDef TIM_BaseStruct;

    /* Enable Timer 6 clock. */
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM6, ENABLE);

    /* Set the timing clock prescaler. */
    TIM_BaseStruct.TIM_Prescaler = 100 - 1;
    /* Set the timer count up. */
    TIM_BaseStruct.TIM_CounterMode = TIM_CounterMode_Up;
    /* Set the timer's top counting value. */
    TIM_BaseStruct.TIM_Period = 10 - 1;
    /* Set the internal clock division. */
    TIM_BaseStruct.TIM_ClockDivision = TIM_CKD_DIV1;
    /* No repetition counter for Timer 6. */
    TIM_BaseStruct.TIM_RepetitionCounter = 0;

    /* Write this data into memory at the address mapped to Timer 6. */
    TIM_TimeBaseInit(TIM6, &TIM_BaseStruct);
    /* Enable Timer 6's counter. */
    TIM_Cmd(TIM6, ENABLE);
}

/* Initial the Timer 4 for PWM. */
static void setup_pwm_timer(void) {
    /* Structure storing the information of Timer 4. */
    TIM_TimeBaseInitTypeDef TIM_BaseStruct;

    /* Enable Timer 4 clock. */
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM4, ENABLE);

    /* Set the timing clock prescaler. */
    TIM_BaseStruct.TIM_Prescaler = 100 - 1;
    /* Set the timer count up. */
    TIM_BaseStruct.TIM_CounterMode = TIM_CounterMode_Up;
    /* Set the timer's top counting value. */
    TIM_BaseStruct.TIM_Period = MAX_LIGHTNESS - 1;
    /* Set the internal clock division. */
    TIM_BaseStruct.TIM_ClockDivision = TIM_CKD_DIV1;
    /* No repetition counter for Timer 4. */
    TIM_BaseStruct.TIM_RepetitionCounter = 0;

    /* Write this data into memory at the address mapped to Timer 4. */
    TIM_TimeBaseInit(TIM4, &TIM_BaseStruct);
    /* Enable Timer 4's counter. */
    TIM_Cmd(TIM4, ENABLE);
}

/* Initial the PWM of Timer 4. */
static void setup_pwm(void) {
    /* Structure storing the information of output capture. */
    TIM_OCInitTypeDef TIM_OCStruct = {0,};

    /* Set output capture as PWM mode. */
    TIM_OCStruct.TIM_OCMode = TIM_OCMode_PWM1;
    /* Enable output compare to the correspond output pin.*/
    TIM_OCStruct.TIM_OutputState = TIM_OutputState_Enable;
    /* Set output compare active as high. */
    TIM_OCStruct.TIM_OCPolarity = TIM_OCPolarity_High;
    /* Set the PWM duty cycle. */
    TIM_OCStruct.TIM_Pulse = 0; /* 0% duty cycle */

    /* Write this data into memory at the address mapped
     * to output capture 1~4. */
    TIM_OC1Init(TIM4, &TIM_OCStruct);
    TIM_OC1PreloadConfig(TIM4, TIM_OCPreload_Enable);
    TIM_OC2Init(TIM4, &TIM_OCStruct);
    TIM_OC2PreloadConfig(TIM4, TIM_OCPreload_Enable);
    TIM_OC3Init(TIM4, &TIM_OCStruct);
    TIM_OC3PreloadConfig(TIM4, TIM_OCPreload_Enable);
    TIM_OC4Init(TIM4, &TIM_OCStruct);
    TIM_OC4PreloadConfig(TIM4, TIM_OCPreload_Enable);
}

/* Initialize the GPIO port D for outputting PWM to LEDs. */
static void setup_leds(void) {
    /* Structure storing the information of GPIO Port D. */
    static GPIO_InitTypeDef GPIO_InitStructure;

    /* Enable the GPIOD peripheral clock. */
    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOD, ENABLE);

    /* Set the alternative function for the LEDs' pins. */
    GPIO_PinAFConfig(LEDS_GPIO_PORT, GPIO_PinSource12, GPIO_AF_TIM4);
    GPIO_PinAFConfig(LEDS_GPIO_PORT, GPIO_PinSource13, GPIO_AF_TIM4);
    GPIO_PinAFConfig(LEDS_GPIO_PORT, GPIO_PinSource14, GPIO_AF_TIM4);
    GPIO_PinAFConfig(LEDS_GPIO_PORT, GPIO_PinSource15, GPIO_AF_TIM4);

    /* Pin numbers of LEDs are mapped to bits number. */
    GPIO_InitStructure.GPIO_Pin   = ALL_LEDS;
    /* Pins in alternative function mode. */
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_AF;
    /* Sampling rate for the selected pins. */
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_100MHz;
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

/* Light LEDs in order. */
static void pwm_leds(int8_t dir) {
    static int8_t i = 0;
    /* LED's lightness in percentage. */
    static uint8_t lightness = 2;
    /* The lightness increase or decrease step. */
    static int16_t step = 2;

    /* Check it is delay enough time. */
    if(pwm_long <= 0) {
        /* Reload pwm_long counter. */
        pwm_long = PWM_LONG;
        /* Set the PWM duty cycle to indexed LED. */
        set_compare(TIM4, i, lightness * MAX_LIGHTNESS / 100);
        /* Bound lightness. */
        if(lightness == 0) {
            step = 2;
            /* Choose next LED. */
            i = (i + dir + LEDn) % LEDn;
        }
        else if(lightness >= 100) {
            step = -2;
        }
        /* Step up/down the lightness. */
        lightness += step;
    }
}

/* Get the status of User Button.
 * 0: Not pressed.
 * 1: Pressed.
 */
#define read_button() (GPIO_ReadInputDataBit(BUTTON_GPIO_PORT, USER_BUTTON))

/* Get LEDs' lighting direction.
 *  1: Clockwise.
 * -1: Counterclockwise.
 */
static int8_t get_leds_direction(void) {
    static int8_t dir = 1;
    /* Current & previous state of User Button. */
    static uint8_t nstate = 0, pstate = 0; // 0: Not pressed, 1: pressed.

    nstate = read_button();
    /* Check the User Button is pressed or not. */
    if((nstate == 1) && (pstate == 0)) {
        /* If the User Button is pressed, reverse the direction. */
        dir *= -1;
        /* Avoid button ocsillation. */
        while(pause_short > 0) {
            delay();
        }
        pause_short = PAUSE_SHORT;
    }
    /* Save the current state by previous state. */
    pstate = nstate;

    return dir;
}

/* Main function, the entry point of this program.
 * The main function is called from the startup code in file
 * Libraries/CMSIS/Device/ST/STM32F4xx/Source/Templates/TrueSTUDIO/
 * startup_stm32f40_41xxx.s  (line 107)
 */
int main(void) {
    int8_t dir;

    /* Setup input / output for User Button and LEDs. */
    setup_leds();
    setup_button();

    /* Setup timer to time. */
    setup_timer();

    /* Setup PWM. */
    setup_pwm_timer();
    setup_pwm();

    while (1) {
        /* Check delay time. */
        delay();
        /* Get the lighting direction. */
        dir = get_leds_direction();
        /* Light LEDs in order. */
        pwm_leds(dir);
    }

    return 0; // never returns actually
}
