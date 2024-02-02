#include <Arduino.h>
#include <avr/dtostrf.h>
#include <Wire.h>
#include <WiFiNINA.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <PubSubClient.h>
#include <SPI.h>
#include <FlashStorage_SAMD.h>
#include <TFT_ILI9163C.h> // Achtung! In der TFT_IL9163C_settings.h muss >> #define __144_BLACK_PCB__ << aktiv sein!. Offenbar ist mein Board nicht von dem Bug betroffen, von dem andere rote Boards betroffen sind. Siehe Readme der TFT_IL9163 Lib.
#include <DS2438.h>
#include "sensors.h"
#include "wifi.h"

// *************** Konfig-Grundeinstellungen
typedef struct {
  char    head           [5] = "MRAb";
  // WLAN
  boolean wifiActive         = false;  // MRA: Debug
  char    wifiMode           = 'a'; // a = Access Point / c = Client
  int     wifiTimeout        = 10;
  char    wifiSsid      [21] = "ArduinoAP";
  char    wifiPass      [21] = "ArduinoAP";

  // MQTT-Zugangsdaten
  boolean mqttActive         = false;
  char    mqttServer    [21] = "127.0.0.1"; // war: char *mqttServer
  int     mqttPort           = 1883;
  char    mqttName      [21] = "ArduinoClient";
  char    mqttUser      [21] = "ArudinoNano";
  char    mqttPassword  [21] = "DEIN_MQTT_PASSWORT";
  
  // Sensor-Konfiguration
  SensorConfig sensorConfig[sensorConfigCount];  

  char    foot           [5] = "MRAe";
} Config;

// WLAN-Port
const int wifiPort = 80; // Port, auf den der HTTP-Server lauscht


// Display
                      // Rot   LED   +3.3V
#define TFT_CLK  13   // Gelb  SCK   D13
#define TFT_MOSI 11   // Braun SDA   D11
#define TFT_DC   4    // Grün  A0    D5
#define TFT_RST  3    // Lila  RES   D2
#define TFT_CS   5    // Blau  CS    D3
                      // Schw. GND   GND
                      // Rot   VCC   +3.3V                     
#define TFT_MISO 12   //   - D12
int     xBegin     = 0;
int     yBegin     = 10;


// 1-Wire
const int PinOneWireBus = 10; // Pin, an dem der 1-Wire-Bus angeschlossen ist // = D10

// Frequenz in Sekunden, in der die WLAN-Verbindung versucht wird
const int wifiCheckInterval = 10;
// Frequenz in Sekunden, in der die Temperaturen abgefragt werden
const int tempCheckInterval = 5;
// Frequenz in Sekunden, in der die Füllstände abgefragt werden
const int levelCheckInterval = 2;
// Frequenz in Sekunden, in der die Temperaturen an MQTT gesendet werden
const int sendInterval = 10;
// Frequenz in Sekunden, in der die Temperaturen angezeigt werden
const int displayInterval = 10;
// Frequenz, in der die orange LED bei Fehlern blinkt in ms
const int blinkInterval = 500; 

// ***************  Globale Variablen
unsigned long tempCheckLast = 0;
unsigned long levelCheckLast = 0;
unsigned long sendLast = 0;
unsigned long displayLast = 0;
unsigned long wifiCheckLast = 0;
unsigned long blinkLast = 0;
boolean blinking = false;

// 1-Wire
SensorData* sensorList = nullptr;         // Zeiger auf das Array von SensorData
int numberOfSensors = 0;                  // Aktuelle Anzahl von Sensoren
OneWire oneWire(PinOneWireBus);           // 1-Wire Grundobjekt
DallasTemperature sensors(&oneWire);      // 1-Wire Objekt für Temperatursensoren

// Webserver
IPAddress   ip; 
WiFiServer  server(wifiPort);
WiFiClient  client; 
int         status = WL_IDLE_STATUS;

// MQTT-Client
WiFiClient   wifiClient;
PubSubClient mqttClient(wifiClient);

// Display
TFT_ILI9163C tft = TFT_ILI9163C(TFT_CS, TFT_DC, TFT_RST);

// Speicher
FlashStorage(configStorage, Config);
Config config;

// *************** Deklaration der Funktionen
boolean sensorsFine();
boolean wifiFine();
boolean checkWiFi();
boolean connectToMQTT();
boolean loadConfig();
void saveConfig();
void printConfig(Config &pconfig);
void copyConfig(const Config &from, Config &to);

// Sensorlisten-Methoden
void addSensor(const SensorAddress address, const SensorName name, const SensorType type, const float value);
void removeSensor(SensorAddress address);
boolean updateSensorValue(const SensorAddress address, const float value);
void clearSensorList();
void getSensorName(const SensorAddress address, SensorName); 
boolean getSensorType(const SensorAddress address, SensorType& type);

// Ein- & Ausgabe-Methoden
void updateTemperatures();
void updateLevels();
void printSensors();
void printSensorAddresses();
void printWiFiStatus();
void displayValues(); 
void sendTemperaturesToMQTT();
char* getValue(const String& data, const char* key);
String getValuesAsHtml();
void htmlGetHeader(int refresh);
void htmlGetStatus();
void htmlGetConfig();
void htmlSetConfig();
void httpProcessRequests();

//  Setup-Methoden
void setup1Wire(); 
void setupDisplay();
void setupMemory();
void setupWifi();
void setup();

// ***************  Funktionen
char* getValue(const String& data, const char* key) {
  static char result[100]; // Annahme: Der Wert passt in einen 100-Byte-Puffer
  String delimiter = "=";
  int keyIndex = data.indexOf(key + delimiter);
  int endIndex;

  Serial.print("getValue(");
  Serial.print(key);
  Serial.println(") begin");
  if (keyIndex == -1) {
    return nullptr;  // Wenn der Schlüssel nicht gefunden wurde, leeres C-String zurückgeben
  }

  keyIndex += strlen(key) + delimiter.length();

  endIndex = data.indexOf('&', keyIndex);
  if (endIndex == -1) {
    endIndex = data.length();
  }

  data.substring(keyIndex, endIndex).toCharArray(result, sizeof(result));

  // Füge den nullterminierenden Zeichen manuell hinzu
  result[sizeof(result) - 1] = '\0';

  Serial.print("getValue() end: ");
  Serial.println(result);
  return result;
}

void copyConfig(const Config &from, Config &to) {
  Serial.println("copyConfig() begin");
  // WiFi
         to.wifiActive   = from.wifiActive;
         to.wifiMode     = from.wifiMode;
         to.wifiTimeout  = from.wifiTimeout;
  strcpy(to.wifiSsid,      from.wifiSsid);
  strcpy(to.wifiPass,      from.wifiPass);

  // MQTT
         to.mqttActive   = from.mqttActive;
  strcpy(to.mqttServer,    from.mqttServer);
  strcpy(to.mqttUser,      from.mqttUser);
         to.mqttPort     = from.mqttPort;
  strcpy(to.mqttName,      from.mqttName);
  strcpy(to.mqttPassword,  from.mqttPassword);
  
  // Sensor-Konfig
  for (int i = 0; i < sensorConfigCount; i++) {
    strcpy(to.sensorConfig[i].address, from.sensorConfig[i].address);  
    strcpy(to.sensorConfig[i].name,    from.sensorConfig[i].name);  
    strcpy(to.sensorConfig[i].format,  from.sensorConfig[i].format);  
  }
  Serial.println("copyConfig() end");
};

void printConfig(Config &pconfig) {
  Serial.println("printConfig() begin");

  Serial.print("wifiActive: ");
  Serial.println(int(pconfig.wifiActive));

  Serial.print("ssid: ");
  Serial.println(pconfig.wifiSsid);

  Serial.print("pass: ");
  Serial.println(pconfig.wifiPass);

  Serial.print("mode: ");
  Serial.println(pconfig.wifiMode);

  Serial.print("wifiTimeout: ");
  Serial.println(pconfig.wifiTimeout);

  Serial.print("mqttActive: ");
  Serial.println(int(pconfig.mqttActive));

  Serial.print("mqttServer: ");
  Serial.println(pconfig.mqttServer);

  Serial.print("mqttPort: ");
  Serial.println(pconfig.mqttPort);

  Serial.print("mqttName: ");
  Serial.println(pconfig.mqttName);

  Serial.print("mqttUser: ");
  Serial.println(pconfig.mqttUser);

  Serial.print("mqttPassword: ");
  Serial.println(pconfig  .mqttPassword);

  for (int i = 0; i < sensorConfigCount; i++) {
    Serial.print("sensorConfig");  
    Serial.print(i);  
    Serial.print(": Address: ");  
    Serial.print(pconfig.sensorConfig[i].address);  
    Serial.print(" Name: ");  
    Serial.print(pconfig.sensorConfig[i].name);  
    Serial.print(" Format: ");  
    Serial.println(pconfig.sensorConfig[i].format);  
  }

  Serial.println("printConfig() end");
}


void clearSensorList() {
  // Befreie den Speicher von sensorList
  free(sensorList);

  // Setze die Anzahl der Sensoren auf 0
  numberOfSensors = 0;

  // Setze den Zeiger auf null, um sicherzustellen, dass er nicht auf ungültigen Speicher zeigt
  sensorList = nullptr;
}

void saveConfig() {
  Serial.println("saveConfig() begin");

  Serial.println("Speichere Konfig:");
  printConfig(config);
  EEPROM.put(0, config);

  if (!EEPROM.getCommitASAP()) {
    Serial.println("CommitASAP nicht gesetzt, führe commit() aus.");
    EEPROM.commit();
  }
  Serial.println("saveConfig() end");
}


boolean loadConfig() {
  Config tempConfig;
  boolean returnValue = false;

  Serial.println("loadConfig() begin");
  Serial.println("Lade Konfig");
  // Lies den EEPROM bei Adresse 0 aus
  EEPROM.get(0, tempConfig);
  // Die Struct enthält als erstes immer den C-String "MRA-b" und als letztes immer "MRA-e". Prüfe darauf.
  if (strcmp(tempConfig.head, "MRAb") == 0 && (strcmp(tempConfig.foot, "MRAe") == 0)) {
    Serial.println("Konfig erfolgreich geladen:");
    copyConfig(tempConfig, config);
    printConfig(config);
    returnValue = true;
  } else {
    Serial.println("Konfig konnte nicht geladen werden, habe folgendes erhalten:");
    printConfig(tempConfig);
    returnValue = false;
  }
  Serial.println("loadConfig() end");
  return returnValue;
}

void htmlGetHeader(int refresh) {
  // HTTP headers always start with a response code (e.g. HTTP/1.1 200 OK)
  // and a content-type so the client knows what's coming, then a blank line:
  client.println("HTTP/1.1 200 OK");
  client.println("Content-type:text/html");
  client.println();

  // the content of the HTTP response follows the header:
  client.print("<html>");
  client.print("<head>");
  if (refresh > 0) {
    client.print("<meta http-equiv=\"refresh\" content=\"");
    client.print(refresh);
    client.print("; url=http://");
    client.print(ip); 
    client.print("/\"/>");
  }
  client.print("</head>");
}


void htmlGetConfig() {
  htmlGetHeader(0);
  client.print("<html><body>");
  client.print("<h1>Konfiguration</h1>");
  client.print("<form method='post' action='/update'>");
  client.print("<p>WLAN aktiv: <input type='checkbox' name='wifiActive' " + String(config.wifiActive ? "checked" : "") + "></p>");
  client.print("<p>SSID: <input type='text' name='wifiSsid' value='" + String(config.wifiSsid) + "'></p>");
  client.print("<p>Passwort: <input type='text' name='wifiPass' value='" + String(config.wifiPass) + "'></p>");
  client.print("<p>Modus (a=Access Point, c=Client): <input type='text' name='wifiMode' value='" + String(config.wifiMode) + "'></p>");
  client.print("<p>WLAN Timeout: <input type='text' name='wifiTimeout' value='" + String(config.wifiTimeout) + "'></p>");

  client.print("<p>MQTT aktiv: <input type='checkbox' name='mqttActive' " + String(config.mqttActive ? "checked" : "") + "></p>");
  client.print("<p>MQTT Server: <input type='text' name='mqttServer' value='" + String(config.mqttServer) + "'></p>");
  client.print("<p>MQTT Port: <input type='text' name='mqttPort' value='" + String(config.mqttPort) + "'></p>");
  client.print("<p>MQTT Name: <input type='text' name='mqttName' value='" + String(config.mqttName) + "'></p>");
  client.print("<p>MQTT Benutzer: <input type='text' name='mqttUser' value='" + String(config.mqttUser) + "'></p>");
  client.print("<p>MQTT Passwort: <input type='text' name='mqttPassword' value='" + String(config.mqttPassword) + "'></p>");

  client.print("<p/>");
  client.print("<table>");
  client.print("<th></th>");
  client.print("<th>Adresse</th>");
  client.print("<th>Name</th>");
  client.print("<th>Format</th>");
  for (int i = 0; i < sensorConfigCount; i++) {
    client.print("<tr>");  
    client.print("<td>" + String(i) + "</td>");  
    client.print("<td><input type='text' name='sensorAddress" + String(i) + "' value='" + String(config.sensorConfig[i].address) + "'></td>");  
    client.print("<td><input type='text' name='sensorName"    + String(i) + "' value='" + String(config.sensorConfig[i].name)    + "'></td>");  
    client.print("<td><input type='text' name='sensorFormat"  + String(i) + "' value='" + String(config.sensorConfig[i].format)  + "'></td>");  
    client.print("</tr>");  
  }
  client.print("</table>");


  client.print("<input type='submit' value='Speichern'>");
  client.print("</form>");
  client.print("<br/>");
  client.print("<br/>");
  // client.print("<a href=\"/reboot\">Neustart</a>"); // TODO: Klappt noch nicht
  client.print("<br/>");
  client.print("<br/>");
  client.print("<a href=\"/\">Zur&uuml;ck</a>");
  client.print("<br/>");
  client.print("<br/>");
  client.print("<a href=\"load\">Konfig laden</a>");
  client.print("</body></html>");
}

void htmlSetConfig() {
  char c;
  char no[2];
  char name[17];
  Serial.println("htmlSetConfig() begin");
  // Lese den HTTP-Body, der die aktualisierten Daten enthält
  String body = "";
  while (client.available()) {
    c = client.read();
    body += c;
  }
  Serial.print("body: ");
  Serial.println(body);

  // Extrahiere die aktualisierten Werte aus dem HTTP-Body
  config.wifiActive = body.indexOf("wifiActive=on") != -1;

  strcpy(config.wifiSsid, getValue(body, "wifiSsid"));
  strcpy(config.wifiPass, getValue(body, "wifiPass"));
  config.wifiMode = getValue(body, "wifiMode")[0];
  config.wifiTimeout = atoi(getValue(body, "wifiTimeout")); // Umwandlung in Integer

  config.mqttActive = body.indexOf("mqttActive=on") != -1;
  strcpy(config.mqttServer, getValue(body, "mqttServer"));
  Serial.print("mqttServer: ");
  Serial.println(config.mqttServer);
  config.mqttPort = atoi(getValue(body, "mqttPort")); // Umwandlung in Integer
  strcpy(config.mqttName, getValue(body, "mqttName"));
  strcpy(config.mqttUser, getValue(body, "mqttUser"));
  strcpy(config.mqttPassword, getValue(body, "mqttPassword"));

  for (int i = 0; i < sensorConfigCount; i++) {
    itoa(i, no, 10);

    strcpy(name, "sensorAddress");
    strcat(name, no);
    strcpy(config.sensorConfig[i].address, getValue(body, name));

    strcpy(name, "sensorName");
    strcat(name, no);
    strcpy(config.sensorConfig[i].name, getValue(body, name));

    strcpy(name, "sensorFormat");
    strcat(name, no);
    strcpy(config.sensorConfig[i].format, getValue(body, name));
  }

  saveConfig();
  Serial.println("htmlSetConfig() begin");
}

void htmlGetStatus() {
  htmlGetHeader(2);
  // client.print("<html><body>");  // ohne  korrektem html und body passt die Schriftgröße irgendwie immer
  client.print("<p style=\"font-size:80px; font-family: monospace\">"); 
  client.print("Sensoren: </br>");
  client.print(getValuesAsHtml());
  client.print("<br/>");
  client.print("<a href=\"/config\">Konfiguration</a><br/>");
  client.print("</p>");
  client.print("</html>");

  // Die HTTP response endet mit einer weiteren Leerzeile:
  client.println();
}

void httpProcessRequests() {
String currentLine = "";
char c;

  // Vergleich den aktuellen mit dem vorherigen Status
  if (status != WiFi.status()) {
    // Wenn sie sich geändert hat, persistiere den Zustand
    status = WiFi.status();
    if (status == WL_AP_CONNECTED) {
      Serial.println("Device connected to AP");
    } else {
      Serial.println("Device disconnected from AP");
    }
  }

  // Verarbeite HTTP-Anfragen
  client = server.available();
  // Wenn sich ein Client verbunden hat,
  if (client) {                             
    Serial.println("httpProcessRequests() new client");           
    // Solange der Client verbunden ist,
    while (client.connected()) {            
      // und wenn Daten vom Client vorliegen
      if (client.available()) {             
        // Lies ein Byte
        c = client.read();
        // und gibt es seriell aus                  
        Serial.write(c);                    
        // wenn das Byte ein "Newline" ist,
        if (c == '\n') {                    
          // Wenn die aktuelle Zeile leer ist, haben wir zwei Newlines hintereinander und damit das Ende des Requests
          if (currentLine.length() == 0) {
            Serial.println("GET / => Status");
            htmlGetStatus();
            break;
          }
          else {      
            // Für jede weitere Zeile leeren wir currentLine
            currentLine = "";
          }
        }
        // Falls wir irgendwas anderes als einen CR erhalten,
        else if (c != '\r') {    
          // Fügen wir das Zeichen currentLine hinzu
          currentLine += c;  
        }

        // "Konfiguration anzeigen"
        if (currentLine.endsWith("GET /config")) {
          Serial.println("GET /config => Config");
          htmlGetConfig();
          break;
        }

        // "Konfiguration laden"
        if (currentLine.endsWith("GET /load")) {
          Serial.println("GET /load => Load Config");
          htmlGetHeader(0);
          client.print("<html><body>");
          if (loadConfig()) {
            client.print("Konfig erfolgreich geladen<br/><br/>");
          } else {
            client.print("Konfig konnte nicht geladen werden<br/><br/>");
          }          
          client.print("<a href=\"/\">Zur&uuml;ck</a>");
          client.print("</body></html>");
          break;
        }

        // "Konfiguration speichern"
        if (currentLine.indexOf("POST") != -1) {
          htmlSetConfig();
          htmlGetConfig();
          break;
        }                      

        // "Neustarten"
        if (currentLine.endsWith("GET /reboot")) {
          //reboot(); TODO: Klappt noch nicht
        }

      } else {
        // Serial.println("Nothing to read from client");
      }
    }

    // Verbindung schließen
    client.stop();
    Serial.println("Client getrennt");
  } else {
    // Kein neuer Client
  }

}

void printSensors() {
  Serial.println("Anzahl Sensoren im Array: " + String(numberOfSensors));
  for (int i = 0; i < numberOfSensors; i++) {
    Serial.print("Sensor " + String(i) + " Adresse: ");
    Serial.print(sensorList[i].address);
    Serial.print(" Name: ");
    Serial.print(sensorList[i].name);
    Serial.print(" Typ: ");
    Serial.print(sensorList[i].type);
    Serial.print(" Wert: ");
    Serial.println(sensorList[i].value);
  }
}

void addSensor(const SensorAddress address, const SensorName name, const SensorType type, const float value) {
  SensorData*   tempArray = (SensorData*)malloc((numberOfSensors + 1) * sizeof(SensorData));
  DeviceAddress tempDs2438DeviceAddress;

  Serial.println("addSensor(): begin");

  // Übertrage vorhandene Daten in das temporäre Array
  for (int i = 0; i < numberOfSensors; i++) {
    tempArray[i] = sensorList[i];
  }

  // Füge das neue Sensorobjekt hinzu
  Serial.print("  Füge Sensor ");
  Serial.print(address);
  Serial.println(" hinzu");
  strcpy(tempArray[numberOfSensors].address, address);
  strcpy(tempArray[numberOfSensors].name, name);
  tempArray[numberOfSensors].type = type;
  tempArray[numberOfSensors].value = value;
  strToDeviceAddress(String(address), tempDs2438DeviceAddress);
  copyDeviceAddress(tempDs2438DeviceAddress, tempArray[numberOfSensors].deviceAddress);

  // Erhöhe die Anzahl der Sensoren
  numberOfSensors++;

  // Befreie den alten Speicher
  free(sensorList);

  // Weise den neuen Speicher zu
  sensorList = tempArray;

  Serial.println("addSensor(): end");
}

boolean updateSensorValue(const SensorAddress address, const float value) {
   for (int i = 0; i < numberOfSensors; i++) {
     if (strcmp(sensorList[i].address, address) == 0) {
       sensorList[i].value = value;
       return true; 
     }
   }
   return false;
}

boolean getSensorType(const SensorAddress address, SensorType& type) {
   for (int i = 0; i < numberOfSensors; i++) {
     if (strcmp(sensorList[i].address, address) == 0) {
       type = sensorList[i].type;
       return true; 
     }
   }
   return false;
}

void getSensorName(const SensorAddress address, SensorName name) {
  strcpy(name, "");
  // Tacker durch das sensorConfig Array in config durch
  for (int i = 0; i < sensorConfigCount; i++) {
    // Vergleich die Adresse aus dem Parameter mit der in der Config
    if (strcmp(address, config.sensorConfig[i].address) == 0) {
      // Und gib bei Übereinstimmung den Namen zurück
      strcpy(name, config.sensorConfig[i].name);
      Serial.print("getSensorName(): Sensor gefunden: Adresse=");
      Serial.print(address);
      Serial.print(" name=");
      Serial.println(name);
      break;
    }
  }
}

void removeSensor(SensorAddress address) {
  for (int i = 0; i < numberOfSensors; i++) {
    if (sensorList[i].address == address) {
      // Sensor gefunden, löschen, indem die nachfolgenden Elemente verschoben werden
      for (int j = i; j < numberOfSensors - 1; j++) {
        sensorList[j] = sensorList[j + 1];
      }

      // Verringere die Anzahl der Sensoren
      numberOfSensors--;

      // Reduziere die Speichergröße
      SensorData* tempArray = (SensorData*)malloc(numberOfSensors * sizeof(SensorData));

      // Übertrage Daten in das temporäre Array
      for (int k = 0; k < numberOfSensors; k++) {
        tempArray[k] = sensorList[k];
      }

      // Befreie den alten Speicher
      free(sensorList);

      // Weise den neuen Speicher zu
      sensorList = tempArray;

      break;
    }
  }
}

void setupDisplay() {
  Serial.println("setupDisplay() begin");
  tft.begin();
  tft.setBitrate(24000000);
  tft.clearScreen();
  tft.defineScrollArea(128, 128);
  tft.setCursor(xBegin, yBegin);
  tft.println("Start");
  Serial.println("setupDisplay() end");
}

boolean wifiFine() {
  switch(config.wifiMode) {
    case 'c': 
      return WiFi.status() == WL_CONNECTED; // Positiver Zustand als Client
    case 'a': 
      return (WiFi.status() == WL_AP_CONNECTED || WiFi.status() == WL_AP_LISTENING); // Positive Zustände als AP
    default: 
      return false;
  }
}

boolean sensorsFine() {
 return sensors.getDeviceCount() > 0;
}

void blink() {
  if (wifiFine() && sensorsFine()) {
    digitalWrite(LED_BUILTIN, HIGH);
    return;
  } else {
    // Brich ab, wenn unser Inverall noch nicht erreicht ist
    if (millis() < blinkLast + (blinkInterval)) {
      return;
    }
    blinkLast = millis();
    digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
  }
}

void updateLevels() {
  // Brich ab, wenn unser Inverall noch nicht erreicht ist
  if (millis() < levelCheckLast + (levelCheckInterval * 1000)) {
    return;
  }
  Serial.println("updateLevels() begin");
  levelCheckLast = millis();

  for (int i = 0; i < numberOfSensors; i++) {
    if (sensorList[i].type == 'b') {
      DS2438 ds2438(&oneWire, sensorList[i].deviceAddress);
      ds2438.begin();
      Serial.print("  Sensor DS2438 ");
      Serial.print(sensorList[i].address);
      Serial.print(" Adresse ");
      Serial.print((unsigned int)&ds2438, HEX);
      ds2438.update();
      if (ds2438.isError()) {
        Serial.print(" erfolglos abgefragt"); 
      } else {
        Serial.print(" erfolgreich abgefragt");
        updateSensorValue(sensorList[i].address, ds2438.getVoltage(DS2438_CHA));
      }
      Serial.print(": Timestamp: ");
      Serial.print(ds2438.getTimestamp());
      Serial.print(": Temperatur = ");
      Serial.print(ds2438.getTemperature(), 1);
      Serial.print("C, Kanal A = ");
      Serial.print(ds2438.getVoltage(DS2438_CHA), 1); // Pin 1
      Serial.print("v, Kanal B = ");
      Serial.print(ds2438.getVoltage(DS2438_CHB), 1);
      Serial.println("v.");
      //delete &ds2438;  // MRA: Keine Ahnung warum, aber das führt zu nem Freeze
    }
  }
  Serial.println("updateLevels() end");
}

void updateTemperatures() {
  // Brich ab, wenn unser Inverall noch nicht erreicht ist
  if (millis() < tempCheckLast + (tempCheckInterval * 1000)) {
    return;
  }
  Serial.println("updateTemperatures() begin");
  tempCheckLast = millis();

  // Aktualisiere die Temperaturdaten
  Serial.println("  Aktualisiere Temperaturen");
  sensors.requestTemperatures();

  // Iteriere durch alle Sensoren
  for (int i = 0; i < numberOfSensors; i++) {
    if (sensorList[i].type == 't') {
      // Aktualisiere die Liste
      updateSensorValue(sensorList[i].address, sensors.getTempC(sensorList[i].deviceAddress));
    }
  }
  Serial.println("updateTemperatures() end");
}

void setup1Wire() {
  byte          addrArray[8];
  String        address;
  SensorAddress addressC;
  SensorName    nameC;
  SensorType    typeC;
  DeviceAddress addr;

  Serial.println("setup1Wire() begin");
    // Initialisiere die OneWire- und DallasTemperature-Bibliotheken
  if (oneWire.search(addrArray)) {
  } else {
    tft.println("Keine Geräte gefunden");
    Serial.println("  Keine Geräte gefunden");
  }

  // Starte Objekt für Temperatur-Sensoren
  sensors.begin();

  // Leere die Liste
  clearSensorList();

  Serial.println("  Gefundene 1-Wire-Sensoren:");
  tft.println("Gefundene 1-Wire-Sensoren:");
  printSensorAddresses();

  // Iteriere durch alle Sensoren
  for (int i = 0; i < sensors.getDeviceCount(); i++) {
    
    // Ermittle die Adresse
    Serial.println("  Ermittle Adresse Sensor " + String(i));
    sensors.getAddress(addr, i); 
    address = deviceAddressToStr(addr);
    strcpy (addressC, address.c_str());
    Serial.println("  address: " + address);
    Serial.print("  addressC: ");
    Serial.println(addressC);

    // Ermittle den Namen
    Serial.println("  Ermittle Name Sensor " + String(i));
    getSensorName(addressC, nameC);
    Serial.print("  Name Sensor " + String(i) + ": ");
    Serial.println(nameC);

    // Ermittle den Typ
    Serial.println("  Ermittle Typ Sensor " + String(i));
    if (getSensorTypeByAddress(addressC, typeC) == true) {
      Serial.println("  Typ erfolgreich ermittelt");
    } else {
      Serial.println("  Typ nicht erfolgreich ermittelt");
    }
    Serial.print("  Typ Sensor " + String(i) + ": ");
    Serial.println(typeC);

    addSensor(addressC, nameC, typeC, 0);
  }  
  updateTemperatures();
  updateLevels();
  Serial.println("setup1Wire() end");
}

String getValuesAsHtml() {
// TODO: Informationen aus der sensorList beziehen
  String address;
  String temp;
  DeviceAddress addr;
  String returnString = "";
  float tempC;

  Serial.println("getValuesAsHtml() begin");
  for (int i = 0; i < sensors.getDeviceCount(); i++) {
    tempC = sensors.getTempCByIndex(i);
    sensors.getAddress(addr, i);
    address = deviceAddressToStr(addr);
    temp = String(tempC);
    returnString = returnString + address.substring(0, address.length()-2) + "-<b>" + address.substring(address.length()-2) + "</b>: " + temp + " &#8451;</br>";
    Serial.println("  Ermittle Inhalt für Webserver: " + returnString);
  }
  return returnString;
  Serial.println("getValuesAsHtml() end");
}

void setup() {
  // Starte die serielle Kommunikation
  Serial.begin(9600);
  delay(500);
  Serial.println("setup() begin");
  pinMode(LED_BUILTIN, OUTPUT);

  // Konfig
  setupMemory();
  loadConfig(); // Achtung! Schlägt direkt nach dem Upload fehl

  // Display
  setupDisplay();

  // Stelle Verbindung mit dem WLAN her
  setupWifi();
  checkWiFi();

  // Verbinde mit dem MQTT-Server
  connectToMQTT();

  // Öffne den 1-Wire Bus
  setup1Wire();

  printSensors();

  tft.clearScreen();
}

void printWiFiStatus() {
  Serial.println("printWiFiStatus() begin");

  Serial.print("  SSID: ");
  Serial.println(WiFi.SSID());

  if (config.wifiMode == 'a') {
    Serial.print("  Password: ");
    Serial.println(config.wifiPass);
  };

  ip = WiFi.localIP();
  Serial.print("  IP Address: ");
  Serial.println(ip);

  Serial.print("Server Status: ");
  Serial.println(server.status());
  Serial.println("printWiFiStatus() end");
}

boolean checkWiFi() {
  // Prüfe, ob WiFi überhaupt aktiviert tist
  if (!config.wifiActive) {
    return true;
  }
  // Ermittle den Zeitpunkt, bis zu dem der Verbindungsversuch dauern darf
  unsigned int deadline = millis() + (config.wifiTimeout * 1000);

  // Brich ab, wenn unser Inverall noch nicht erreicht ist
  if (millis() < wifiCheckLast + (wifiCheckInterval * 1000)) {
    return false;
  }
  wifiCheckLast = millis();

  Serial.println("checkWiFi() Prüfe, ob WLAN Verbindung besteht");

  // Wenn WLAN nicht verbunden ist,
  if (!wifiFine()) {
    Serial.println("checkWiFi() WLAN Verbindung besteht nicht");
    if (config.wifiMode == 'c') {
      // versuch, die Verbindung aufzubauen
      Serial.println("  Verbinde mit WLAN...");
      while (WiFi.begin(config.wifiSsid, config.wifiPass) != WL_CONNECTED) {
        delay(1000);
        Serial.println("  Verbindung wird hergestellt...");
        if (millis() > deadline) {
          break;
        }
      }
    } else {
      // Versuch, den AP zu starten
      Serial.println("  Starte WLAN AccessPoint...");
      while (WiFi.beginAP(config.wifiSsid, config.wifiPass) != WL_AP_LISTENING) {
        delay(1000);
        Serial.println("  AccessPoint wird gestartet...");
        if (millis() > deadline) {
          break;
        }
      }
    }
  } else {
    // Wenn die Verbindung besteht, steig aus
    Serial.println("checkWiFi() WLAN Verbindung besteht");
    return true;
  } 

  Serial.println("checkWiFi() Prüfe erneut, ob WLAN Verbindung besteht");
  // Prüfe, ob nun eine Verbindung besteht
  if (wifiFine()) {
    Serial.println("  Verbunden mit WLAN");
    // Starte den Webserver
    Serial.println("  Starte Server");
    server.begin();
    printWiFiStatus();
    return true;
  } else  {
    Serial.println("  WLAN nicht verbunden!");
    return false;
  } 
  Serial.println("checkWiFi() end");
}

void setupMemory() {
  Serial.println("setupMemory() begin");
  Serial.print("  Board: ");
  Serial.println(BOARD_NAME);
  Serial.print("  Flash und SAMD Version: ");
  Serial.println(FLASH_STORAGE_SAMD_VERSION);
  Serial.print("  EEPROM Länge: ");
  Serial.println(EEPROM.length());
  Serial.println("setupMemory() end");
}

void setupWifi() {
  if (!config.wifiActive) {
    return;
  }
  Serial.println("setupWifi() begin");
  // check for the WiFi module:
  if (WiFi.status() == WL_NO_MODULE) {
    Serial.println("  Konnte das WiFi-Modul nicht ansprechen!");
  }

  String fv = WiFi.firmwareVersion();
  if (fv < WIFI_FIRMWARE_LATEST_VERSION) {
    Serial.println("***** Bitte WIFI Firmware aktualisieren! *****");
  }
  Serial.println("setupWifi() end");
}

boolean connectToMQTT() {
  // Prüfe, ob MQTT überhaupt aktivier tist
  if (!config.mqttActive) {
    return true;
  }

  if (!wifiFine()) {
    Serial.println("connectToMQTT(): Keine WLAN-Verbindung, breche Verbindung mit MQTT-Server ab");
    return false;
  }
  Serial.println("connectToMQTT(): Verbinde mit MQTT-Server...");
  mqttClient.setServer(config.mqttServer, config.mqttPort);
  while (!mqttClient.connected()) {
    if (mqttClient.connect(config.mqttName, config.mqttUser, config.mqttPassword)) {
      Serial.println("connectToMQTT(): Verbunden mit MQTT-Server");
      return true;
    } else {
      Serial.print("connectToMQTT(): Verbindung mit MQTT-Server fehlgeschlagen, rc=");
      Serial.println(mqttClient.state());
      return false;
    }
  }
  return false;
}

void displayValues() {
  SensorName name;

  // Brich ab, wenn unser Inverall noch nicht erreicht ist
  if (millis() < displayLast + (displayInterval * 1000)) {
    return;
  }
  displayLast = millis();

  tft.fillRect(xBegin, yBegin, 128, 20, 0x0000);
  tft.setCursor(xBegin, yBegin);

  // Fehlermeldung, wenn keine Sensoren gefunden wurden
  if (numberOfSensors <= 0) {
    Serial.println("displayTemperatures(): Keine Sensoren gefunden, deren Daten angezeigt werden könnten"); 
  }

  // Iteriere durch alle Sensoren
  for (int i = 0; i < numberOfSensors; i++) {
    // und wenn ein Name gesetzt ist,
    if (!sensorList[i].name[0] == '\0') {
      // Nimm den
      strcpy(name, sensorList[i].name);
    } else {
      // Sonst die Adresse
      strcpy(name, sensorList[i].address);
    }
    tft.println(String(name) + ": " + sensorList[i].value);

    /*
    Serial.print("> ");
    Serial.print(name);
    Serial.print(": ");
    Serial.println(sensorList[i].value);
    */
  }
}



void sendTemperaturesToMQTT() {
  char topic[30] = "n/a";
  char payload[10];

  // Prüfe, ob WiFi überhaupt aktivier tist
  if (!config.mqttActive) {
    return;
  }

  // Brich ab, wenn unser Inverall noch nicht erreicht ist
  if (millis() < sendLast + (sendInterval * 1000)) {
    return;
  }
  sendLast = millis();

  // Wenn MQTT noch nicht verbunden ist
  if (!mqttClient.connected()) {
    Serial.println("sendTemperaturesToMQTT(): Keine Verbindung zum MQTT-Server, versuche Verbindungsaufbau");
    // Versuche einen Verbindungsaufbau
    if (!connectToMQTT()) {
      // Wenn das fehlgeschlagen ist, brich ab
      Serial.println("sendTemperaturesToMQTT(): Verbindungsaufbau fehlgeschlagen, breche ab");
      return;
    }
  }

  // Fehlermeldung, wenn keine Sensoren gefunden wurden
  if (sensors.getDeviceCount() <= 0) {
    Serial.println("sendTemperaturesToMQTT(): Keine Sensoren gefunden, deren Daten übermittelt werden könnten"); 
  }

  // Iteriere durch alle Sensoren
  for (int i = 0; i < numberOfSensors; i++) {
    // Ermittle die Temperatur
    Serial.println("  Ermittle temperatur sensor " + String(i));
    // Und bilde die MQTT-Nachricht
    strcpy(topic, "sensor/");
    strcat(topic, sensorList[i].address);
    strcat(topic, "/temperature");
    dtostrf(sensorList[i].value, 3, 2, payload);
    
    Serial.print("topic: ");
    Serial.print(topic);
    Serial.print(" - payload: ");
    Serial.println(payload);
    mqttClient.publish(topic, payload);
  }
}

void printSensorAddresses() {
  DeviceAddress tempAddress;

  Serial.print("  Anzahl: ");
  Serial.println(sensors.getDeviceCount());
  tft.print("Anzahl: ");
  tft.println(sensors.getDeviceCount());
  for (int i = 0; i < sensors.getDeviceCount(); i++) {   
    sensors.getAddress(tempAddress, i);

    Serial.print("  Sensor ");
    Serial.print(i);
    Serial.print(" Adresse: ");

    tft.print("Sensor ");
    tft.print(i + 1);
    tft.print(" Adresse: ");

    for (uint8_t j = 0; j < 8; j++) {
      if (tempAddress[j] < 16) Serial.print("0");
      Serial.print(tempAddress[j], HEX);
      tft.print(tempAddress[j], HEX);
    }

    Serial.println();
    tft.println();
  }
}


void loop() {
  blink();

  checkWiFi();

  updateTemperatures();
  updateLevels();

  displayValues(); 

  sendTemperaturesToMQTT();

  httpProcessRequests();

}
