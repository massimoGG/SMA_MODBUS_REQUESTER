# SMA_MODBUS_REQUESTER
Simple SMA Sunny Boy Modbus TCP/IP fetcher written in C++. 

Continuously tested on
- SMA Sunny Boy 3000 with the webconnect module (SB3000TL-21)
- SMA Sunny Boy 4000 with the webconnect module (SB4000TL-21)

## Output example
The following is a typical output of the program.
```
---------------------------
INVERTER - SB4000TL-21
172.19.40.0
---------------------------
Total yield: 39230580Wh
Day yield: 1954Wh
Inverter
        Temperature: 30.000000C Heatsink: 0.000000C
DC 1
        Volt: 358.760000V
        Amp: 0.936000A
        Watt: 335W
DC 2
        Volt: 220.240000V
        Amp: 0.847000A
        Watt: 186W
AC
        Volt: 233.830000V
        Amp: 2.147000A
        Watt: 496W
        GridFreq: 49.990000
        ReactiveP: 0 VAr
        ApparentP: 0
```

These values are then sent to an Influx Database 2.0 using the HTTP line protocol.

```
INFLUXDB DEBUG: POST /api/v2/write?bucket=solar&org=massimogg&precision=s HTTP/1.1
Host: 172.17.3.0:8086
User-Agent: influxdb-client-cheader
Content-Length: 303
Authorization: Token jj553uNGBo1rHgTuEjb3D-iZhECzs3i99Ubt4S9xAeoccRolxxBGS-rfVXdO2deokw265_FecKYMif-Fwu4NFA==

measurement,inverter=SB4000TL-21 Condition=307i,Temperature=30.000000,DayYield=1954i,TotalYield=39230580i,Pac1=496i,Pdc1=335i,Pdc2=186i,Uac1=233.830000,Udc1=358.760000,Udc2=220.240000,Iac1=2.147000,Idc1=0.936000,Idc2=0.847000,GridRelay=51i,GridFreq=49.990000,ReactivePower=0i,ApparentPower=0i 1716631685
```

## Config
See config file

## Build instructions
Simply run
`make` and `./main`


