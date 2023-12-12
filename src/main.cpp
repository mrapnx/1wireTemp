#include <Arduino.h>
#include <Wire.h>
#include <WiFiNINA.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <PubSubClient.h>

// Deklaration der Funktionen
void connectToWiFi();
void connectToMQTT();
void sendTemperatureToMQTT();
void printSensorAddresses();
void setup();
void printWiFiStatus();


// WLAN-Zugangsdaten
char ssid[] = "Muspelheim";
char pass[] = "dRN4wlan5309GTD9fR";

// MQTT-Zugangsdaten
const char *mqttServer = "192.168.66.21";
const int mqttPort = 1883;
const char *mqttUser = "ArudinoNano";
const char *mqttPassword = "DEIN_MQTT_PASSWORT";

// Pin, an dem der 1-Wire-Bus angeschlossen ist
const int PinOneWireBus = 2; // Ändere dies entsprechend deiner Verkabelung
byte addrArray[8];
OneWire oneWire(PinOneWireBus);
DallasTemperature sensors(&oneWire);

// Erstelle einen Webserver auf Port 80
IPAddress   ip; 
WiFiServer server(80);

// Erstelle einen MQTT-Client
WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);

void setup() {
  // Starte die serielle Kommunikation
  Serial.begin(9600);

  // Verbinde mit dem WLAN
  connectToWiFi();

  // Verbinde mit dem MQTT-Server
  connectToMQTT();

  // Initialisiere die OneWire- und DallasTemperature-Bibliotheken
  if (oneWire.search(addrArray)) {
    Serial.print("Geräte gefunden: ");
    // Serial.println(addrArray);
  } else {
    Serial.println("Keine Geräte gefunden");
  }


  // Suche nach angeschlossenen Sensoren
  sensors.begin();

  Serial.println("Gefundene 1-Wire-Sensoren:");
  printSensorAddresses();
}

void printWiFiStatus() {
  // print the SSID of the network you're attached to:
  Serial.print("SSID: ");
  Serial.println(WiFi.SSID());

  // print your WiFi shield's IP address:
  ip = WiFi.localIP();
  Serial.print("IP Address: ");
  Serial.println(ip);

  // print where to go in a browser:
  Serial.print("To see this page in action, open a browser to http://");
  Serial.println(ip);
}

void loop() {
  // Verarbeite HTTP-Anfragen
  WiFiClient client = server.available();
  if (client) {                             // if you get a client,
    Serial.println("new client");           // print a message out the serial port
    String currentLine = "";                // make a String to hold incoming data from the client
    while (client.connected()) {            // loop while the client's connected
      if (client.available()) {             // if there's bytes to read from the client,
        char c = client.read();             // read a byte, then
        Serial.write(c);                    // print it out the serial monitor
        if (c == '\n') {                    // if the byte is a newline character

          // if the current line is blank, you got two newline characters in a row.
          // that's the end of the client HTTP request, so send a response:
          if (currentLine.length() == 0) {

            // HTTP headers always start with a response code (e.g. HTTP/1.1 200 OK)
            // and a content-type so the client knows what's coming, then a blank line:
            client.println("HTTP/1.1 200 OK");
            client.println("Content-type:text/html");
            client.println();

            // the content of the HTTP response follows the header:
            client.print("<html>");
            client.print("<head>");
            client.print("<meta http-equiv=\"refresh\" content=\"1; url=http://");
            client.print(ip);
            client.print("/\"/>");
            client.print("</head>");
            client.print("<p style=\"font-size:80px; font-family: monospace\">"); 
            client.print("Sensoren: ");
            client.print("s <b><a href=\"/m\">+</a>&nbsp;&nbsp;<a href=\"/l\">-</a></b>");
            client.print("<br/>");
            client.print("<a href=\"/1\">Start</a><br/>");
            client.print("<a href=\"/0\">Stop</a><br/>");
            client.print("</p>");
            client.print("</html>");

            // The HTTP response ends with another blank line:
            client.println();

            // break out of the while loop:
            break;

          }
          else {      // if you got a newline, then clear currentLine:
            currentLine = "";
          }
        }

        else if (c != '\r') {    // if you got anything else but a carriage return character,
          currentLine += c;      // add it to the end of the currentLine
        }

        // "On"
        if (currentLine.endsWith("GET /1")) {
        }

        // "Off"
        if (currentLine.endsWith("GET /0")) {
        }

        // Zeit erhöhen, hiernach wird die Seite mit dem neuen Wert ausgeliefert
        if (currentLine.endsWith("GET /m")) {
        }

        // Zeit verringern, hiernach wird die Seite mit dem neuen Wert ausgeliefert
        if (currentLine.endsWith("GET /l")) {
        }
      }
    }

    // close the connection:
    client.stop();
    Serial.println("client disconnected");
  }


  // Aktualisiere die Temperaturdaten
  sensors.requestTemperatures();

  // Sende die Temperaturdaten an den MQTT-Server
  sendTemperatureToMQTT();

  // Verzögerung
  delay(1000);
}

void connectToWiFi() {
  Serial.println("Verbinde mit WLAN...");
  while (WiFi.begin(ssid, pass) != WL_CONNECTED) {
    delay(1000);
    Serial.println("Verbindung wird hergestellt...");
  }
  Serial.println("Verbunden mit WLAN");

  // Starte den Webserver
  server.begin();

  printWiFiStatus();
}

void connectToMQTT() {
  Serial.println("Verbinde mit MQTT-Server...");
  mqttClient.setServer(mqttServer, mqttPort);
  while (!mqttClient.connected()) {
    if (mqttClient.connect("ArduinoClient", mqttUser, mqttPassword)) {
      Serial.println("Verbunden mit MQTT-Server");
    } else {
      Serial.print("Fehlgeschlagen, rc=");
      Serial.print(mqttClient.state());
      Serial.println(" Nächster Versuch in 5 Sekunden...");
      delay(5000);
    }
  }
}

void sendTemperatureToMQTT() {
  for (int i = 0; i < sensors.getDeviceCount(); i++) {
    float tempC = sensors.getTempCByIndex(i);
    String topic = "sensor/" + String(i + 1) + "/temperature";
    String payload = String(tempC);

    mqttClient.publish(topic.c_str(), payload.c_str());
  }
}

void printSensorAddresses() {
  Serial.print("Anzahl: ");
  Serial.println(sensors.getDeviceCount());
  for (int i = 0; i < sensors.getDeviceCount(); i++) {
    DeviceAddress tempAddress;
    sensors.getAddress(tempAddress, i);

    Serial.print("Sensor ");
    Serial.print(i + 1);
    Serial.print(" Adresse: ");

    for (uint8_t j = 0; j < 8; j++) {
      if (tempAddress[j] < 16) Serial.print("0");
      Serial.print(tempAddress[j], HEX);
    }

    Serial.println();
  }
}
