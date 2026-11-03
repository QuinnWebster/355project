
#include <stdio.h>
#include "diag/Trace.h"
#include "cmsis/cmsis_device.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wmissing-declarations"
#pragma GCC diagnostic ignored "-Wreturn-type"


#define R_TOTAL_OHMS 5000 // We found this in the pot section of the manual


void ADC_INIT(void);

volatile uint32_t adc_value = 0;

volatile uint32_t millivolts = 0;
volatile uint32_t resistance = 0;

void oled_config(void);                          // tells the compiler these exist, defined further down
void oled_clear(void);
void oled_set_cursor(uint8_t page, uint8_t col);
void oled_print(const char *s);


void SystemClock48MHz( void )
{
    RCC->CR &= ~(RCC_CR_PLLON);
    while (( RCC->CR & RCC_CR_PLLRDY ) != 0 );
    RCC->CFGR = 0x00280000;
    RCC->CR |= RCC_CR_PLLON;
    while (( RCC->CR & RCC_CR_PLLRDY ) != RCC_CR_PLLRDY );
    RCC->CFGR = ( RCC->CFGR & (~RCC_CFGR_SW_Msk)) | RCC_CFGR_SW_PLL;
    SystemCoreClockUpdate();
}

int main(int argc, char* argv[]){

    SystemClock48MHz();

    oled_config();              // set up GPIO + SPI2, reset and initialize the display
    oled_clear();               // wipe random startup pixels
    oled_set_cursor(0, 0);      // top line (page 0), left edge (column 0)
    oled_print("hello");        // draw the word

    trace_printf("Testing ADC\n");

    ADC_INIT();

    uint32_t counter = 0;

    while (1)
    {
        while ((ADC1->ISR & ADC_ISR_EOC) == 0) 
        {
            // Wait for conversion to complete
        }

        adc_value = ADC1->DR;  // Read the converted value

        millivolts = (adc_value * 3300) / 4095; // 4095 is the max value of adc we read from the dial, 3300 mili volts corresponds to 4095/4095
        resistance = (adc_value * R_TOTAL_OHMS) / 4095;

        char buffer[17];   // 16 characters fit on one line + 1 for the '\0' terminator


        counter++;
        if(counter > 50000)
        {
            counter = 0;
            trace_printf("ADC Value: %u\n", adc_value); // Only print every so often

            trace_printf("ADC: %u  Voltage: %u mV  Resistance: %u ohms\n", adc_value, millivolts, resistance);

            snprintf(buffer, sizeof(buffer), "ADC: %4u", (unsigned int)adc_value);   // text like "ADC: 2048"
            oled_set_cursor(0, 0);       // page 0 = top line, column 0 = left edge
            oled_print(buffer);          // draw it

            snprintf(buffer, sizeof(buffer), "R: %4u Ohms", (unsigned int)resistance); // text like "R: 2500 Ohms"
            oled_set_cursor(2, 0);       // page 2 = third line
            oled_print(buffer);


        }
    }

    return 0;


}


void ADC_INIT(void){

    RCC->AHBENR |= RCC_AHBENR_GPIOAEN; /*
    Turns on power to GPIO port A
    RCC is the Reset and Clock Control register
    AHBENR is the bus that the GPIO is on
    RCC_AHBENR_GPIOAEN is the bit that turns on the clock for GPIO port A
    */ 
                                        

    RCC->APB2ENR |= RCC_APB2ENR_ADCEN; /*
    Turns on power to the ADC
    APB2ENR is the bus that the ADC is on
    RCC_APB2ENR_ADCEN is the bit that turns on the clock for the ADC
    */

    RCC->CR2 |= RCC_CR2_HSI14ON; /*
    
    CR2 handles the 14 MHz clock for the ADC
    RCC_CR2_HSI14ON is the bit that turns on the clock for the ADC
    */

    while((RCC->CR2 & RCC_CR2_HSI14RDY) == 0) // This pauses until the ADC clock is ready
    // CR2 is the clock control register (It holds the status of the clock)
    // RCC_CR2_HSI14RDY is the bit that indicates whether the clock is ready
    // 1 means the clock is ready, 0 means it is not ready
    {
        // Wait for HSI14 to be ready
    }

    GPIOA->MODER |= GPIO_MODER_MODER1; /*
    This switches pin PA1 to analog mode
    GPIOA is the GPIO port A
    MODER is the mode register
    GPIO_MODER_MODER1 is the bit that switches pin 1 to analog mode

    //00 is Input
    //01 is Output
    //10 is Alternate Function
    //11 is Analog

    */

    GPIOA->PUPDR &= ~(GPIO_PUPDR_PUPDR1); /*
    
    Makes sure that pin PA1 has no pull-up or pull-down resistor
    We do this since we want to measure the voltage on the pin, and a pull-up or pull-down resistor would affect the voltage by making it higher or lower than it actually is
    GPIOA is the GPIO port A
    PUPDR is the pull-up/pull-down register
    GPIO_PUPDR_PUPDR1 is the bit that controls the pull-up/pull-down resistor for pin 1
    We want to make sure that this bit is 00, which means no pull-up/pull-down resistor
    Since were turning it off we use AND
    */

    // Calibration

    // First have to make sure the ADC is off

    /*
    This checks to see if the ADC is enabled
    ADC1 is the ADC
    CR is the control register
    ADC_CR_ADEN is the bit that indicates whether the ADC is enabled
    1 means the ADC is enabled, 0 means it is not
    */
    if(( ADC1->CR & ADC_CR_ADEN) != 0)
    {
        ADC1->CR |= ADC_CR_ADDIS; // This disables the ADC

        while((ADC1->CR & ADC_CR_ADEN) != 0)
        {
            // Wait for ADC to be disabled
        }
    }

    ADC1->CR |= ADC_CR_ADCAL; // Setting ADCAL means to start calibrating it

    while ((ADC1->CR & ADC_CR_ADCAL) != 0); // Wait until the calibration is done



    // Configuration

    ADC1->CFGR1 &= ~(ADC_CFGR1_RES | ADC_CFGR1_ALIGN); /*
    
    CFRG1 is the ADC main settings register
    ADC_CFGR1_RES control how many bits the value of the ADC is
    00 is 12 bits
    01 is 10 bits
    10 is 8 bits
    11 is 6 bits
    ADC_CFGR1_ALIGN controls whether the value of the ADC is left or right aligned
    0 is right aligned
    1 is left aligned
    
    */

    ADC1->CFGR1 |= ADC_CFGR1_CONT;/*
    
    CFGR1 is the ADC main settings register
    ADC_CFGR1_CONT controls whether the ADC is in continuous mode or single conversion mode
    0 is single conversion mode
    1 is continuous mode

    */

    ADC1->CFGR1 |= ADC_CFGR1_OVRMOD;/*
    
    ADC_CFGR1_OVRMOD controls if ADC is in overrun mode or not
    0 is overrun mode disabled
    1 is overrun mode enabled
    Overrun mode means that if a new result arrives before the old one is read
    the old one is overwritten and lost
    */

    ADC1->CHSELR = ADC_CHSELR_CHSEL1;/*
    
    CHSELR is the channel selection register
    ADC_CHSELR_CHSEL1 is the bit that selects channel 1 (PA1)
    Channel 1 means we use PA1 as the input to the ADC
    We dont to OR this because we only want to use channel 1, not any other channels

    */

    ADC1->SMPR |= ADC_SMPR_SMP;/*
    
    SMPR is the sample time register
    ADC_SMPR_SMP is the bit that controls the sample time
    We went with the longest sample time because it is the most accurate
    111, which is 239.5 ADC clock cycles
    
    */



    
    ADC1->ISR |= ADC_ISR_ADRDY;/*
    
    We want to clear the ADRDY bit in the ISR register before we turn on the ADC
    We do that by writing 1 which clears the bit to 0
    */


    // Turn the ADC on
    ADC1->CR |= ADC_CR_ADEN;



    while ((ADC1->ISR & ADC_ISR_ADRDY) == 0); // Wait for the ADC to be ready


    // Start it

    ADC1->CR |= ADC_CR_ADSTART;/*
    
    CR is the control register
    ADC_CR_ADSTART is the bit that starts the ADC conversion
    1 means start the conversion
    0 means do not start the conversion

    */


}


#define CS(x)  (GPIOB->BSRR = (x) ? (1U << 8)  : (1U << (8 + 16)))   // PB8  CS#:  1 = deselect, 0 = select display
#define DC(x)  (GPIOB->BSRR = (x) ? (1U << 9)  : (1U << (9 + 16)))   // PB9  D/C#: 1 = data byte, 0 = command byte
#define RES(x) (GPIOB->BSRR = (x) ? (1U << 11) : (1U << (11 + 16)))  // PB11 RES#: 1 = run, 0 = hold in reset

uint8_t oled_init_cmds[] = {                                  // setup commands copied from the handout
    0xAE, 0x20, 0x00, 0x40, 0xA0 | 0x01, 0xA8, 0x40 - 1,      // display off, horizontal addressing, flip, 64 rows
    0xC0 | 0x08, 0xD3, 0x00, 0xDA, 0x32, 0xD5, 0x80,          // scan direction, offset, pin config, clock
    0xD9, 0x22, 0xDB, 0x30, 0x81, 0xFF, 0xA4, 0xA6,           // precharge, VCOMH, max contrast, normal display
    0xAD, 0x30, 0x8D, 0x10, 0xAE | 0x01, 0xC0, 0xA0           // as handout, 0xAF = display ON
};

//
// Character specifications for LED Display (1 row = 8 bytes = 1 ASCII character)
// Example: to display '4', retrieve 8 data bytes stored in Characters[52][X] row
//          (where X = 0, 1, ..., 7) and send them one by one to LED Display. 
// Row number = character ASCII code (e.g., ASCII code of '4' is 0x34 = 52)
//
unsigned char Characters[][8] = {
    {0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000,0b00000000, 0b00000000, 0b00000000},  // SPACE
    {0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000,0b00000000, 0b00000000, 0b00000000},  // SPACE
    {0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000,0b00000000, 0b00000000, 0b00000000},  // SPACE
    {0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000,0b00000000, 0b00000000, 0b00000000},  // SPACE
    {0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000,0b00000000, 0b00000000, 0b00000000},  // SPACE
    {0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000,0b00000000, 0b00000000, 0b00000000},  // SPACE
    {0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000,0b00000000, 0b00000000, 0b00000000},  // SPACE
    {0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000,0b00000000, 0b00000000, 0b00000000},  // SPACE
    {0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000,0b00000000, 0b00000000, 0b00000000},  // SPACE
    {0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000,0b00000000, 0b00000000, 0b00000000},  // SPACE
    {0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000,0b00000000, 0b00000000, 0b00000000},  // SPACE
    {0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000,0b00000000, 0b00000000, 0b00000000},  // SPACE
    {0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000,0b00000000, 0b00000000, 0b00000000},  // SPACE
    {0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000,0b00000000, 0b00000000, 0b00000000},  // SPACE
    {0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000,0b00000000, 0b00000000, 0b00000000},  // SPACE
    {0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000,0b00000000, 0b00000000, 0b00000000},  // SPACE
    {0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000,0b00000000, 0b00000000, 0b00000000},  // SPACE
    {0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000,0b00000000, 0b00000000, 0b00000000},  // SPACE
    {0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000,0b00000000, 0b00000000, 0b00000000},  // SPACE
    {0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000,0b00000000, 0b00000000, 0b00000000},  // SPACE
    {0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000,0b00000000, 0b00000000, 0b00000000},  // SPACE
    {0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000,0b00000000, 0b00000000, 0b00000000},  // SPACE
    {0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000,0b00000000, 0b00000000, 0b00000000},  // SPACE
    {0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000,0b00000000, 0b00000000, 0b00000000},  // SPACE
    {0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000,0b00000000, 0b00000000, 0b00000000},  // SPACE
    {0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000,0b00000000, 0b00000000, 0b00000000},  // SPACE
    {0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000,0b00000000, 0b00000000, 0b00000000},  // SPACE
    {0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000,0b00000000, 0b00000000, 0b00000000},  // SPACE
    {0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000,0b00000000, 0b00000000, 0b00000000},  // SPACE
    {0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000,0b00000000, 0b00000000, 0b00000000},  // SPACE
    {0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000,0b00000000, 0b00000000, 0b00000000},  // SPACE
    {0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000,0b00000000, 0b00000000, 0b00000000},  // SPACE
    {0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000,0b00000000, 0b00000000, 0b00000000},  // SPACE
    {0b00000000, 0b00000000, 0b01011111, 0b00000000, 0b00000000,0b00000000, 0b00000000, 0b00000000},  // !
    {0b00000000, 0b00000111, 0b00000000, 0b00000111, 0b00000000,0b00000000, 0b00000000, 0b00000000},  // "
    {0b00010100, 0b01111111, 0b00010100, 0b01111111, 0b00010100,0b00000000, 0b00000000, 0b00000000},  // #
    {0b00100100, 0b00101010, 0b01111111, 0b00101010, 0b00010010,0b00000000, 0b00000000, 0b00000000},  // $
    {0b00100011, 0b00010011, 0b00001000, 0b01100100, 0b01100010,0b00000000, 0b00000000, 0b00000000},  // %
    {0b00110110, 0b01001001, 0b01010101, 0b00100010, 0b01010000,0b00000000, 0b00000000, 0b00000000},  // &
    {0b00000000, 0b00000101, 0b00000011, 0b00000000, 0b00000000,0b00000000, 0b00000000, 0b00000000},  // '
    {0b00000000, 0b00011100, 0b00100010, 0b01000001, 0b00000000,0b00000000, 0b00000000, 0b00000000},  // (
    {0b00000000, 0b01000001, 0b00100010, 0b00011100, 0b00000000,0b00000000, 0b00000000, 0b00000000},  // )
    {0b00010100, 0b00001000, 0b00111110, 0b00001000, 0b00010100,0b00000000, 0b00000000, 0b00000000},  // *
    {0b00001000, 0b00001000, 0b00111110, 0b00001000, 0b00001000,0b00000000, 0b00000000, 0b00000000},  // +
    {0b00000000, 0b01010000, 0b00110000, 0b00000000, 0b00000000,0b00000000, 0b00000000, 0b00000000},  // ,
    {0b00001000, 0b00001000, 0b00001000, 0b00001000, 0b00001000,0b00000000, 0b00000000, 0b00000000},  // -
    {0b00000000, 0b01100000, 0b01100000, 0b00000000, 0b00000000,0b00000000, 0b00000000, 0b00000000},  // .
    {0b00100000, 0b00010000, 0b00001000, 0b00000100, 0b00000010,0b00000000, 0b00000000, 0b00000000},  // /
    {0b00111110, 0b01010001, 0b01001001, 0b01000101, 0b00111110,0b00000000, 0b00000000, 0b00000000},  // 0
    {0b00000000, 0b01000010, 0b01111111, 0b01000000, 0b00000000,0b00000000, 0b00000000, 0b00000000},  // 1
    {0b01000010, 0b01100001, 0b01010001, 0b01001001, 0b01000110,0b00000000, 0b00000000, 0b00000000},  // 2
    {0b00100001, 0b01000001, 0b01000101, 0b01001011, 0b00110001,0b00000000, 0b00000000, 0b00000000},  // 3
    {0b00011000, 0b00010100, 0b00010010, 0b01111111, 0b00010000,0b00000000, 0b00000000, 0b00000000},  // 4
    {0b00100111, 0b01000101, 0b01000101, 0b01000101, 0b00111001,0b00000000, 0b00000000, 0b00000000},  // 5
    {0b00111100, 0b01001010, 0b01001001, 0b01001001, 0b00110000,0b00000000, 0b00000000, 0b00000000},  // 6
    {0b00000011, 0b00000001, 0b01110001, 0b00001001, 0b00000111,0b00000000, 0b00000000, 0b00000000},  // 7
    {0b00110110, 0b01001001, 0b01001001, 0b01001001, 0b00110110,0b00000000, 0b00000000, 0b00000000},  // 8
    {0b00000110, 0b01001001, 0b01001001, 0b00101001, 0b00011110,0b00000000, 0b00000000, 0b00000000},  // 9
    {0b00000000, 0b00110110, 0b00110110, 0b00000000, 0b00000000,0b00000000, 0b00000000, 0b00000000},  // :
    {0b00000000, 0b01010110, 0b00110110, 0b00000000, 0b00000000,0b00000000, 0b00000000, 0b00000000},  // ;
    {0b00001000, 0b00010100, 0b00100010, 0b01000001, 0b00000000,0b00000000, 0b00000000, 0b00000000},  // <
    {0b00010100, 0b00010100, 0b00010100, 0b00010100, 0b00010100,0b00000000, 0b00000000, 0b00000000},  // =
    {0b00000000, 0b01000001, 0b00100010, 0b00010100, 0b00001000,0b00000000, 0b00000000, 0b00000000},  // >
    {0b00000010, 0b00000001, 0b01010001, 0b00001001, 0b00000110,0b00000000, 0b00000000, 0b00000000},  // ?
    {0b00110010, 0b01001001, 0b01111001, 0b01000001, 0b00111110,0b00000000, 0b00000000, 0b00000000},  // @
    {0b01111110, 0b00010001, 0b00010001, 0b00010001, 0b01111110,0b00000000, 0b00000000, 0b00000000},  // A
    {0b01111111, 0b01001001, 0b01001001, 0b01001001, 0b00110110,0b00000000, 0b00000000, 0b00000000},  // B
    {0b00111110, 0b01000001, 0b01000001, 0b01000001, 0b00100010,0b00000000, 0b00000000, 0b00000000},  // C
    {0b01111111, 0b01000001, 0b01000001, 0b00100010, 0b00011100,0b00000000, 0b00000000, 0b00000000},  // D
    {0b01111111, 0b01001001, 0b01001001, 0b01001001, 0b01000001,0b00000000, 0b00000000, 0b00000000},  // E
    {0b01111111, 0b00001001, 0b00001001, 0b00001001, 0b00000001,0b00000000, 0b00000000, 0b00000000},  // F
    {0b00111110, 0b01000001, 0b01001001, 0b01001001, 0b01111010,0b00000000, 0b00000000, 0b00000000},  // G
    {0b01111111, 0b00001000, 0b00001000, 0b00001000, 0b01111111,0b00000000, 0b00000000, 0b00000000},  // H
    {0b01000000, 0b01000001, 0b01111111, 0b01000001, 0b01000000,0b00000000, 0b00000000, 0b00000000},  // I
    {0b00100000, 0b01000000, 0b01000001, 0b00111111, 0b00000001,0b00000000, 0b00000000, 0b00000000},  // J
    {0b01111111, 0b00001000, 0b00010100, 0b00100010, 0b01000001,0b00000000, 0b00000000, 0b00000000},  // K
    {0b01111111, 0b01000000, 0b01000000, 0b01000000, 0b01000000,0b00000000, 0b00000000, 0b00000000},  // L
    {0b01111111, 0b00000010, 0b00001100, 0b00000010, 0b01111111,0b00000000, 0b00000000, 0b00000000},  // M
    {0b01111111, 0b00000100, 0b00001000, 0b00010000, 0b01111111,0b00000000, 0b00000000, 0b00000000},  // N
    {0b00111110, 0b01000001, 0b01000001, 0b01000001, 0b00111110,0b00000000, 0b00000000, 0b00000000},  // O
    {0b01111111, 0b00001001, 0b00001001, 0b00001001, 0b00000110,0b00000000, 0b00000000, 0b00000000},  // P
    {0b00111110, 0b01000001, 0b01010001, 0b00100001, 0b01011110,0b00000000, 0b00000000, 0b00000000},  // Q
    {0b01111111, 0b00001001, 0b00011001, 0b00101001, 0b01000110,0b00000000, 0b00000000, 0b00000000},  // R
    {0b01000110, 0b01001001, 0b01001001, 0b01001001, 0b00110001,0b00000000, 0b00000000, 0b00000000},  // S
    {0b00000001, 0b00000001, 0b01111111, 0b00000001, 0b00000001,0b00000000, 0b00000000, 0b00000000},  // T
    {0b00111111, 0b01000000, 0b01000000, 0b01000000, 0b00111111,0b00000000, 0b00000000, 0b00000000},  // U
    {0b00011111, 0b00100000, 0b01000000, 0b00100000, 0b00011111,0b00000000, 0b00000000, 0b00000000},  // V
    {0b00111111, 0b01000000, 0b00111000, 0b01000000, 0b00111111,0b00000000, 0b00000000, 0b00000000},  // W
    {0b01100011, 0b00010100, 0b00001000, 0b00010100, 0b01100011,0b00000000, 0b00000000, 0b00000000},  // X
    {0b00000111, 0b00001000, 0b01110000, 0b00001000, 0b00000111,0b00000000, 0b00000000, 0b00000000},  // Y
    {0b01100001, 0b01010001, 0b01001001, 0b01000101, 0b01000011,0b00000000, 0b00000000, 0b00000000},  // Z
    {0b01111111, 0b01000001, 0b00000000, 0b00000000, 0b00000000,0b00000000, 0b00000000, 0b00000000},  // [
    {0b00010101, 0b00010110, 0b01111100, 0b00010110, 0b00010101,0b00000000, 0b00000000, 0b00000000},  // back slash
    {0b00000000, 0b00000000, 0b00000000, 0b01000001, 0b01111111,0b00000000, 0b00000000, 0b00000000},  // ]
    {0b00000100, 0b00000010, 0b00000001, 0b00000010, 0b00000100,0b00000000, 0b00000000, 0b00000000},  // ^
    {0b01000000, 0b01000000, 0b01000000, 0b01000000, 0b01000000,0b00000000, 0b00000000, 0b00000000},  // _
    {0b00000000, 0b00000001, 0b00000010, 0b00000100, 0b00000000,0b00000000, 0b00000000, 0b00000000},  // `
    {0b00100000, 0b01010100, 0b01010100, 0b01010100, 0b01111000,0b00000000, 0b00000000, 0b00000000},  // a
    {0b01111111, 0b01001000, 0b01000100, 0b01000100, 0b00111000,0b00000000, 0b00000000, 0b00000000},  // b
    {0b00111000, 0b01000100, 0b01000100, 0b01000100, 0b00100000,0b00000000, 0b00000000, 0b00000000},  // c
    {0b00111000, 0b01000100, 0b01000100, 0b01001000, 0b01111111,0b00000000, 0b00000000, 0b00000000},  // d
    {0b00111000, 0b01010100, 0b01010100, 0b01010100, 0b00011000,0b00000000, 0b00000000, 0b00000000},  // e
    {0b00001000, 0b01111110, 0b00001001, 0b00000001, 0b00000010,0b00000000, 0b00000000, 0b00000000},  // f
    {0b00001100, 0b01010010, 0b01010010, 0b01010010, 0b00111110,0b00000000, 0b00000000, 0b00000000},  // g
    {0b01111111, 0b00001000, 0b00000100, 0b00000100, 0b01111000,0b00000000, 0b00000000, 0b00000000},  // h
    {0b00000000, 0b01000100, 0b01111101, 0b01000000, 0b00000000,0b00000000, 0b00000000, 0b00000000},  // i
    {0b00100000, 0b01000000, 0b01000100, 0b00111101, 0b00000000,0b00000000, 0b00000000, 0b00000000},  // j
    {0b01111111, 0b00010000, 0b00101000, 0b01000100, 0b00000000,0b00000000, 0b00000000, 0b00000000},  // k
    {0b00000000, 0b01000001, 0b01111111, 0b01000000, 0b00000000,0b00000000, 0b00000000, 0b00000000},  // l
    {0b01111100, 0b00000100, 0b00011000, 0b00000100, 0b01111000,0b00000000, 0b00000000, 0b00000000},  // m
    {0b01111100, 0b00001000, 0b00000100, 0b00000100, 0b01111000,0b00000000, 0b00000000, 0b00000000},  // n
    {0b00111000, 0b01000100, 0b01000100, 0b01000100, 0b00111000,0b00000000, 0b00000000, 0b00000000},  // o
    {0b01111100, 0b00010100, 0b00010100, 0b00010100, 0b00001000,0b00000000, 0b00000000, 0b00000000},  // p
    {0b00001000, 0b00010100, 0b00010100, 0b00011000, 0b01111100,0b00000000, 0b00000000, 0b00000000},  // q
    {0b01111100, 0b00001000, 0b00000100, 0b00000100, 0b00001000,0b00000000, 0b00000000, 0b00000000},  // r
    {0b01001000, 0b01010100, 0b01010100, 0b01010100, 0b00100000,0b00000000, 0b00000000, 0b00000000},  // s
    {0b00000100, 0b00111111, 0b01000100, 0b01000000, 0b00100000,0b00000000, 0b00000000, 0b00000000},  // t
    {0b00111100, 0b01000000, 0b01000000, 0b00100000, 0b01111100,0b00000000, 0b00000000, 0b00000000},  // u
    {0b00011100, 0b00100000, 0b01000000, 0b00100000, 0b00011100,0b00000000, 0b00000000, 0b00000000},  // v
    {0b00111100, 0b01000000, 0b00111000, 0b01000000, 0b00111100,0b00000000, 0b00000000, 0b00000000},  // w
    {0b01000100, 0b00101000, 0b00010000, 0b00101000, 0b01000100,0b00000000, 0b00000000, 0b00000000},  // x
    {0b00001100, 0b01010000, 0b01010000, 0b01010000, 0b00111100,0b00000000, 0b00000000, 0b00000000},  // y
    {0b01000100, 0b01100100, 0b01010100, 0b01001100, 0b01000100,0b00000000, 0b00000000, 0b00000000},  // z
    {0b00000000, 0b00001000, 0b00110110, 0b01000001, 0b00000000,0b00000000, 0b00000000, 0b00000000},  // {
    {0b00000000, 0b00000000, 0b01111111, 0b00000000, 0b00000000,0b00000000, 0b00000000, 0b00000000},  // |
    {0b00000000, 0b01000001, 0b00110110, 0b00001000, 0b00000000,0b00000000, 0b00000000, 0b00000000},  // }
    {0b00001000, 0b00001000, 0b00101010, 0b00011100, 0b00001000,0b00000000, 0b00000000, 0b00000000},  // ~
    {0b00001000, 0b00011100, 0b00101010, 0b00001000, 0b00001000,0b00000000, 0b00000000, 0b00000000}   // <-
};


void delay_ms(uint32_t ms)                                    // rough delay, only used for the display reset
{
    for (volatile uint32_t i = 0; i < ms * 4000U; i++);       // ~4000 passes per ms at 48 MHz; volatile keeps the loop alive
}

void oled_send(uint8_t byte, uint8_t is_data)                 // sends ONE byte to the display
{
    CS(1);                                                    // make sure display starts deselected
    DC(is_data);                                              // tell display: 0 = command, 1 = pixel data
    CS(0);                                                    // select display (start of transfer)
    while ((SPI2->SR & SPI_SR_TXE) == 0);                     // wait until SPI transmit buffer is empty
    *(volatile uint8_t *)&SPI2->DR = byte;                    // 8-bit write; a normal write sends 16 bits on the F0
    while ((SPI2->SR & SPI_SR_TXE) == 0);                     // wait until byte has moved into the shift register
    while ((SPI2->SR & SPI_SR_BSY) != 0);                     // wait until last bit is fully clocked out
    CS(1);                                                    // deselect (end of transfer)
}

void oled_set_cursor(uint8_t page, uint8_t col)
{
    col += 2;                           // this display's first visible column is SEG 2
    oled_send(0xB0 | page, 0);          // select page (line) 0-7
    oled_send(col & 0x0F, 0);           // lower 4 bits of the starting column
    oled_send(0x10 | (col >> 4), 0);    // upper bits of the starting column
}

void oled_clear(void)
{
    for (uint8_t page = 0; page < 8; page++)   // every line
    {
        oled_set_cursor(page, 0);               // start of that line
        for (uint8_t i = 0; i < 128; i++)       // 128 columns
            oled_send(0x00, 1);                 // all pixels off
    }
}

void oled_print(const char *s)                                // draws a string of digits
{
    while (*s)                                                // loop until the string's '\0' terminator
    {
        for (uint8_t i = 0; i < 8; i++)                       // each character = 8 columns
        {
            oled_send(Characters[(uint8_t)*s][i], 1);            
        }
        s++;                                                  // move to next character
    }
}



void oled_config(void)                                        // one-time display setup
{
    RCC->AHBENR  |= RCC_AHBENR_GPIOBEN;                       // turn on clock for GPIOB
    RCC->APB1ENR |= RCC_APB1ENR_SPI2EN;                       // turn on clock for SPI2 

    GPIOB->MODER &= ~(GPIO_MODER_MODER8  | GPIO_MODER_MODER9  | GPIO_MODER_MODER11 |
                      GPIO_MODER_MODER13 | GPIO_MODER_MODER15);   // clear the mode bits of all 5 pins


    GPIOB->MODER |= GPIO_MODER_MODER8_0 | GPIO_MODER_MODER9_0 | GPIO_MODER_MODER11_0; // PB8, PB9, PB11 = 01 = general purpose output mode (DC, RES, CS)


    GPIOB->MODER |= GPIO_MODER_MODER13_1 | GPIO_MODER_MODER15_1;  // PB13, PB15 = 10 = alternate function mode (SCK, MOSI), this means that a peripheral is controlling the pin, not us

    GPIOB->AFR[1] &= ~((0xFU << 20) | (0xFU << 28));          // AFR[1] = pins 8-15; clear PB13 and PB15 -> AF0 = SPI2

    CS(1);                                                    // idle: display deselected
    RES(1);                                                   // idle: display not in reset

    SPI2->CR1 = SPI_CR1_BIDIMODE                              // The display only shows data, we dont read from it, so we can use the SPI in 1-line bidirectional mode
              | SPI_CR1_BIDIOE                                // that one direction is output (transmit)
              | SPI_CR1_MSTR                                  // Set STM32 as the master
              | SPI_CR1_SSM                                   // SPI hardware has its own chip-select feature, but it doesn’t suit our display. We control CS# (PB8) manually with CS(0) and CS(1). “SSM” is Software Slave Management, which tells SPI2 to ignore its own hardware chip-select pin.
              | SPI_CR1_SSI                                   // internal NSS = high, else master mode faults
              | (7U << 3);                                    // Start the display with it being slow
                                                              
              
    SPI2->CR2 = 0x0700;                                       // Every frame we send is exactly 8 bits, 0111 means 8 bits
    SPI2->CR1 |= SPI_CR1_SPE;                                 // Turns on SPI2, which is the peripheral that sends data to the display

    RES(0);                                                   // pull display reset low (Reset the display)
    delay_ms(5);                                              // Pause for 5 milliseconds to make sure the display is reset
    RES(1);                                                   // Release the reset
    delay_ms(5);                                              // Let the display wake up

    for (unsigned int i = 0; i < sizeof(oled_init_cmds); i++) // send every init command
        oled_send(oled_init_cmds[i], 0);                      // 0 = these are commands, not data
}