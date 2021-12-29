#include "./sysclk.h"

#ifdef __cplusplus
	extern "C" {
#endif

/*
	System tick timer
*/

volatile unsigned long int systemMillis					= 0;
volatile unsigned long int systemMilliFractional		= 0;
volatile unsigned long int systemMonotonicOverflowCnt	= 0;

ISR(TIMER0_OVF_vect) {
	unsigned long int m, f;

	m = systemMillis;
	f = systemMilliFractional;

	m = m + SYSCLK_MILLI_INCREMENT;
	f = f + SYSCLK_MILLIFRACT_INCREMENT;

	if(f >= SYSCLK_MILLIFRACT_MAXIMUM) {
		f = f - SYSCLK_MILLIFRACT_MAXIMUM;
		m = m + 1;
	}

	systemMonotonicOverflowCnt = systemMonotonicOverflowCnt + 1;

	systemMillis = m;
	systemMilliFractional = f;
}

unsigned long int millis() {
	unsigned long int m;

	uint8_t srOld = SREG;

	cli();

	m = systemMillis;
	SREG = srOld;

	return m;
}

unsigned long int micros() {
	uint8_t srOld = SREG;
	unsigned long int overflowCounter;
	unsigned long int timerCounter;

	cli();

	overflowCounter = systemMonotonicOverflowCnt;
	timerCounter = TCNT0;

	if(((TIFR0 & 0x01) != 0) && (timerCounter < 255)) {
		overflowCounter = overflowCounter + 1;
	}

	SREG = srOld;

	return ((overflowCounter << 8) + timerCounter) * (64L / (F_CPU / 1000000L));
}

void delay(unsigned long millisecs) {
	unsigned int lastMicro;

	lastMicro = (unsigned int)micros();

	while(millisecs > 0) {
		unsigned int curMicro = micros();
		if(curMicro - lastMicro >= 1000)  {
			lastMicro = lastMicro + 1000;
			millisecs = millisecs - 1;
		}
	}
	return;
}

void delayMicros(unsigned int microDelay) {
	#if F_CPU == 20000000L
		__asm__ __volatile__ (
			"nop\n"
			"nop\n"
		);
		if((microDelay = microDelay - 1) == 0) {
			return;
		}

		microDelay = (microDelay << 2) + microDelay;
	#elif F_CPU == 16000000L
		if((microDelay = microDelay - 1) == 0) {
			return;
		}

		microDelay = (microDelay << 2) - 2;
	#elif F_CPU == 8000000L
		if((microDelay = microDelay - 1) == 0) {
			return;
		}
		if((microDelay = microDelay - 1) == 0) {
			return;
		}

		microDelay = (microDelay << 1) - 1;
	#else
		#error No known delay loop calibration available for this F_CPU
	#endif

	__asm__ __volatile__ (
		"lp: sbiw %0, 1\n"
		"    brne lp"
		: "=w" (microDelay)
		: "0" (microDelay)
	);
	return;
}

void systickInit() {
	uint8_t sregOld = SREG;

	cli();

	TCCR0A = 0x00;
	TCCR0B = 0x03;		/* /64 prescaler */
	TIMSK0 = 0x01;		/* Enable overflow interrupt */

	PRR = PRR & (~0x20);

	SREG = sregOld;
}

void systickDisable() {
	TIMSK0 = 0x00;
}

#ifdef __cplusplus
	} /* extern "C" { */
#endif
