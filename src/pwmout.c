#include "./pwmout.h"
#include "./sysclk.h"

#ifdef __cplusplus
	extern "C" {
#endif

struct pwmOutputState pwmState[PWMCHANNELS_MAX];
unsigned long int dwPWMLastTick;
unsigned long int dwPWMTickDuration;

void pwmInit(
    unsigned long int dwTickLength,
    bool bStaggeredFirstTicks,

    unsigned long int dwChannels,
    struct pwmConfiguration* lpPortConfiguration
) {
    unsigned long int i;
    unsigned long int dwOffsetStepSize;

    /*@
        loop assigns pwmState[0 .. (PWMCHANNELS_MAX-1)].pwmCycle;
        loop assigns pwmState[0 .. (PWMCHANNELS_MAX-1)].avrPort;
        loop assigns pwmState[0 .. (PWMCHANNELS_MAX-1)].avrPin;
        loop assigns pwmState[0 .. (PWMCHANNELS_MAX-1)].pwmOffset;
        loop assigns pwmState[0 .. (PWMCHANNELS_MAX-1)].pwmCounter;

        loop invariant 0 <= i < PWMCHANNELS_MAX;
    */
    for(i = 0; i < PWMCHANNELS_MAX; i=i+1) {
        pwmState[i].pwmCycle   = 0;
        pwmState[i].avrPort    = NULL;
        pwmState[i].avrPin     = 0x00;
        pwmState[i].pwmOffset  = 0;
        pwmState[i].pwmCounter = 0;
    }

    if(bStaggeredFirstTicks != true) {
        dwOffsetStepSize = 0;
    } else {
        dwOffsetStepSize = (dwTickLength * 10000L) / dwChannels;
    }

    /*@
        loop assigns pwmState[0 .. (dwChannels-1)].pwmCycle;
        loop assigns pwmState[0 .. (dwChannels-1)].avrPort;
        loop assigns pwmState[0 .. (dwChannels-1)].avrPin;
        loop assigns pwmState[0 .. (dwChannels-1)].pwmOffset;
        loop assigns pwmState[0 .. (dwChannels-1)].pwmCounter;

        loop invariant 0 <= i < dwChannels;
    */
    for(i = 0; i < dwChannels; i=i+1) {
        pwmState[i].pwmOffset = i * dwOffsetStepSize;
        pwmState[i].avrPort = lpPortConfiguration->avrPort;
        pwmState[i].avrPin = lpPortConfiguration->avrPin;

        /*
            Set pin mode to output and pull low (disable output))
        */
        lpPortConfiguration->avrPortDDR[0] = lpPortConfiguration->avrPortDDR[0] | lpPortConfiguration->avrPin;
        lpPortConfiguration->avrPort[0] = lpPortConfiguration->avrPort[0] & (~(lpPortConfiguration->avrPin));
    }

    dwPWMLastTick = millis();
    dwPWMTickDuration = dwTickLength;
}

void pwmTickLoop() {
    unsigned long int dwCurrentMillis = millis();
    unsigned long int dwElapsedTime;
    unsigned long int i;

    if(dwCurrentMillis > dwPWMLastTick) {
        dwElapsedTime = dwCurrentMillis - dwPWMLastTick;
    } else {
        dwElapsedTime = ((~0) - dwPWMLastTick) + dwCurrentMillis;
    }

    if(dwElapsedTime < dwPWMTickDuration) { return; }

    dwPWMLastTick = dwCurrentMillis;

    /* Perform ticks */
    for(i = 0; i < PWMCHANNELS_MAX; i=i+1) {
        if(pwmState[i].avrPort != NULL) {
            pwmState[i].pwmCounter = (pwmState[i].pwmCounter  + 1) % 1000;

            if(((pwmState[i].pwmCounter  + pwmState[i].pwmOffset) % 1000) < pwmState[i].pwmCycle) {
                /* On */
                pwmState[i].avrPort[0] = pwmState[i].avrPort[0] | pwmState[i].avrPin;
            } else {
                /* Off */
                pwmState[i].avrPort[0] = pwmState[i].avrPort[0] & (~(pwmState[i].avrPin));
            }
        }
    }
}

void pwmSet(
    unsigned long int dwChannel,
    unsigned long int dwPermille
) {
	if(dwChannel >= PWMCHANNELS_MAX) {
		return;
	}

	if(dwPermille > 1000) { dwPermille = 1000; } /* Clamp in case we're larger than 1 ppm */
	pwmState[dwChannel].pwmCycle = dwPermille;
}


#ifdef __cplusplus
	} /* extern "C" { */
#endif
