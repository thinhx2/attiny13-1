// tinyCylon_2_1.c
// 22 february 2010 - dale wheat - added mode memory
// based on tinyCylon2.c
// revised firmware for tinyCylon LED scanner
// written by dale wheat - 18 november 2008
// based on behavior of original tinyCylon firmware

// notes:

// device = ATtiny13A
// clock = 128 KHz internal RC oscillator
// max ISP frequency ~20 KHz
// brown-out detect = 1.8 V

#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/eeprom.h> // 2.1

// These registers are available on the ATtiny13A but not the original ATtiny13

// Brown out detector control register

#define BODCR _SFR_IO8(0x30)
#define BODSE 0
#define BODS 1

// Power reduction register

#define PRR _SFR_IO8(0x3C)
#define PRADC 0
#define PRTIM0 1

///////////////////////////////////////////////////////////////////////////////
// macros
///////////////////////////////////////////////////////////////////////////////

#define nop() asm("nop")

#define sbi(port, bit) (port) |= (1 << (bit))
#define cbi(port, bit) (port) &= ~(1 << (bit))

typedef enum {
  MODE_0, // cylon scanner
  MODE_MAX // off
} MODE;

volatile MODE mode __attribute__ ((section (".noinit")));

// EEPROM non-volatile storage section

unsigned char eeprom_do_not_use EEMEM; // bad luck - do not use location 0
unsigned char eeprom_mode EEMEM; // mode storage location

///////////////////////////////////////////////////////////////////////////////
// init() - initialize everything
// note:  this "function" is in section .init3 so it is executed before main()
///////////////////////////////////////////////////////////////////////////////

void init(void) __attribute__ ((naked, section(".init3")));
void init(void) {

  // turn off unused peripherals to save power

  PRR = 0<<PRTIM0 | 1<<PRADC; // power down ADC - 2.1
  ACSR = 1<<ACD; // disable analog comparator

  // disable all digital inputs
  DIDR0 = 1<<ADC3D | 1<<ADC2D | 1<<ADC1D | 1<<ADC0D | 1<<AIN1D | 1<<AIN0D; 

  // EEPROM address register is always pointed to the "mode memory" address, 1
  // EEPROM address regsiter always points to mode memory location: 1
  asm("ldi r24, 1"); 
  asm("out 0x1e, r24");

  if(bit_is_set(MCUSR, PORF)) {
    //mode = MODE_POWER_ON; // power on!
    //mode = eeprom_read_byte(&eeprom_mode); // recall
    sbi(EECR, EERE); // strobe EEPROM read enable signal
    mode = EEDR; // read mode memory location from EEPROM
    if(mode > MODE_MAX) {
      mode = MODE_0; // classic cylon scanner
    }
  } else if(bit_is_set(MCUSR, EXTRF)) {
    mode++; // advance mode
    if(mode > MODE_MAX) {
      mode = MODE_0; // reset mode
    }
    //eeprom_write_byte(&eeprom_mode, mode); // memorize
    EEDR = mode; // move mode to EEPROM data register, address register is already set up
    asm("out 0x1c, r1"); // write a zero to the EEPROM control register
    sbi(EECR, EEMPE); // enable the "master program" bit
    sbi(EECR, EEPE); // write the data to the EEPROM
  }

  MCUSR = 0; // reset bits

  // initialize ATtiny13 input & output port

  // PORTB
  //  PB0 5 MOSI/AIN0/OC0A/PCINT0       D1 output, active low
  //  PB1 6 MISO/AIN1/OC0B/INT0/PCINT1  D2 output, active low
  //  PB2 7 SCK/ADC1/T0/PCINT2          D3 output, active low
  //  PB3 2 PCINT3/CLKI/ADC3            D4 output, active low
  //  PB4 3 PCINT4/ADC2                 D5 output, active low
  //  PB5 1 PCINT5/-RESET/ADC0/dW       MODE advance pushbutton

  PORTB = 0; //<<PORTB5 | 1<<PORTB4 | 1<<PORTB3 | 1<<PORTB2 | 1<<PORTB1 | 1<<PORTB0;
  DDRB = 0; //<<DDB5 | 1<<DDB4 | 1<<DDB3 | 1<<DDB2 | 1<<DDB1 | 1<<DDB0;

  // initialize ATtiny13 timer/counter
  TCCR0B = 0<<FOC0A | 0<<FOC0B | 0<<WGM02 | 0<<CS02 | 0<<CS01 | 1<<CS00;
  TIMSK0 = 0<<OCIE0B | 0<<OCIE0A | 1<<TOIE0; // interrupts

  sei(); // enable global interrupts
}

///////////////////////////////////////////////////////////////////////////////
// timing & delay functions
///////////////////////////////////////////////////////////////////////////////

volatile unsigned char downcounter;
void delay(unsigned char n) {
  downcounter = n;
  while(downcounter) {
    MCUCR = 1<<PUD | 1<<SE | 0<<SM1 | 0<<SM0 | 0<<ISC01 | 0<<ISC00; // idle mode
    asm("sleep"); // go to sleep to save power
  }
}

///////////////////////////////////////////////////////////////////////////////
// pseudorandom number generator
///////////////////////////////////////////////////////////////////////////////

unsigned int prand(void) {
  static unsigned int prand_value = 0xDA1E; // randomly seeded ;)
  prand_value = (prand_value >> 1) ^ (-(prand_value & 1) & 0xd001);
  return prand_value;
}

///////////////////////////////////////////////////////////////////////////////
// cylon() - simulate cylon scanner
///////////////////////////////////////////////////////////////////////////////

#define CYLON_SCAN_DELAY 30
const unsigned char led_mask[] = {
  0b00011, 0b00101, 0b01001, 0b10001,
  0b00110, 0b01010, 0b10010,
  0b01100, 0b10100,
  0b11000,
};

const unsigned char leds[] = {
  0b00001, 0b00010, 0b00001, 0b00100, 0b00001, 0b01000, 0b00001, 0b10000,
  0b00010, 0b00100, 0b00010, 0b01000, 0b00010, 0b10000,
  0b00100, 0b01000, 0b00100, 0b10000,
  0b01000, 0b10000,
};

void cylon(unsigned char cylon_style) {

  unsigned char i; // array iterator

  while(1) {
    if(cylon_style == 0) {
      // traditional (back & forth) cylon scanner

      for(i = 0; i < sizeof(leds); i++) {
	DDRB = led_mask[i >> 1];
        PORTB = leds[i];
        delay(CYLON_SCAN_DELAY);
	DDRB = 0;
      }

      for(i = sizeof(leds) - 1; i > 1; i--) {
	DDRB = led_mask[i >> 1];
        PORTB = leds[i];
        delay(CYLON_SCAN_DELAY);
	DDRB = 0;
      }
    } 
  }
}

///////////////////////////////////////////////////////////////////////////////
// pig_eyes() - glowing pig eyes are scary
///////////////////////////////////////////////////////////////////////////////

void pig_eyes(unsigned char n) {

  unsigned char leds_on, leds_off = 0b00011111;
  unsigned char pwm_counter, pwm_value;

  while(1) {
  
    if(n == 1) {
      leds_on = 0b00011110; // one eye
    } else if(n == 2) {
      leds_on = 0b00010011; // 2 eyes
    } else if(n == 3) {
      leds_on = 0b00010010; // 3 eyes
    } else if(n == 4) {
      leds_on = 0b00000001; // 4 eyes
    } else {
      leds_on = 0b00011111 ^ (1 << (prand() % 5)); // single (random) eye
    }

    // ramp up...

    for(pwm_value = 1; pwm_value < 128; pwm_value++) {
      for(pwm_counter = 0; pwm_counter < 128; pwm_counter++) {
        if(pwm_value > pwm_counter) {
          PORTB = leds_on;
        } else {
          PORTB = leds_off;
        }
      }
    }

    // ... & ramp back down again

    for(pwm_value = 127; pwm_value > 0; pwm_value--) {
      for(pwm_counter = 0; pwm_counter < 128; pwm_counter++) {
        if(pwm_value > pwm_counter) {
          PORTB = leds_on;
        } else {
          PORTB = leds_off;
        }
      }
    }

    delay(250); // scary dark time
  }
}

///////////////////////////////////////////////////////////////////////////////
// random() - random blinking
///////////////////////////////////////////////////////////////////////////////

void random(unsigned char n) {

  while(1) {

    if(n == 0) {

      // light up a random number of LEDs
      PORTB = prand() & 0x1F;

    } else if(n == 1) {

      // light up a single LED every time
      PORTB = 0b00011111 ^ (1 << (prand() % 5));

    } else {

      if((prand() & n) == n) {
  
        // light up an LED (maybe)
        PORTB = 0b00011111 ^ (1 << (prand() % 5));

      } else {

        // no LEDs on (maybe not)
        PORTB = 0b00011111;
      }
    }

    delay(50);
  }
}

///////////////////////////////////////////////////////////////////////////////
// main() - main program function
///////////////////////////////////////////////////////////////////////////////

void main(void) {

  switch(mode) {

    case MODE_0: // traditional (back & forth) cylon scanner
      cylon(0);
      break;

    case MODE_MAX: // off
      PORTB = 0b00011111; // all LEDs off
      while(1) {
        // deepest sleep mode
        cli(); // disable interrupts
        PRR = 1<<PRTIM0 | 1<<PRADC; // power down timer/counter0 & ADC
        BODCR = 1<<BODS | 1<<BODSE; // enable BOD disable during sleep, step 1
        BODCR = 1<<BODS | 0<<BODSE; // step 2
        MCUCR = 1<<PUD | 1<<SE | 1<<SM1 | 0<<SM0 | 0<<ISC01 | 0<<ISC00; // select "power down" mode
        asm("sleep"); // go to sleep to save power
      }
  }
}

///////////////////////////////////////////////////////////////////////////////
// timer/counter0 overflow interrupt handler
///////////////////////////////////////////////////////////////////////////////

ISR(TIM0_OVF_vect) {

  downcounter--; // decrement downcounter for delay functions
}

///////////////////////////////////////////////////////////////////////////////

// [end-of-file]
