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

// Every x seconds
#define INTERVAL 10

typedef unsigned short uint16_t;

/**
 * Requests everything needed from an inverter
 * @param ip IP address of modbus-enabled inverter.
 */
int processInverter(const char *ip)
{
    SMA_Inverter inv;
    strcpy(inv.ip, ip);

    printf("\n---------------------------\nINVERTER - %s\n---------------------------\n", ip);

    modbus_t *t = modbus_connect_tcp(ip, 502);

    t->slave = 0x03; // 0 = broadcast, 3= my inverters

    /** Total Yield and Day Yield */
    modbus_regs regs = modbus_read_registers(t, 30529, 54);
    if (regs == NULL)
    {
        return -1;
    }

    inv.TOTAL_YIELD     = getValue(regs, 30529, 30529);
    inv.DAY_YIELD       = getValue(regs, 30529, 30535);

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
    inv.DC_1_VOLT   = getValue(regs, 30769, 30771) / 100; 
    inv.DC_1_AMP    = getValue(regs, 30769, 30769) / 1000;
    inv.DC_1_WATT   = getValue(regs, 30769, 30773);


    inv.AC_L1_VOLT  = getValue(regs, 30769, 30783) / 100,
    inv.AC_L1_WATT  = getValue(regs, 30769, 30775);

    modbus_free_registers(regs);

    /** TEMPERATURE, DC AMP, VOLT, WATT B AMP_L1-3 */
    regs = modbus_read_registers(t, 30953, 30);
    if (regs == NULL)
    {
        return -1;
    }

    inv.temperature = getValue(regs, 30953, 30953) / 10;

    inv.DC_2_VOLT   = getValue(regs, 30953, 30959) / 100;
    inv.DC_2_AMP    = getValue(regs, 30953, 30957) / 1000;
    inv.DC_2_WATT   = getValue(regs, 30953, 30961);

    inv.AC_L1_AMP   = getValue(regs, 30953, 30977) / 1000;

    modbus_free_registers(regs);

    modbus_close(t);

    printf("Total yield: %luWh\n", inv.TOTAL_YIELD);
    printf("Day yield: %luWh\n", inv.DAY_YIELD);
    printf("Inverter\n\tTemperature: %fC\n", inv.temperature);
    printf("DC 1\n\tVolt: %fV\n\tAmp: %fmA\n\tWatt: %luW\n", inv.DC_1_VOLT, inv.DC_1_AMP, inv.DC_1_WATT);
    printf("AC\n\tVolt: %fV\n\tAmp: %fA\n\tWatt: %luW\n", inv.AC_L1_VOLT, inv.AC_L1_AMP, inv.AC_L1_WATT);
    printf("DC 2\n\tVolt: %fV\n\tAmp: %fmA\n\tWatt: %luW\n", inv.DC_2_VOLT, inv.DC_2_AMP, inv.DC_2_WATT);

    return 0;
}

int main(void)
{
    // Connect to clients
    const char *ip[] = {
        "172.19.1.38",
        "172.19.1.65"};
    processInverter(ip[0]);
    processInverter(ip[1]);

    // While loop
    // for (;;)
    // {

    //     sleep(INTERVAL);
    // }

    return 0;
}
