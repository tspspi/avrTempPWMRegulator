#include <avr/io.h>
#include <avr/interrupt.h>
#include <math.h>
#include <util/twi.h>
#include <stdint.h>

#define SYSCLK_TIMER_OVERFLOW_MICROS	(64L * ((256L * 1000000L) / F_CPU))
#define SYSCLK_MILLI_INCREMENT			(SYSCLK_TIMER_OVERFLOW_MICROS / 1000L)
#define SYSCLK_MILLIFRACT_INCREMENT		((SYSCLK_TIMER_OVERFLOW_MICROS % 1000L) >> 3)
#define SYSCLK_MILLIFRACT_MAXIMUM		(1000 >> 3)

#ifdef __cplusplus
	extern "C" {
#endif

void systickInit();
void systickDisable();

unsigned long int millis();
unsigned long int micros();

void delay(unsigned long millisecs);
void delayMicros(unsigned int microDelay);

#ifdef __cplusplus
	} /* extern "C" { */
#endif
