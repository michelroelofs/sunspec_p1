#ifndef INTERFACE_H
#define INTERFACE_H

#include <sys/types.h>
#include <sys/socket.h>
#include <stdint.h>

/* The struct below contains some "global" data:
    - datapipe, for transferring data from the measurement thread to worker thread
    - fd for the serial device, used by measurement thread
    - pointer to first PowerTimeStamps struct
    - pointer to current PowerTimeStamps struct. Current means the one to write the first
      following measurement to
    - counter: pointer to the number of measurements done, this is written into the data
      file and used to quickly find 'current'
    - udpport: port to listen to data
 */

typedef double (*CalculateValue)(const uint16_t* data, const int scaleOffset);

typedef struct
{
        struct sockaddr* modbusAddr;
        socklen_t        modbusAddrlen;
        int              ai_family;
        int              ai_socktype;
        int              ai_protocol;
        int              debug;
        int              serialDeviceFd;
        const char*      serialDeviceName;
        unsigned         port;
} InitializationData;

typedef struct
{
        int valueFieldNr;
        int scaleFieldOffset;  /* Offset to valuefield */
        int valueLength;
        const char* description;
        const char* unit;
        CalculateValue calcFn;
} SunSpecValue;

int openP1Device(const char* p_device);

int reporter(InitializationData* id);

const int modbusBase;
const int modbusRegCount;

double int16ToDouble(const uint16_t* data, const int scaleOffset);
double uint16ToDouble(const uint16_t* data, const int scaleOffset);
double acc32ToDouble(const uint16_t* data, const int scaleOffset);
SunSpecValue* getParam(int nr); /* Specify 0 for the first, loop until valueFieldNr==0 */

double getSunSpecValue(const uint16_t* data, int fieldnr);

#endif // INTERFACE_H
