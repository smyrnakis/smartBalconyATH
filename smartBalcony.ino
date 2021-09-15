
#include <Arduino.h>
#include <ESP8266WiFi.h>
#include "Wire.h"
#include "BH1750.h"
#include "Adafruit_SHT31.h"
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>
#include <WiFiUdp.h>
#include <NTPClient.h>
#include <ArduinoOTA.h>
#include "secrets.h"

// INPUTS
//   PIR_IN      --> D4  (!! used as BUILTIN_LED too !!)
//   BUTTONS     --> A0  (analog IN)
//   I2C sensors
//      SDA      --> D1
//      SCL      --> D2

// OUTPUTS
//   RELAY_W      --> D5  (RELAY green lamp)
//   RELAY_G      --> D6  (RELAY white lamp)
//   RELAY_WT     --> D7  (RELAY water valve)
//   BUILTIN_LED  --> D4  (!! used as PIR_IN too !!)

#define BTN_IN A0
// #define BUILTIN_LED D4 (used for PIR_IN & BUILTIN_LED)

#define RELAY_W D5
#define RELAY_G D6
#define RELAY_WT D7

#define bcd2dec(bcd_in) (bcd_in >> 4) * 10 + (bcd_in & 0x0f)
#define dec2bcd(dec_in) ((dec_in / 10) << 4) + (dec_in % 10)

char defaultSSID[]  = WIFI_DEFAULT_SSID;
char defaultPASS[]  = WIFI_DEFAULT_PASS;

char apiKey[]       = THINGSP_WR_APIKEY;
char otaAuthPin[]   = OTA_AUTH_PIN;

String httpHeader;
// String formatedTime;
char formatedTime[9];
// String formatedTimeExtended;
char formatedTimeExtended[38];
String macAddr;
String wifiSSID;
String wifiSignal;
String localIPaddress;

bool wifiAvailable    = false;
bool sht3xAvailable   = false;
bool bh1750Available  = false;

int analogValue;
int luminosity;
float humidity;
float temperature;
bool movementFlag;
int luminosity_threshold        = 20;

unsigned short blinkCounter     = 0;

unsigned long lastAnalogTime    = 0;
unsigned long lastNTPtime       = 0;
unsigned long lastRTCtime       = 0;
unsigned long lastUploadTime    = 0;
unsigned long lastSensorsTime   = 0;
unsigned long lastWiFiLostTime  = 0;
unsigned long lastWiFiCheckTime = 0;
unsigned long lastLEDblinkTime  = 0;

bool autoGreen    = true;
bool manualGreen  = false;
bool timeToGreen  = false;
bool relayGreenON = false;

bool autoWhite    = true;
bool manualWhite  = false;
bool relayWhiteON = false;

bool autoWater    = false;
bool manualWater  = false;
bool timeToWater  = false;
bool relayWaterON = false;

const int sensorsInterval     = 15000;    //  15 seconds
const int analogReadInterval  = 250;      //  250 ms
unsigned int ledBlinkInterval = 2000;     //  2 seconds

const int ntpInterval         = 3600000;  //  1 hour
const int rtcInterval         = 2000;     //  2 seconds
const int connectionKeepAlive = 2000;     //  2 seconds
const long thingSpeakInterval = 300000;   //  5 minutes

const char* thinkSpeakAPI     = "api.thingspeak.com";       // "184.106.153.149" or api.thingspeak.com

byte DS3231timeData[0x07];                // DS3231 registers (time data)
byte NTPtimeData[0x07];                   // NTP server time data

const long GMToffsetSec       = 7200;     // 3600: GMT+1 (CH) / 7200: GMT+2 (GR)
unsigned long DSToffsetSec    = 0;        // Summer time: 3600 / Winter time: 0
bool DSTupdated               = false;

String weekDay[7] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
String month[12]  = {"January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December"};

// const sunsetTime[2][12] = {
//   {17,18,18,20,20,20,20,20,19,18,17,17},  // hours
//   {30,0,30,0,25,45,40,15,40,40,15,10}     // minutes
// };

WiFiServer server(80);
WiFiClient client;
WiFiClient clientThSp;

WiFiUDP ntpUDP;
// NTPClient timeClient(ntpUDP, "pool.ntp.org", GMToffsetSec);
NTPClient timeClient(ntpUDP, "pool.ntp.org");

BH1750 lightMeter;
Adafruit_SHT31 sht31 = Adafruit_SHT31();


void setup() {

  Serial.begin(115200);
  Serial.println();
  delay(200);

  Wire.begin();
  // Wire.begin(I2C_SDA, I2C_SCL);
  Serial.println();
  delay(200);

  if (! sht31.begin(0x44)) {                             // Alternative address: 0x45
    Serial.println("[ERROR] initialising SHT31");
  }
  else {
    sht3xAvailable = true;
    Serial.println("[SUCCESS] SHT31 initialised");
  }
  delay(200);

  // lightMeter.begin();
  // if (lightMeter.begin(BH1750::CONTINUOUS_LOW_RES_MODE)) {
  if (lightMeter.begin(BH1750::CONTINUOUS_HIGH_RES_MODE)) {
    bh1750Available = true;
    Serial.println("[SUCCESS] BH1750 initialised");
  }
  else {
    Serial.println("[ERROR] initialising BH1750");
  }
  Serial.println();
  delay(200);

  WiFiManager wifiManager;
  //wifiManager.resetSettings();
  wifiManager.setConfigPortalTimeout(120);              // 2 min timeout for WiFi configuration
  wifiManager.autoConnect(defaultSSID, defaultPASS);
  
  server.begin();
  Serial.println("[SUCCESS] HTTP server started on port 80");
  delay(200);

  timeClient.begin();
  timeClient.setTimeOffset(GMToffsetSec + DSToffsetSec);
  Serial.println("[SUCCESS] NTP client started");
  delay(200);

  handleOTA();
  delay(200);

  if (WiFi.waitForConnectResult() != WL_CONNECTED) {
    wifiAvailable = false;
    Serial.println("[ERROR] connecting on WiFi - operating 'OFFLINE'");
  }
  else {
    wifiAvailable = true;
    localIPaddress = (WiFi.localIP()).toString();
    wifiSignal = String(WiFi.RSSI());
    wifiSSID = String(WiFi.SSID());
    macAddr = String(WiFi.macAddress());

    Serial.print("[SUCCESS] Connected on WiFi: ");
    Serial.println(wifiSSID);
    Serial.print("Local IP address: ");
    Serial.println(localIPaddress);
  }
  delay(200);

  pinMode(BTN_IN, INPUT);
  pinMode(BUILTIN_LED, INPUT);

  pinMode(RELAY_G, OUTPUT);
  pinMode(RELAY_W, OUTPUT);
  pinMode(RELAY_WT, OUTPUT);
  // pinMode(BUILTIN_LED, OUTPUT);

  digitalWrite(RELAY_G, HIGH);
  digitalWrite(RELAY_W, HIGH);
  digitalWrite(RELAY_WT, HIGH);
  // digitalWrite(BUILTIN_LED, HIGH);
}

void handleOTA() {
  // Port defaults to 8266
  // ArduinoOTA.setPort(8266);

  // Hostname defaults to esp8266-[ChipID]
  ArduinoOTA.setHostname("SmartBalcony");

  ArduinoOTA.setPassword((const char *)otaAuthPin);

  ArduinoOTA.onStart([]() {
    Serial.println("Start");
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
  });
  ArduinoOTA.begin();
}

void thingSpeakRequest() {
  clientThSp.stop();
  if (clientThSp.connect(thinkSpeakAPI,80)) {
    String postStr = apiKey;
    if (temperature != -100) {
      postStr +="&field1=";
      postStr += String(temperature);
    }
    if (humidity != -100) {
      postStr +="&field2=";
      postStr += String(humidity);
    }
    if (luminosity != -100) {
      postStr +="&field3=";
      postStr += String(luminosity);
    }
    // if (movementFlag != -100) {
    //   postStr +="&field4=";
    //   postStr += String(movementFlag);
    // }
    postStr += "\r\n\r\n";

    clientThSp.print("POST /update HTTP/1.1\n");
    clientThSp.print("Host: api.thingspeak.com\n");
    clientThSp.print("Connection: close\n");
    clientThSp.print("X-THINGSPEAKAPIKEY: " + (String)apiKey + "\n");
    clientThSp.print("Content-Type: application/x-www-form-urlencoded\n");
    clientThSp.print("Content-Length: ");
    clientThSp.print(postStr.length());
    clientThSp.print("\n\n");
    clientThSp.print(postStr);
    clientThSp.stop();
    Serial.println("[SUCCESS] data uploaded to thingspeak");

    lastUploadTime = millis();
  }
  else {
    Serial.println("[ERROR] could not connect to thingspeak");
  }
}

void refreshToRoot() {
  Serial.println("/refreshToRoot");
  client.println("<!DOCTYPE html><html>");
  client.println("<head>");
  client.println("<meta charset=\"UTF-8\" />");
  client.println("<meta http-equiv=\"refresh\" content=\"0;url=/\">");
  client.println("</head>");
  client.println("</html>");
}

void handleClientConnection() {

  unsigned long currentTime;
  unsigned long previousTime;
  currentTime = millis();
  previousTime = currentTime;

  String currentLine = "";                      // incoming HTML request
  bool mobileDevice = false;

  // loop while the client's connected
  while (client.connected() && currentTime - previousTime <= connectionKeepAlive) {
    currentTime = millis();

    if (client.available()) {                   // if there's bytes to read from the client,
      char c = client.read();                   // read a byte
      // Serial.write(c);                       // print it to the serial monitor
      httpHeader += c;
      if (c == '\n') {                          // if the byte is a newline character
        // if the current line is blank, you got two newline characters in a row.
        // that's the end of the client HTTP request, so send a response:

        if (currentLine.length() == 0) {
          // HTTP headers always start with a response code (e.g. HTTP/1.1 200 OK)
          // and a content-type so the client knows what's coming, then a blank line:
          client.println("HTTP/1.1 200 OK");
          client.println("Content-type:text/html");
          client.println("Connection: close");
          client.println();

          // Handle requests
          if (httpHeader.indexOf("GET /autoWhite") >= 0) {
            Serial.println("autoWhite");
            autoWhite = !autoWhite;
            refreshToRoot();
          }
          else if (httpHeader.indexOf("GET /manualWhite") >= 0) {
            Serial.println("manualWhite");
            manualWhite = !manualWhite;
            refreshToRoot();
          }
          else if (httpHeader.indexOf("GET /autoGreen") >= 0) {
            Serial.println("autoGreen");
            autoGreen = !autoGreen;
            refreshToRoot();
          }
          else if (httpHeader.indexOf("GET /manualGreen") >= 0) {
            Serial.println("manualGreen");
            manualGreen = !manualGreen;
            refreshToRoot();
          }
          else if (httpHeader.indexOf("GET /autoWater") >= 0) {
            Serial.println("autoWater");
            autoWater = !autoWater;
            refreshToRoot();
          }
          else if (httpHeader.indexOf("GET /manualWater") >= 0) {
            Serial.println("manualWater");
            manualWater = !manualWater;
            refreshToRoot();
          }
          else if (httpHeader.indexOf("GET /lumUp") >= 0) {
            Serial.println("lumUp");
            if (luminosity_threshold <= 1014) {
              luminosity_threshold += 10;
            }
            refreshToRoot();
          }
          else if (httpHeader.indexOf("GET /lumDow") >= 0) {
            Serial.println("lumDow");
            if (luminosity_threshold >= 10) {
              luminosity_threshold -= 10;
            }
            refreshToRoot();
          }
          else if (httpHeader.indexOf("GET /debug") >= 0) {
            Serial.println("/debug");
            // Send HTML web page
            client.println("<!DOCTYPE html><html>");
            client.println("<meta http-equiv=\"refresh\" content=\"5\" >\n");
            client.println("<head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">");
            client.println("<link rel=\"icon\" href=\"data:,\">");
            client.println("<title>Smart Balcony</title>");
            // CSS styling
            client.println("<style>html { font-family: Helvetica; display: inline-block; margin: 0px auto; text-align: center;}");
            // client.println("body{margin-top: 50px;} h1 {color: #B4F9F3;margin: 50px auto 30px;}");
            client.println("body {color: white; background: black;}");
            // client.println(".button { background-color: #195B6A; border: none; color: white; padding: 16px 50px;");
            client.println(".button { background-color: #195B6A; border: none; color: white; height: 50px; width: 130px;");
            client.println("text-decoration: none; text-align: center; font-size: 20px; margin: 2px; cursor: pointer;}");
            client.println(".button3 {background-color: #ff3300;}");
            client.println(".button2 {background-color: #77878A;}</style></head>");

            client.println("<body><h1>Smart Balcony</h1>");
            client.println("<body><h2>Debugging Page (auto refresh: 5\")</h2>");
            client.println("<p></p>");

            client.println("<table style=\"margin-left:auto;margin-right:auto;\">");

            client.println("<tr>");
            client.println("<td colspan=\"2\"> SENSORS </td>");
            client.println("</tr>");
            client.println("<tr>");
            client.println("<td>SHT3x OK:</td>");
            client.println("<td>" + String(sht3xAvailable) + "</td>");
            client.println("</tr>");
            client.println("<tr>");
            client.println("<td>BH1750 OK:</td>");
            client.println("<td>" + String(bh1750Available) + "</td>");
            client.println("</tr>");

            client.println("<tr>");
            client.println("<td colspan=\"2\"> </td>");
            client.println("</tr>");

            client.println("<tr>");
            client.println("<td colspan=\"2\"> INPUTS </td>");
            client.println("</tr>");
            client.println("<tr>");
            client.println("<td>Temperature:</td>");
            client.println("<td>" + String(temperature) + " &#176C</td>");
            client.println("</tr>");
            client.println("<tr>");
            client.println("<td>Humidity:</td>");
            client.println("<td>" + String(humidity) + " %</td>");
            client.println("</tr>");
            client.println("<tr>");
            client.println("<td>Luminosity:</td>");
            client.println("<td>" + String(luminosity) + " lux</td>");
            client.println("</tr>");
            client.println("<tr>");
            client.println("<td>Movement:</td>");
            String tempMove;
            if (movementFlag) {
              tempMove = "yes";
            }
            else {
              tempMove = "no";
            }
            client.println("<td>" + tempMove  + "</td>");
            client.println("</tr>");

            client.println("<tr>");
            client.println("<td colspan=\"2\"> </td>");
            client.println("</tr>");

            client.println("<tr>");
            client.println("<td colspan=\"2\"> NETWORK </td>");
            client.println("</tr>");
            client.println("<tr>");
            client.println("<td>SSID:</td>");
            client.println("<td>" + wifiSSID + "</td>");
            client.println("</tr>");
            client.println("<tr>");
            client.println("<td>Signal:</td>");
            client.println("<td>" + wifiSignal + "</td>");
            client.println("</tr>");
            client.println("<tr>");
            client.println("<td>Local IP:</td>");
            client.println("<td>" + localIPaddress + "</td>");
            client.println("</tr>");
            client.println("<tr>");
            client.println("<td>MAC:</td>");
            client.println("<td>" + macAddr + "</td>");
            client.println("</tr>");

            client.println("<tr>");
            client.println("<td colspan=\"2\"> </td>");
            client.println("</tr>");

            client.println("<tr>");
            client.println("<td colspan=\"2\"> RUNTIME </td>");
            client.println("</tr>");
            client.println("<tr>");
            client.println("<td>Time:</td>");
            // client.println("<td>" + String(formatedTime) + "</td>");
            client.println("</tr>");
            client.println("<tr>");
            client.println("<td>DST offset:</td>");
            client.println("<td>" + String(DSToffsetSec) + "</td>");
            client.println("</tr>");
            client.println("<tr>");
            client.println("<td>millis():</td>");
            client.println("<td>" + String(millis()) + "</td>");
            client.println("</tr>");
            client.println("<tr>");
            client.println("<td>Up time (millis):</td><td>");
            client.println(millisToTime(true));
            // if (millis() >= 86400000) {                           // more than one day
            //   client.println(millisToTime(true));
            // }
            // else {
            //   client.println(millisToTime(false));
            // }
            // client.println(tempUpMin,1);
            client.println("</td></tr>");
            client.println("<tr>");
            client.println("<td>PIR_IN:</td>");
            client.println("<td>" + String(digitalRead(BUILTIN_LED)) + "</td>");
            client.println("</tr>");
            client.println("<tr>");
            client.println("<td>BTN_IN:</td>");
            client.println("<td>" + String(analogValue) + "</td>");
            client.println("</tr>");
            client.println("<tr>");
            client.println("<td>RELAY_W:</td>");
            short tmpRL;
            tmpRL = map(digitalRead(RELAY_W), 0, 1, 1, 0);
            client.println("<td>" + String(tmpRL) + "</td>");
            client.println("</tr>");
            client.println("<tr>");
            client.println("<td>RELAY_G:</td>");
            tmpRL = map(digitalRead(RELAY_G), 0, 1, 1, 0);
            client.println("<td>" + String(tmpRL) + "</td>");
            client.println("</tr>");
            client.println("<tr>");
            client.println("<td>RELAY_WT:</td>");
            tmpRL = map(digitalRead(RELAY_WT), 0, 1, 1, 0);
            client.println("<td>" + String(tmpRL) + "</td>");
            client.println("</tr>");

            client.println("<tr>");
            client.println("<td colspan=\"2\"> </td>");
            client.println("</tr>");
            client.println("<tr>");

            client.println("<tr>");
            client.println("<td colspan=\"2\"> FLAGS </td>");
            client.println("</tr>");
            // client.println("<tr>");
            // client.println("<td>itIsTime_green:</td>");
            // client.println("<td>" + String(timeToGreen) + "</td>");
            // client.println("</tr>");
            // client.println("<tr>");
            // client.println("<td>itIsTime_water:</td>");
            // client.println("<td>" + String(timeToWater) + "</td>");
            // client.println("</tr>");
            client.println("<tr>");
            client.println("<td>Lux threshold:</td>");
            client.println("<td>" + String(luminosity_threshold) + "</td>");
            client.println("</tr>");
            // client.println("<tr><td> </td><td> </td></tr>");
            client.println("<tr>");
            client.println("<td>manualWhite:</td>");
            client.println("<td>" + String(manualWhite) + "</td>");
            client.println("</tr>");
            client.println("<tr>");
            client.println("<td>autoWhite:</td>");
            client.println("<td>" + String(autoWhite) + "</td>");
            client.println("</tr>");
            client.println("<tr>");
            client.println("<td>manualGreen:</td>");
            client.println("<td>" + String(manualGreen) + "</td>");
            client.println("</tr>");
            client.println("<tr>");
            client.println("<td>autoGreen:</td>");
            client.println("<td>" + String(autoGreen) + "</td>");
            client.println("</tr>");
            client.println("<tr>");
            client.println("<td>manualWater:</td>");
            client.println("<td>" + String(manualWater) + "</td>");
            client.println("</tr>");
            client.println("<tr>");
            client.println("<td>autoWater:</td>");
            client.println("<td>" + String(autoWater) + "</td>");
            client.println("</tr>");

            client.println("<tr>");
            client.println("<td colspan=\"2\"> </td>");
            client.println("</tr>");
            client.println("<tr>");

            client.println("</table>");
            client.println("<p></p>");
            client.println("<p></p>");
            client.println("<div><label for=\"debugging\">" + String(httpHeader) + "</label></div>");
            client.println("<p></p>");
            client.println("<p></p>");

            client.println("<table style=\"margin-left:auto;margin-right:auto;\">");
            client.println("<tr>");
            client.println("<td><p><a href=\"/\"><button class=\"button\">back</button></a></p></td>");
            client.println("<td><p><a href=\"/restart\"><button class=\"button button3\">RESTART</button></a></p></td>");
            client.println("</tr>");
            client.println("</table>");

            client.println("<p></p>");
            client.println("</body></html>");

            // The HTTP response ends with another blank line
            client.println();
            break;
          }
          else if (httpHeader.indexOf("GET /restart") >= 0) {
            refreshToRoot();
            delay(1000);
            ESP.restart();
          }

          if (httpHeader.indexOf("Android") >= 0) {
            Serial.println("Mobile device detected");
            mobileDevice = true;
          }

          // Send HTML web page
          client.println("<!DOCTYPE html><html>");
          // client.println("<meta http-equiv=\"refresh\" content=\"15\" >\n");
          client.println("<head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">");
          // client.println("<meta charset=\"UTF-8\" />");
          client.println("<meta charset=\"UTF-8\">");
          client.println("<link rel=\"icon\" href=\"data:,\">");
          client.println("<title>Smart Balcony</title>");
          // CSS styling
          client.println("<style>html { font-family: Helvetica; display: inline-block; margin: 0px auto; text-align: center;}");
          // client.println("body{margin-top: 50px;} h1 {color: #B4F9F3;margin: 50px auto 30px;}");
          client.println("body {color: white; background: black;}");
          // client.println(".button { background-color: #195B6A; border: none; color: white; padding: 16px 50px;");
          client.println(".button { background-color: #195B6A; border: none; color: white; height: 30px; width: 110px;");
          client.println("text-decoration: none; text-align: center; font-size: 15px; margin: 1px; cursor: pointer;}");
          client.println(".button3 {background-color: #ff3300;}");
          client.println(".button2 {background-color: #77878A;}</style></head>");

          client.println("<body><h1>Smart Balcony</h1>");
          client.println("<p></p>");

          // client.println("<p><h3>" + String(formatedTime) + "</h3></p>");
          // client.println("<p><h3>" + formatedTime + "</h3></p>");
          client.println("<p><h3>" + String(formatedTimeExtended) + "</h3></p>");
          client.println("<p></p>");

          client.println("<table style=\"margin-left:auto; margin-right:auto;\">");

          client.println("<tr>");
          client.println("<td align=\"left\"><b>Temperature:</b></td>");
          if (temperature != -100) {
            client.println("<td colspan=\"2\" align=\"right\">" + String(temperature) + " &#176C</td>");
            // client.println("<td colspan=\"2\">" + String(temperature) + " C</td>");
          }
          else {
            client.println("<td colspan=\"2\" align=\"right\"><i>ERROR</i></td>");
          }
          client.println("</tr>");

          client.println("<tr>");
          client.println("<td align=\"left\"><b>Humidity:</b></td>");
          if (humidity != -100) {
            // client.println("<td colspan=\"2\" align=\"right\">" + String(humidity) + " %</td>");
            client.println("<td colspan=\"2\" align=\"right\">" + String(humidity) + " &#37;</td>");
          }
          else {
            client.println("<td colspan=\"2\" align=\"right\"><i>ERROR</i></td>");
          }
          client.println("</tr>");

          client.println("<tr>");
          client.println("<td align=\"left\"><b>Luminosity:</b></td>");
          if (luminosity != -100) {
            client.println("<td colspan=\"2\" align=\"right\">" + String(luminosity) + " lux</td>");
          }
          else {
            client.println("<td colspan=\"2\" align=\"right\"><i>ERROR</i></td>");
          }
          client.println("</tr>");

          client.println("<tr>");
          client.println("<td align=\"left\"><b>Movement:</b></td>");
          String tempMove;
          if (movementFlag) {
              tempMove = "yes";
            }
            else {
              tempMove = "no";
            }
          client.println("<td colspan=\"2\" align=\"right\">" + tempMove  + "</td>");
          client.println("</tr>");
          client.println("</table>");

          // client.println("<tr>");
          // client.println("<td>");
          // client.println("</td>");
          // client.println("</tr>");

          client.println("<table style=\"margin-left:auto; margin-right:auto;\">");
          client.println("<tr>");
          client.println("<td align=\"left\">White lamp</td>");
          if (relayWhiteON) {
            client.println("<td><img src=\"https://www.clker.com/cliparts/L/v/b/9/1/a/3d-green-ball-th.png\" alt=\"green dot\" width=\"25\" height=\"25\"></td>");
          }
          else {
            client.println("<td><img src=\"https://www.clker.com/cliparts/z/p/6/A/G/o/reddot-th.png\" alt=\"red dot\" width=\"25\" height=\"25\"></td>");
          }
          if (autoWhite) {
            client.println("<td><p><a href=\"/autoWhite\"><button style=\"width:100%\" class=\"button\">Auto</button></a></p></td>");
          } else {
            client.println("<td><p><a href=\"/autoWhite\"><button style=\"width:100%\" class=\"button button2\">Auto</button></a></p></td>");
          }
          if (manualWhite) {
            client.println("<td><p><a href=\"/manualWhite\"><button style=\"width:100%\" class=\"button\">ON</button></a></p></td>");
          } else {
            client.println("<td><p><a href=\"/manualWhite\"><button style=\"width:100%\" class=\"button button2\">OFF</button></a></p></td>");
          }
          client.println("</tr>");

          client.println("<tr>");
          client.println("<td align=\"left\">Green lamp</td>");
          if (relayGreenON) {
            client.println("<td><img src=\"https://www.clker.com/cliparts/L/v/b/9/1/a/3d-green-ball-th.png\" alt=\"green dot\" width=\"25\" height=\"25\"></td>");
          }
          else {
            client.println("<td><img src=\"https://www.clker.com/cliparts/z/p/6/A/G/o/reddot-th.png\" alt=\"red dot\" width=\"25\" height=\"25\"></td>");
          }
          if (autoGreen) {
            client.println("<td><p><a href=\"/autoGreen\"><button style=\"width:100%\" class=\"button button2\">Auto</button></a></p></td>");
          } else {
            client.println("<td><p><a href=\"/autoGreen\"><button style=\"width:100%\" class=\"button\">Auto</button></a></p></td>");
          }
          if (manualGreen) {
            client.println("<td><p><a href=\"/manualGreen\"><button style=\"width:100%\" class=\"button button2\">OFF</button></a></p></td>");
          } else {
            client.println("<td><p><a href=\"/manualGreen\"><button style=\"width:100%\" class=\"button\">ON</button></a></p></td>");
          }
          client.println("</tr>");

          client.println("<tr>");
          client.println("<td align=\"left\">Watering system</td>");
          if (relayWaterON) {
            client.println("<td><img src=\"https://www.clker.com/cliparts/z/p/6/A/G/o/reddot-th.png\" alt=\"red dot\" width=\"25\" height=\"25\"></td>");
          }
          else {
            client.println("<td><img src=\"https://www.clker.com/cliparts/L/v/b/9/1/a/3d-green-ball-th.png\" alt=\"green dot\" width=\"25\" height=\"25\"></td>");
          }
          if (autoWater) {
            client.println("<td><p><a href=\"/autoWater\"><button style=\"width:100%\" class=\"button\">Auto</button></a></p></td>");
          } else {
            client.println("<td><p><a href=\"/autoWater\"><button style=\"width:100%\" class=\"button button2\">Auto</button></a></p></td>");
          }
          if (manualWater) {
            client.println("<td><p><a href=\"/manualWater\"><button style=\"width:100%\" class=\"button\">ON</button></a></p></td>");
          } else {
            client.println("<td><p><a href=\"/manualWater\"><button style=\"width:100%\" class=\"button button2\">OFF</button></a></p></td>");
          }
          client.println("</tr>");

          // client.println("<tr>");
          // client.println("<td align=\"left\">Lux threshold (" + String(luminosity_threshold) + ")</td>");
          // // client.println("<td>Lux threshold</td>");
          // client.println("<td><p><a href=\"/lumUp\"><button style=\"width:100%\" class=\"button\">+</button></a></p></td>");
          // client.println("<td><p><a href=\"/lumDow\"><button style=\"width:100%\" class=\"button\">-</button></a></p></td>");
          // client.println("</tr>");

          client.println("</table>");
          client.println("<p></p>");
          // client.println("<p><input type=\"checkbox\" name=\"autoRefresh\" value=\"AutoRefresh\" checked> Auto refresh ?</p>");
          // client.println("<p></p>");

          if (mobileDevice) {
            client.println("<p><iframe width=\"450\" height=\"250\" style=\"border: 0px solid #cccccc;\" src=\"https://thingspeak.com/channels/943716/charts/1?average=10&bgcolor=%232E2E2E&color=%23d62020&dynamic=true&results=144&round=1&title=Temperature+%2810%27+average%29&type=spline\"></iframe></p>");
            client.println("<p><iframe width=\"450\" height=\"250\" style=\"border: 0px solid #cccccc;\" src=\"https://thingspeak.com/channels/943716/charts/2?average=10&bgcolor=%232E2E2E&color=%232020d6&dynamic=true&results=144&round=1&title=Humidity+%2810%27+average%29&type=spline\"></iframe></p>");
            client.println("<p><iframe width=\"450\" height=\"250\" style=\"border: 0px solid #cccccc;\" src=\"https://thingspeak.com/channels/943716/charts/3?average=10&bgcolor=%232E2E2E&color=%23d6d620&dynamic=true&results=144&round=0&title=Luminosity+%2810%27+average%29&type=spline\"></iframe></p>");
          } else {
            client.println("<table style=\"margin-left:auto; margin-right:auto;\">");
            client.println("<tr>");
            client.println("<td>");
            client.println("<p><iframe width=\"450\" height=\"250\" style=\"border: 0px solid #cccccc;\" src=\"https://thingspeak.com/channels/943716/charts/1?average=10&bgcolor=%232E2E2E&color=%23d62020&dynamic=true&results=144&round=1&title=Temperature+%2810%27+average%29&type=spline\"></iframe></p>");
            client.println("</td>");
            client.println("<td>");
            client.println("<p><iframe width=\"450\" height=\"250\" style=\"border: 0px solid #cccccc;\" src=\"https://thingspeak.com/channels/943716/charts/2?average=10&bgcolor=%232E2E2E&color=%232020d6&dynamic=true&results=144&round=1&title=Humidity+%2810%27+average%29&type=spline\"></iframe></p>");
            client.println("</td>");
            client.println("<td>");
            client.println("<p><iframe width=\"450\" height=\"250\" style=\"border: 0px solid #cccccc;\" src=\"https://thingspeak.com/channels/943716/charts/3?average=10&bgcolor=%232E2E2E&color=%23d6d620&dynamic=true&results=144&round=0&title=Luminosity+%2810%27+average%29&type=spline\"></iframe></p>");
            client.println("</td>");
            client.println("</tr>");
            client.println("</table>");
          }

          client.println("<p></p>");
          client.println("</body>");
          client.println("</html>");
          client.println();     // HTTP response ends with a blank line

          break;
        }
        else {                  // if new content --> clear 'currentLine'
          currentLine = "";
        }
      }
      else if (c != '\r') {     // any input BUT a '\r' --> append it to 'currentLine'
        currentLine += c;
      }
    }
  }
  httpHeader = "";
}

void getSensorData() {
  if (sht3xAvailable) {
    humidity = sht31.readHumidity();
    temperature = sht31.readTemperature();
  }
  else {
    humidity = -100;
    temperature = -100;
  }
  if (isnan(humidity) || isnan(temperature)) {
    humidity = -100;
    temperature = -100;
  }

  if (bh1750Available) {
    luminosity = lightMeter.readLightLevel();
  }
  else {
    luminosity = -100;
  }
}

void pullNTPtime(bool printData) {
  Serial.println("[INFO] pulling time from NTP servers");

  timeClient.update();
  delay(10);

  // formatedTime = timeClient.getFormattedTime();

  time_t epochTime = timeClient.getEpochTime();

  NTPtimeData[0] = (int)timeClient.getSeconds();  
  NTPtimeData[1] = (int)timeClient.getMinutes();  
  NTPtimeData[2] = (int)timeClient.getHours();  
  NTPtimeData[3] = (int)timeClient.getDay();

  struct tm *ptm = gmtime ((time_t *)&epochTime);
  // ptm->tm_isdst = 1;
  NTPtimeData[4] = (int)ptm->tm_mday;  
  NTPtimeData[5] = (int)ptm->tm_mon + 1;  
  NTPtimeData[6] = (int)ptm->tm_year - 100;

  if (printData) {
    // Serial.println(F(">>>> NTP Webtime <<<<\n"));
    // Serial.print("epochTime: "); Serial.println(epochTime);
    // Serial.print("Seconds: "); Serial.println(NTPtimeData[0]);
    // Serial.print("Minutes: "); Serial.println(NTPtimeData[1]);
    // Serial.print("Hours:   "); Serial.println(NTPtimeData[2]);
    // Serial.print("DoW:     "); Serial.println(weekDay[NTPtimeData[3]]);
    // Serial.print("Day:     "); Serial.println(NTPtimeData[4]);
    // Serial.print("Month:   "); Serial.println(month[NTPtimeData[5]-1]);
    // Serial.print("Year:    "); Serial.println(NTPtimeData[6]);
    Serial.println(timeClient.getFormattedTime());        // format time HH:MM:SS (23:05:00)
    Serial.print("DSToffsetSec: ");
    Serial.println(DSToffsetSec);
  }
  lastNTPtime = millis();
}

void getRTCdatetime(bool printData) {
  Wire.beginTransmission (0x68);
  Wire.write (0x00); 
  Wire.endTransmission ();

  // request 7 bytes from the DS3231 and release the I2C bus
  Wire.requestFrom(0x68, 0x07, true);

  int idx = 0;

  while(Wire.available()) {    
    byte input = Wire.read();
    DS3231timeData[idx] = input;
    idx++;
  }

  if (printData) {
    Serial.println(F(">>>> DS3231 content <<<<\n"));
    Serial.print(F("DS3231 Register[0] Seconds: ")); Serial.println(bcd2dec(DS3231timeData[0]));
    Serial.print(F("DS3231 Register[1] Minutes: ")); Serial.println(bcd2dec(DS3231timeData[1]));
    Serial.print(F("DS3231 Register[2] Hours:   ")); Serial.println(bcd2dec(DS3231timeData[2]));
    Serial.print(F("DS3231 Register[3] DoW:     ")); Serial.println(weekDay[bcd2dec(DS3231timeData[3])]);
    Serial.print(F("DS3231 Register[4] Day:     ")); Serial.println(bcd2dec(DS3231timeData[4]));
    Serial.print(F("DS3231 Register[5] Month:   ")); Serial.println(month[bcd2dec(DS3231timeData[5]-1)]);
    Serial.print(F("DS3231 Register[6] Year:    ")); Serial.println(bcd2dec(DS3231timeData[6]));
    Serial.println("\n");
  }

  // output:  03:42:04        (HH:MM:SS)
  sprintf(formatedTime, "%02d:%02d:%02d" ,
    String(bcd2dec(DS3231timeData[2])),
    String(bcd2dec(DS3231timeData[1])),
    String(bcd2dec(DS3231timeData[0]))
  );

  // formatedTime =
  //   String(bcd2dec(DS3231timeData[2])) +
  //   ":" +
  //   String(bcd2dec(DS3231timeData[1])) +
  //   ":" +
  //   String(bcd2dec(DS3231timeData[0]))
  // ;

  // output:  23:05:42 Wednesday, 25 September 2021 (HH:MM:SS d, dd mm YYYY)
  sprintf(formatedTimeExtended, "%02d:%02d:%02d %s, %02d %s 20%02d",
    bcd2dec(DS3231timeData[2]),
    bcd2dec(DS3231timeData[1]),
    bcd2dec(DS3231timeData[0]),
    String(weekDay[bcd2dec(DS3231timeData[3])]),
    bcd2dec(DS3231timeData[4]),
    String(month[bcd2dec(DS3231timeData[5]-1)]),
    bcd2dec(DS3231timeData[6])
  );

  // formatedTimeExtended =
  //   String(bcd2dec(DS3231timeData[2])) +
  //   ":" +
  //   String(bcd2dec(DS3231timeData[1])) +
  //   ":" +
  //   String(bcd2dec(DS3231timeData[0])) +
  //   " " +
  //   String(weekDay[bcd2dec(DS3231timeData[3])]) +
  //   ", " +
  //   String(bcd2dec(DS3231timeData[4])) +
  //   " " +
  //   String(month[bcd2dec(DS3231timeData[5]-1)]) +
  //   " " +
  //   String(bcd2dec(DS3231timeData[6]))
  // ;

  lastRTCtime = millis();
}

void checkDST() {
  unsigned long oldDSToffset = DSToffsetSec;
  DSToffsetSec = 0;

  // get the day of the week. 0 = Sunday, 6 = Saturday
  int previousSunday = bcd2dec(DS3231timeData[4]) - bcd2dec(DS3231timeData[3]);

  // month > March AND < October --> DST on
  if (bcd2dec(DS3231timeData[5]) > 3 && bcd2dec(DS3231timeData[5]) < 10) {
    DSToffsetSec = 3600;
  }

  // if in March AND if previous Sunday was on or after the 25th --> DST on
  if (bcd2dec(DS3231timeData[5]) == 3) {                              // Today is Sunday
    if (bcd2dec(DS3231timeData[3]) == 0) {                            // and it is a Sunday on or after 25th (there can't be a Sunday in March after this)
      if (previousSunday >= 25 && bcd2dec(DS3231timeData[2]) >= 2) {  // 2:00 AM
        DSToffsetSec = 3600;
      }
    }
    // if not Sunday and the last Sunday has passed
    else if (previousSunday >= 25) {
      DSToffsetSec = 3600;
    }
  }

  // In October we must be before the last Sunday to be in DST for Europe.
  // In this case we are changing time at 2:00 AM so since the change to the previous Sunday
  // happens at midnight the previous Sunday is actually this Sunday at 2:00 AM
  // That means the previous Sunday must be on or before the 31st but after the 25th.
  if (bcd2dec(DS3231timeData[5]) == 10) {                             // October
    if (bcd2dec(DS3231timeData[3]) == 0) {                            // if today is Sunday
      if (previousSunday >= 25 && bcd2dec(DS3231timeData[2]) <= 1) {  // AND it is on or after the 25th and less than 2:00 AM
        DSToffsetSec = 3600;
      }
      else if (previousSunday < 25) {                     // if it is not yet the last Sunday
        DSToffsetSec = 3600;
      }
    }
    else if (previousSunday < 25) {                       // if it is not Sunday
      DSToffsetSec = 3600;
    }
  }

  if (oldDSToffset != DSToffsetSec) {
    timeClient.setTimeOffset(GMToffsetSec + DSToffsetSec);
    DSTupdated = true;
    Serial.print("[INFO] DST offset updated: ");
    Serial.println(DSToffsetSec);
  }
  // else {
  //   DSTupdated = false;
  // }
}

void syncRTC_NTP() {
  // if(DS3231timeData != NTPtimeData){
  if ((DS3231timeData[0] != dec2bcd(NTPtimeData[0])) ||
      (DS3231timeData[1] != dec2bcd(NTPtimeData[1])) ||
      (DS3231timeData[2] != dec2bcd(NTPtimeData[2])) ||
      (DS3231timeData[3] != dec2bcd(NTPtimeData[3])) ||
      (DS3231timeData[4] != dec2bcd(NTPtimeData[4])) ||
      (DS3231timeData[5] != dec2bcd(NTPtimeData[5])) ||
      (DS3231timeData[6] != dec2bcd(NTPtimeData[6]))
  ){
    Wire.beginTransmission (0x68);
    Wire.write (0x00);
    for (int idx = 0; idx < 7; idx++) {
      Wire.write (dec2bcd(NTPtimeData[idx]));
    }
    Wire.endTransmission ();

    Serial.println("[INFO] DS3231 time updated");
    getRTCdatetime(false);
  }
  else {
    // Serial.println("[INFO] DS3231 time is OK");
  }
}

bool checkTime_green() {                // allow GREEN: 17:00 to 1:00
  bool itIsTime_green = false;
  
  if ((bcd2dec(DS3231timeData[2]) >= 17) || (bcd2dec(DS3231timeData[2]) < 1)) {
    itIsTime_green = true;
  }

  return itIsTime_green;
}

bool checkTime_water() {
  // ~~~~~~~~~~~~~~~~~~~~~ WATER PROGRAMME ~~~~~~~~~~~~~~~~~~~~~
    // (months)  1   2   3   4   5   6   7   8   9   10  11  12 
	  //  (days) -------------------------------------------------
	  //    SU	 | 1 , 1 , 1 , 1 , 1 , 1 , 2 , 2 , 1 , 1 , 0 , 1
    //    MO	 | 2 , 2 , 2 , 2 , 2 , 3 , 3 , 3 , 2 , 2 , 2 , 2
	  //    TU	 | 0 , 0 , 0 , 0 , 0 , 1 , 1 , 1 , 1 , 0 , 0 , 0
	  //    WE	 | 1 , 1 , 1 , 2 , 2 , 1 , 2 , 3 , 1 , 1 , 0 , 1
	  //    TH	 | 0 , 0 , 0 , 0 , 0 , 2 , 1 , 1 , 2 , 0 , 2 , 0
	  //    FR	 | 2 , 2 , 2 , 2 , 2 , 1 , 3 , 3 , 1 , 2 , 0 , 2
	  //    SA	 | 0 , 0 , 0 , 0 , 1 , 2 , 1 , 1 , 2 , 0 , 1 , 0

		// 0: no water
		// 1: water for 2' @ 8:00
		// 2: water for 2' @ 8:00  &  1' @ 8:10
		// 3: water for 2' @ 8:00  &  1' @ 8:10  &  2' @ 8:20
  // ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

  // Seconds  NTPtimeData[0]
  // Minutes  NTPtimeData[1]
  // Hours    NTPtimeData[2]
  // DoW      NTPtimeData[3]  -  weekDay[NTPtimeData[3]]
  // Day      NTPtimeData[4]
  // Month    NTPtimeData[5]  -  month[NTPtimeData[5]-1])
  // Year     NTPtimeData[6]

  const short water_table[7][12] = {
  // Months: 1 . 3 . 5 . 7 . 9 . 11 .
            {1,1,1,1,1,1,2,2,1,1,0,1},    // Sunday
            {2,2,2,2,2,3,3,3,2,2,2,2},    // Monday
            {0,0,0,0,0,1,1,1,1,0,0,0},    // Tuesday
            {1,1,1,2,2,1,2,3,1,1,0,1},    // Wednesday
            {0,0,0,0,0,2,1,1,2,0,2,0},    // Thursday
            {2,2,2,2,2,1,3,3,1,2,0,2},    // Friday
            {0,0,0,0,1,2,1,1,2,0,1,0}     // Saturday        
  };

  bool itIsTime_water = false;

  if ((bcd2dec(DS3231timeData[2]) == 8) && (bcd2dec(DS3231timeData[1]) <= 23)){ 
    switch(water_table[bcd2dec(DS3231timeData[3])][(bcd2dec(DS3231timeData[5]) - 1)]) {
      case 0:
        itIsTime_water = false;
      break;
      case 1:         // 9:00 - 2 mins
        if (bcd2dec(DS3231timeData[1]) <= 1) {
          itIsTime_water = true;
        }
        else {
          itIsTime_water = false;
        }
      break;
      case 2:         // 9:00 - 2 mins, 9:10 - 1 min
        if ((bcd2dec(DS3231timeData[1]) <= 1) || (bcd2dec(DS3231timeData[1]) == 10)) {
          itIsTime_water = true;
        }
        else {
          itIsTime_water = false;
        }
      break;
      case 3:         // 9:00 - 2 mins , 9:10 - 1 min , 9:20 - 2 mins
        if ((bcd2dec(DS3231timeData[1]) <= 1) || (bcd2dec(DS3231timeData[1]) == 10) ||
          ((bcd2dec(DS3231timeData[1]) >= 20) && (bcd2dec(DS3231timeData[1]) <= 21))) {
          itIsTime_water = true;
        }
        else {
          itIsTime_water = false;
        }
      break;
      default:
        itIsTime_water = false;
      break;
    }
  }

  return itIsTime_water;
}

void serialPrintAll() {
  Serial.println();
  // Serial.println(timeClient.getFormattedTime());
  // Serial.println(formatedTime);
  Serial.println(formatedTimeExtended);
  Serial.print("Temperature:\t");
  Serial.print(String(temperature));
  Serial.println(" °C");
  // Serial.println(" C");
  Serial.print("Humidity:\t");
  Serial.print(String(humidity));
  Serial.println(" %");
  Serial.print("Luminosity:\t");
  Serial.print(luminosity);
  Serial.println(" lux");
  Serial.print("Lux threshold:\t");
  Serial.print(luminosity_threshold);
  Serial.println(" lux");
  Serial.print("Movement:\t ");
  Serial.print(movementFlag);
  Serial.println(" [0/1]");
  Serial.println();
}

void ledBlinker(unsigned short blinks) {
  if (millis() > lastLEDblinkTime + ledBlinkInterval) {
    pinMode(BUILTIN_LED, OUTPUT);

    digitalWrite(BUILTIN_LED, !digitalRead(BUILTIN_LED));

    pinMode(BUILTIN_LED, INPUT);
    lastLEDblinkTime = millis();
  }
  // if ((millis() > lastLEDblinkTime + ledBlinkInterval) && blinks > 0) {
  //   pinMode(BUILTIN_LED, OUTPUT);

  //   short flashed = 0;
  //   do {
  //     delay(10);
  //     digitalWrite(BUILTIN_LED, LOW);
  //     delay(40);
  //     digitalWrite(BUILTIN_LED, HIGH);
  //     delay(40);
  //     flashed += 1;
  //   } while (flashed < blinks);

  //   pinMode(BUILTIN_LED, INPUT);
  //   lastLEDblinkTime = millis();
  // }
}

String millisToTime(bool calcDays) {

  char outString[16];

  unsigned long millisecondsNow = millis();
  unsigned long tempTime = millisecondsNow / 1000;

  unsigned long seconds = tempTime % 60;

  tempTime = (tempTime - seconds) / 60;
  unsigned long minutes = tempTime % 60;

  tempTime = (tempTime - minutes) / 60;
  unsigned long hours = tempTime % 24;

  unsigned long days = (tempTime - hours) / 24;

  // ~~~~~~~~~~ another algorithm ~~~~~~~~~~
  // int days = n / (24 * 3600);

  // n = n % (24 * 3600);
  // int hours = n / 3600;

  // n %= 3600;
  // int minutes = n / 60 ;

  // n %= 60;
  // int seconds = n;

  if (calcDays) {
    // output:  1d 03h 42' 04"  (d HH MM SS)
    sprintf(outString, "%dd %02dh %02d' %02d\"", days,hours,minutes,seconds);
  }
  else {
    // output:  03:42:04        (HH:MM:SS)
    sprintf(outString, "%02d:%02d:%02d" ,hours,minutes,seconds);
  }

  return outString;
}

void loop(){

  ArduinoOTA.handle();

  // ~~~~~~ REBOOT DEVICE IF NO WiFi FOR 15' (900000 ms) (1h : 3600000) ~~~~~~~
  // check connection every 30"
  if (millis() > lastWiFiCheckTime + 30000) {
    if (WiFi.status() != WL_CONNECTED) {
      wifiAvailable = false;
      lastWiFiLostTime = millis();
      Serial.println("[ERROR] WiFi connection lost");
    }
    else {
      wifiAvailable = true;
    }
    lastWiFiCheckTime = millis();
  }
  if ((!wifiAvailable) && (millis() > lastWiFiLostTime + 900000)) {
    Serial.println("[ERROR] no WiFi for 15 min. Reconnecting ...");
    delay(100);
    WiFi.disconnect();
    WiFi.reconnect();
    delay(5000);

    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("[ERROR] WiFi reconnection failled. Rebooting ...");
      ESP.restart();
    }
  }
  // ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~


  // ~~~~~~~~~~~~~~~~~~~~~~~ NTP - THINGSPEAK - SERVER ~~~~~~~~~~~~~~~~~~~~~~~~
  if (wifiAvailable) {
    if (millis() > lastNTPtime + ntpInterval) {
      pullNTPtime(false);         // NTP values --> store them in the NTPtimeData[] array
    }

    if (millis() > lastUploadTime + thingSpeakInterval) {
      thingSpeakRequest();
    }

    client = server.available();

    if (client) {
      // clientsHandler();        // test on '.webpageHandler.cpp'
      handleClientConnection();
      // httpHeader = "";
      client.stop();
    }
  }
  // ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~


  // ~~~~~~~~~~~~~~~~~~~~~~~~~~ TIME CONFIGURATION ~~~~~~~~~~~~~~~~~~~~~~~~~~~~
  if (millis() > lastRTCtime + rtcInterval) {
    getRTCdatetime(false);      // RTC values --> store them in the DS3231timeData[] array

    checkDST();

    if (DSTupdated) {
      if (wifiAvailable) {
        pullNTPtime(false);
        syncRTC_NTP();          // check / sync DS3231 and NTP

        DSTupdated = false;
      }
      else {
        // TO DO:  update RTC registers if no WiFi !!!!!!!!!!!!!!
        Serial.println("[WARNING] no WiFi - NTP was not updated with new DST setting");
      }
    }
  }
  // ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~


  // ~~~~~~~~~~~~~~~~~~~~~~~~ READ SENSORS AND INPUTS ~~~~~~~~~~~~~~~~~~~~~~~~~
  if (millis() > lastSensorsTime + sensorsInterval) {
    // Serial.println("[INFO] reading sensor data...");
    getSensorData();
    lastSensorsTime = millis();
    serialPrintAll();
  }

  // used for PIR_IN
  if (digitalRead(BUILTIN_LED)) {
    movementFlag = false;
  }
  else {
    movementFlag = true;
  }

  // check buttons
  if (millis() > lastAnalogTime + analogReadInterval) {
    // Serial.println("[INFO] reading analog input...");
    analogValue = analogRead(BTN_IN);
    analogValue = map(analogValue, 0, 1024, 1024, 0);
    lastAnalogTime = millis();

    if (analogValue <= 300) {         // btn_1  (214)
      // Serial.println("[DEBUG] BTN_1 pressed");
      manualWhite = !manualWhite;
    }
    else if (analogValue <= 550) {    // btn_2  (480)
      // Serial.println("[DEBUG] BTN_2 pressed");
      manualGreen = !manualGreen;
    }
    else if (analogValue <= 850) {    // btn_3  (747)
      // Serial.println("[DEBUG] BTN_3 pressed");
      manualWater = !manualWater;
    }
  }
  // ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~


  // ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ RELAY HANDLER ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
  timeToGreen = checkTime_green();
  timeToWater = checkTime_water();

  // white lamp
  if (manualWhite || (autoWhite && movementFlag && (luminosity <= luminosity_threshold))) {
    relayWhiteON = true;
    // digitalWrite(RELAY_W, LOW);
  }
  else {
    relayWhiteON = false;
    // digitalWrite(RELAY_W, HIGH);
  }

  // green lamp
  if (manualGreen || (autoGreen && timeToGreen && (luminosity <= luminosity_threshold))) {
    relayGreenON = true;
    // digitalWrite(RELAY_G, LOW);
  }
  else {
    relayGreenON = false;
    // digitalWrite(RELAY_G, HIGH);
  }

  // watering valve
  // if (manualWater || timeToWater) {
  if (manualWater) {
    relayWaterON = true;
    // digitalWrite(RELAY_WT, LOW);
  }
  else {
    relayWaterON = false;
    // digitalWrite(RELAY_WT, HIGH);
  }

  digitalWrite(RELAY_WT, relayWhiteON);
  digitalWrite(RELAY_WT, relayGreenON);
  digitalWrite(RELAY_WT, relayWaterON);
  // ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~


  // ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ LED HANDLER ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
  if (!movementFlag) {                                  // only if NO PIR input
    if (!wifiAvailable) {
      blinkCounter      = 3;
      ledBlinkInterval  = 250;
      // ledBlinker(3);
    }
    else if (manualWater) {
      blinkCounter      = 2;
      ledBlinkInterval  = 1000;
      // ledBlinker(3);
    }
    else {
      blinkCounter      = 1;
      ledBlinkInterval  = 2000;
      // ledBlinker(1);
    }
    ledBlinker(blinkCounter);
  }
  // ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
}