#ifndef MODBUS_H
#define MODBUS_H

#define DEBUG 0
#define RETRIES 3

typedef unsigned char uint8_t;
typedef unsigned short uint16_t;

#define MODBUS_READ_HOLDING_REGISTERS 0x03
#define MODBUS_READ_INPUT_REGISTERS 0x04

#define MODBUS_MAX_FRAME_LENGTH 260
#define MODBUS_TCP_REQ_LENGTH 12
#define MODBUS_DATA_OFFSET 9

enum
{
    MODBUS_EXCEPTION_ILLEGAL_FUNCTION       = 0x01,
    MODBUS_EXCEPTION_ILLEGAL_DATA_ADDRESS   = 0x02,
    MODBUS_EXCEPTION_ILLEGAL_DATA_VALUE     = 0x03,
};

typedef struct
{
    int s;

    unsigned char slave;
    unsigned short transaction_id;

    char ip[16];
    unsigned short port;
} modbus_t;

typedef uint8_t **modbus_regs;

/**
 * Function predefinitions
 */
modbus_t *modbus_connect_tcp(const char *ip, unsigned short port);
int modbus_build_request_header(modbus_t *mb, unsigned char function, unsigned short addr, unsigned short qoc, uint8_t *pkg);
unsigned long getValue(modbus_regs regs, unsigned short begin, unsigned short indexAddress);
modbus_regs modbus_read_registers(modbus_t *mb, int addr, int qoc);
void modbus_free_registers(modbus_regs regs);
void modbus_close(modbus_t *t);

#endif
