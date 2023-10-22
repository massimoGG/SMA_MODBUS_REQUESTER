# SMA_MODBUS_REQUESTER
Requests everything I need from my SMA Sunny Boys using Modbus over TCP/IP.

Continuously tested on
- SMA Sunny Boy 30000 with the webconnect module (SB3000TL-21)
- SMA Sunny Boy 40000 with the webconnect module (SB4000TL-21)

## Output example
The followingg is an example of what this program will request from inverters over the modbus TCP/IP protocol.
```
Inverter
        Total Yield: 37336732Wh
        Day Yield: 2769Wh
        Temperature: 30.0C
DC 1
        Volt: 349.170000V
        Amp: 0.454000A
        Watt: 158W
DC 2
        Volt: 218.580000V
        Amp: 0.411000A
        Watt: 89W
AC
        Volt: 226.030000V
        Amp: 0.977000A
        Watt: 220W
```

These values are then sent to an Influx Database 2.0 using the HTTP line protocol.

## Config


## Build instructions
Simply run
`make`