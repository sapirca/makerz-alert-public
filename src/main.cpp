#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>
#include <ESP_DoubleResetDetector.h>
#include <PubSubClient.h>
#include <FastLED.h>
#include <FS.h>
#include <ArduinoJson.h>
#include <string.h>

//Genereal
#define version 0.9

// DRD
#define DRD_TIMEOUT 10 // timeout for 2nd click (double reset)
#define DRD_ADDRESS 0  // RTC Memory Address for the DoubleResetDetector

// WiFi
#define AP_NAME "makers-"
DoubleResetDetector drd(DRD_TIMEOUT, DRD_ADDRESS);
WiFiManager wifiManager;
WiFiClient espClient;
std::unique_ptr<WiFiManagerParameter> custom_arealist;
bool fsMounted = false;
bool shouldSaveConfig = false;

// MQTT
#define TOPIC_ALERT "ra_alert"
#define TOPIC_WARNING "ra_warning"
#define TOPIC_ALERT_COUNT "ra_alert/count"
#define TOPIC_WARNING_COUNT "ra_warning/count"
#define TOPIC_HEARTBEAT "client_heartbeat"
#define TOPIC_CLIENT_SELF_TEST "ra_client/self-test"
#define TOPIC_CLIENT_RGB "ra_client/RGB"

PubSubClient mqttClient(espClient);
const char *mqtt_server = "mqtt.***********.co.il";
const int mqtt_port = 1883;
char arealist[2000] = "";
#define MIN_TIME_BETWEEN_ALERTS_MILLIS 8000

// Heartbeat print
#define HEARTBEAT_MILLIS (1000 * 60 * 5) //five minutes
#define HEARTBEAT_MSG_BUFFER_SIZE 50
char msg[HEARTBEAT_MSG_BUFFER_SIZE];
unsigned long lastMsg = 0;
long int heartbeatValue = 0;

// I'm alive LED
unsigned long last_blink = millis();
#ifndef STATUS_LED_DELAY
#define STATUS_LED_DELAY -1
#endif
signed long statusLEDInterval = STATUS_LED_DELAY;

// Leds
#define LED_OFF LOW
#define LED_ON HIGH
#define BRIGHTNESS 128
#define ALERT_COLOR_WAIT_TIME 500

#ifndef NUM_LEDS
#warning NUM_LEDS not provided as a build flag (in platform.ini). using default value.
#define NUM_LEDS 300
#endif // NUM_LEDS

#ifndef PIN_LEDS
#warning PIN_LEDS not provided as a build flag (in platform.ini). using default value.
#define PIN_LEDS 3
#endif // PIN_LEDS

CRGB leds[NUM_LEDS];

void utils_inPlaceReverse(String &str);

void leds_initStrip()
{
  FastLED.addLeds<WS2812B, PIN_LEDS, GRB>(leds, NUM_LEDS);
  FastLED.setBrightness(BRIGHTNESS);
}

void leds_turnOff()
{
  FastLED.clear();
  FastLED.show();
}

void leds_fadeOut()
{
  int fadeAmount = 1;
  int scale = 255;
  while (scale >= 0)
  {
    nscale8(leds, NUM_LEDS, scale);
    FastLED.show();
    delay(25);
    scale = scale - fadeAmount;
  }
}

void leds_rainbow(uint8_t hue)
{
  fill_rainbow(leds, NUM_LEDS, hue, 255 / NUM_LEDS);
  FastLED.show();
}
void leds_fadeIn(CRGB target)
{
  int steps = 50;
  for (uint8_t b = 0; b < steps; b++)
  {
    fill_solid(leds, NUM_LEDS, CRGB(target.red * b / steps, target.green * b / steps, target.blue * b / steps));
    FastLED.show();

    delay(10);
  };
}
void leds_programStarted()
{
  // fill_solid(leds, NUM_LEDS, CRGB::Green); // https://github.com/FastLED/FastLED/wiki/Pixel-reference#predefined-colors-list
  leds_fadeIn(CRGB::WhiteSmoke);
  delay(500);
  leds_fadeOut();
}
void leds_wifiConnected()
{
  // fill_solid(leds, NUM_LEDS, CRGB::Green); // https://github.com/FastLED/FastLED/wiki/Pixel-reference#predefined-colors-list
  leds_fadeIn(CRGB::Green);
  delay(500);
  leds_fadeOut();
}

void leds_wifiFailedToConnect()
{
  fill_solid(leds, NUM_LEDS, CRGB::Blue);
}

void leds_mqttConnected()
{
  leds_fadeIn(CRGB::HotPink);

  leds_fadeOut();
}

void leds_redAlert()
{
  for (int i = 0; i <= 5; i++)
  {
    leds_fadeIn(CRGB::Red);
    delay(ALERT_COLOR_WAIT_TIME);
    leds_fadeOut();
    delay(ALERT_COLOR_WAIT_TIME);
  }
}

void leds_redAlertWarning()
{
  fill_solid(leds, NUM_LEDS, CRGB::Orange);
  FastLED.show();
  delay(ALERT_COLOR_WAIT_TIME);
  leds_fadeOut();
}
String mqtt_generateClientId()
{
  String s = WiFi.macAddress();
  s.replace("-", "");
  s.replace(":", "");
  s.replace(" ", "");
  s = s.substring(6);
  utils_inPlaceReverse(s);
  return s;
}
void fs_init()
{
  // Read configuration from FS json
  Serial.println("mounting FS...");
  fsMounted = SPIFFS.begin();
  if (fsMounted)
  {
    Serial.println("Mounted file system");
    if (SPIFFS.exists("/config.json"))
    {
      // File exists, reading and loading
      Serial.println("reading config file");
      File configFile = SPIFFS.open("/config.json", "r");
      if (configFile)
      {
        Serial.println("opened config file");
        size_t size = configFile.size();
        // Allocate a buffer to store the content of the file
        std::unique_ptr<char[]> buf(new char[size]);
        configFile.readBytes(buf.get(), size);
        Serial.println(buf.get());
        DynamicJsonDocument doc(1024);
        deserializeJson(doc, buf.get());
        strcpy(arealist, doc["arealist"]);
        Serial.print("areas:  ");
        Serial.println(arealist);
      }
    }
    else
    {
      Serial.println("/config.json not found");
    }
  }
  else
  {
    Serial.println("failed to mount FS");
  }
  custom_arealist.reset(new WiFiManagerParameter("area_list", "רשימת אזורים", arealist, 250));
}

// Callback indicating to save config
void saveConfigCallback()
{
  Serial.println("Should save config");
  shouldSaveConfig = true;
}

void saveConfig()
{
  if (fsMounted)
  {
    Serial.println("Saving config...");
    //read updated parameters
    strcpy(arealist, custom_arealist->getValue());
    StaticJsonDocument<256> json;
    //  JsonObject json = jsonBuffer.to<JsonObject>();

    json["arealist"] = arealist;
    String output;
    serializeJson(json, output);

    File configFile = SPIFFS.open("/config.json", "w");
    if (!configFile)
    {
      Serial.println("failed to open config file for writing");
    }
    else
    {
      Serial.println(output);
      configFile.print(output);
      configFile.close();
    }
  }
  else
  {
    Serial.println("failed to mount FS");
  }
  shouldSaveConfig = false;
  drd.stop();
}

void wifi_begin()
{

  // WiFi.printDiag(Serial);
  bool doubleReset = drd.detectDoubleReset();
  bool noSSID = WiFi.SSID() == "";
  String _ssid = AP_NAME + mqtt_generateClientId();
  int str_len = _ssid.length() + 1;
  char char_array[str_len];
  _ssid.toCharArray(char_array, str_len);

  if (doubleReset || noSSID)
  {
    Serial.println("double reset or no SSID");
    wifiManager.setSaveConfigCallback(saveConfigCallback);
    wifiManager.addParameter(custom_arealist.get());
    if (!wifiManager.startConfigPortal(char_array))
    {
      leds_wifiFailedToConnect();
      Serial.println("failed to connect and should not get here");
    }
    if (shouldSaveConfig)
    {
      saveConfig();
    }

    // TODO needs clarification
    // rebooting is the easiest way to start making use of the new configuration.
    // once only wifi is being set, move this reboot to where the config setup failed.
    Serial.println("going to reboot after saving");
    delay(3000);
    ESP.reset();
    delay(5000);
  }
}

void wifi_printStatus()
{
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }
  randomSeed(micros());
  Serial.println("");
  Serial.println("WiFi connected");
  leds_wifiConnected();
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
}

void utils_inPlaceReverse(String &str)
{
  int n = str.length();
  // Swap character starting from two corners
  for (int i = 0; i < n / 2; i++)
  {
    char t;
    t = str[n - i - 1];
    str[n - i - 1] = str[i];
    str[i] = t;
  }
}
long last_alert = millis();
void mqtt_eventCallback(char *topic, byte *payload, unsigned int length)
{
  Serial.print("Event received [topic ");
  Serial.print(topic);
  Serial.print("]: ");

  char chrPayload[length + 1];
  String strPayload = "";
  String strAlertCounter = "";
  String strReversedPayload = "";

  for (uint i = 0; i < length; i++)
  {
    char currentChar = (char)payload[i];
    strPayload += currentChar;
    chrPayload[i] = currentChar;
    if (isDigit(currentChar))
    {
      strAlertCounter += currentChar;
    }
  }
  strPayload.replace(" ", "");
  strReversedPayload = strPayload;
  utils_inPlaceReverse(strReversedPayload);

  if (String(topic).indexOf(TOPIC_ALERT_COUNT) != -1)
  {
    int alertCounter = strAlertCounter.toInt();
    if (alertCounter > 0 && (millis() - last_alert > MIN_TIME_BETWEEN_ALERTS_MILLIS))
    {
      ///TODO: light LEDs only if alert is on in current location.
      leds_redAlert();
      last_alert = millis();
    }
  }

  if (String(topic).indexOf(TOPIC_CLIENT_SELF_TEST) != -1)
  {
    Serial.println("*self-test*");
    Serial.println(mqtt_generateClientId());
    if (strPayload == mqtt_generateClientId())
    {
      leds_rainbow(100);
      leds_fadeOut();
      delay(300);
      leds_fadeIn(CRGB::Aquamarine);
      leds_fadeOut();
      delay(300);
      leds_fadeIn(CRGB::DarkBlue);
      leds_fadeOut();
      delay(300);
      leds_fadeIn(CRGB::LightGoldenrodYellow);
      leds_fadeOut();
      delay(300);
    }
  }
  if (String(topic).indexOf(TOPIC_CLIENT_RGB) != -1)
  {
    Serial.println("*RGB*");
    long C = (long)strtol(&strPayload[2], NULL, 16);
    unsigned int R = C >> 16;
    unsigned int G = C >> 8 & 0xFF;
    unsigned int B  = C & 0xFF;

    Serial.print(R);
    Serial.print(",");
    Serial.print(G);
    Serial.print(",");
    Serial.println(B);
    CRGB color = CRGB(R, G, B);

    leds_fadeIn(color);
    leds_fadeOut();
  }

  Serial.print("strPayload: ");
  Serial.println(strPayload);
  Serial.print("strAlertCounter ");
  Serial.println(strAlertCounter);
  Serial.print("strReversedPayload: ");
  Serial.println(strReversedPayload);
}

void mqtt_init()
{
  mqttClient.setServer(mqtt_server, mqtt_port);
  mqttClient.setCallback(mqtt_eventCallback);
  Serial.println("MQTT client set-up");
}

void mqtt_reconnect()
{
  while (!mqttClient.connected())
  {
    drd.loop();

    Serial.println("trying to connect to MQTT...");

    if (mqttClient.connect(mqtt_generateClientId().c_str()))
    {
      Serial.println("mqtt connected");
      mqttClient.subscribe(TOPIC_ALERT_COUNT);
      mqttClient.subscribe(TOPIC_WARNING_COUNT);
      mqttClient.subscribe(TOPIC_ALERT);
      mqttClient.subscribe(TOPIC_WARNING);
      mqttClient.subscribe(TOPIC_CLIENT_RGB);
      mqttClient.subscribe(TOPIC_CLIENT_SELF_TEST);
      leds_mqttConnected();
      // Notify server
      //  mqttClient.publish(TOPIC_HEARTBEAT, "device connected");
    }
    else
    {
      Serial.print("failed, rc=");
      Serial.print(mqttClient.state());
      Serial.println(" try again in 5 seconds");
      delay(5000);
    }
  }
}
void utils_printHeartbeat()
{
  unsigned long now = millis();
  if (now - lastMsg > HEARTBEAT_MILLIS)
  {
    lastMsg = now;
    ++heartbeatValue;
    snprintf(msg, HEARTBEAT_MSG_BUFFER_SIZE, "heartbeat #%ld %s", heartbeatValue, mqtt_generateClientId().c_str());
    Serial.println(msg);
  }
}

void utils_printLogo()
{
  Serial.println();
  Serial.println("___  ___      _                   _ ");
  Serial.println("|  \\/  |     | |                 | |");
  Serial.println("| .  . | __ _| | _____ _ __ ____ | |");
  Serial.println("| |\\/| |/ _` | |/ / _ \\ '__|_  / | |");
  Serial.println("| |  | | (_| |   <  __/ |   / /  |_|");
  Serial.println("\\_|  |_/\\__,_|_|\\_\\___|_|  /___| (_)");
  Serial.println();
  Serial.println("Red Alert Client MaTrI0N v" + String(version));
  Serial.println("client id " + mqtt_generateClientId());
}

void utils_BlinkAlive(int r, int g, int b)
{

  if (statusLEDInterval == -1)
    return;
  if (millis() - last_blink > statusLEDInterval)
  {
    last_blink = millis();

    int steps = 75;
    int strength = 64;
    for (uint8_t i = 0; i < steps; i++)
    {
      leds[3] = CRGB(r * i / steps, g * i / steps, b * i / steps);
      FastLED.show();

      delay(10);
    };
    for (uint8_t i = steps; i > 0; i--)
    {
      leds[3] = CRGB(r * i / steps, g * i / steps, b * i / steps);
      FastLED.show();

      delay(10);
    };
    leds[3] = CRGB(0, 0, 0);
    FastLED.show();
  }
}
void utils_BlinkAlive()
{
  utils_BlinkAlive(0, 255, 0);
}
WiFiEventHandler gotIpEventHandler, disconnectedEventHandler;
bool isWifiConnected = false;
void onDisconnected(const WiFiEventStationModeDisconnected &event)
{
  isWifiConnected = false;
  statusLEDInterval = 1000;
}
void onGotIP(const WiFiEventStationModeGotIP &event)
{
  isWifiConnected = true;
  statusLEDInterval = STATUS_LED_DELAY;
}
void wifi_registerEventHandlers()
{
  disconnectedEventHandler = WiFi.onStationModeDisconnected(&onDisconnected);
  gotIpEventHandler = WiFi.onStationModeGotIP(&onGotIP);
}

void setup()
{

  Serial.begin(115200);
  Serial.println();
  delay(500);
  utils_printLogo();
  wifi_registerEventHandlers();
  leds_initStrip();
  leds_programStarted();
  fs_init();
  wifi_begin();
  wifi_printStatus();
  mqtt_init();
}

void loop()
{
  drd.loop();
  if (!mqttClient.connected())
  {
    mqtt_reconnect();
  }
  if (!isWifiConnected)
  {
    utils_BlinkAlive(255, 0, 0); //red
  }
  else
  {
    utils_BlinkAlive(); //green
  }
  mqttClient.loop();
  utils_printHeartbeat();
}
