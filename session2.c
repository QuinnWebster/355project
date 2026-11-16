
#include <stdio.h>
#include "diag/Trace.h"
#include "cmsis/cmsis_device.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wmissing-declarations"
#pragma GCC diagnostic ignored "-Wreturn-type"


// General overview

/*

Data Flow:

- User turns the pot, which cahnges the voltage on the ADC pin (PA1), which outputs a value between 0 and 4095
Main polls the ADC until it finished converting a calue and stores the value in adc_value

- The adc_value is then written to the DAC, which outputs a proportional voltage on the DAC pin (PA4)
  That voltage then drives the LED side of the optocoupler, which then acts as a voltage-controlled resistor for the 555 timer circuit
  so that means turning the pot changes the 555 output frequency
  essentially, this just means that by turning the pot we can change the frequency of the square wave output from the 555 timer

- The adc_value is also converted to milivolts and to resistance, 
  which are then displayed on the OLED display, so that the user can see what the pot is set to

- The signal that is being measured is wired to PB3 (If 555 output) or PB2 (If function generator output)/ 
  Each rising edge triggers an interrupt, the first edge zeros and starts the timer, the second edge stops it. The tick count in one period, 
  so we can calculate the frequency by doing SystemCoreClock / ticks, and then display that on the OLED display

  Each time the user button is pressed, it clears the old frequency, flips the use_555 flag

*/


/*

Pin inputs that trigger interrupts:

- PA0: User button, when this button is pressed, a rising edge occurs and is detected it runs the EXTI0_1_IRQHandler, which clears the old frequency and flips the use_555 flag
- PB2: Function generator output, when a rising edge occurs, it is detected it runs the EXTI2_3_IRQHandler, which either starts or stops the timer depending on if its the first or second edge
- PB3: 555 timer output, when a rising edge occurs, it is detected it runs the EXTI2_3_IRQHandler, which either starts or stops the timer depending on if its the first or second edge

Analog Pins:

- PA1: The ADC input pin, which is connected to the potentiometer, which is used to control the frequency of the 555 timer output
- PA4: The DAC output pin, which is connected to the LED side of the optocoupler, which is used to control the resistance of the 555 timer circuit

Display Pins:
- PB8: Chip Select pin, which is used to select the OLED display, low means the display is selected, high means it is not selected
- P89: Low means the next byte is a command, high means pixel data.
- PB11: Reset pin, low means the display is held in reset, high means the display is running
- PB13: The SPI hardware pulses this to clock each bit
- PB15: The SPI hardware shifts the bits out here

*/








#define R_TOTAL_OHMS 5000 // We found this in the pot section of the manual



// Used to turn the ADC's unitless fraction (0-4095) into ohms; 5000 is the value at full turn


void ADC_INIT(void);
void DAC_INIT(void);
void TIM2_INIT(void);
void EXTI_INIT(void);


volatile uint32_t adc_value = 0; // The raw value read from the ADC, ranges from 0 to 4095, also the number we send to the DAC

volatile uint32_t millivolts = 0; // The ADC reading converted to millivolts, ranges from 0 to 3300
                                  // Because ADC is just the voltage expressed as a number between 0 and 4095


volatile uint32_t resistance = 0; // The resistance of the potentiometer, ranges from 0 to 5000 ohms, calculated from the ADC reading

volatile uint32_t frequency = 0;     // result, in Hz
volatile uint8_t  first_edge = 1; // If 1, then we are waiting for the first edge then well start timing
                                    // If 0, then we are timing and waiting for the second edge to stop the timer


volatile uint8_t use_555 = 1; // If 1, then we are measuring the 555 timer output
                              // If 0, then we are measuring the function generator output

void oled_config(void);  // tells the compiler these exist, defined further down
void oled_clear(void);
void oled_set_cursor(uint8_t page, uint8_t col);
void oled_print(const char *s);


void SystemClock48MHz( void )
{
    RCC->CR &= ~(RCC_CR_PLLON); // Turn off the PLL, the PLL is part of the clock that changes the speed of it
    while (( RCC->CR & RCC_CR_PLLRDY ) != 0 ); // Wait for the PLL to turn off
    RCC->CFGR = 0x00280000; // Set PLL to 12, which sets the clock to 48 MHz, the fastest it can go
    RCC->CR |= RCC_CR_PLLON; // Turn the PLL back on
    while (( RCC->CR & RCC_CR_PLLRDY ) != RCC_CR_PLLRDY ); // Wait for the PLL to turn back on
    RCC->CFGR = ( RCC->CFGR & (~RCC_CFGR_SW_Msk)) | RCC_CFGR_SW_PLL; // Switch the system clock to the PLL
    SystemCoreClockUpdate(); 
}

int main(int argc, char* argv[]){

    SystemClock48MHz(); 

    oled_config();              // set up the display
    oled_clear();               // wipe random startup pixels from display

    // Init the ADC, DAC, Timer 2, and External Interrupts
    ADC_INIT();
    DAC_INIT();
    TIM2_INIT();     
    EXTI_INIT();


    uint32_t counter = 0; // Used for testing print statement frequency

    while (1)
    {
        while ((ADC1->ISR & ADC_ISR_EOC) == 0){} // Poll the ADC until it finishes converting the analog voltage to a digital value
                                                // It gets the value from the potentiometer
    

        adc_value = ADC1->DR;  // Read the converted value, DR is the data register, which is where the ADC puts the converted value

        DAC->DHR12R1 = adc_value; // Write the value to the DAC
        // Copy the ADC reading straight into the DAC's channel 1 holding register (output on PA4).

        millivolts = (adc_value * 3300) / 4095; // 4095 is the max value of adc we read from the dial, 3300 mili volts corresponds to 4095/4095
        resistance = (adc_value * R_TOTAL_OHMS) / 4095; // 4095 is the max value of adc we read from the dial, 5000 ohms corresponds to 4095/4095

        char buffer[17];   // 16 characters fit on one line + 1 for the '\0' terminator


        // counter++;
        // if(counter > 50000)
        // {
            counter = 0;
            // trace_printf("ADC Value: %u\n", adc_value); // Only print every so often

            // trace_printf("ADC: %u  Voltage: %u mV  Resistance: %u ohms\n", adc_value, millivolts, resistance);

            // snprintf(buffer, sizeof(buffer), "ADC: %4u", (unsigned int)adc_value);   // text like "ADC: 2048"
            // oled_set_cursor(0, 0);       // page 0 = top line, column 0 = left edge
            // oled_print(buffer);          // draw it


            snprintf(buffer, sizeof(buffer), "R: %4u Ohms  ", (unsigned int)resistance);
            oled_set_cursor(0, 0);                     // top line
            oled_print(buffer);

            snprintf(buffer, sizeof(buffer), "F: %5u Hz   ", (unsigned int)frequency);
            oled_set_cursor(2, 0);
            oled_print(buffer);

            // snprintf(buffer, sizeof(buffer), "Src: %-8s", use_555 ? "555" : "FuncGen");
            // oled_set_cursor(4, 0);
            // oled_print(buffer);

        // }
    }

    return 0;


}


// Interrupt handler for when user presses button
void EXTI0_1_IRQHandler(void)
{

    // Only continue if EXTI line 0 (the USER button on PA0) is the one that's pending.
    // Lines 0 and 1 share this handler, so we confirm which line fired before acting.
    if (EXTI->PR & EXTI_PR_PR0) // EXTI_PR_PR0 is the pending register for line 0, which is the USER button on PA0
    {
        TIM2->CR1 &= ~TIM_CR1_CEN;             // stop any measurement in progress
        first_edge = 1;                        // next edge will start a fresh measurement
        frequency = 0;                         // clear the old frequency, so that we can remeasure frequency each time the wave source is changed

        use_555 = !use_555;                    // flip the choice

        if (use_555) {
            EXTI->IMR &= ~EXTI_IMR_MR2;        // ignore function generator
            EXTI->IMR |=  EXTI_IMR_MR3;        // listen to 555
        } else {
            EXTI->IMR &= ~EXTI_IMR_MR3;        // ignore 555
            EXTI->IMR |=  EXTI_IMR_MR2;        // listen to function generator
        }

        // 
        EXTI->PR = EXTI_PR_PR2 | EXTI_PR_PR3;  // throw away leftover edges
        EXTI->PR = EXTI_PR_PR0;                // clear the button flag, if we didnt do this then the interrupt would fire again immediately after we exit this handler
    }
}


// function to handle the external interrupts from the 555 timer and function generator
void EXTI2_3_IRQHandler(void)
{
if (EXTI->PR & (EXTI_PR_PR2 | EXTI_PR_PR3))    {
        if (first_edge) // If its the first edge, we start the timer and wait for the second edge to stop it
        {
            TIM2->CNT = 0;                   // reset timer
            TIM2->CR1 |= TIM_CR1_CEN;        // start timer
            first_edge = 0;
        }
        else
        {
            TIM2->CR1 &= ~TIM_CR1_CEN;       // stop timer
            uint32_t ticks = TIM2->CNT;      // The number of ticks that have passed between the first and second edge, which is the period of the wave in ticks
            if (ticks > 0) // Just to check that we dont divide by zero
                frequency = SystemCoreClock / ticks;
            first_edge = 1;
        }
        EXTI->PR = EXTI_PR_PR2 | EXTI_PR_PR3;              // tell the hardware "handled it" (write 1 to clear)
    }
}


#define CS(x)  (GPIOB->BSRR = (x) ? (1U << 8)  : (1U << (8 + 16)))   // PB8  CS#:  1 = deselect, 0 = select display
#define DC(x)  (GPIOB->BSRR = (x) ? (1U << 9)  : (1U << (9 + 16)))   // PB9  D/C#: 1 = data byte, 0 = command byte
#define RES(x) (GPIOB->BSRR = (x) ? (1U << 11) : (1U << (11 + 16)))  // PB11 RES#: 1 = run, 0 = hold in reset

uint8_t oled_init_cmds[] = {                                  // setup commands copied from the handout, these just make sure the led is setup correctly
    0xAE, 0x20, 0x00, 0x40, 0xA0 | 0x01, 0xA8, 0x40 - 1,      
    0xC0 | 0x08, 0xD3, 0x00, 0xDA, 0x32, 0xD5, 0x80,          
    0xD9, 0x22, 0xDB, 0x30, 0x81, 0xFF, 0xA4, 0xA6,           
    0xAD, 0x30, 0x8D, 0x10, 0xAE | 0x01, 0xC0, 0xA0           
};

//
// Character specifications for LED Display
// Row number is the character ASCII code, so like the number 4 is ASCII is 52 so its in row 52

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


// Power up DAC channel 1 and connect it to pin PA4 as an analog output.
// The DAC turns the ADC reading into a voltage that drives the optocoupler, which controls the 555's frequency
void DAC_INIT(void){

     RCC->AHBENR |= RCC_AHBENR_GPIOAEN;
    /*
    Turns on power to GPIO port A
    RCC is the Reset and Clock Control register
    AHBENR is the bus that the GPIO is on
    RCC_AHBENR_GPIOAEN is the bit that turns on the clock for GPIO port A
    */ 

    RCC->APB1ENR |= RCC_APB1ENR_DACEN;      // Turn on the DAC clock

    GPIOA->MODER |= GPIO_MODER_MODER4;      // PA4 = 11 = analog mode
    GPIOA->PUPDR &= ~GPIO_PUPDR_PUPDR4;     // no pull-up/pull-down

    DAC->CR |= DAC_CR_EN1;                  // switch on DAC channel 1
}

void TIM2_INIT(void)
{
    RCC->APB1ENR |= RCC_APB1ENR_TIM2EN;   // turn on TIM2's clock
    TIM2->PSC = 0;                        // no slowdown: count at 48 MHz, it divides by this value + 1, so 0 means divide by 1
    TIM2->ARR = 0xFFFFFFFF;               // count as high as possible before wrapping, this will take about 89 seconds at 48 MHz
    TIM2->EGR |= TIM_EGR_UG;              // apply those settings
}



void EXTI_INIT(void)
{
    // Clocks
    RCC->AHBENR  |= RCC_AHBENR_GPIOAEN | RCC_AHBENR_GPIOBEN;
    RCC->APB2ENR |= RCC_APB2ENR_SYSCFGCOMPEN;

    // Pins as inputs
    GPIOA->MODER &= ~GPIO_MODER_MODER0;                // PA0 = USER button
    GPIOB->MODER &= ~(GPIO_MODER_MODER2 | GPIO_MODER_MODER3);   // PB2 = func gen, PB3 = 555
    GPIOB->PUPDR &= ~(GPIO_PUPDR_PUPDR2 | GPIO_PUPDR_PUPDR3);   // no pull

    // Route pins to EXTI lines
    SYSCFG->EXTICR[0] &= ~(SYSCFG_EXTICR1_EXTI0 | SYSCFG_EXTICR1_EXTI2 | SYSCFG_EXTICR1_EXTI3);
    SYSCFG->EXTICR[0] |=  (SYSCFG_EXTICR1_EXTI2_PB | SYSCFG_EXTICR1_EXTI3_PB);  // line 0 stays port A

    // Rising edge triggers
    EXTI->RTSR |= EXTI_RTSR_TR0 | EXTI_RTSR_TR2 | EXTI_RTSR_TR3;

    // Enable: button + 555 only. PB2 (MR2) stays off until the button is pressed.
    EXTI->IMR |= EXTI_IMR_MR0 | EXTI_IMR_MR3;

    // CPU side
    NVIC_SetPriority(EXTI0_1_IRQn, 0);                 // button = most urgent
    NVIC_EnableIRQ(EXTI0_1_IRQn);
    NVIC_SetPriority(EXTI2_3_IRQn, 1);
    NVIC_EnableIRQ(EXTI2_3_IRQn);
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
