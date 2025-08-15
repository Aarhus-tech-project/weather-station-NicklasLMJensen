Weatehr station H3 Nicklas Lykke Møller Jensen

Jeg har sat et domæne op (nickschoolproject.uk), hvor du kan se mit arbejde. Du bliver ført til en Grafana-side, hvor du skal logge ind for at se temperatur, luftfugtighed og tryk.
Brugernavn og adgangskode til denne bruger er:
testuser
Datait2025!

Vejrstationen forbinder til Wi-Fi med oplysninger, som findes i filen “Arduino_secrets.h”, men den er med i .gitignore for at holde Wi-Fi-adgangskoden privat. Filen ser ud som nedenfor.




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





