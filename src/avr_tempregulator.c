#include <avr/io.h>
#include <avr/interrupt.h>
#include <math.h>
#include <util/twi.h>
#include <stdint.h>

#include "./avr_tempregulator.h"
#include "./sysclk.h"
#include "./pwmout.h"
#include "./serial.h"

/*
    Pin usage:
        PD0         USART RXD
        PD1         USART TXD

        PC4         I2C SDA
        PC5         I2C SCL

    SSRs:
        PD2         PWM 0
        PD3         PWM 1
        PD4         PWM 2
        PD5         PWM 3
        PD6         PWM 4
        PD7         PWM 5

        PC0         PWM 6
        PC1         PWM 7
        PC2         PWM 8
        PC3         PWM 9

        PB0         PWM10
        PB1         PWM11
        PB2         PWM12
        PB3         PWM13
        PB4         PWM14
        PB5         PWM15
*/

static struct pwmConfiguration pwmPorts[] = {
    { &PORTD, &DDRD, 0x04 },
    { &PORTD, &DDRD, 0x08 },
    { &PORTD, &DDRD, 0x10 },
    { &PORTD, &DDRD, 0x20 },
    { &PORTD, &DDRD, 0x40 },
    { &PORTD, &DDRD, 0x80 }
};

int main() {
    systickInit();

    pwmInit(4, true, sizeof(pwmPorts) / sizeof(struct pwmConfiguration), pwmPorts);
    serialInit();

    pwmSet(0, 500);

    /*
        GPIO setup
    */

    for(;;) {
        pwmTickLoop();
        serialHandleEvents();
    }
}
