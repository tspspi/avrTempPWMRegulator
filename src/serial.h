#include <avr/io.h>
#include <avr/interrupt.h>
#include <math.h>
#include <stdint.h>
#include <stddef.h>

#define SERIAL_OPCODE_ID								0x01
#define SERIAL_OPCODE_GETCHANNELCOUNT		0x02
#define SERIAL_OPCODE_SETDUTY						0x03

#define SERIAL_RESPCODE_ID							0x02
#define SERIAL_RESPCODE_GETCHANNELCOUNT	0x03

#ifndef SERIAL_MAX_PACKET_SIZE
	#define SERIAL_MAX_PACKET_SIZE 32
#endif
#ifndef SERIAL_RINGBUFFER_SIZE
	#define SERIAL_RINGBUFFER_SIZE 512
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

void serialInit();
void serialHandleEvents();

void serialTransmitPacket(
	unsigned char* lpPayload,
	unsigned long int dwPayloadLength,
	uint8_t respCode
);

#ifdef __cplusplus
    } /* extern "C" { */
#endif
