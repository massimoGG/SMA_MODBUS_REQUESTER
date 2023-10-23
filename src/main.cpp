/**
 * Project Mike Sierra Delta
 * Modbus SMA Database
 * This program extracts necessary values from SMA inverters using ModBus,
 * and stores this to an influxDB OSS2
 * It can be ran as a service, or inside a container
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "modbus.h"
#include "sma.h"
#include "influx.hpp"

// Every x seconds
#define INTERVAL 15
#define SHOW 0 

/**
 * Influx stuff
 */
#define INFLUX_TOKEN "PMZqaSeeUEPTmnC6DJZBWDP3pGmSlhTUQU4u7Erw9x6qEwpiLT99-GV0gh9h6e5lQg4tV-G5Mfde07gPRaJzpg=="
#define INFLUX_HOST "172.16.1.3"
#define INFLUX_PORT 8086
#define INFLUX_ORG "massimog"
#define INFLUX_BUCKET "SMA"

int processInverter(SMA_Inverter *pinv, modbus_t *t);
int exportToInflux(Influx &ifx, SMA_Inverter *pinv, unsigned long currentTimestamp);
void printInverter(SMA_Inverter *pinv);

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
        return -1;
    }
    inv->GridRelay = getValue(regs, 30211, 30217);

    modbus_free_registers(regs);

    /** 
     * Total Yield and Day Yield 
     */
    regs = modbus_read_registers(t, 30529, 54);
    if (regs == NULL)
    {
        return -1;
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
        return -1;
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
        return -1;
    }

    inv->GridFreq       = ((double)getValue(regs, 30803, 30803) / 100); // Hz
    inv->ReactivePower  = getValue(regs, 30803, 30805);    // VAr
    inv->ApparentPower  = getValue(regs, 30803, 30813);    // VA

    modbus_free_registers(regs);

    /** 
     * TEMPERATURE, DC AMP, VOLT, WATT B AMP_L1-3 
     */
    regs = modbus_read_registers(t, 30953, 30);
    if (regs == NULL)
    {
        return -1;
    }

    inv->Temperature = getValue(regs, 30953, 30953) / 10;

    inv->Udc2 = ((double)getValue(regs, 30953, 30959) / 100);
    inv->Idc2 = ((double)getValue(regs, 30953, 30957) / 1000);
    inv->Pdc2 = getValue(regs, 30953, 30961);

    inv->Iac1 = ((double)getValue(regs, 30953, 30977) / 1000);

    modbus_free_registers(regs);

    return 0;
}

int exportToInflux(Influx &ifx, SMA_Inverter *inv, unsigned long currentTimestamp)
{
    // If Inverter is producing nothing 
    if (inv->GridRelay != SMA_GRID_RELAY_CLOSED )
    {
        return ifx
            .tag("inverter",inv->Name)

            .meas("status")
            .field("condition", inv->Condition)

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

void printInverter(SMA_Inverter *inv)
{
    printf("\n\n\n\033[1m---------------------------\nINVERTER - %s\n%s\n---------------------------\033[0m\n", 
        inv->Name, inv->Ip);

    printf("Total yield: %luWh\n", inv->TotalYield);
    printf("Day yield: %luWh\n", inv->DayYield);
    printf("Inverter\n\tTemperature: %fC\tHeatsink: %fC\n", inv->Temperature, inv->HeatsinkTemperature);
    printf("DC 1\n\tVolt: %fV\n\tAmp: %fA\n\tWatt: %luW\n", inv->Udc1, inv->Idc1, inv->Pdc1);
    printf("DC 2\n\tVolt: %fV\n\tAmp: %fA\n\tWatt: %luW\n", inv->Udc2, inv->Idc2, inv->Pdc2);
    printf("AC\n\tVolt: %fV\n\tAmp: %fA\n\tWatt: %luW\n",   inv->Uac1, inv->Iac1, inv->Pac1);
    printf("\tGridFreq: %f\n\tReactiveP: %lu VAr\n\tApparentP: %li\n", inv->GridFreq, inv->ReactivePower, inv->ApparentPower);
}

int main(void)
{
    /**
     * Connect to InfluxDB
     */
    Influx ifx(INFLUX_HOST, INFLUX_PORT, INFLUX_ORG, INFLUX_BUCKET, INFLUX_TOKEN);
    if (ifx.connectNow() != 0)
    {
        fprintf(stderr, "main: InfluxDB connection failed\n");
        return -1;
    }

    // Connect to clients
    SMA_Inverter sb3000 = {
        .Ip = strdup("172.19.1.38"),
        .Port = 502,
        .Name = strdup("SB3000TL-21"),
    };
    modbus_t *sb3000_conn = modbus_connect_tcp(sb3000.Ip, sb3000.Port);
    puts("Connected to SB3000TL");

    SMA_Inverter sb4000 = {
        .Ip = strdup("172.19.1.65"),
        .Port = 502,
        .Name = strdup("SB4000TL-21"),
    };
    modbus_t *sb4000_conn = modbus_connect_tcp(sb4000.Ip, sb4000.Port);
    puts("Connected to SB4000TL");

    // TODO  HANDLE UNIX SIGNALS
    for (unsigned long long i = 0;; i++)
    {
        unsigned long currentTimestamp = time(NULL);

        processInverter(&sb3000, sb3000_conn);
        processInverter(&sb4000, sb4000_conn);

#if SHOW
        printInverter(&sb3000);
        printInverter(&sb4000);
#endif
        /**
         * Export to InfluxDB using the same timestamp
         */
        exportToInflux(ifx, &sb3000, currentTimestamp);
        exportToInflux(ifx, &sb4000, currentTimestamp);

        sleep(INTERVAL);
    }

    modbus_close(sb3000_conn);
    modbus_close(sb4000_conn);

    return 0;
}
