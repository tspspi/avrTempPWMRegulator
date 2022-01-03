#define OTA_ENABLE 1
#define MQTT_ENABLE 1

#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <ESP8266WebServer.h>
#include <WiFiUdp.h>
#include <EEPROM.h>
#ifdef OTA_ENABLE
  #include <ArduinoOTA.h>
#endif
#ifdef MQTT_ENABLE
  #include <Adafruit_MQTT.h>
  #include <Adafruit_MQTT_Client.h>
#endif

// #define AUTOREBOOT_INTERVAL 86400000

#ifndef SERIAL_MAX_PACKET_SIZE
  #define SERIAL_MAX_PACKET_SIZE 32
#endif
#ifndef SERIAL_RINGBUFFER_SIZE
  #define SERIAL_RINGBUFFER_SIZE 512
#endif

#define PWMCHANNELS                     6

#define SERIAL_OPCODE_ID                0x01
#define SERIAL_OPCODE_GETCHANNELCOUNT   0x02
#define SERIAL_OPCODE_SETDUTY           0x03

#define SERIAL_RESPCODE_ID              0x02
#define SERIAL_RESPCODE_GETCHANNELCOUNT 0x03


static ESP8266WebServer httpServer(80);

#define FACTORYRESETPORT                    0

#ifndef FW_FAMILY
  #define FW_FAMILY "PWM Controller"
#endif
#ifndef FW_ID
  #define FW_ID "607ed210-9d50-40e1-9502-6cb8d93c3b1c"
#endif

#ifndef MQTT_PING_TIMEOUT
  #define MQTT_PING_TIMEOUT 15*1000
#endif

#define eepromConfiguration__DEFAULT__FLAGS   0x00

#define DEFAULT_INTERVAL__WIFIRECONNECT     15
#define DEFAULT_INTERVAL__MQTTRECONNECT     15

#define EEPROM_CONFIGURATION_OFFSET 0

struct eepromConfiguration {
  uint8_t   flags;

  char      wifiSSID[32];
  char      wifiPSK[32];

  char      mqttHost[253];
  char      mqttUser[32];
  char      mqttPassword[32];
  uint16_t  mqttPort;

  char      otaHostname[32];
  char      otaPassword[32];

  uint8_t   checksum;

  unsigned long int dwWiFiReconnectInterval;
  unsigned long int dwMQTTReconnectInterval;
};

/* Timeout values (i.e. absolute numbers when a timeout will occure */
static unsigned long dwWiFiCheckTimeout;
static unsigned long dwMQTTPingTimeout;

/* Network status */
enum netState {
  netState_Disconnect,
  netState_SoftAP,
  netState_WiFiConnected,
  netState_MQTTConnected
};

struct allStates {
  bool                  otaUpdateRunning;
  unsigned long int     dwOTAProgress;

  enum netState         netStatus;

  unsigned long int     dwLastWiFiCheckCycle;
  unsigned long int     dwLastMQTTCheckCycle;
};

static unsigned long int dwPWMDutyCycle[PWMCHANNELS] = { 0, 0, 0, 0, 0, 0 };

static enum netState stateNet;
static struct allStates currentState;

static char defaultName[12+8+1];

static WiFiClient wclient;

/* Webpages */

static const PROGMEM char* htmlConfigPage = "<!DOCTYPE html><html><head><title>PWM controller configuration</title></head><body><h1>PWM controller configuration</h1><form action=\"/config\" method=\"POST\"><h2>Networking</h2><p>Note: This device always uses DHCP</p><table border=\"0\"><tr><td>SSID:</td><td><input type=\"text\" name=\"wifissid\" value=\"%s\"></td></tr><tr><td>PSK:</td><td><input type=\"password\" name=\"wifipsk\" value=\"%s\"></td></tr></table><h2>Over the air update (OTA)</h2><table border=\"0\"><tr><td>Hostname:</td><td><input type=\"text\" name=\"otahost\" value=\"%s\"></td></tr><tr><td>Password:</td><td><input type=\"password\" name=\"otakey\" value=\"%s\"></td></tr></table><h2>MQTT</h2><table border=\"0\"><tr><td>Host:</td><td><input type=\"text\" name=\"mqtthost\" value=\"%s\"></td></tr><tr><td>User:</td><td><input type=\"text\" name=\"mqttuser\" value=\"%s\"></td></tr><tr><td>Password:</td><td><input type=\"password\" name=\"mqttpwd\" value=\"%s\"></td></tr><tr><td>Port:</td><td><input type=\"number\" name=\"mqttport\" value=\"%u\"></td></tr></table><h3>MQTT Topics</h3><table border=\"0\"><tr><td>Heater:</td><td><input type=\"text\" name=\"mqtttopic\" value=\"\"></td></tr></table><h2>Intervals</h2><table border=\"0\"><tr><td>WiFi reconnect (s):</td><td><input type=\"number\" name=\"intwifire\" value=\"%u\"></td></tr><tr><td>MQTT reconnect (s):</td><td><input type=\"number\" name=\"intmqttre\" value=\"%u\"></td></tr></table><input type=\"submit\" value=\"Update settings\"></form></body></html>";
static const PROGMEM char* htmlStatusPage = "<!DOCTYPE html><html><head><title>PWM controller</title></head><body><h1>PWM controller</h1><p> All values are per thousand (percent times 10) </p><form action=\"/\" method=\"POST\"><table border=\"1\"><tr> <td> Channel 1: </td> <td> <input type=\"number\" name=\"chan0\" value=\"%lu\"> </td> </tr><tr> <td> Channel 2: </td> <td> <input type=\"number\" name=\"chan1\" value=\"%lu\"> </td> </tr><tr> <td> Channel 3: </td> <td> <input type=\"number\" name=\"chan2\" value=\"%lu\"> </td> </tr><tr> <td> Channel 4: </td> <td> <input type=\"number\" name=\"chan3\" value=\"%lu\"> </td> </tr><tr> <td> Channel 5: </td> <td> <input type=\"number\" name=\"chan4\" value=\"%lu\"> </td> </tr><tr> <td> Channel 6: </td> <td> <input type=\"number\" name=\"chan5\" value=\"%lu\"> </td> </tr></table><p><input type=\"submit\" value=\"Set values\"></form><p><a href=\"/config\">Configuration</a></p></body></html>";

static struct eepromConfiguration cfgCurrent;

/*
  ==========
  = EEPROM =
  ==========
*/

static void cfgEEPROMStore(bool bDefaults) {
  unsigned long int i;

  if(bDefaults) {
    cfgCurrent.flags = eepromConfiguration__DEFAULT__FLAGS;
    memset(cfgCurrent.wifiSSID,    0, sizeof(cfgCurrent.wifiSSID));
    memset(cfgCurrent.wifiPSK,     0, sizeof(cfgCurrent.wifiPSK));

    strcpy(cfgCurrent.otaHostname, defaultName);
    strcpy(cfgCurrent.otaPassword, defaultName);

    cfgCurrent.mqttPort            = 1883;
    memset(cfgCurrent.mqttHost,    0, sizeof(cfgCurrent.mqttHost));
    memset(cfgCurrent.mqttUser,    0, sizeof(cfgCurrent.mqttUser));
    memset(cfgCurrent.mqttPassword,0, sizeof(cfgCurrent.mqttPassword));

    cfgCurrent.dwWiFiReconnectInterval  = DEFAULT_INTERVAL__WIFIRECONNECT;
    cfgCurrent.dwMQTTReconnectInterval  = DEFAULT_INTERVAL__MQTTRECONNECT;
  }
  cfgCurrent.checksum = 0;

  /* Calculate checksum */
  cfgCurrent.checksum = 0x00;
  uint8_t chkChecksum = 0xAA;
  for(i = 0; i < sizeof(struct eepromConfiguration); i=i+1) {
    chkChecksum = chkChecksum ^ ((uint8_t*)(&cfgCurrent))[i];
  }
  cfgCurrent.checksum = chkChecksum;

  for(i = 0; i < sizeof(struct eepromConfiguration); i=i+1) {
    EEPROM.write(EEPROM_CONFIGURATION_OFFSET + i, ((uint8_t*)(&cfgCurrent))[i]);
  }
  EEPROM.commit();
}

static void cfgEEPROMLoad() {
  unsigned long int i;
  for(i = 0; i < sizeof(struct eepromConfiguration); i=i+1) {
    ((uint8_t*)(&cfgCurrent))[i] = EEPROM.read(EEPROM_CONFIGURATION_OFFSET + i);
  }

  /* Check checksum */
  uint8_t chkChecksum = 0x00;
  for(i = 0; i < sizeof(struct eepromConfiguration); i=i+1) {
    chkChecksum = chkChecksum ^ ((uint8_t*)(&cfgCurrent))[i];
  }
  if(chkChecksum != 0xAA) {
    delay(5000);
    cfgEEPROMStore(true);
  } else {
    delay(2000);
  }
}

/*
  ========================
  = Over the air updates =
  ========================
*/
static void setupOTA() {
  ArduinoOTA.setHostname(cfgCurrent.otaHostname);
  ArduinoOTA.setPassword(cfgCurrent.otaPassword);

  ArduinoOTA.onStart([]() {
    currentState.otaUpdateRunning = true;
    currentState.dwOTAProgress = 0;
  });
  ArduinoOTA.onEnd([]() {
    currentState.otaUpdateRunning = false;
  });

  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    currentState.dwOTAProgress = (unsigned long int)(progress / (total / 100));
  });

  ArduinoOTA.onError([](ota_error_t error) {
    if (error == OTA_AUTH_ERROR) {
      currentState.otaUpdateRunning = false;
    } else if (error == OTA_BEGIN_ERROR) {
      currentState.otaUpdateRunning = false;
    } else if (error == OTA_CONNECT_ERROR) {
      currentState.otaUpdateRunning = false;
    } else if (error == OTA_RECEIVE_ERROR) {
      currentState.otaUpdateRunning = false;
    } else if (error == OTA_END_ERROR) {
      currentState.otaUpdateRunning = false;
    }
  });

  ArduinoOTA.begin();
}

/*
  =============
  = Webserver =
  =============
*/

ESP8266WebServer server(80);

static char pageTemp[3*1024];

static unsigned long int httpHandleConfig__ReadValueArgs(const char* lpFieldName, unsigned long int dwMin, unsigned long int dwMax, unsigned long int dwOldValue) {
  if(!server.hasArg(lpFieldName)) { return dwOldValue; }

  unsigned int dwNewValue;
  if(sscanf(server.arg(lpFieldName).c_str(), "%u", &dwNewValue) != 1) {
    return dwOldValue;
  }

  if((dwNewValue >= dwMin) && (dwNewValue <= dwMax)) {
    return dwNewValue;
  } else {
    return dwOldValue;
  }
}

static void httpHandleNotFound() {
  server.send(404, "text/plain", "Unknown URI");
};
static void httpHandleStatus() {
  if(server.hasArg("chan0") && server.hasArg("chan1") && server.hasArg("chan2") && server.hasArg("chan3") && server.hasArg("chan4") && server.hasArg("chan5")) {
    dwPWMDutyCycle[0] = httpHandleConfig__ReadValueArgs("chan0", 0, 1000, dwPWMDutyCycle[0]);
    dwPWMDutyCycle[1] = httpHandleConfig__ReadValueArgs("chan1", 0, 1000, dwPWMDutyCycle[1]);
    dwPWMDutyCycle[2] = httpHandleConfig__ReadValueArgs("chan2", 0, 1000, dwPWMDutyCycle[2]);
    dwPWMDutyCycle[3] = httpHandleConfig__ReadValueArgs("chan3", 0, 1000, dwPWMDutyCycle[3]);
    dwPWMDutyCycle[4] = httpHandleConfig__ReadValueArgs("chan4", 0, 1000, dwPWMDutyCycle[4]);
    dwPWMDutyCycle[5] = httpHandleConfig__ReadValueArgs("chan5", 0, 1000, dwPWMDutyCycle[5]);

    yield();
    pwmSendDutyUpdate();
    yield();
  }

  sprintf(
    pageTemp,
    htmlStatusPage,
    dwPWMDutyCycle[0],
    dwPWMDutyCycle[1],
    dwPWMDutyCycle[2],
    dwPWMDutyCycle[3],
    dwPWMDutyCycle[4],
    dwPWMDutyCycle[5]
  );
  server.sendHeader("Refresh", "60; url=/");
  server.sendHeader("Cache-control", "no-cache, no-store, must-revalidate");
  server.sendHeader("Pragma", "no-cache");
  server.send(200, "text/html", pageTemp);
}

static void httpHandleConfig() {
  bool bUpdateAndRestart = false;

  /* If present process received data ... */
  if(server.hasArg("wifissid") && server.hasArg("wifipsk") && server.hasArg("otahost") && server.hasArg("otakey") && server.hasArg("mqtthost") && server.hasArg("mqttuser") && server.hasArg("mqttpwd") && server.hasArg("mqttport") && server.hasArg("mqtttopic") && server.hasArg("intwifire") && server.hasArg("intmqttre") && server.hasArg("intmeas")) {
    /* Process arguments and update EEPROM - system will reboot after we've delivered the webpage ... */
    strcpy(cfgCurrent.wifiSSID, server.arg("wifissid").c_str());
    strcpy(cfgCurrent.wifiPSK, server.arg("wifipsk").c_str());

    strcpy(cfgCurrent.otaHostname, server.arg("otahost").c_str());
    strcpy(cfgCurrent.otaPassword, server.arg("otakey").c_str());

    strcpy(cfgCurrent.mqttHost, server.arg("mqtthost").c_str());
    strcpy(cfgCurrent.mqttUser, server.arg("mqttuser").c_str());
    strcpy(cfgCurrent.mqttPassword, server.arg("mqttpwd").c_str());
    {
      unsigned int newPort = cfgCurrent.mqttPort;
      sscanf(server.arg("mqttport").c_str(), "%u", &newPort);
      if((newPort > 0) && (newPort < 65536)) {
        cfgCurrent.mqttPort = newPort;
      }
    }

    cfgCurrent.dwWiFiReconnectInterval    = httpHandleConfig__ReadValueArgs("intwifire",    5,   3600,  cfgCurrent.dwWiFiReconnectInterval    );
    cfgCurrent.dwMQTTReconnectInterval    = httpHandleConfig__ReadValueArgs("intmqttre",    5,   3600,  cfgCurrent.dwWiFiReconnectInterval    );

    bUpdateAndRestart = true;
  }
  /* Create page output */

  sprintf(
    pageTemp,
    htmlConfigPage,
    cfgCurrent.wifiSSID,
    cfgCurrent.wifiPSK,
    cfgCurrent.otaHostname,
    cfgCurrent.otaPassword,
    cfgCurrent.mqttHost,
    cfgCurrent.mqttUser,
    cfgCurrent.mqttPassword,
    cfgCurrent.mqttPort,
    cfgCurrent.dwWiFiReconnectInterval,
    cfgCurrent.dwMQTTReconnectInterval
  );
  server.sendHeader("Cache-control", "no-cache, no-store, must-revalicate");
  server.sendHeader("Pragma", "no-cache");
  server.send(200, "text/html", pageTemp);

  if(bUpdateAndRestart) {
    cfgEEPROMStore(false);
    delay(5000);
    ESP.restart();
  }
}

/*
  ==================
  = PWM controller =
  ==================
*/

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

static uint8_t serialMessageBuffer[SERIAL_MAX_PACKET_SIZE];

static volatile struct ringBuffer serialRB0_RX;


void serialTransmitPacket(
  unsigned char* lpPayload,
  unsigned long int dwPayloadLength,
  uint8_t respCode
) {
  uint8_t chkSum = 0x00;
  unsigned long int i;

  Serial.write(0xAA); chkSum = chkSum ^ 0xAA;
  Serial.write(0x55); chkSum = chkSum ^ 0x55;
  Serial.write((uint8_t)(dwPayloadLength + 5)); chkSum = chkSum ^ (uint8_t)(dwPayloadLength + 5);
  Serial.write(respCode); chkSum = chkSum ^ respCode;
  if(dwPayloadLength > 0) {
    Serial.write((unsigned char*)lpPayload, dwPayloadLength);
    for(i = 0; i < dwPayloadLength; i=i+1) {
      chkSum = chkSum ^ lpPayload[i];
    }
  }
  Serial.write(chkSum);
}

void serialHandleMessage(
  unsigned long int dwMessageSize
) {
  // uint8_t dwPacketLen = serialMessageBuffer[0];
  uint8_t dwOpCode = serialMessageBuffer[1];

  switch(dwOpCode) {
    case SERIAL_RESPCODE_ID:              break;
    case SERIAL_RESPCODE_GETCHANNELCOUNT: break;
    default:                              break;
  }
}

void serialHandleEvents() {
  // Hacked in modification for ESP8266:
  while(Serial.available() > 0) {
    ringBuffer_WriteChar(&serialRB0_RX, Serial.read());
  }

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



static void pwmInit() {
  Serial.begin(115200);
  delay(250);
  for(int i = 0; i < 10; i++) { Serial.write(0x00); }
  delay(250);
  while(Serial.available() > 0) {
    Serial.read();
    delay(100);
  }

  yield();

  /* Ask for number of channels ... */
  serialTransmitPacket(NULL, 0, SERIAL_OPCODE_GETCHANNELCOUNT);
}

static void pwmSendDutyUpdate() {
  unsigned long int i;

  uint8_t payloadBuffer[2 * PWMCHANNELS];

  for(i = 0; i < PWMCHANNELS; i=i+1) {
    payloadBuffer[i * 2 + 0] = (uint8_t)((((uint16_t)dwPWMDutyCycle[i]) & 0x00FF));
    payloadBuffer[i * 2 + 1] = (uint8_t)((((uint16_t)dwPWMDutyCycle[i]) & 0xFF00) >> 8);
  }

  serialTransmitPacket(payloadBuffer, 2 * PWMCHANNELS, SERIAL_OPCODE_SETDUTY);
}

/*
  MQTT
*/
static Adafruit_MQTT_Client* mqttConnection = NULL;
// static Adafruit_MQTT_Publish* mqttTopicOutCO2 = NULL; /* (&mqtt, "automation.hehuette.livingroom.motion.sens01"); */

static void mqttInit() {
  if(mqttConnection != NULL) {
/*    if(mqttTopicOutCO2 != NULL) {
      delete mqttTopicOutCO2;
      mqttTopicOutCO2 = NULL;
    } */
    mqttConnection->disconnect();
    delay(250);
    delete mqttConnection;
    mqttConnection = NULL;
  }

  mqttConnection = new Adafruit_MQTT_Client(&wclient, cfgCurrent.mqttHost, cfgCurrent.mqttPort, cfgCurrent.mqttUser, cfgCurrent.mqttPassword);
  mqttConnection->connect();

/*  if((mqttTopicOutCO2 == NULL) && (strlen(cfgCurrent.mqttTopicCO2) > 0)) {
    mqttTopicOutCO2 = new Adafruit_MQTT_Publish(mqttConnection, cfgCurrent.mqttTopicCO2);
  } */
}

#if 0
  static void mqttPublish(
    unsigned long int dwPPM,
    unsigned long int dwCelsius
  ) {
    if((mqttTopicOutCO2 != NULL) && (mqttConnection->connected())) {
      String rep = "{ \"co2\" : \"";
      rep += dwPPM;
      rep += "\", \"temperature\" : \"";
      rep += dwCelsius;
      rep += "\" }";

      mqttTopicOutCO2->publish(rep.c_str());
    }
  }
#endif

/*
  =======================
  = Main setup and loop =
  =======================
*/
void setup() {
  EEPROM.begin(sizeof(struct eepromConfiguration));

  pinMode(FACTORYRESETPORT, INPUT);

  currentState.netStatus            = netState_Disconnect;
  currentState.dwLastWiFiCheckCycle = millis();
  currentState.otaUpdateRunning     = false;

  {
    byte mac[6];
    WiFi.macAddress(mac);
    sprintf(defaultName, "iotPWM-%02x%02x%02x%02x%02x%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  }

  cfgEEPROMLoad();
  setupOTA();

  pwmInit();

  /* Setup webserver */
  server.on("/", httpHandleStatus);
  server.on("/config", httpHandleConfig);
  server.onNotFound(httpHandleNotFound);
  server.begin();
}

void loop(void) {
  unsigned long int dwWiFiCheckElapsed;
  unsigned long int dwMQTTCheckElapsed;
  unsigned long int dwMeasElapsed;
  unsigned long currentMillis = millis();

  #ifdef AUTOREBOOT_INTERVAL
    if(currentMillis > AUTOREBOOT_INTERVAL) {
      ESP.restart();
      return;
    }
  #endif

  if(currentState.dwLastWiFiCheckCycle < currentMillis) {
    dwWiFiCheckElapsed = currentMillis - currentState.dwLastWiFiCheckCycle;
  } else {
    dwWiFiCheckElapsed = ((~0) - currentState.dwLastWiFiCheckCycle) + currentMillis;
  }

  if(currentState.dwLastMQTTCheckCycle < currentMillis) {
    dwMQTTCheckElapsed = currentMillis - currentState.dwLastMQTTCheckCycle;
  } else {
    dwMQTTCheckElapsed = ((~0) - currentState.dwLastMQTTCheckCycle) + currentMillis;
  }

  /*
    WiFi check ...
  */
  {
    int wifiState = WiFi.status();
    if(currentState.netStatus != netState_SoftAP) {
      if(dwWiFiCheckElapsed > (cfgCurrent.dwWiFiReconnectInterval*1000)) {
        currentState.dwLastWiFiCheckCycle = currentMillis;
        if (wifiState != WL_CONNECTED) {
          if(strlen(cfgCurrent.wifiSSID) == 0) {
            WiFi.mode(WIFI_OFF);
            WiFi.softAP(defaultName, defaultName);
            currentState.netStatus = netState_SoftAP;
          } else {
            WiFi.mode(WIFI_OFF);
            WiFi.mode(WIFI_STA);
            WiFi.begin(cfgCurrent.wifiSSID, cfgCurrent.wifiPSK);
            currentState.netStatus = netState_Disconnect;
          }
        } else {
          if(currentState.netStatus == netState_Disconnect) {
            currentState.netStatus = netState_WiFiConnected;
          }
        }
      }
    }

    if(currentState.netStatus != netState_Disconnect) {
      ArduinoOTA.handle();
      server.handleClient();
    }
  }
  yield();

  /*
    MQTT check
  */
  if(dwMQTTCheckElapsed  > cfgCurrent.dwMQTTReconnectInterval*1000) {
    currentState.dwLastMQTTCheckCycle = currentMillis;

    if(mqttConnection != NULL) {
      if(mqttConnection ->connected()) {
        currentState.netStatus = netState_MQTTConnected;
      } else {
        /*
        if(mqttTopicOutCO2 != NULL) {
          delete mqttTopicOutCO2;
          mqttTopicOutCO2 = NULL;
        }
        */
        mqttConnection->disconnect();
        delay(125);
        delete mqttConnection;
        mqttConnection = NULL;
      }
    } else {
      if((currentState.netStatus == netState_WiFiConnected) && (strlen(cfgCurrent.mqttHost) > 0)) {
        if(mqttConnection == NULL) {
          mqttInit();
        }
      }
    }
  }
  yield();

  /*
    Check for factory reset
  */
  if(digitalRead(FACTORYRESETPORT) == 0) {
    delay(10000);
    cfgEEPROMStore(true);
    ESP.restart();
  }

  serialHandleEvents();
}
