#ifndef SMA_H
#define SMA_H

typedef struct {
    long unsigned DayYield;
    long unsigned TotalYield;

    char *Ip;
    unsigned short Port;
    char *Name;
    double Temperature;

    double Udc1;
    double Idc1;
    long unsigned Pdc1;
    
    double Udc2;
    double Idc2;
    long unsigned Pdc2;

    double Uac1;
    double Iac1;
    long unsigned Pac1;
} SMA_Inverter;

#endif