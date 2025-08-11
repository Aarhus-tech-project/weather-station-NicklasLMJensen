Weatehr station H3 Nicklas Lykke Møller Jensen


Weather station connects to the wifi network with the infomation in a file called "Arduino_secrets.h" but is in .gitignore to keep wifi passwords private. The file is structored like below


#define SECRET_SSID   "Wifi Name"
#define SECRET_PASS   "Wifi password"
#define MQTT_BROKER   "Broker IP"  
#define MQTT_PORT     1883



