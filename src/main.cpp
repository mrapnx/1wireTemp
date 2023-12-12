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
void handleRoot();
void printSensorAddresses();



// WLAN-Zugangsdaten
char ssid[] = "DEIN_WIFI_SSID";
char pass[] = "DEIN_WIFI_PASSWORT";

// MQTT-Zugangsdaten
const char *mqttServer = "DEIN_MQTT_SERVER";
const int mqttPort = 1883;
const char *mqttUser = "DEIN_MQTT_BENUTZERNAME";
const char *mqttPassword = "DEIN_MQTT_PASSWORT";

// Pin, an dem der 1-Wire-Bus angeschlossen ist
const int oneWireBus = 2; // Ändere dies entsprechend deiner Verkabelung

// Initialisiere die OneWire- und DallasTemperature-Bibliotheken
OneWire oneWire(oneWireBus);
DallasTemperature sensors(&oneWire);

// Erstelle einen Webserver auf Port 80
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

  // Suche nach angeschlossenen Sensoren
  sensors.begin();

  Serial.println("Gefundene 1-Wire-Sensoren:");
  printSensorAddresses();
}

void loop() {
  // Verarbeite HTTP-Anfragen
  WiFiClient client = server.available();
  if (client) {
    while (client.connected()) {
      if (client.available()) {
        char c = client.read();
        Serial.write(c);
      }
    }
    client.stop();
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
