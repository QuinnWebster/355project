
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


        counter++;
        if(counter > 50000)
        {
            counter = 0;
            trace_printf("ADC Value: %u\n", adc_value); // Only print every so often

            trace_printf("ADC: %u  Voltage: %u mV  Resistance: %u ohms\n", adc_value, millivolts, resistance);


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
