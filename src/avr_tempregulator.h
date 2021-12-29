#ifndef PID_INTEGRAL_STEPS
    #define PID_INTEGRAL_STEPS 32
#endif

/*
    Note: All temperatures are specified in 0.01 degree units.
    So for example 200 degrees is 20000, etc.
*/

#ifndef __cplusplus
        typedef int bool;
        #define true 1
        #define false 0
#endif


#if 0
struct pwmChannelState {
    /* Settings */
    uint32_t            dwTargetTemperature;            /* User set the target temperature */

    /*
        Measurements by the external measurement system
        0 always being the newest
    */
    uint32_t            dwMeasurements[PID_INTEGRAL_STEPS]

    /*
        Calculated from slope limiting OR simply copying dwTargetTemperature
        if slope limiting is disabled
    */
    uint32_t            dwCurrentTargetTemperature;     /* This is the slope limited current target temperature */
    uint32_t            dwMaximumRamp;                  /* Maximum rampnig speed */


    /* Calculated settings processed by PWM output module */
    uint8_t             dwPWMDutyCycle;
    uint8_t             dwPWMCounter;
};

struct pwmPinsPorts {
    volatile uint8_t*   port;
    volatile uint8_t*   ddr;

    uint8_t             maskSet;
    uint8_t             maskClear;
};
#endif
