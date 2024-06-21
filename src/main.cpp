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

int processInverter(SMA_Inverter *pinv, modbus_t *t);
int exportToInflux(Influx &ifx, SMA_Inverter *pinv, unsigned long currentTimestamp);
void printInverter(SMA_Inverter *pinv);

/**
 * Requests everything needed from an inverter and pushes to influxDB
 * @param SMA_Inverter Inverter struct with IP already filled in
 */
int processInverter(SMA_Inverter *inv, modbus_t *t)
{
    if (t == NULL) {
        return -1;
    }

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
    ifx.clear();
    
    // can be a way to see if the inverter is off? 
    if (inv->Temperature > 10000)
    {
        return ifx.meas("measurement")
            .tag("inverter", inv->Name)
            .field("Condition", inv->Condition)

            // .field("Heatsink", inv->HeatsinkTemperature)
            .field("DayYield", inv->DayYield)
            .field("TotalYield", inv->TotalYield)

            .field("GridRelay", inv->GridRelay)
            .field("GridFreq", inv->GridFreq)

            .timestamp(currentTimestamp)
            .post();
    }

    return ifx.meas("measurement")
        .tag("inverter", inv->Name)
        .field("Condition", inv->Condition)

        .field("Temperature", inv->Temperature)
        // .field("Heatsink", inv->HeatsinkTemperature)
        .field("DayYield", inv->DayYield)
        .field("TotalYield", inv->TotalYield)

        .field("Pac1", inv->Pac1)
        .field("Pdc1", inv->Pdc1)
        .field("Pdc2", inv->Pdc2)

        .field("Uac1", inv->Uac1)
        .field("Udc1", inv->Udc1)
        .field("Udc2", inv->Udc2)

        .field("Iac1", inv->Iac1)
        .field("Idc1", inv->Idc1)
        .field("Idc2", inv->Idc2)

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
     * Get environment variables
     */
    const char *influx_host     = getenv("INFLUX_HOST");
    const int influx_port       = atoi(getenv("INFLUX_PORT"));
    const char *influx_org      = getenv("INFLUX_ORGANISATION");
    const char *influx_bucket   = getenv("INFLUX_BUCKET");
    const char *influx_token    = getenv("INFLUX_TOKEN"); // jaja, I know
    const int interval          = atoi(getenv("INTERVAL")); 
    const int debug             = atoi(getenv("DEBUG"));

    /**
     * Connect to InfluxDB
     */
    Influx ifx(influx_host, influx_port, influx_org, influx_bucket, influx_token);
    if (ifx.connectNow() != 0)
    {
        fprintf(stderr, "main: InfluxDB connection failed\n");
        return -1;
    }

    fprintf(stdout, "Connecting to Inverters...\n");

    // Connect to clients
    SMA_Inverter sb3000 = {
        .Ip = strdup("172.19.30.0"),
        .Port = 502,
        .Name = strdup("SB3000TL-21"),
    };
    modbus_t *sb3000_conn = modbus_connect_tcp(sb3000.Ip, sb3000.Port);
    puts("Connected to SB3000TL");

    SMA_Inverter sb4000 = {
        .Ip = strdup("172.19.40.0"),
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

        if (debug){
            printInverter(&sb3000);
            printInverter(&sb4000);
        }

        /**
         * Export to InfluxDB using the same timestamp
         */
        int ret = exportToInflux(ifx, &sb3000, currentTimestamp);
        ret = exportToInflux(ifx, &sb4000, currentTimestamp);
        if (ret != 0) {
            break;
            // Abort if connection with Influx lost
        }

        printf("%u OK\n", (unsigned)time(NULL));

        sleep(interval);
    }

    modbus_close(sb3000_conn);
    modbus_close(sb4000_conn);

    return 0;
}
