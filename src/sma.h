#ifndef SMA_H
#define SMA_H

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

#endif