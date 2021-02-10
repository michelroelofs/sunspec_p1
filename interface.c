#include "interface.h"
#include <stdio.h>
#include <arpa/inet.h>
#include <math.h>

const int modbusBase=40001;
const int modbusRegCount=109;

SunSpecValue params[] = {
        /*        {40070,    0,      1,     "C_SunSpec_DID",    "",     uint16},    */
        /*        {40071,    0,      1,     "C_SunSpec_Length",    "",     uint16}, */
        {40072,    4,      1,     "I_AC_Current",    "A",     uint16ToDouble},
        {40073,    3,      1,     "I_AC_CurrentA",    "A",     uint16ToDouble},
        {40074,    2,      1,     "I_AC_CurrentB",    "A",     uint16ToDouble},
        {40075,    1,      1,     "I_AC_CurrentC",    "A",     uint16ToDouble},
        {40077,    6,      1,     "I_AC_VoltageAB",    "V",     uint16ToDouble},
        {40078,    5,      1,     "I_AC_VoltageBC",    "V",     uint16ToDouble},
        {40079,    4,      1,     "I_AC_VoltageCA",    "V",     uint16ToDouble},
        {40080,    3,      1,     "I_AC_VoltageAN 2",    "V",     uint16ToDouble},
        {40081,    2,      1,     "I_AC_VoltageBN 1",    "V",     uint16ToDouble},
        {40082,    1,      1,     "I_AC_VoltageCN 1",    "V",     uint16ToDouble},
        {40084,    1,      1,     "I_AC_Power",    "W",     int16ToDouble},
        {40086,    1,      1,     "I_AC_Frequency",    "Hz",     uint16ToDouble},
        {40088,    1,      1,     "I_AC_VA",    "VA",     int16ToDouble},
        {40090,    1,      1,     "I_AC_VAR",    "VAR",     int16ToDouble},
        {40092,    1,      1,     "I_AC_PF",    "%",     int16ToDouble},
        {40094,    2,      2,     "I_AC_Energy_WH",    "Wh",     acc32ToDouble},
        {40097,    1,      1,     "I_DC_Current",    "A",     uint16ToDouble},
        {40099,    1,      1,     "I_DC_Voltage",    "V",     uint16ToDouble},
        {40101,    1,      1,     "I_DC_Power",    "W",     int16ToDouble},
        {40104,    1,      1,     "I_Temp_Sink",    "C",     int16ToDouble},
        /*        {40108,    0,      1,     "I_Status",    "",     uint16}, */
        /*        {40109,    0,      1,     "I_Status_Vendor",    "",     uint16}, */
        {0, 0, 0, 0, 0, 0},
};


double int16ToDouble(const uint16_t* data, const int scaleOffset)
{
        short tenPower=ntohs((short)*(data+scaleOffset));
        double val=ntohs(*(short*)data);
        while (tenPower>0) { val*=10; tenPower--; }
        while (tenPower<0) { val/=10; tenPower++; }
        return val;
}

double uint16ToDouble(const uint16_t* data, const int scaleOffset)
{
        short tenPower=ntohs((short)*(data+scaleOffset));
        double val=ntohs(*data);
        while (tenPower>0) { val*=10; tenPower--; }
        while (tenPower<0) { val/=10; tenPower++; }
        return val;
}

double acc32ToDouble(const uint16_t* data, const int scaleOffset)
{
        short tenPower=(short)data[scaleOffset];
        uint32_t intVal=ntohs(data[0]);
        intVal<<=16;
        intVal+=ntohs(data[1]);
        double val=intVal;
        while (tenPower>0) { val*=10; tenPower--; }
        while (tenPower<0) { val/=10; tenPower++; }
        return val;
}

SunSpecValue* getParam(int nr)
{
        if (nr==0) return params;
        for (SunSpecValue* ssv=params; ssv->valueFieldNr;ssv++)
        {
                if (ssv->valueFieldNr==nr) return ssv;
        }
        return 0;
}

double getSunSpecValue(const uint16_t* data, int nr)
{
        double value=NAN;
        SunSpecValue* ssv=getParam(nr);
        if (data && ssv) value=ssv->calcFn(data+ssv->valueFieldNr-modbusBase,ssv->scaleFieldOffset);
        return value;
}
