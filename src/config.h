#ifndef CONFIG_H
#define CONFIG_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sma.h"

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

/**
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

#endif