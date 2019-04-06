/* ____________________________________________________________________________
 *                                                                             )
 *                                                                            /
 *  ________ ___  ___  ________   ___  __    ________                        (
 * |\  _____\\  \|\  \|\   ___  \|\  \|\  \ |\   __  \                        \
 * \ \  \__/\ \  \\\  \ \  \\ \  \ \  \/  /|\ \  \|\  \  ____________          )
 *  \ \   __\\ \  \\\  \ \  \\ \  \ \   ___  \ \   __  \|\____________\       /
 *   \ \  \_| \ \  \\\  \ \  \\ \  \ \  \\ \  \ \  \ \  \|____________|      (
 *    \ \__\   \ \_______\ \__\\ \__\ \__\\ \__\ \__\ \__\                    \
 *     \|__|    \|_______|\|__| \|__|\|__| \|__|\|__|\|__|                     )
 *  _______  ________  ________  ________   ___  ________  ________           /
 * |\___   ___\\   __  \|\   __  \|\   ___  \|\  \|\   ____\|\   ____\       (
 * \|___ \  \_\ \  \|\  \ \  \|\  \ \  \\ \  \ \  \ \  \___|\ \  \___|_       \
 *      \ \  \ \ \   _  _\ \  \\\  \ \  \\ \  \ \  \ \  \    \ \_____  \       )
 *       \ \  \ \ \  \\  \\ \  \\\  \ \  \\ \  \ \  \ \  \____\|____|\  \     /
 *        \ \__\ \ \__\\ _\\ \_______\ \__\\ \__\ \__\ \_______\____\_\  \   (
 *         \|__|  \|__|\|__|\|_______|\|__| \|__|\|__|\|_______|\_________\   \
 *                                                            \|_________|     )
 *                                                                            /
 *                                                                           (
 *                                                                            \
 * ----------------------------------------------------------------------------)
 *                                                                            /
 *                                                                           (
 *                  A MIDI Clock -> Analog Sync Converter                     \
 *                         Based on the ATtiny85                               )
 *                         built by Funkatronics                              /
 *                                                                           (
 *                                                                            \
 * ----------------------------------------------------------------------------)
 *                                                                            /
 *                                                                           (
 *                    Copyright (C) 2019 Marco Martinez                       \
 *                                                                             )
 *                       funkatronicsmail@gmail.com                           /
 *                                                                           (
 *                                                                            \
 * ----------------------------------------------------------------------------)
 *                                                                            /
 *    This program is free software: you can redistribute it and/or modify   (
 *    it under the terms of the GNU General Public License as published by    \
 *     the Free Software Foundation, either version 3 of the License, or       )
 *                   (at your option) any later version.                      /
 *                                                                           (
 *       This program is distributed in the hope that it will be useful,      \
 *       but WITHOUT ANY WARRANTY; without even the implied warranty of        )
 *        MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the        /
 *                 GNU General Public License for more details               (
 *                                                                            \
 *     You should have received a copy of the GNU General Public License       )
 *    along with this program. If not, see <http://www.gnu.org/licenses/>.    /
 *                                                                           (
 *                                                                            \
 * ____________________________________________________________________________)
 */

// Oscillator Calibration - use TinyTuner to find this value for each chip
#define OSC_CAL 0xAB

// Length of the sync pulse in milliseconds
#define PULSE_TIME 5

// Number of MIDI clock messages per pulse
// 24 gives 1/4 note sync, 12 gives 1/8 note sync, etc.
#define CLOCKS_PER_PULSE 12

// MIDI Messages (bit reversed, for efficiency)
#define MIDI_CLOCK 0x1F
#define MIDI_START 0x5F
#define MIDI_CONTINUE 0xDF
#define MIDI_STOP 0x3F

// Sync output pin
#define SYNC_OUT PB3

// State variables
uint8_t clockCount = 0;
boolean play = false;

// Interrupt Service Routine
ISR (PCINT0_vect) {
  if (!(PINB & 1<<PINB0)) {       // Ignore if DI is high
    GIMSK &= ~(1 << PCIE);        // Disable pin change interrupts
    TCCR0A = 2 << WGM00;          // CTC mode
    TCCR0B = 0<<WGM02 | 2<<CS00;  // Set prescaler to /8
    GTCCR |= 1 << PSR0;           // Reset prescaler
    TCNT0 = 0;                    // Count up from 0
    OCR0A = 5;                    // Delay till middle of start bit
    TIFR |= 1 << OCF0A;           // Clear output compare flag
    TIMSK |= 1 << OCIE0A;         // Enable output compare interrupt
  }
}

// Timer0 Compare Interrupt
ISR (TIMER0_COMPA_vect) {
  TIMSK &= ~(1<<OCIE0A);          // Disable COMPA interrupt
  TCNT0 = 0;                      // Count up from 0
  OCR0A = 31;                     // Shift every bit
  // Enable USI OVF interrupt, and select Timer0 compare match as USI Clock source:
  USICR = 1<<USIOIE | 0<<USIWM0 | 1<<USICS0;
  USISR = 1<<USIOIF | 8;          // Clear USI OVF flag, and set counter
}

// USI Overflow Interrupt 
ISR (USI_OVF_vect) {
  USICR = 0;                      // Disable USI         
  uint8_t in = USIDR;             // Read USI data
  
  //Parse MIDI data
  if(play && in == MIDI_CLOCK) {
    sync();
  } else if(in == MIDI_START) {
    play = true;
    clockCount = 0;
  } else if(in == MIDI_CONTINUE) {
    play = true;
  } else if(in == MIDI_STOP) {
    play = false;
    killSync();
  }
    
  GIFR = 1<<PCIF;                 // Clear pin change interrupt flag.
  GIMSK |= 1<<PCIE;               // Enable pin change interrupts again
}

// Sends a sync pulse
inline void sync() {
  if(clockCount == 0) {
    //pulseTime = millis();
    PORTB |= (1 << SYNC_OUT);
    _delay_ms(PULSE_TIME);
    PORTB &= ~(1 << SYNC_OUT);
  }
  clockCount++;
  if(clockCount == CLOCKS_PER_PULSE) 
    clockCount = 0;
}

// Kills the sync pulse if it is active
inline void killSync() {
  PORTB &= ~(1 << SYNC_OUT);
}

void setup() {
  // Oscillator Calibration
  OSCCAL = OSC_CAL;

  // Define output pin for sync signal
  DDRB |= (1 << SYNC_OUT);
  
  // Setup USI for serial communication
  DDRB &= ~(1 << PB0);    // Enable DI pin (PB0)
  PORTB |= 1 << PB0;      // Enable internal pull-up resistor on DI
  USICR = 0;              // Disable USI.
  GIFR = 1 << PCIF;       // Clear pin change interrupt flag.
  GIMSK |= 1 << PCIE;     // Enable pin change interrupts
  PCMSK |= 1 << PCINT0;   // Enable pin change on pin 0
}

void loop() {}
