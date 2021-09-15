#include <Arduino.h>
#include <ESP8266WiFi.h>
#include "Wire.h"
#include "BH1750.h"
#include "Adafruit_SHT31.h"
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>
#include <NTPClient.h>
#include <ArduinoOTA.h>
#include "secrets.h"

void clientsHandler() {

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
          else if (httpHeader.indexOf("GET /settings") >= 0) {
            // Send HTML web page
            client.println("<!DOCTYPE html><html>");
            // client.println("<meta http-equiv=\"refresh\" content=\"15\" >\n");
            client.println("<head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">");
            client.println("<link rel=\"icon\" href=\"data:,\">");
            client.println("<title>Smart Balcony</title>");
            // CSS styling
            client.println("<style>html { font-family: Helvetica; display: inline-block; margin: 0px auto; text-align: center;}");
            // client.println("body{margin-top: 50px;} h1 {color: #B4F9F3;margin: 50px auto 30px;}");
            client.println("body {color: white; background: black;}");
            // client.println(".button { background-color: #195B6A; border: none; color: white; padding: 16px 50px;");
            client.println(".button { background-color: #195B6A; border: none; color: white; height: 50px; width: 130px;");
            client.println("text-decoration: none; text-align: center; font-size: 30px; margin: 2px; cursor: pointer;}");
            client.println(".button3 {background-color: #ff3300;}");
            client.println(".button2 {background-color: #77878A;}</style></head>");

            client.println("<body><h1>Smart Balcony</h1>");
            client.println("<h2>Settings page</h2>");
            client.println("<p></p>");
            // break;
          }
          else if (httpHeader.indexOf("GET /debug") >= 0) {
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
            client.println("text-decoration: none; text-align: center; font-size: 30px; margin: 2px; cursor: pointer;}");
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
            client.println("<td>" + formatedTime + "</td>");
            client.println("</tr>");
            client.println("<tr>");
            client.println("<td>millis():</td>");
            client.println("<td>" + String(millis()) + "</td>");
            client.println("</tr>");
            float tempUpMin;
            tempUpMin = (float)(millis() / 1000.0) / 60.0;
            client.println("<tr>");
            client.println("<td>Up time (millis):</td><td>");
            client.println(tempUpMin,1);
            client.println("'</td></tr>");
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
            client.println("<div><label for=\"debugging\">" + httpHeader + "</label></div>");
            client.println("<p></p>");
            client.println("<p></p>");

            client.println("<table style=\"margin-left:auto;margin-right:auto;\">");
            client.println("<tr>");
            client.println("<td><p><a href=\"/\"><button class=\"button\">back</button></a></p></td>");
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
            delay(200);
            ESP.restart();
          }

          if (httpHeader.indexOf("Android") >= 0) {
            Serial.println("Mobile device detected");
            mobileDevice = true;
          }

          // Send HTML web page
          client.println("<!DOCTYPE html><html>");
          client.println("<meta http-equiv=\"refresh\" content=\"15\" >\n");
          client.println("<head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">");
          client.println("<link rel=\"icon\" href=\"data:,\">");
          client.println("<title>Smart Balcony</title>");
          // CSS styling
          client.println("<style>html { font-family: Helvetica; display: inline-block; margin: 0px auto; text-align: center;}");
          // client.println("body{margin-top: 50px;} h1 {color: #B4F9F3;margin: 50px auto 30px;}");
          client.println("body {color: white; background: black;}");
          // client.println(".button { background-color: #195B6A; border: none; color: white; padding: 16px 50px;");
          client.println(".button { background-color: #195B6A; border: none; color: white; height: 50px; width: 130px;");
          client.println("text-decoration: none; text-align: center; font-size: 30px; margin: 2px; cursor: pointer;}");
          client.println(".button3 {background-color: #ff3300;}");
          client.println(".button2 {background-color: #77878A;}</style></head>");

          client.println("<body><h1>Smart Balcony</h1>");
          client.println("<p></p>");

          client.println("<p><h3>" + formatedTime + "</h3></p>");
          client.println("<p></p>");

          client.println("<table style=\"margin-left:auto;margin-right:auto;\">");

          client.println("<tr>");
          client.println("<td>Temperature:</td>");
          client.println("<td colspan=\"2\">" + String(temperature) + " &#176C</td>");
          client.println("</tr>");

          client.println("<tr>");
          client.println("<td>Humidity:</td>");
          client.println("<td colspan=\"2\">" + String(humidity) + " %</td>");
          client.println("</tr>");

          client.println("<tr>");
          client.println("<td>Luminosity:</td>");
          client.println("<td colspan=\"2\">" + String(luminosity) + " lux</td>");
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
          client.println("<td colspan=\"2\">" + tempMove  + "</td>");
          client.println("</tr>");

          client.println("<tr>");
          client.println("<td>");
          client.println("</td>");
          client.println("</tr>");

          client.println("<tr>");
          client.println("<td>White lamp</td>");
          if (autoWhite) {
            client.println("<td><p><a href=\"/autoWhite\"><button class=\"button\">Auto</button></a></p></td>");
          } else {
            client.println("<td><p><a href=\"/autoWhite\"><button class=\"button button2\">Auto</button></a></p></td>");
          }
          if (manualWhite) {
            client.println("<td><p><a href=\"/manualWhite\"><button class=\"button\">ON</button></a></p></td>");
          } else {
            client.println("<td><p><a href=\"/manualWhite\"><button class=\"button button2\">OFF</button></a></p></td>");
          }
          client.println("</tr>");

          client.println("<tr>");
          client.println("<td>Green lamp</td>");
          if (autoGreen) {
            client.println("<td><p><a href=\"/autoGreen\"><button class=\"button\">Auto</button></a></p></td>");
          } else {
            client.println("<td><p><a href=\"/autoGreen\"><button class=\"button button2\">Auto</button></a></p></td>");
          }
          if (manualGreen) {
            client.println("<td><p><a href=\"/manualGreen\"><button class=\"button\">ON</button></a></p></td>");
          } else {
            client.println("<td><p><a href=\"/manualGreen\"><button class=\"button button2\">OFF</button></a></p></td>");
          }
          client.println("</tr>");

          client.println("<tr>");
          client.println("<td>Watering system</td>");
          if (autoWater) {
            client.println("<td><p><a href=\"/autoWater\"><button class=\"button\">Auto</button></a></p></td>");
          } else {
            client.println("<td><p><a href=\"/autoWater\"><button class=\"button button2\">Auto</button></a></p></td>");
          }
          if (manualWater) {
            client.println("<td><p><a href=\"/manualWater\"><button class=\"button\">ON</button></a></p></td>");
          } else {
            client.println("<td><p><a href=\"/manualWater\"><button class=\"button button2\">OFF</button></a></p></td>");
          }
          client.println("</tr>");
          client.println("</table>");
          client.println("<p></p>");
          // client.println("<p><input type=\"checkbox\" name=\"autoRefresh\" value=\"AutoRefresh\" checked> Auto refresh ?</p>");
          // client.println("<p></p>");

          if (mobileDevice) {
            client.println("<p><iframe width=\"450\" height=\"250\" style=\"border: 0px solid #cccccc;\" src=\"https://thingspeak.com/channels/943716/charts/1?average=10&bgcolor=%232E2E2E&color=%23d62020&dynamic=true&results=288&round=1&title=Temperature+%2810%27+average%29&type=spline\"></iframe></p>");
            client.println("<p><iframe width=\"450\" height=\"250\" style=\"border: 0px solid #cccccc;\" src=\"https://thingspeak.com/channels/943716/charts/2?average=10&bgcolor=%232E2E2E&color=%232020d6&dynamic=true&results=288&round=1&title=Humidity+%2810%27+average%29&type=spline\"></iframe></p>");
            client.println("<p><iframe width=\"450\" height=\"250\" style=\"border: 0px solid #cccccc;\" src=\"https://thingspeak.com/channels/943716/charts/3?average=10&bgcolor=%232E2E2E&color=%23d6d620&dynamic=true&results=288&round=0&title=Luminosity+%2810%27+average%29&type=spline\"></iframe></p>");
          } else {
            client.println("<table style=\"margin-left:auto;margin-right:auto;\">");
            client.println("<tr>");
            client.println("<td>");
            client.println("<p><iframe width=\"450\" height=\"250\" style=\"border: 0px solid #cccccc;\" src=\"https://thingspeak.com/channels/943716/charts/1?average=10&bgcolor=%232E2E2E&color=%23d62020&dynamic=true&results=288&round=1&title=Temperature+%2810%27+average%29&type=spline\"></iframe></p>");
            client.println("</td>");
            client.println("<td>");
            client.println("<p><iframe width=\"450\" height=\"250\" style=\"border: 0px solid #cccccc;\" src=\"https://thingspeak.com/channels/943716/charts/2?average=10&bgcolor=%232E2E2E&color=%232020d6&dynamic=true&results=288&round=1&title=Humidity+%2810%27+average%29&type=spline\"></iframe></p>");
            client.println("</td>");
            client.println("<td>");
            client.println("<p><iframe width=\"450\" height=\"250\" style=\"border: 0px solid #cccccc;\" src=\"https://thingspeak.com/channels/943716/charts/3?average=10&bgcolor=%232E2E2E&color=%23d6d620&dynamic=true&results=288&round=0&title=Luminosity+%2810%27+average%29&type=spline\"></iframe></p>");
            client.println("</td>");
            client.println("</tr>");
            client.println("</table>");
          }

          client.println("<p></p>");
          client.println("</body></html>");

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