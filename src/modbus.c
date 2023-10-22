#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <poll.h>
#include <netinet/tcp.h>

#include "modbus.h"

/**
 * Prepares modbus type, connects to target
 * @return modbus_type
 */
modbus_t *modbus_connect_tcp(const char *ip, unsigned short port)
{
    modbus_t *mb = (modbus_t *)malloc(sizeof(modbus_t));
    memset(mb, 0, sizeof(modbus_t));

    strncpy(mb->ip, ip, strlen(ip));
    mb->port = port; // INET6_ADDRSTRLEN
    mb->transaction_id = -1;

    /**
     * TCP socket
     */
    struct sockaddr_in sa;
    inet_pton(AF_INET, ip, &(sa.sin_addr));
    sa.sin_family = AF_INET;
    sa.sin_port = htons(port);

    if ((mb->s = socket(PF_INET, SOCK_STREAM, 0)) == -1)
    {
        fprintf(stderr, "socket: socket\n");
        return NULL;
    }

    if (connect(mb->s, (struct sockaddr *)&sa, sizeof(struct sockaddr_in)) == -1)
    {
        close(mb->s);
        fprintf(stderr, "socket: connect\n");
        return NULL;
    }

    // Set TCP_NODELAY so we don't have to flush
    int flag = 1;
    setsockopt(mb->s, IPPROTO_TCP, TCP_NODELAY, (char *)&flag, sizeof(int));

    return mb;
}

/**
 * Builds MODBUS holding register request header
 * ADU = Additional Address + PDU + error check = Modbus frame = MAX 260 b
 * PDU = Function code + data = MAX SIZE = 253 b
 * @param mb Modbus type
 * @param function function code
 * @param addr Address of register
 * @param qoc Quantity of coils to read: from 1 to 20000
 * @param pkg buffer
 */
int modbus_build_request_header(modbus_t *mb, unsigned char function, unsigned short addr, unsigned short qoc, uint8_t *pkg)
{
    // Substract header length from message length
    int mbap_length = MODBUS_TCP_REQ_LENGTH - 6;
    mbap_length = 6; // unit ID, function code, reg addr (2), data (2b)

    /* Increase transaction ID */
    if (mb->transaction_id < 0xFFFF)
        mb->transaction_id++;
    else
        mb->transaction_id = 0;

    /**
     * MODBUS TCP/IP ADU
     * = MBAP Header + PDU
     */
    /* Transaction identifier */
    pkg[0] = (mb->transaction_id >> 8);
    pkg[1] = (mb->transaction_id & 0x00FF);

    /* Protocol Modbus */
    pkg[2] = 0;
    pkg[3] = 0;

    /* Length = Number of remaining bytes in this frame */
    pkg[4] = (mbap_length >> 8);
    pkg[5] = (mbap_length & 0x00FF);

    /* Unit identifier = 255 if not used */
    pkg[6] = mb->slave;

    /**
     * PDU = Function + data
     *  function = 1 byte
     *  Starting Address = 2 bytes
     *  Quantity of coils to read = 2 bytes
     */
    // Function code = 1 byte
    pkg[7] = function;
    // Address = 2 bytes big-endian
    pkg[8] = addr >> 8;
    pkg[9] = addr & 0x00FF;
    // Quantitiy of coils (n bytes)/8
    pkg[10] = (qoc >> 8);
    pkg[11] = (qoc & 0x00FF);

    return MODBUS_TCP_REQ_LENGTH;
}

/**
 * DEPRECATED
 * @param rsp Response buffer from Modbus
 * @param begin Address that we requested
 * @param indexAddress Address we want
 *
 */
unsigned long getU32(uint8_t *rsp, unsigned short begin, unsigned short indexAddress)
{
    // !WARN SMA REGISTER IS 2 BYTES BIG ENDIAN.
    // SO  30529 -> 30531 = 4 bytes in between because U32
    // Header offset = 7 so begin at 8
    unsigned char sma_registers_offset = (indexAddress - begin) / 2;
    unsigned char bytes_registers_offset = sma_registers_offset * 4;
    unsigned char offset = 8 + bytes_registers_offset + 1; // I have no fucking clue what the 0x6C is after the function code.

#if DEBUG
    printf("SMA register offset: %d\n", sma_registers_offset);
    printf("bytes register offset: %d\n", bytes_registers_offset);

    printf("Offset: %d\t -> %x\n", offset, rsp[offset]);
#endif
    return (rsp[offset] << 24) | (rsp[offset + 1] << 16) | (rsp[offset + 2] << 8) | (rsp[offset + 3] & 0xFF);
}

/**
 * @param rsp Response buffer from Modbus
 * @param begin Address that we requested from Modbus
 * @param indexAddress Address we want
 */
unsigned long getValue(modbus_regs mrsp, unsigned short begin, unsigned short indexAddress)
{
    uint8_t *rsp = *mrsp + MODBUS_DATA_OFFSET;
    unsigned char offset = (indexAddress - begin) / 2 * 4;

#if DEBUG
    printf("getValue\tOffset: %d\t -> [%.2X]\n", offset, rsp[offset]);
#endif
    return (rsp[offset] << 24) | (rsp[offset + 1] << 16) | (rsp[offset + 2] << 8) | (rsp[offset + 3] & 0xFF);
}

/**
 * Requests read register
 * @param *mb modbus_type
 * @param function function code, most of the times MODBUS_READ_HOLDING_REGISTERS
 * @param addr Address of the register
 * @param nb number of registers?
 * @return pointer to start of registers
 */
modbus_regs modbus_read_registers(modbus_t *mb, int addr, int qoc)
{
    int rc, req_length;
    uint8_t req[MODBUS_TCP_REQ_LENGTH];
    uint8_t *rsp = malloc(sizeof(uint8_t) * MODBUS_MAX_FRAME_LENGTH);
    modbus_regs mrsp = &rsp;

    req_length = modbus_build_request_header(mb, MODBUS_READ_HOLDING_REGISTERS, addr, qoc, req);

#if DEBUG
    printf("Sending\t\t");
    for (int i = 0; i < req_length; i++)
        printf("[%.2X]", req[i]);
    printf("\n");
#endif

    /**
     * Send Modbus packet
     */
    rc = send(mb->s, req, req_length, 0);
    if (rc <= 0)
    {
        // An error occured on the socket level
        fprintf(stderr, "modbus: send failed\n");
        return NULL;
    }

    /**
     * Receive packet
     */
    struct pollfd pfds[1];
    pfds[0].fd = mb->s;
    pfds[0].events = POLLIN;

    /**
     * Retry algorithm
     */
    int retry = 0;
    for (; retry <= RETRIES; retry++)
    {

        int num_events = poll(pfds, 1, 5000);
        if (num_events == 0)
        {
            printf("read_registers: write poll timed out!\n");
            // Consider as fail, retry
            continue;
        }

        if (!(pfds[0].revents & POLLIN))
        {
            fprintf(stderr, "read_registers: poll failed!\n");
            continue;
        }

        // FD is ready to write
        rc = recv(mb->s, (char *)rsp, MODBUS_MAX_FRAME_LENGTH, 0);

        //!WARN: IDK why, but I sometimes receive only 1 byte 0xFF, so consider as fail
        if (rc <= 1)
        {
            // Error or timeout
            fprintf(stderr, "modbus: read register failed. Retrying %d\n", retry);
            // SMA's bullshit implementation
            sleep(3);
            continue;
        }

        int func_code = rsp[7];

#if DEBUG
    printf("read_registers: Received %d bytes: function code: %d\n", rc, func_code);
#endif

        // Exception Function code MSB bit = 1 = 0x80 higher
        if ((func_code >> 7) == 0x01)
        {
            switch (func_code)
            {
            case 0x81:
                printf("read_registers: Illegal function\n");
                break;
            case 0x82:
                printf("read_registers: Illegal Data Address\n");
                break;
            case 0x83:
                printf("read_registers: Illegal Data Value\n");
                break;
            default:
                printf("read_registers: Illegal error : %d\n", func_code);
            }
            return NULL;
        }

        break;
    }

    if (retry >= RETRIES)
        return NULL;

#if DEBUG
    printf("Received\t");
    for (int i = 0; i < rc; i++)
        printf("[%.2X]", rsp[i]);
    printf("\n");
#endif

    return mrsp;
}

/**
 * Wrapper function for socket recv() with a timeout using select()
 * @param mb modbus_type
 * @param *rsp Reponse buffer
 * @param rsp_length Length of the buffer
 * @return status code
 */
int _modbus_receive(modbus_t *mb, uint8_t *rsp, int rsp_length)
{
    int rc;

    struct pollfd pfds[1];
    pfds[0].fd = mb->s;
    pfds[0].events = POLLIN; // Ready to read

    struct timeval tv;
    tv.tv_sec = 5; // Timeout of 5 seconds
    tv.tv_usec = 0;

    fd_set rset;
    FD_ZERO(&rset);
    FD_SET(mb->s, &rset);

    rc = select(mb->s + 1, &rset, NULL, NULL, &tv);
    if (rc < 0)
    {
        // An error occured
        fprintf(stderr, "recv: select\n");
        return rc;
    }
    if (FD_ISSET(mb->s, &rset))
    {
        return recv(mb->s, (char *)rsp, rsp_length, 0);
    }
    else
    {
        // Timeout
        fprintf(stderr, "recv: timeout\n");
        return rc;
    }
}

void modbus_free_registers(modbus_regs regs)
{
    // Figure out pointer to pointer
    free(*regs);
}

void modbus_close(modbus_t *t)
{
    close(t->s);
}