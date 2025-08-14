Weatehr station H3 Nicklas Lykke Møller Jensen


Weather station connects to the wifi network with the infomation in a file called "Arduino_secrets.h" but is in .gitignore to keep wifi passwords private. The file is structored like below



#pragma once


// arduino_secrets.h

#define WIFI_SSID    "Wifi Name"

#define WIFI_PASS   "Wifi password"


// InfluxDB Cloud

#define INFLUX_HOST   "influx.(Your domain).com"

#define INFLUX_PORT   443

#define INFLUX_ORG     "Influx Org"

#define INFLUX_BUCKET  "Influx Bucket"

#define INFLUX_TOKEN   "Influx Token"





