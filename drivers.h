/*
 * drivers.h
 *
 *  Created on: Apr 30, 2020
 *      Author: krad2
 */

#ifndef DRIVERS_H_
#define DRIVERS_H_

#include <msp430.h>

static void clock_init(void) {
	DCOCTL = CALDCO_1MHZ;
	BCSCTL1 = CALBC1_1MHZ;
	BCSCTL1 &= ~(XTS | XT2OFF | DIVA_3);
	BCSCTL3 |= LFXT1S_2;
}

static void adc_init(void *ptr, unsigned size) {
	// Stop the ADC and wait for it to complete whatever it was doing
	ADC10CTL0 &= ~ENC;
	ADC10CTL0 |= ADC10ON;
	while (ADC10CTL1 & ADC10BUSY);

	// ADC will operate on a [0, 3.3V] range and initialize reference circuitry to LPM, long sample time too
	ADC10CTL0 |= SREF_0 | REFBURST | ADC10SR | ADC10SHT_3;

	// ADC will use the sleepwalking oscillator, repeated single-channel triggered by TA0CCR0
	ADC10CTL1 = ADC10SSEL_0 | CONSEQ_2 | SHS_2;

	// Prime the ADC DMA
	ADC10DTC0 |= ADC10CT;
	ADC10DTC1 = size;	// number of 16 bit words per block
	if (!ptr) {
		P1OUT |= BIT0;
		_low_power_mode_3();
	} else ADC10SA = (unsigned) ptr;

	// Then start the ADC on P1.3
	ADC10AE0 |= BIT3;
	ADC10CTL1 |= INCH_3;
	ADC10CTL0 |= ADC10IE | ENC | ADC10SC;
}

#pragma vector = ADC10_VECTOR
__attribute__((interrupt)) void adc_isr(void) {
	// write to FIFO
	P1OUT ^= BIT6;
	__bic_SR_register_on_exit(LPM3_bits + GIE);
}

static void timer_init(unsigned period) {
	TACTL = MC_0 | TACLR;

	TACCR0 = period - 1;
	TACCTL0 = OUTMOD_4;

	TACTL = TASSEL_1 | MC_1;                  // ACLK, up mode
}

static void uart_init() {
	// P1.1 = RXD, P1.2 = TXD
	P1SEL |= BIT1 | BIT2;
	P1SEL2 |= BIT1 | BIT2;

	// Stop the UART, set SMCLK
	UCA0CTL1 |= UCSWRST | UCSSEL_2;

	// Set baud rate parameters according to the datasheet for SMCLK = 1 MHz
	UCA0BR0 = 0x68;
	UCA0BR1 = 0x00;
	UCA0MCTL = UCBRS_1;

	// Re-enable the UART
	UCA0CTL1 &= ~UCSWRST;
}

static inline void uart_putc(char c) {
	UCA0TXBUF = c;
	while (UCA0STAT & UCBUSY);
}

static void uart_puts(char *s) {
	while (*s) {
		uart_putc(*s);
		s++;
	}
}

#endif /* DRIVERS_H_ */
