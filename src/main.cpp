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
#include "config.h"

volatile sig_atomic_t run = 0;

void sigint_handler(int sig);

/**
 * Interrupt signal, finish last run and stop loop
 */
void sigint_handler(int sig)
{
    fprintf(stdout, "Stopping.\n");
    run = sig;
}

int main(int argc, char *argv[], char *envp[])
{
    /**
     * Prepare interrupt signal
     */
    struct sigaction sa_int;
    sa_int.sa_handler = sigint_handler;
    sa_int.sa_flags = SA_RESTART;

/*
    if ((sigaction(SIGINT, &sa_int, NULL) == -1))
    {
        fprintf(stderr, "sigaction failed\n");
        return -1;
    }*/

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

    int printVerbose = 0;
    /**
     * Command line argument verbose
     */
    if (argc >= 3) {
        if (strncmp(argv[2], "-v", 2) == 0)
            printVerbose = 1;
    }

    /**
     * Connect to InfluxDB
     */
    Influx ifx(config.InfluxHost, config.InfluxPort, config.InfluxOrg, config.InfluxBucket, config.InfluxToken);
    ifx.setVerbosity(printVerbose);
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

        if (printVerbose)
        {
            printf("\n\nTimestamp: %lu\n", currentTimestamp);
            for (int i = 0; i < config.numOfInverters; i++)
                printInverter(config.inverters[i]);
        }

        /**
         * Export to InfluxDB using the same timestamp
         */
        if (printVerbose)
            printf("Exporting to InfluxDB.\n");
        for (int i = 0; i < config.numOfInverters; i++)
        {
            int rc = exportToInflux(ifx, config.inverters[i], currentTimestamp);
            if (printVerbose)
                printf("exportToInflux: %d\n",rc);
        }
            

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

    ifx.closeDB();

    for (int i = 0; i < config.numOfInverters; i++)
    {
        modbus_close(connections[i]);
        free(connections[i]);
    }
    free(connections);

    return 0;
}