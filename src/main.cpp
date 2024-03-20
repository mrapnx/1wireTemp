/* *************   Wie es geht....

loadConfig()
1. Config wird aus Flash geladen 
   => Config liegt in config.sensorConfig vor

setup1Wire()
2. Alle Sensoren werden per oneWire.search() ermittelt  
   => Sensoren liegen in dallasSensors vor
3. Es wird durch alle Sensoren in dallasSensors iteriert und die Config-Werte ermittelt
4. Zusammen mit den Config-Werten wird nun per addSensor() ein Eintrag in sensors.sensorList erzeugt
   => Nun liegen alle Sensoren in sensors.sensorList vor

loop()
5. Per updateTemperatures() und updateLevels() werden anhand der Sensor-Adressen in sensors.sensorList die aktuellen Werte aus dallasSensors bzw. aus DS2438 ermittelt und in sensors.sensorList geschrieben
6. Per sensorValueToDisplay() wird nun der Wert mit Hilfe der Config im Sensor in einen C-String konvertiert

*/

#include <Arduino.h>
#include <avr/dtostrf.h>
#include <Wire.h>
#include <WiFiNINA.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <PubSubClient.h>
#include <SPI.h>
#include <FlashStorage_SAMD.h>
#include <TFT_ILI9163C.h> // Achtung! In der TFT_IL9163C_settings.h muss >> #define __MRA_PCB__ << aktiv sein!. Offenbar ist mein Board nicht von dem Bug betroffen, von dem andere rote Boards betroffen sind. Siehe Readme der TFT_IL9163 Lib.
#include <DS2438.h>
#include "sensors.h"

// #define DRYRUN // Erzeugt Dummy-Sensoren, wenn keine echten angeschlossen sind

// *************** Konfig-Grundeinstellungen
const int sensorConfigCount = 10;  // Gibt fan, wie viele Sensoren konfiguriert und gespeichert werden können

typedef struct {
  char    head           [5] = "MRAb";
  // WLAN
  boolean wifiEnabled        = true;
  char    wifiMode           = 'a'; // a = Access Point / c = Client
  int     wifiTimeout        = 10;
  char    wifiSsid      [21] = "ArduinoAP";
  char    wifiPass      [21] = "ArduinoAP";

  // MQTT-Zugangsdaten
  boolean mqttEnabled        = false;
  char    mqttServer    [21] = "127.0.0.1"; // war: char *mqttServer
  int     mqttPort           = 1883;
  char    mqttName      [21] = "ArduinoClient";
  char    mqttUser      [21] = "ArudinoNano";
  char    mqttPassword  [21] = "DEIN_MQTT_PASSWORT";
  
  // Sensor-Konfiguration
  PersistantSensorConfig sensorConfig[sensorConfigCount];  

  char    foot           [5] = "MRAe";
} Config;

// WLAN-Port
const int wifiPort = 80; // Port, auf den der HTTP-Server lauscht

// Reset Pin
#define RST_PIN  21   // D21

// Button Pin
#define BUTTON_PIN 18 // D18

// Display
#define TFT_LED  2    // Rot    LED   D2 +3.3V
#define TFT_CLK  13   // Orange SCK   D13 / Nicht änderbar
#define TFT_MOSI 11   // Braun  SDA   D11 / Nicht änderbar
#define TFT_DC   5    // Grün   A0    D5
#define TFT_RST  6    // Lila   RES   D6
#define TFT_CS   7    // Blau   CS    D7
                      // Schw.  GND   GND
                      // Rot    VCC   +3.3V   

int       xBegin      = 0;
int       yBegin      = 33;
uint16_t  textColor   = 0xFFFF; // Weiß
uint16_t  backColor   = 0x0000; // Schwarz
boolean   initalClear = false;


// 1-Wire
const int PinOneWireBus = 10; // Pin, an dem der 1-Wire-Bus angeschlossen ist // = D10

// Intervalle
const int wifiCheckInterval   = 10;  // Frequenz in Sekunden, in der die WLAN-Verbindung versucht wird
const int tempCheckInterval   = 5;   // Frequenz in Sekunden, in der die Temperaturen abgefragt werden
const int levelCheckInterval  = 2;   // Frequenz in Sekunden, in der die Füllstände abgefragt werden
const int sendInterval        = 10;  // Frequenz in Sekunden, in der die Temperaturen an MQTT gesendet werden
const int displayInterval     = 10;  // Frequenz in Sekunden, in der die Temperaturen angezeigt werden
const int blinkInterval       = 500; // Frequenz in Millisekunden, in der die orange LED bei Fehlern blinkt


// ***************  Globale Variablen

// Allgemein
unsigned long tempCheckLast   = 0;
unsigned long levelCheckLast  = 0;
unsigned long sendLast        = 0;
unsigned long displayLast     = 0;
unsigned long wifiCheckLast   = 0;
unsigned long blinkLast       = 0;
boolean       blinking        = false;
boolean       buttonState     = false;
boolean       dummySensors    = false;

// 1-Wire
OneWire           oneWire(PinOneWireBus);     // 1-Wire Grundobjekt
DallasTemperature dallasSensors(&oneWire);    // 1-Wire Objekt für Temperatursensoren
Sensors           sensors;                    // Sensorliste

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

// Zustands-Funktionen
boolean sensorsFine();
boolean wifiFine();
boolean checkWiFi();
boolean connectToMQTT();

// Config-Funktionen
boolean loadConfig();
void saveConfig();
void printConfig(Config &pconfig);
void copyConfig(const Config &from, Config &to);

// Sensorlisten-Funktionen
void addSensor(const SensorAddress address, const SensorName name, const SensorType type, const SensorValueFormat format, const SensorValueFormatMin formatMin, const SensorValueFormatMax formatMax, const SensorValuePrecision precision, const SensorValueMin min, const SensorValueMax max, float value);
void addSensor(Sensor sensor);
void removeSensor(SensorAddress address);
boolean updateSensorValue(const SensorAddress address, const float value);
void clearSensorList();
boolean getSensorType(const SensorAddress address, SensorType& type);
boolean getSensorConfig(const SensorAddress address, SensorConfig &output);

// Ein- & Ausgabe-Funktionen
void updateTemperatures();
void updateLevels();
void printSensors();
void printSensorAddresses();
void printWiFiStatus();
void displayBackground(); 
void displayValues(); 
void sendTemperaturesToMQTT();
boolean getButtonState();
void reset();

// HTTP-Funktionen
char* getValue(const String& data, const char* key);
String getValuesAsHtml();
char* urlDecode(const char* input);
int hexToDec(char c);
void htmlGetHeader(int refresh);
void htmlGetStatus();
void htmlGetConfig();
void htmlSetConfig();
void httpProcessRequests();

//  Setup-Funktionen
void setup1Wire(); 
void setupDisplay();
void setupMemory();
void setupWifi();
void setup();

// ***************  Funktionen
char* urlDecode(const char* input) {
  // Decode URL-encoded data
  String decoded = "";
  char a, b;
  for (size_t i = 0; i < strlen(input); i++) {
    if (input[i] == '%') {
      a = input[++i];
      b = input[++i];
      decoded += char((hexToDec(a) << 4) | hexToDec(b));
    } else {
      if (input[i] == '+') {
        decoded += ' ';
      } else {
        decoded += input[i];
      }
    }
  }

  char* result = new char[decoded.length() + 1];
  strcpy(result, decoded.c_str());
  return result;
}

int hexToDec(char c) {
  return (c >= '0' && c <= '9') ? c - '0' : (c >= 'A' && c <= 'F') ? c - 'A' + 10 : (c >= 'a' && c <= 'f') ? c - 'a' + 10 : 0;
}

char* getValue(const String& data, const char* key) {
  static char result[100]; // Annahme: Der Wert passt in einen 100-Byte-Puffer
  String delimiter = "=";
  char temp[100];
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
  Serial.print("  result vor urldecode: ");
  Serial.println(result);
  strcpy(temp, urlDecode(result));
  strcpy(result, temp);
  Serial.print("  result nach urldecode: ");
  Serial.println(result);
  Serial.println("getValue()");
  return result;
}

void copyConfig(const Config &from, Config &to) {
  Serial.println("copyConfig() begin");
  // WiFi
         to.wifiEnabled  = from.wifiEnabled;
         to.wifiMode     = from.wifiMode;
         to.wifiTimeout  = from.wifiTimeout;
  strcpy(to.wifiSsid,      from.wifiSsid);
  strcpy(to.wifiPass,      from.wifiPass);

  // MQTT
         to.mqttEnabled  = from.mqttEnabled;
  strcpy(to.mqttServer,    from.mqttServer);
  strcpy(to.mqttUser,      from.mqttUser);
         to.mqttPort     = from.mqttPort;
  strcpy(to.mqttName,      from.mqttName);
  strcpy(to.mqttPassword,  from.mqttPassword);
  
  // Sensor-Konfig
  for (int i = 0; i < sensorConfigCount; i++) {
    strcpy(to.sensorConfig[i].address,    from.sensorConfig[i].address);  
    strcpy(to.sensorConfig[i].config.name,       from.sensorConfig[i].config.name);  
    strcpy(to.sensorConfig[i].config.format,     from.sensorConfig[i].config.format);  
           to.sensorConfig[i].config.precision = from.sensorConfig[i].config.precision;
           to.sensorConfig[i].config.min       = from.sensorConfig[i].config.min;
           to.sensorConfig[i].config.max       = from.sensorConfig[i].config.max;
  }
  Serial.println("copyConfig() end");
};

void printConfig(Config &pconfig) {
  Serial.println("printConfig() begin");

  Serial.print("  wifiEnabled: ");
  Serial.println(int(pconfig.wifiEnabled));

  Serial.print("  ssid: ");
  Serial.println(pconfig.wifiSsid);

  Serial.print("  pass: ");
  Serial.println(pconfig.wifiPass);

  Serial.print("  mode: ");
  Serial.println(pconfig.wifiMode);

  Serial.print("  wifiTimeout: ");
  Serial.println(pconfig.wifiTimeout);

  Serial.print("  mqttEnabled: ");
  Serial.println(int(pconfig.mqttEnabled));

  Serial.print("  mqttServer: ");
  Serial.println(pconfig.mqttServer);

  Serial.print("  mqttPort: ");
  Serial.println(pconfig.mqttPort);

  Serial.print("  mqttName: ");
  Serial.println(pconfig.mqttName);

  Serial.print("  mqttUser: ");
  Serial.println(pconfig.mqttUser);

  Serial.print("  mqttPassword: ");
  Serial.println(pconfig  .mqttPassword);

  for (int i = 0; i < sensorConfigCount; i++) {
    Serial.print("  sensorConfig");  
    Serial.print(i);  
    Serial.print(": Address: ");  
    Serial.print(pconfig.sensorConfig[i].address);  
    Serial.print(" Name: ");  
    Serial.print(pconfig.sensorConfig[i].config.name);  
    Serial.print(" Format: ");  
    Serial.print(pconfig.sensorConfig[i].config.format);  
    Serial.print(" Precision: ");  
    Serial.print(pconfig.sensorConfig[i].config.precision);  
    Serial.print(" Min: ");  
    Serial.print(pconfig.sensorConfig[i].config.min);  
    Serial.print(" Max: ");  
    Serial.println(pconfig.sensorConfig[i].config.max);  
  }

  Serial.println("printConfig() end");
}

void clearSensorList() {
  // Befreie den Speicher von sensorList
  free(sensors.sensorList);

  // Setze die Anzahl der Sensoren auf 0
  sensors.count = 0;

  // Setze den Zeiger auf null, um sicherzustellen, dass er nicht auf ungültigen Speicher zeigt
  sensors.sensorList = nullptr;
}

void saveConfig() {
  Serial.println("saveConfig() begin");

  Serial.println("  Speichere Konfig:");
  printConfig(config);
  EEPROM.put(0, config);

  if (!EEPROM.getCommitASAP()) {
    Serial.println("  CommitASAP nicht gesetzt, führe commit() aus.");
    EEPROM.commit();
  }
  Serial.println("saveConfig() end");
}

boolean loadConfig() {
  Config tempConfig;
  boolean returnValue = false;

  Serial.println("loadConfig() begin");
  Serial.println("  Lade Konfig");
  // Lies den EEPROM bei Adresse 0 aus
  EEPROM.get(0, tempConfig); 
  // Die Struct enthält als erstes immer den C-String "MRA-b" und als letztes immer "MRA-e". Prüfe darauf.
  if (strcmp(tempConfig.head, "MRAb") == 0 && (strcmp(tempConfig.foot, "MRAe") == 0)) {
    Serial.println("  Konfig erfolgreich geladen:");
    copyConfig(tempConfig, config);
    printConfig(config);
    returnValue = true;
  } else {
    Serial.println("  Konfig konnte nicht geladen werden, habe folgendes erhalten:");
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
  client.print("<html>");
  client.print("  <body>");
  client.print("    <h1>Konfiguration</h1>");
  client.print("    <form method='get' action='/update'>");
  client.print("      <p>WLAN aktiv: <input type='checkbox' name='wifiEnabled' " + String(config.wifiEnabled ? "checked" : "") + "></p>");
  client.print("      <p>SSID: <input type='text' name='wifiSsid' value='" + String(config.wifiSsid) + "'></p>");
  client.print("      <p>Passwort: <input type='text' name='wifiPass' value='" + String(config.wifiPass) + "'></p>");
  client.print("      <p>Modus (a=Access Point, c=Client): <input type='text' name='wifiMode' value='" + String(config.wifiMode) + "'></p>");
  client.print("      <p>WLAN Timeout: <input type='text' name='wifiTimeout' value='" + String(config.wifiTimeout) + "'></p>");

  client.print("      <p>MQTT aktiv: <input type='checkbox' name='mqttEnabled' " + String(config.mqttEnabled ? "checked" : "") + "></p>");
  client.print("      <p>MQTT Server: <input type='text' name='mqttServer' value='" + String(config.mqttServer) + "'></p>");
  client.print("      <p>MQTT Port: <input type='text' name='mqttPort' value='" + String(config.mqttPort) + "'></p>");
  client.print("      <p>MQTT Name: <input type='text' name='mqttName' value='" + String(config.mqttName) + "'></p>");
  client.print("      <p>MQTT Benutzer: <input type='text' name='mqttUser' value='" + String(config.mqttUser) + "'></p>");
  client.print("      <p>MQTT Passwort: <input type='text' name='mqttPassword' value='" + String(config.mqttPassword) + "'></p>");

  client.print("<p/>");
  client.print("<table>");
  client.print("<th></th>");
  client.print("<th>Adresse</th>");
  client.print("<th>Name</th>");
  client.print("<th>Format</th>");
  client.print("<th>FMin</th>");
  client.print("<th>FMax</th>");
  client.print("<th>Dezimalstellen</th>");
  client.print("<th>Min</th>");
  client.print("<th>Max</th>");
  for (int i = 0; i < sensorConfigCount; i++) {
    client.print("        <tr>");  
    client.print("          <td>" + String(i) + "</td>");  
    client.print("          <td><input type='text' name='sensorAddress"        + String(i) + "' value='" + String(config.sensorConfig[i].address)   + "'></td>");  
    client.print("          <td><input type='text' name='sensorName"           + String(i) + "' value='" + String(config.sensorConfig[i].config.name)      + "'></td>");  
    client.print("          <td><input type='text' name='sensorValueFormat"    + String(i) + "' value='" + String(config.sensorConfig[i].config.format)    + "'></td>");  
    client.print("          <td><input type='text' name='sensorValueFormatMin" + String(i) + "' value='" + String(config.sensorConfig[i].config.formatMin) + "'></td>");  
    client.print("          <td><input type='text' name='sensorValueFormatMax" + String(i) + "' value='" + String(config.sensorConfig[i].config.formatMax) + "'></td>");  
    client.print("          <td><input type='text' name='sensorValuePrecision" + String(i) + "' value='" + String(config.sensorConfig[i].config.precision) + "'></td>");  
    client.print("          <td><input type='text' name='sensorValueMin"       + String(i) + "' value='" + String(config.sensorConfig[i].config.min)       + "'></td>");  
    client.print("          <td><input type='text' name='sensorValueMax"       + String(i) + "' value='" + String(config.sensorConfig[i].config.max)       + "'></td>");  
    client.print("        </tr>");  
  }
  client.print("      </table>");

  client.print("      <input type='submit' value='Speichern'>");
  client.print("    </form>");
  client.print("    <br/>");
  client.print("    <br/>");
  client.print("    <a href=\"/reboot\">Neustart</a>"); 
  client.print("    <br/>");
  client.print("    <br/>");
  client.print("    <br/>");
  client.print("    <a href=\"/\">Zur&uuml;ck</a>");
  client.print("    <br/>");
  client.print("    <br/>");
  client.print("    <a href=\"load\">Konfig laden</a>");
  client.print("  </body>");
  client.print("</html>");
}

void htmlSetConfig() {
  char c;
  char no[2];
  char name[21];
  Serial.println("htmlSetConfig() begin");
  // Lese den HTTP-Body, der die aktualisierten Daten enthält
  String body = "";
  while (client.available()) {
    c = client.read();
    body += c;
  }
  Serial.print("body: ");
  Serial.println(body);

// Wifi
  config.wifiEnabled = body.indexOf("wifiEnabled=on") != -1;
  strcpy(config.wifiSsid, getValue(body, "wifiSsid"));
  strcpy(config.wifiPass, getValue(body, "wifiPass"));
  config.wifiMode = getValue(body, "wifiMode")[0];
  config.wifiTimeout = atoi(getValue(body, "wifiTimeout")); // Umwandlung in Integer

  // MQTT
  config.mqttEnabled = body.indexOf("mqttEnabled=on") != -1;
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
    strcpy(config.sensorConfig[i].config.name, getValue(body, name));

    strcpy(name, "sensorValueFormat");
    strcat(name, no);
    strcpy(config.sensorConfig[i].config.format, getValue(body, name));

    strcpy(name, "sensorValueFormatMin");
    strcat(name, no);
    config.sensorConfig[i].config.formatMin = atof(getValue(body, name)); // Umwandlung nach Float

    strcpy(name, "sensorValueFormatMax");
    strcat(name, no);
    config.sensorConfig[i].config.formatMax = atof(getValue(body, name)); // Umwandlung nach Float

    strcpy(name, "sensorValuePrecision");
    strcat(name, no);
    config.sensorConfig[i].config.precision = atoi(getValue(body, name)); // Umwandlung nach Int

    strcpy(name, "sensorValueMin");
    strcat(name, no);
    config.sensorConfig[i].config.min = atof(getValue(body, name)); // Umwandlung nach Float

    strcpy(name, "sensorValueMax");
    strcat(name, no);
    config.sensorConfig[i].config.max = atof(getValue(body, name)); // Umwandlung nach Float
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
      Serial.println("  Device connected to AP");
    } else {
      Serial.println("  Device disconnected from AP");
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
            Serial.println("  GET / => Status");
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
          Serial.println("  GET /config => Konfig");
          htmlGetConfig();
          break;
        }

        // "Konfiguration laden"
        if (currentLine.endsWith("GET /load")) {
          Serial.println("  GET /load => Konfig laden");
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

        if (currentLine.endsWith("GET /update")) {
          Serial.println("  GET /update => Konfig speichern");
          htmlSetConfig();
          htmlGetConfig();
          break;
        }                      

        // "Neustarten"
        if (currentLine.endsWith("GET /reboot")) {
          Serial.println("  GET /reboot => Neustart");
          reset();
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
  Serial.println("  Anzahl Sensoren im Array: " + String(sensors.count));
  for (int i = 0; i < sensors.count; i++) {
    Serial.print("  Sensor " + String(i) + " Adresse: ");
    Serial.print(sensors.sensorList[i].address);
    Serial.print(" Name: ");
    Serial.print(sensors.sensorList[i].config.name);
    Serial.print(" Typ: ");
    Serial.print(sensors.sensorList[i].type);
    Serial.print(" Wert: ");
    Serial.println(sensors.sensorList[i].value);
  }
}

void addSensor(Sensor sensor) {
  Sensor*   tempArray = (Sensor*)malloc((sensors.count + 1) * sizeof(Sensor));
  DeviceAddress tempDs2438DeviceAddress;

  Serial.println("addSensor(): begin");

  // Übertrage vorhandene Daten in das temporäre Array
  for (int i = 0; i < sensors.count; i++) {
    tempArray[i] = sensors.sensorList[i];
  }

  // Füge das neue Sensorobjekt hinzu
  Serial.print("  Füge Sensor ");
  Serial.print(sensor.address);
  Serial.println(" hinzu");
  strcpy(tempArray[sensors.count].address, sensor.address);
  strcpy(tempArray[sensors.count].config.name, sensor.config.name);
  tempArray[sensors.count].type = sensor.type;
  strcpy(tempArray[sensors.count].config.format, sensor.config.format);
  tempArray[sensors.count].config.formatMin = sensor.config.formatMin;
  tempArray[sensors.count].config.formatMax = sensor.config.formatMax;
  tempArray[sensors.count].config.precision = sensor.config.precision;
  tempArray[sensors.count].config.min = sensor.config.min;
  tempArray[sensors.count].config.max = sensor.config.max;
  tempArray[sensors.count].value = sensor.value;
  strToDeviceAddress(String(sensor.address), tempDs2438DeviceAddress);
  copyDeviceAddress(tempDs2438DeviceAddress, tempArray[sensors.count].deviceAddress);

  // Erhöhe die Anzahl der Sensoren
  sensors.count++;

  // Befreie den alten Speicher
  free(sensors.sensorList);

  // Weise den neuen Speicher zu
  sensors.sensorList = tempArray;

  Serial.println("addSensor(): end");
}

[[deprecated("Diese Funktion wird eigentlich nicht mehr gebraucht, da es eine Version gibt, die eine Sensor-Struct annimmt")]]
void addSensor(const SensorAddress address, const SensorName name, const SensorType type, const SensorValueFormat format, const SensorValueFormatMin formatMin, const SensorValueFormatMax formatMax, const SensorValuePrecision precision, const SensorValueMin min, const SensorValueMax max, float value) {
  Sensor*   tempArray = (Sensor*)malloc((sensors.count + 1) * sizeof(Sensor));
  DeviceAddress tempDs2438DeviceAddress;

  Serial.println("addSensor(): begin");

  // Übertrage vorhandene Daten in das temporäre Array
  for (int i = 0; i < sensors.count; i++) {
    tempArray[i] = sensors.sensorList[i];
  }

  // Füge das neue Sensorobjekt hinzu
  Serial.print("  Füge Sensor ");
  Serial.print(address);
  Serial.println(" hinzu");
  strcpy(tempArray[sensors.count].address, address);
  strcpy(tempArray[sensors.count].config.name, name);
  tempArray[sensors.count].type = type;
  strcpy(tempArray[sensors.count].config.format, format);
  tempArray[sensors.count].config.formatMin = formatMin;
  tempArray[sensors.count].config.formatMax = formatMax;
  tempArray[sensors.count].config.min = min;
  tempArray[sensors.count].config.max = max;
  tempArray[sensors.count].config.precision = precision;
  tempArray[sensors.count].value = value;
  strToDeviceAddress(String(address), tempDs2438DeviceAddress);
  copyDeviceAddress(tempDs2438DeviceAddress, tempArray[sensors.count].deviceAddress);

  // Erhöhe die Anzahl der Sensoren
  sensors.count++;

  // Befreie den alten Speicher
  free(sensors.sensorList);

  // Weise den neuen Speicher zu
  sensors.sensorList = tempArray;

  Serial.println("addSensor(): end");
}

boolean updateSensorValue(const SensorAddress address, const float value) {
   for (int i = 0; i < sensors.count; i++) {
     if (strcmp(sensors.sensorList[i].address, address) == 0) {
       sensors.sensorList[i].value = value;
       return true; 
     }
   }
   return false;
}

boolean getSensorType(const SensorAddress address, SensorType& type) {
   for (int i = 0; i < sensors.count; i++) {
     if (strcmp(sensors.sensorList[i].address, address) == 0) {
       type = sensors.sensorList[i].type;
       return true; 
     }
   }
   return false;
}

boolean getSensorConfig(const SensorAddress address, SensorConfig &output) {
  // Tacker durch das sensorConfig Array in config durch
  for (int i = 0; i < sensorConfigCount; i++) {
    // Vergleich die Adresse aus dem Parameter mit der in der Config
    if (strcmp(address, config.sensorConfig[i].address) == 0) {
      // Und gib bei Übereinstimmung den Min-Wert zurück
      Serial.print("getSensorConfig(): Sensor gefunden: Adresse=");
      Serial.println(address);
      strcpy(output.name, config.sensorConfig[i].config.name);
      strcpy(output.format, config.sensorConfig[i].config.format);
      output.formatMin = config.sensorConfig[i].config.formatMin;
      output.formatMax = config.sensorConfig[i].config.formatMax;
      output.precision = config.sensorConfig[i].config.precision;
      output.min = config.sensorConfig[i].config.min;
      output.max = config.sensorConfig[i].config.max;
      return true;
    }
  }
  return false;
}

void removeSensor(SensorAddress address) {
  for (int i = 0; i < sensors.count; i++) {
    if (sensors.sensorList[i].address == address) {
      // Sensor gefunden, löschen, indem die nachfolgenden Elemente verschoben werden
      for (int j = i; j < sensors.count - 1; j++) {
        sensors.sensorList[j] = sensors.sensorList[j + 1];
      }

      // Verringere die Anzahl der Sensoren
      sensors.count--;

      // Reduziere die Speichergröße
      Sensor* tempArray = (Sensor*)malloc(sensors.count * sizeof(Sensor));

      // Übertrage Daten in das temporäre Array
      for (int k = 0; k < sensors.count; k++) {
        tempArray[k] = sensors.sensorList[k];
      }

      // Befreie den alten Speicher
      free(sensors.sensorList);

      // Weise den neuen Speicher zu
      sensors.sensorList = tempArray;

      break;
    }
  }
  initalClear = false;
}

void setupDisplay() {
  Serial.println("setupDisplay() begin");

  // Hintergrundbeleuchtung an
  pinMode(TFT_LED, OUTPUT);
  digitalWrite(TFT_LED, HIGH);

  tft.begin();
  tft.setBitrate(24000000);
  tft.setTextColor(WHITE, BLACK);
  tft.setRotation(2); // Anschlusspins sind unten
  tft.setCursor(xBegin, yBegin);
  Serial.print("  Höhe: ");
  Serial.print(tft.height());
  Serial.print("  Breite: ");
  Serial.println(tft.width());
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
 return dallasSensors.getDeviceCount() > 0;
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

  for (int i = 0; i < sensors.count; i++) {
    if (sensors.sensorList[i].type == 'b') {
      DS2438 ds2438(&oneWire, sensors.sensorList[i].deviceAddress);
      ds2438.begin();
      Serial.print("  Sensor DS2438 ");
      Serial.print(sensors.sensorList[i].address);
      Serial.print(" Adresse ");
      Serial.print((unsigned int)&ds2438, HEX);
      if (!dummySensors) {
        ds2438.update();
        if (ds2438.isError()) {
          Serial.print(" erfolglos abgefragt"); 
        } else {
          Serial.print(" erfolgreich abgefragt");
          updateSensorValue(sensors.sensorList[i].address, ds2438.getVoltage(DS2438_CHA));
}
      } else {
        updateSensorValue(sensors.sensorList[i].address, random(0,2) + (1 / random(1,10)));
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
            //delete &ds2438;  // MR: Keine Ahnung warum, aber das führt zu nem Freeze
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
  dallasSensors.requestTemperatures();

  // Iteriere durch alle Sensoren
  for (int i = 0; i < sensors.count; i++) {
    if (sensors.sensorList[i].type == 't') {
      // Aktualisiere die Liste
      if (!dummySensors) {
        updateSensorValue(sensors.sensorList[i].address, dallasSensors.getTempC(sensors.sensorList[i].deviceAddress));
      } else {
        updateSensorValue(sensors.sensorList[i].address, random(15,25));
      }
    }
  }
  Serial.println("updateTemperatures() end");
}

void setup1Wire() {
  byte              addrArray[8];
  String            address;
  Sensor            sensor;

  Serial.println("setup1Wire() begin");

  // Initialisiere die OneWire- und DallasTemperature-Bibliotheken
  if (oneWire.search(addrArray)) {
  } else {
    tft.println("Keine Geräte gefunden");
    Serial.println("  Keine Geräte gefunden");
  }

  // Starte Objekt für Temperatur-Sensoren
  dallasSensors.begin();

  // Leere die Liste
  clearSensorList();

  // Gib die gefundenen Sensoren aus
  Serial.println("  Gefundene 1-Wire-Sensoren:");
  tft.println("Gefundene 1-Wire-Sensoren:");
  printSensorAddresses();

  // Iteriere durch alle Sensoren
  for (int i = 0; i < dallasSensors.getDeviceCount(); i++) {
    
    // Ermittle die Adresse
    Serial.println("  Ermittle Adresse Sensor " + String(i));
    dallasSensors.getAddress(sensor.deviceAddress, i); 
    address = deviceAddressToStr(sensor.deviceAddress);
    strcpy (sensor.address, address.c_str());
    Serial.println("  address: " + address);
    Serial.print("  addressC: ");
    Serial.println(sensor.address);
    
    // Ermittle den Typ
    Serial.println("  Ermittle Typ Sensor " + String(i));
    if (getSensorTypeByAddress(sensor.address, sensor.type) == true) {
      Serial.println("  Typ erfolgreich ermittelt");
    } else {
      Serial.println("  Typ nicht erfolgreich ermittelt");
    }
    Serial.print("  Typ Sensor " + String(i) + ": ");
    Serial.println(sensor.type);

    // Ermittle die Konfig
    getSensorConfig(sensor.address, sensor.config);

    // Füg den Sensor der Liste hinzu
    addSensor(sensor);
  }  

  #ifdef DRYRUN
    if (dallasSensors.getDeviceCount() <= 0) {
      Serial.println("  Dryrun, erzeuge Dummy-Geräte");
      tft.println("Dryrun, erzeuge Dummy-Geraete");
      dummySensors = true;
      addSensor("28EE3F8C251601", "Dmy Tmp 1", 't', "%2s C", -1,  -1, 0, -1,  -1, 23);
      addSensor("28FF3F8C251601", "Dmy Tmp 2", 't', "%2s C", -1,  -1, 0, -1,  -1, 40);
      addSensor("33EB3F8C251601", "Dmy Lvl 1", 'b', "%2s %%", 0, 120, 0,  0, 100, 25);
      addSensor("33EA3F8C251601", "Dmy Lvl 2", 'b', "%2s %%", 0, 100, 0,  0,   2, 1.5);
    }
  #endif

  updateTemperatures();
  updateLevels();
  Serial.println("setup1Wire() end");
}

String getValuesAsHtml() {
  String address;
  String temp;
  SensorName name;
  char buffer[10];
  String returnString = "";

  Serial.println("getValuesAsHtml() begin");
  for (int i = 0; i < sensors.count; i++) {
  // Iteriere durch alle Sensoren
    // und wenn ein Name gesetzt ist,
    if (!sensors.sensorList[i].config.name[0] == '\0') {
      // Nimm den
      strcpy(name, sensors.sensorList[i].config.name);
    } else {
      // Sonst die Adresse
      strcpy(name, sensors.sensorList[i].address);
    }
    // Formattiere den Wert entspr. dem Value String
    sensorValueToDisplay(sensors.sensorList[i], buffer);
    returnString = returnString + String(name) + ": " + String(buffer) + "</br>";

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

  // Eingebaute LED als Zustands-Indikator
  pinMode(LED_BUILTIN, OUTPUT);

  // Eingabe-Knopf
  pinMode(BUTTON_PIN, INPUT);

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

  // Gib die gefundenen Sensoren seriell aus
  printSensors();

  // Erzeuge die Sensor-Beschriftungen
  displayBackground();

  Serial.println("setup() end");
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

  Serial.print("  Server Status: ");
  Serial.println(server.status());
  Serial.println("printWiFiStatus() end");
}

boolean checkWiFi() {
  // Prüfe, ob WiFi überhaupt aktiviert tist
  if (!config.wifiEnabled) {
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
  if (!config.wifiEnabled) {
    return;
  }
  Serial.println("setupWifi() begin");
  // check for the WiFi module:
  if (WiFi.status() == WL_NO_MODULE) {
    Serial.println("  Konnte das WiFi-Modul nicht ansprechen!");
  }

  String fv = WiFi.firmwareVersion();
  if (fv < WIFI_FIRMWARE_LATEST_VERSION) {
    Serial.println("  ***** Bitte WIFI Firmware aktualisieren! *****");
  }
  Serial.println("setupWifi() end");
}

boolean connectToMQTT() {
  // Prüfe, ob MQTT überhaupt aktivier tist
  if (!config.mqttEnabled) {
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

void displayBackground() {
  int         height      = tft.height() - yBegin;
  int         lineheight  = height / sensors.count;
  int         line        = yBegin;

  Serial.println("displayBackground() begin"); 
  
  #ifdef DRYRUN
    tft.fillRect(xBegin, yBegin, 4, 4, WHITE);  // Oben Links
    tft.fillRect(xBegin, tft.height()-4, 4, 4, WHITE); // Unten Links
    tft.fillRect(tft.width()-4, yBegin, 4, 4, WHITE); // Oben Rechts
    tft.fillRect(tft.width()-4, tft.height()-4, 4, 4, WHITE); // Unten Rechts
  #endif

  // Wenn keine Sensoren erkannt wurden, brich ab
  if (sensors.count <= 0) {
  Serial.println("  Keine Sensoren vorhanden, breche ab"); 
  Serial.println("displayBackground() end"); 
    return;
  }

  tft.clearScreen();

  // Beschriftungen
  tft.setTextSize(1);
  for (int i = 0; i <= sensors.count; i++) {
    tft.setCursor(xBegin, line);

    // Wenn ein Name gesetzt ist,
    if (!sensors.sensorList[i].config.name[0] == '\0') {
      // Nimm den
      tft.println(sensors.sensorList[i].config.name);
    } else {
      // Sonst die Adresse
      tft.println(sensors.sensorList[i].address);
    }
    line = line + lineheight;
  }
  tft.setCursor(xBegin, yBegin);
  Serial.println("displayBackground() end"); 
}

void displayValues() {
  char        buffer[10];
  int         height      = tft.height() - yBegin;
  int         lineheight  = height / sensors.count;
  int         line        = yBegin;
  // Brich ab, wenn unser Inverall noch nicht erreicht ist
  if (millis() < displayLast + (displayInterval * 1000)) {
    return;
  }
  displayLast = millis();
 
  Serial.println("displayValues() begin"); 

  // Falls wir vor displayBackground() aufgerufen wurden, hol den Aufruf nach
  if (!initalClear) {
    displayBackground();
    initalClear = true;
  }

  // Fehlermeldung, wenn keine Sensoren gefunden wurden
  if (sensors.count <= 0) {
    Serial.println("  Keine Sensoren gefunden, deren Daten angezeigt werden könnten"); 
    Serial.println("displayValues() end"); 
    return;
  }

  tft.setTextSize(2);

  // Iteriere durch alle Sensoren
  for (int i = 0; i < sensors.count; i++) {
    if (sensors.count <= 4) {
      tft.setCursor(40, line+10);
    } else {
      tft.setCursor(70, line);
    }

    // Formattiere den Wert entspr. dem Value String
    sensorValueToDisplay(sensors.sensorList[i], buffer);
    tft.println(buffer);
    line = line + lineheight;
  }

  Serial.println("displayValues() end"); 
}

boolean getButtonState() {
  boolean newState;
  newState = digitalRead(BUTTON_PIN);
  if (!newState == buttonState) {
    buttonState = newState;
    Serial.print("  Knopf betätigt: ");
    Serial.println(int(buttonState));
    return true;
  } else {
    return false;
  }
}

void reset() {
  Serial.println("reset() begin");
  pinMode(RST_PIN, OUTPUT);
  digitalWrite(RST_PIN, HIGH);  
  digitalWrite(RST_PIN, LOW);  
  Serial.println("reset() end (Das sollte man eigentlich nicht sehen)");
}

void sendTemperaturesToMQTT() {
  char topic[30] = "n/a";
  char payload[10];

  // Prüfe, ob WiFi überhaupt aktivier tist
  if (!config.mqttEnabled) {
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
  if (dallasSensors.getDeviceCount() <= 0) {
    Serial.println("sendTemperaturesToMQTT(): Keine Sensoren gefunden, deren Daten übermittelt werden könnten"); 
  }

  // Iteriere durch alle Sensoren
  for (int i = 0; i < sensors.count; i++) {
    // Ermittle die Temperatur
    Serial.println("  Ermittle temperatur sensor " + String(i));
    // Und bilde die MQTT-Nachricht
    strcpy(topic, "sensor/");
    strcat(topic, sensors.sensorList[i].address);
    strcat(topic, "/temperature");
    dtostrf(sensors.sensorList[i].value, 3, 2, payload);
    
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
  Serial.println(dallasSensors.getDeviceCount());
  tft.print("Anzahl: ");
  tft.println(dallasSensors.getDeviceCount());
  for (int i = 0; i < dallasSensors.getDeviceCount(); i++) {   
    dallasSensors.getAddress(tempAddress, i);

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

  getButtonState();

  checkWiFi();

  updateTemperatures();
  updateLevels();

  displayValues(); 

  sendTemperaturesToMQTT();

  httpProcessRequests();

}
