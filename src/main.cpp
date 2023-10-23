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
#include <signal.h>

#include "modbus.h"
#include "sma.h"
#include "influx.hpp"

typedef struct
{
    /**
     * General settings
     */
    unsigned long Interval;

    /**
     * InfluxDB
     */
    char *InfluxHost;
    unsigned short InfluxPort;
    char *InfluxToken;
    char *InfluxOrg;
    char *InfluxBucket;

    int numOfInverters;
    // Array of pointers :D
    SMA_Inverter *inverters[];
} Config;

volatile sig_atomic_t run = 0;

void sigint_handler(int sig);
int readConfigFile(char *path, Config *config);

int processInverter(SMA_Inverter *pinv, modbus_t *t);
int exportToInflux(Influx &ifx, SMA_Inverter *pinv, unsigned long currentTimestamp);
void printInverter(SMA_Inverter *pinv);

/**
 * Interrupt signal, finish last run and stop loop
 */
void sigint_handler(int sig)
{
    fprintf(stdout, "Stopping.\n");
    run = sig;
}

/**
 * TODO Place this in its own config.h file :)
 * @param path Path of the config file
 * @param config initializated Config struct to be filled
 * @return status
 */
int readConfigFile(char *path, Config *config)
{
    FILE *file = fopen(path, "r");
    if (file == NULL)
    {
        return -1;
    }

    config->numOfInverters = 0;

    char *line = NULL;
    size_t len = 0;
    ssize_t read;
    while ((read = getline(&line, &len, file)) != -1)
    {
        // Ignore comments
        if (line[0] == '#')
            continue;
        // Remove \n from line
        line[strlen(line) - 1] = '\0';

        if (strncmp(line, "INTERVAL", 8) == 0)
            config->Interval = atoi(line + 9);

        if (strncmp(line, "INFLUX_HOST", 11) == 0)
            config->InfluxHost = strdup(line + 12);

        if (strncmp(line, "INFLUX_PORT", 11) == 0)
            config->InfluxPort = atoi(line + 12);

        if (strncmp(line, "INFLUX_TOKEN", 12) == 0)
            config->InfluxToken = strdup(line + 14);

        if (strncmp(line, "INFLUX_ORG", 10) == 0)
            config->InfluxOrg = strdup(line + 11);

        if (strncmp(line, "INFLUX_BUCKET", 13) == 0)
            config->InfluxBucket = strdup(line + 14);

        /**
         * Inverter configs
         */
        if (strncmp(line, "INVERTERS_IP", 12) == 0)
        {
            // Split by space
            for (char *invIp = strtok(line + 13, " "); invIp != NULL; invIp = strtok(NULL, " "))
            {
                SMA_Inverter *si = (SMA_Inverter *)malloc(sizeof(SMA_Inverter));
                si->Ip = strdup(invIp);

                // append
                config->inverters[config->numOfInverters] = si;

                config->numOfInverters++;
            }
        }
        if (strncmp(line, "INVERTERS_NAME", 14) == 0)
        {
            int i = 0;
            for (char *invName = strtok(line + 15, " "); invName != NULL; invName = strtok(NULL, " "), i++)
            {
                config->inverters[i]->Name = strdup(invName);
            }
        }
        if (strncmp(line, "INVERTERS_PORT", 14) == 0)
        {
            int i = 0;
            for (char *invPort = strtok(line + 15, " "); invPort != NULL; invPort = strtok(NULL, " "), i++)
            {
                unsigned short port = atoi(invPort);
                config->inverters[i]->Port = port;
            }
        }
    }

    // DAMN, This worked perfectly first-try.
    //  printf("Read config\ninter: %d\nhost: '%s'\nport %d\nToken %s\norg: %s\nbucket:%s\n",
    // config->Interval, config->InfluxHost, config->InfluxPort, config->InfluxToken, config->InfluxOrg, config->InfluxBucket);
    // Anyway, TODO: Do this in smaller functions etc.
    fclose(file);
    return 0;
}

int main(int argc, char *argv[], char *envp[])
{
    /**
     * Prepare interrupt signal
     */
    struct sigaction sa_int;
    sa_int.sa_handler = sigint_handler;
    sa_int.sa_flags = SA_RESTART;

    if ((sigaction(SIGINT, &sa_int, NULL) == -1))
    {
        fprintf(stderr, "sigaction failed\n");
        return -1;
    }

    /**
     * Read Config file
     */
    Config config;
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <configfile-path> [-v]\n", argv[0]);
        return -1;
    }
    readConfigFile(argv[1], &config);
    unsigned int interval = config.Interval; // 15;

    int printInverters = 0;
    /**
     * Command line argument verbose
     */
    if (argc >= 3) {
        if (strncmp(argv[2], "-v", 2) == 0)
            printInverters = 1;
    }

    /**
     * Connect to InfluxDB
     */
    Influx ifx(config.InfluxHost, config.InfluxPort, config.InfluxOrg, config.InfluxBucket, config.InfluxToken);
    if (ifx.connectNow() != 0)
    {
        fprintf(stderr, "main: InfluxDB connection failed\n");
        return -1;
    }

    /**
     * Connect to modbus
     */
    modbus_t **connections = (modbus_t **)malloc(sizeof(modbus_t *) * config.numOfInverters);
    for (int i = 0; i < config.numOfInverters; i++)
    {
        printf("Connecting to %s (%s)\n", config.inverters[i]->Ip, config.inverters[i]->Name);
        connections[i] = modbus_connect_tcp(config.inverters[i]->Ip, config.inverters[i]->Port);
        if (connections[i] == NULL)
        {
            fprintf(stderr, "Connection failed!");
            return -1;
        }
        printf("Connected!\n");
    }

    /**
     * Timestamp prep
     */
    unsigned long currentTimestamp = time(NULL), nextTimestamp = 0;
    // Round timestamp
    currentTimestamp -= (currentTimestamp % interval);

    /**
     * Main loop
     */
    int rc = 0;
    for (unsigned long long i = 0; !run; i++)
    {
        nextTimestamp = currentTimestamp + interval;

        for (int i = 0; i < config.numOfInverters; i++)
        {
            rc = processInverter(config.inverters[i], connections[i]);
            if (rc < 0)
                fprintf(stderr, "modbus: Error: Failed fetching modbus details (%d) for %s\n", rc, config.inverters[i]->Name);
        }

        if (printInverters)
        {
            printf("\n\nTimestamp: %lu\n", currentTimestamp);
            for (int i = 0; i < config.numOfInverters; i++)
                printInverter(config.inverters[i]);
        }

        /**
         * Export to InfluxDB using the same timestamp
         */
        for (int i = 0; i < config.numOfInverters; i++)
            exportToInflux(ifx, config.inverters[i], currentTimestamp);

        long waitTime = nextTimestamp - time(NULL);
        if (waitTime < 0)
        {
            fprintf(stderr, "Warning: Modbus fetching took longer than %d seconds!\n", interval);
            waitTime = 0;
        }

        sleep(waitTime);
        // Increase currentTimestamp
        currentTimestamp += interval;
    }

    for (int i = 0; i < config.numOfInverters; i++)
    {
        modbus_close(connections[i]);
        free(connections[i]);
    }
    free(connections);

    return 0;
}

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
