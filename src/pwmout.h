#include <avr/io.h>
#include <avr/interrupt.h>
#include <math.h>
#include <util/twi.h>
#include <stdint.h>
#include <stddef.h>

#ifndef PWMCHANNELS_MAX
    #define PWMCHANNELS_MAX 16
#endif

#ifndef __cplusplus
    #ifndef true
        #define true 1
        #define false 0
        typedef unsigned char bool;
    #endif
#endif

#ifdef __cplusplus
	extern "C" {
#endif

struct pwmConfiguration {
    volatile uint8_t*   avrPort;
    volatile uint8_t*   avrPortDDR;
    uint8_t             avrPin;
};

struct pwmOutputState {
    /* Current duty cycle configuration */
    uint32_t            pwmCycle;

    /* Pin and port cnfiguration */
    volatile uint8_t*   avrPort;
    uint8_t             avrPin;

    /* INternal state */
    uint32_t            pwmOffset;
    uint32_t            pwmCounter;
};

extern struct pwmOutputState pwmState[PWMCHANNELS_MAX];
extern unsigned long int dwPWMLastTick;
extern unsigned long int dwPWMTickDuration;


/*@
    requires dwChannels <= PWMCHANNELS_MAX;
    requires dwTickLength > 0;
    requires (bStaggeredFirstTicks == true) || (bStaggeredFirstTicks == false);
    requires (lpPortConfiguration != NULL) && \valid(lpPortConfiguration);
    requires (dwTickLength * 10000 <= 4294967296);
    requires
        \forall int n; 0 <= n < dwChannels
        ==> (lpPortConfiguration[n].avrPort != NULL) &&
            (
                (lpPortConfiguration[n].avrPin == 0x01)
                || (lpPortConfiguration[n].avrPin == 0x02)
                || (lpPortConfiguration[n].avrPin == 0x04)
                || (lpPortConfiguration[n].avrPin == 0x08)
                || (lpPortConfiguration[n].avrPin == 0x10)
                || (lpPortConfiguration[n].avrPin == 0x20)
                || (lpPortConfiguration[n].avrPin == 0x40)
                || (lpPortConfiguration[n].avrPin == 0x80)
            );

    assigns pwmState[0 .. PWMCHANNELS_MAX];
    assigns dwPWMLastTick;
    assigns dwPWMTickDuration;

    ensures
        \forall int n; dwChannels <= n < PWMCHANNELS_MAX
        ==> (pwmState[n].avrPort == NULL) && (pwmState[n].avrPin == 0);
    ensures
        \forall int n; 0 <= n < PWMCHANNELS_MAX
        ==> (pwmState[n].pwmCycle == 0) && (pwmState[n].pwmCounter == 0);
*/
void pwmInit(
    unsigned long int dwTickLength,                     /* Defines the tick duration in milliseconds (for 1 per thousand). This will be calculated against sysclk */
    bool bStaggeredFirstTicks,

    unsigned long int dwChannels,
    struct pwmConfiguration* lpPortConfiguration
);

/*@
    assigns pwmState[0 .. PWMCHANNELS_MAX].pwmCounter;
    assigns (*(pwmState[0 .. PWMCHANNELS_MAX].avrPort));
    assigns dwPWMLastTick;

    ensures
        \forall int n; 0 <= n < PWMCHANNELS_MAX
        ==> (pwmState[n].avrPort == NULL) || (pwmState[n].pwmCounter == ((pwmState[n].pwmCounter + 1) % 1000));

*/
void pwmTickLoop(); /* This is called inside the tight loop and checks if the PWM tick has happened */

void pwmSet(
    unsigned long int dwChannel,
    unsigned long int dwPermille
);

unsigned long int pwmGetConfiguredChannelCount();

#ifdef __cplusplus
	} /* extern "C" { */
#endif
