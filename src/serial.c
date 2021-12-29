#include "./serial.h"

#ifdef __cplusplus
	extern "C" {
#endif

/* Ringbuffer declarations */

struct ringBuffer;
static inline void ringBuffer_Init(volatile struct ringBuffer* lpBuf);
static inline bool ringBuffer_Available(volatile struct ringBuffer* lpBuf);
static inline bool ringBuffer_Writable(volatile struct ringBuffer* lpBuf);
static inline unsigned long int ringBuffer_AvailableN(volatile struct ringBuffer* lpBuf);
static inline unsigned long int ringBuffer_WriteableN(volatile struct ringBuffer* lpBuf);
static unsigned char ringBuffer_ReadChar(volatile struct ringBuffer* lpBuf);
static unsigned char ringBuffer_PeekChar(volatile struct ringBuffer* lpBuf);
static unsigned char ringBuffer_PeekCharN(
    volatile struct ringBuffer* lpBuf,
    unsigned long int dwDistance
);
static inline void ringBuffer_discardN(
    volatile struct ringBuffer* lpBuf,
    unsigned long int dwCount
);
static unsigned long int ringBuffer_ReadChars(
    volatile struct ringBuffer* lpBuf,
    unsigned char* lpOut,
    unsigned long int dwLen
);
static void ringBuffer_WriteChar(
    volatile struct ringBuffer* lpBuf,
    unsigned char bData
);
static void ringBuffer_WriteChars(
    volatile struct ringBuffer* lpBuf,
    unsigned char* bData,
    unsigned long int dwLen
);

/*
	The ringbuffers themselves
*/
static volatile struct ringBuffer serialRB0_TX;
static volatile struct ringBuffer serialRB0_RX;
static volatile int serialRX0Flag;

/*
	I/O routines
*/

static uint8_t serialMessageBuffer[SERIAL_MAX_PACKET_SIZE];


void serialTransmitPacket(
	char* lpPayload,
	unsigned long int dwPayloadLength,
	uint8_t respCode
) {
	uint8_t dwLenField;
	uint8_t chkSum = 0x00;
	unsigned long int i;

	ringBuffer_WriteChar(&serialRB0_TX, 0xAA); chkSum = chkSum ^ 0xAA;
	ringBuffer_WriteChar(&serialRB0_TX, 0x55); chkSum = chkSum ^ 0x55;
	ringBuffer_WriteChar(&serialRB0_TX, (uint8_t)(dwPayloadLength + 5)); chkSum = chkSum ^ (uint8_t)(dwPayloadLength + 5);
	ringBuffer_WriteChar(&serialRB0_TX, respCode); chkSum = chkSum ^ respCode;
	if(dwPayloadLength > 0) {
		ringBuffer_WriteChars(&serialRB0_TX, lpPayload, dwPayloadLength);
		for(i = 0; i < dwPayloadLength; i=i+1) {
			chkSum = chkSum ^ lpPayload[i];
		}
	}
	ringBuffer_WriteChar(&serialRB0_TX, chkSum);
}



void serialHandleMessage_UnknownOrError() {
	return;
}

static char* serialHandleMessage_Identify_Response = "PWMController v0.1";

void serialHandleMessage_Identify(unsigned long int dwLength) {
	if(dwLength != 2) {
		serialHandleMessage_UnknownOrError();
	} else {
		serialTransmitPacket(serialHandleMessage_Identify_Response, strlen(serialHandleMessage_Identify_Response), SERIAL_RESPCCODE_ID);
	}
}

void serialHandleMessage(
	unsigned long int dwMessageSize
) {
	uint8_t dwPacketLen = serialMessageBuffer[0];
	uint8_t dwOpCode = serialMessageBuffer[1];

	switch(dwOpCode) {
		case SERIAL_OPCODE_ID:		serialHandleMessage_Identify(dwMessageSize);
		default:					serialHandleMessage_UnknownOrError();
	}
}

void serialHandleEvents() {
	unsigned long int dwAvailableLength;
	unsigned long int i;

	dwAvailableLength = ringBuffer_AvailableN(&serialRB0_RX);
    if(dwAvailableLength < 5) { return; } /* We cannot even see a full packet ... */

	while((ringBuffer_PeekChar(&serialRB0_RX) != 0xAA) && (ringBuffer_PeekCharN(&serialRB0_RX, 1) != 0x55) && (ringBuffer_AvailableN(&serialRB0_RX) >= 5)) {
		ringBuffer_discardN(&serialRB0_RX, 1); /* Skip next character */
	}
	if(ringBuffer_AvailableN(&serialRB0_RX) < 5) { return; }

	uint8_t dwPacketLength = ringBuffer_PeekCharN(&serialRB0_RX, 2);
	if(dwPacketLength > SERIAL_MAX_PACKET_SIZE) {
		/* Discard two bytes and leave ... this packet is invalid for sure */
		ringBuffer_discardN(&serialRB0_RX, 2);
		return;
	}

	if(ringBuffer_AvailableN(&serialRB0_RX) < dwPacketLength) {
		return; /* Retry next time ... */
	}

	/*
		Perform checksum checking
	*/
	uint8_t dwChecksum = 0x00;
	for(i = 0; i < dwPacketLength; i=i+1) {
		dwChecksum = dwChecksum ^ ringBuffer_PeekCharN(&serialRB0_RX, i);
	}

	if(dwChecksum != 0x00) {
		/*
			Discard two bytes and leave ... this packet is invalid for sure
			We only discard 2 bytes though since the real next packet
			might start somewhere inside the area we thought would be
			a packet.
		*/
		ringBuffer_discardN(&serialRB0_RX, 2);
		return;
	}

	/*
		Checksum valid, packet valid - extract into a linear buffer for easier
		handling and call processing function ...
	*/
	ringBuffer_discardN(&serialRB0_RX, 2); /* We discard our synchronization pattern */
	ringBuffer_ReadChars(&serialRB0_RX, serialMessageBuffer, dwPacketLength-2-1); /* Copy everything except checksum */
	ringBuffer_discardN(&serialRB0_RX, 1); /* Discard checksum */

	serialHandleMessage(dwPacketLength - 3);
}

void serialModeTX0() {
    uint8_t sregOld = SREG;
    #ifndef FRAMAC_SKIP
        cli();
    #endif

    UCSR0A = UCSR0A | 0x40; /* Reset TXCn bit */
    UCSR0B = UCSR0B | 0x08 | 0x20;

    #ifndef FRAMAC_SKIP
        SREG = sregOld;
    #endif
}







/*
    Ringbuffer (ToDo: Refactor)
*/
/*
    Ringbuffer utilis
*/
struct ringBuffer {
    volatile unsigned long int dwHead;
    volatile unsigned long int dwTail;

    volatile unsigned char buffer[SERIAL_RINGBUFFER_SIZE];
};

static inline void ringBuffer_Init(volatile struct ringBuffer* lpBuf) {
    lpBuf->dwHead = 0;
    lpBuf->dwTail = 0;
}
static inline bool ringBuffer_Available(volatile struct ringBuffer* lpBuf) {
    return (lpBuf->dwHead != lpBuf->dwTail) ? true : false;
}
static inline bool ringBuffer_Writable(volatile struct ringBuffer* lpBuf) {
    return (((lpBuf->dwHead + 1) % SERIAL_RINGBUFFER_SIZE) != lpBuf->dwTail) ? true : false;
}
static inline unsigned long int ringBuffer_AvailableN(volatile struct ringBuffer* lpBuf) {
    if(lpBuf->dwHead >= lpBuf->dwTail) {
        return lpBuf->dwHead - lpBuf->dwTail;
    } else {
        return (SERIAL_RINGBUFFER_SIZE - lpBuf->dwTail) + lpBuf->dwHead;
    }
}
static inline unsigned long int ringBuffer_WriteableN(volatile struct ringBuffer* lpBuf) {
    return SERIAL_RINGBUFFER_SIZE - ringBuffer_AvailableN(lpBuf);
}

static unsigned char ringBuffer_ReadChar(volatile struct ringBuffer* lpBuf) {
    char t;

    if(lpBuf->dwHead == lpBuf->dwTail) {
        return 0x00;
    }

    t = lpBuf->buffer[lpBuf->dwTail];
    lpBuf->dwTail = (lpBuf->dwTail + 1) % SERIAL_RINGBUFFER_SIZE;

    return t;
}
static unsigned char ringBuffer_PeekChar(volatile struct ringBuffer* lpBuf) {
    if(lpBuf->dwHead == lpBuf->dwTail) {
        return 0x00;
    }

    return lpBuf->buffer[lpBuf->dwTail];
}
static unsigned char ringBuffer_PeekCharN(
    volatile struct ringBuffer* lpBuf,
    unsigned long int dwDistance
) {
    if(lpBuf->dwHead == lpBuf->dwTail) {
        return 0x00;
    }
    if(ringBuffer_AvailableN(lpBuf) <= dwDistance) {
        return 0x00;
    }

    return lpBuf->buffer[(lpBuf->dwTail + dwDistance) % SERIAL_RINGBUFFER_SIZE];
}
static inline void ringBuffer_discardN(
    volatile struct ringBuffer* lpBuf,
    unsigned long int dwCount
) {
    lpBuf->dwTail = (lpBuf->dwTail + dwCount) % SERIAL_RINGBUFFER_SIZE;
    return;
}
static unsigned long int ringBuffer_ReadChars(
    volatile struct ringBuffer* lpBuf,
    unsigned char* lpOut,
    unsigned long int dwLen
) {
    char t;
    unsigned long int i;

    if(dwLen > ringBuffer_AvailableN(lpBuf)) {
        return 0;
    }

    for(i = 0; i < dwLen; i=i+1) {
        t = lpBuf->buffer[lpBuf->dwTail];
        lpBuf->dwTail = (lpBuf->dwTail + 1) % SERIAL_RINGBUFFER_SIZE;
        lpOut[i] = t;
    }

    return i;
}

static void ringBuffer_WriteChar(
    volatile struct ringBuffer* lpBuf,
    unsigned char bData
) {
    if(((lpBuf->dwHead + 1) % SERIAL_RINGBUFFER_SIZE) == lpBuf->dwTail) {
        return; /* Simply discard data */
    }

    lpBuf->buffer[lpBuf->dwHead] = bData;
    lpBuf->dwHead = (lpBuf->dwHead + 1) % SERIAL_RINGBUFFER_SIZE;
}
static void ringBuffer_WriteChars(
    volatile struct ringBuffer* lpBuf,
    unsigned char* bData,
    unsigned long int dwLen
) {
    unsigned long int i;

    for(i = 0; i < dwLen; i=i+1) {
        ringBuffer_WriteChar(lpBuf, bData[i]);
    }
}

/*
    Real serial stuff
*/

void serialInit() {
    uint8_t sregOld = SREG;
    #ifndef FRAMAC_SKIP
        cli();
    #endif

	ringBuffer_Init(&serialRB0_TX);
    ringBuffer_Init(&serialRB0_RX);
	serialRX0Flag = 0;

    UBRR0   = 103; // 16 : 115200, 103: 19200
    UCSR0A  = 0x02;
    UCSR0B  = 0x10 | 0x80; /* Enable receiver and RX interrupt */
    UCSR0C  = 0x06;

    SREG = sregOld;

    return;
}

ISR(USART_RX_vect) {
    ringBuffer_WriteChar(&serialRB0_RX, UDR0);
    serialRX0Flag = 1;
}
ISR(USART_UDRE_vect) {
    if(ringBuffer_Available(&serialRB0_TX) == true) {
        /* Shift next byte to the outside world ... */
        UDR0 = ringBuffer_ReadChar(&serialRB0_TX);
    } else {
        /*
            Since no more data is available for shifting simply stop
            the transmitter and associated interrupts
        */
        UCSR0B = UCSR0B & (~(0x08 | 0x20));
    }
}

#ifdef __cplusplus
    } /* extern "C" { */
#endif
