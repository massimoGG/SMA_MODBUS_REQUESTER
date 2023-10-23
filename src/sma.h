#ifndef SMA_H
#define SMA_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>

#include "modbus.h"
#include "sma.h"
#include "influx.hpp"


typedef struct {
    long unsigned DayYield;
    long unsigned TotalYield;

    char *Ip;
    unsigned short Port;
    char *Name;

    long unsigned FeedIntime;   // 30543

    double Temperature;         // 30953
    double HeatsinkTemperature; // 34109
    unsigned long Condition;    // 30201
    unsigned long GridRelay;    // 30217

    double Udc1;
    double Idc1;
    long unsigned Pdc1;
    
    double Udc2;
    double Idc2;
    long unsigned Pdc2;

    double Uac1;
    double Iac1;
    long unsigned Pac1;
    double GridFreq;            // 30803
    long unsigned ReactivePower;// 30805
    long unsigned ApparentPower;// 30813
} SMA_Inverter;

enum {
    SMA_INVERTER_CONDITION_FAULT = 35,
    SMA_INVERTER_CONDITION_OFF  = 303,
    SMA_INVERTER_CONDITION_OK   = 307,
    SMA_INVERTER_CONDITION_WARN = 455,

    SMA_GRID_RELAY_CLOSED   = 51,
    SMA_GRID_RELAY_OPEN     = 311,
    SMA_GRID_RELAY_NANSTT   = 16777213,
};


/**
 * Requests everything needed from an inverter and pushes to influxDB
 * @param SMA_Inverter Inverter struct with IP already filled in
 */
int processInverter(SMA_Inverter *inv, modbus_t *t)
{
    modbus_regs regs;

    t->slave = 0x03; // 0 = broadcast, 3= my inverters

    /**
     * Inverter Condition
     * 	35: Fault (Alm)
     *  303: Off (Off)
     *  307: Ok (Ok)
     *  455: Warning (Wrn)
     * */
    regs = modbus_read_registers(t, 30201, 4);
    if (regs == NULL)
    {
        return -1;
    }

    inv->Condition = getValue(regs, 30201, 30201);

    modbus_free_registers(regs);

    /**
     * Grid Relay
     * 	51: Closed (Cls)
     *  311: Open (Opn)
     *  16777213: Information not available (NaNStt)
     */
    regs = modbus_read_registers(t, 30211, 16);
    if (regs == NULL)
    {
        return -2;
    }
    inv->GridRelay = getValue(regs, 30211, 30217);

    modbus_free_registers(regs);

    /**
     * Total Yield and Day Yield
     */
    regs = modbus_read_registers(t, 30529, 54);
    if (regs == NULL)
    {
        return -3;
    }

    inv->TotalYield = getValue(regs, 30529, 30529);
    inv->DayYield = getValue(regs, 30529, 30535);

    modbus_free_registers(regs);

    /**
     * DC AMP, VOLT, WATT A; AC Watt, L1-3, ACVOLTAGE L1-3
     * Grid freq, AC_R_POWER_L1-3, AC_A_POWER_L!-3
     */
    regs = modbus_read_registers(t, 30769, 52);
    if (regs == NULL)
    {
        return -4;
    }
    inv->Udc1 = ((double)getValue(regs, 30769, 30771) / 100);
    inv->Idc1 = ((double)getValue(regs, 30769, 30769) / 1000);
    inv->Pdc1 = getValue(regs, 30769, 30773);

    inv->Uac1 = ((double)getValue(regs, 30769, 30783) / 100);
    inv->Pac1 = getValue(regs, 30769, 30775);

    modbus_free_registers(regs);

    /**
     * Grid Freq, Reactive Power, Apparent Power
     */
    regs = modbus_read_registers(t, 30803, 10);
    if (regs == NULL)
    {
        return -5;
    }

    inv->GridFreq = ((double)getValue(regs, 30803, 30803) / 100); // Hz
    inv->ReactivePower = getValue(regs, 30803, 30805);            // VAr
    inv->ApparentPower = getValue(regs, 30803, 30813);            // VA

    modbus_free_registers(regs);

    /**
     * TEMPERATURE, DC AMP, VOLT, WATT B AMP_L1-3
     */
    regs = modbus_read_registers(t, 30953, 30);
    if (regs == NULL)
    {
        return -6;
    }

    inv->Temperature = getValue(regs, 30953, 30953) / 10;

    inv->Udc2 = ((double)getValue(regs, 30953, 30959) / 100);
    inv->Idc2 = ((double)getValue(regs, 30953, 30957) / 1000);
    inv->Pdc2 = getValue(regs, 30953, 30961);

    inv->Iac1 = ((double)getValue(regs, 30953, 30977) / 1000);

    modbus_free_registers(regs);

    return 0;
}

/**
 * @param ifx Influx class to initialized InfluxDB
 * @param inv Pointer to SMA_Inverter struct that is being exported to InfluxDB
 * @param currentTimestamp the current time in seconds precision unix time
 * @return Return value of post()
 */
int exportToInflux(Influx &ifx, SMA_Inverter *inv, unsigned long currentTimestamp)
{
    /**
     * Limited view since inverter only sends 0xFFFFFFFF except for the following values
     */
    if (inv->GridRelay == SMA_GRID_RELAY_NANSTT)
    {
        return ifx
            .tag("inverter", inv->Name)

            .meas("status")
            .field("condition", inv->Condition)

            .meas("yield")
            .field("DayYield", inv->DayYield)
            .field("TotalYield", inv->TotalYield)

            .meas("grid")
            .field("GridRelay", inv->GridRelay)

            .timestamp(currentTimestamp)
            .post();
    }

    return ifx
        .tag("inverter", inv->Name)

        .meas("power")
        .field("Pac1", inv->Pac1)
        .field("Pdc1", inv->Pdc1)
        .field("Pdc2", inv->Pdc2)

        .meas("voltage")
        .field("Uac1", inv->Uac1)
        .field("Udc1", inv->Udc1)
        .field("Udc2", inv->Udc2)

        .meas("current")
        .field("Iac1", inv->Iac1)
        .field("Idc1", inv->Idc1)
        .field("Idc2", inv->Idc2)

        .meas("status")
        .field("condition", inv->Condition)
        .field("temperature", inv->Temperature)
        // .field("Heatsink", inv->HeatsinkTemperature)

        .meas("yield")
        .field("DayYield", inv->DayYield)
        .field("TotalYield", inv->TotalYield)

        .meas("grid")
        .field("GridRelay", inv->GridRelay)
        .field("GridFreq", inv->GridFreq)
        .field("ReactivePower", inv->ReactivePower)
        .field("ApparentPower", inv->ApparentPower)

        .timestamp(currentTimestamp)
        .post();
}

/**
 * @param inv Pointer to struct of SMA inverter to be printed
 */
void printInverter(SMA_Inverter *inv)
{
    printf("\033[1m---------------------------\nINVERTER - %s\n%s\n---------------------------\033[0m\n",
           inv->Name, inv->Ip);

    printf("Total yield: %luWh\n", inv->TotalYield);
    printf("Day yield: %luWh\n", inv->DayYield);
    printf("Inverter\n\tCondition: %li\n\tTemperature: %fC\n\tHeatsink: %fC\n", inv->Condition, inv->Temperature, inv->HeatsinkTemperature);
    printf("DC 1\n\tVolt: %fV\n\tAmp: %fA\n\tWatt: %luW\n", inv->Udc1, inv->Idc1, inv->Pdc1);
    printf("DC 2\n\tVolt: %fV\n\tAmp: %fA\n\tWatt: %luW\n", inv->Udc2, inv->Idc2, inv->Pdc2);
    printf("AC\n\tVolt: %fV\n\tAmp: %fA\n\tWatt: %luW\n", inv->Uac1, inv->Iac1, inv->Pac1);
    printf("\tGridFreq: %f\n\tGridRelay: %lu\n\tReactiveP: %lu VAr\n\tApparentP: %li\n", inv->GridFreq, inv->GridRelay, inv->ReactivePower, inv->ApparentPower);
}


#endif