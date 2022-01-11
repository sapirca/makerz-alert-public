# ✨ Makerz alerts ✨
## Red Alert visual notifier

Makerz alerts (aka המתריעון pronounced as Ha-matri-on) is a product that provides visual alerts when a siren goes off in Israel.
This repository is the client-side code for ESP8266 controller.

What you'll need to build one by yourself:
- ESP8266
- 5v charger with micro-usb connector
- addressable LED strip (roughly 1 meter)
- JST connectors / Wires
- Soldering kit (solder iron, tin, etc)

## How to build the kit
install Visual Studio Code with PlatformIO (not yet supporting Arduino IDE. 
If  you're intrested in the latter, the entire code is placed in main.cpp, so you can copy-paste into the IDE editor - please make sure to install all the necessary libraries (FastLED, PubSubClient, ArduinoJson, etc). They're all listed in platform.ini under lib_deps. This explanation has not yet been tested though).
    
Connect the LEDs to the ESP8266 IO pin D3. There are two ways to change the data pin - via the build flag `PIN_LEDS` in platform.ini  or the equivalent `#define PIN_LEDS` in main.cpp.

In the published guide in Hebrew we explain how to power the LEDs through the controller itself, it is not best practive but rather simplier for new users. It allows you to power at most 50-100 LEDs in low brightness. If you're reading this you're porbably advanced enough to do as follows: power the LEDs via an external power source, and connect the data line to the controller, don't forget to share the grounds (meaning that D3 + GND soldered to the ESP8266, VCC + GND to the external power source).
    
This is the client code for the IOT devices. We also maintain a server with an mqtt broker that is accessible under the domain defined as `mqtt_server` in main.cpp.  

## Lights chart
    - White light at boot
    - Green light when connected to WiFi
    - Pink light when connected to our server via mqtt
    - Red light when red alert is being sound in the configured location.
    
## Setting up WiFi credentials
If the microcontroller is unable to connect to WiFi, it opens an access point called makers-alerts-AP. The SSID defined in main.cpp under `AP_NAME`
    After connecting to the AP wifi, open a web broswer and navigate to 192.168.4.1
    Configure the WiFi credentials. After hitting Save the ESP will reboot and try to connect to the provided WiFi network. Upon success, green light fade will show up. 
    

