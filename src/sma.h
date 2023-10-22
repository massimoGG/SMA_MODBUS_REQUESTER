#ifndef SMA_H
#define SMA_H

typedef struct {
    long unsigned DAY_YIELD;
    long unsigned TOTAL_YIELD;

    char ip[16];
    char name[32];
    double temperature;

    double DC_1_VOLT;
    double DC_1_AMP;
    long unsigned DC_1_WATT;
    
    double DC_2_VOLT;
    double DC_2_AMP;
    long unsigned DC_2_WATT;

    double AC_L1_VOLT;
    double AC_L1_AMP;
    long unsigned AC_L1_WATT;
} SMA_Inverter;

#endif