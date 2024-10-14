#include <Arduino.h>
#include <DallasTemperature.h>

typedef char  SensorAddress           [17];
typedef char  SensorName              [21];
typedef char  SensorValueFormat       [11];
typedef int   SensorValuePrecision;
typedef float SensorValueMin;
typedef float SensorValueMax;
typedef float SensorValueFormatMin;
typedef float SensorValueFormatMax;
typedef char  SensorValueBonds        [60];

typedef enum {
	T_DS18B20 = 't',
  T_DS18S20 = 't',
  T_DS1822  = 't',
  T_DS2438  = 'b',
  T_UNKNOWN = 'u'
} SensorType;

struct SensorConfig {
  SensorName            name            = "";        // Name zur Anzeige
  SensorValueFormat     format          = "%s";      // Format-String zur Darstellung des Wertes
  SensorValueFormatMin  formatMin       = -1;        // Minimum des Anzeige-Wertes
  SensorValueFormatMax  formatMax       = -1;        // Maximum des Anzeige-Wertes
  SensorValuePrecision  precision       = 0;         // Dezimalstellen des Wertes
  SensorValueMin        min             = -1;        // Minimum des Messwertes 
  SensorValueMax        max             = -1;        // Maximum des Messwertes
  SensorValueBonds      bonds           = "";
};

struct PersistantSensorConfig {
  SensorAddress         address         = "";        // Addresse des zu konfigurierenden Sensors
  SensorConfig          config;                      // Anzuwendende Konfig
};

struct Sensor {
  SensorAddress         address         = "";         // Adresse des Sensors userfriendly
  DeviceAddress         deviceAddress;                // Adresse des Sensors als HEX
  SensorType            type            = T_UNKNOWN;  // Typ, derzeit werden nur t, b und u unterstützt
  SensorConfig          config;
  float                 value;
};

struct Sensors {
  Sensor*               sensorList      = nullptr;    // Zeiger auf das Array von SensorData
  int                   count           = 0;          // Aktuelle Anzahl von Sensoren
};

struct Bond {
  float sensorValue;
  float displayValue;
};

struct Bonds {
  Bond bond[20];
  int  count = 0;
};


/* 
    Die Logik zur Anzeige ist wie folgt: 

    * Direkt Anzeige
    Der Wert des Sensors wird ermittelt und steht in => value (z.B.  1,5)
    Der Wert kann nun direkt ausgegeben werden (Standard).
    Beispiel
    format ="%s C" / precision = 1  (Implizit: formatMin = -1 / formatMax = -1 / min = -1 / max = -1)
    Bei einem Messwert von 1,5 wird 1,5 C angezeigt.

    * Prozent-Anzeige
    Der Wert kann aber auch in ein Verhältnis gesetzt werden, wenn sich z.B. der Messbereich von 0 bis 3 erstreckt, entspräche 1,5 50%.
    Beispiel:
    Wir messen 0 bis 3 und zeigen das als prozentualen Level an.
    min = 0 / max = 3 / format = "%s %%"   (Implizit: formatMin = -1 / formatMax = -1 / precision = 0)
    Bei value = 1,5 wird dann "50 %" angezeigt (Das doppelte "%%"" führt zur Darstellung von "%").
    
   * Anteilige Anzeige
   Haben wir nun z.B. einen Wassertank, der 120l fasst, können wir auch vom Prozent-Wert auf den anteiligen Wert umrechnen lassen.
   Beispiel:
   Wir messen 0 bis 3 und wollen das als Füllstand von 0 bis 120 l anzeigen.
   min = 0 / max = 3 / formatMin = 0 / formatMax = 120 / format = "%s l"   (Implizit: precision = 0)
   Bei einem Messwert von 1,5 wird nun "60 l" angezeigt

  * Nicht-lineare Anzeige
  Hat der Wassertank nun keine parallelen Wände, sondern ist z.B. konisch oder mit stufen aufgebaut, können wir nicht linear rechnen.
  Daher nutzen wir hier ein array von Zuweisungen zwischen dem Mess- und dem Anzeigewert.
  Beispiel:
  Wir messen 0 bis 10, wollen als Füllstand 0 bis 200 l anzeigen, wobei 100 l schon bei einem Messwert von 2 erreicht sind.
  bonds = "0=0;1=50;2=100;10=200" / format = "%s l" (Implizit: precision = 0)
*/

// *************** Deklaration der Funktionen
byte convertHexCStringToByte(const char* hexString);
String deviceAddressToStr(DeviceAddress addr);
void deviceAddressToStrNew(const DeviceAddress addr, String out);
const char* deviceAddressToChar(DeviceAddress addr); 
bool strToDeviceAddress(const String &str, DeviceAddress &addr);
bool getSensorTypeByAddress(const SensorAddress manufacturerCode, SensorType &sensorType);
void copyDeviceAddress(const DeviceAddress in, DeviceAddress out);
void sensorValueToDisplay(const Sensor sensor, char displayValue[30]);
void parseValuePairs(const char* input, Bonds pairs);

// ***************  Funktionen
void parseValuePairs(const char* input, Bonds pairs) {
  const char* current = input;
  const char* semicolonPos;
  pairs.count = 0;

  while ((semicolonPos = strchr(current, ';')) != nullptr) {
    if (pairs.count >= 20) break;

    // Temporäre Zeichenkette für das aktuelle Paar
    int length = semicolonPos - current;
    char temp[length + 1];
    strncpy(temp, current, length);
    temp[length] = '\0';  // Null-terminieren

    // Splitte bei '='
    char* equalsPos = strchr(temp, '=');
    if (equalsPos != nullptr) {
      *equalsPos = '\0';  // Trenne den Wert und die Anzeige
      pairs.bond[pairs.count].sensorValue =  atof(temp);
      pairs.bond[pairs.count].displayValue = atof(equalsPos + 1);
      pairs.count++;
    }

    current = semicolonPos + 1;  // Weiter zum nächsten Paar
  }

  // Verarbeitung des letzten Paares (ohne Semikolon)
  if (*current != '\0' && pairs.count < 20) {
    char* equalsPos = strchr(current, '=');
    if (equalsPos != nullptr) {
      *equalsPos = '\0';
      pairs.bond[pairs.count].sensorValue = atof(current);
      pairs.bond[pairs.count].displayValue = atof(equalsPos + 1);
      pairs.count++;
    }
  }
}



void sensorValueToDisplay(const Sensor sensor, char displayValue[30]) {
    char stringBuffer[30] = "";
  float calcedValue = -1;
  Serial.println("sensorValueToDisplay() begin");

  // Wenn min oder max nicht gesetzt sind
  if (sensor.config.min < 0 || sensor.config.max < 0) {
    // Erfolgt keine Umrechnung, sondern die Übernahme des float Wertes
    Serial.println("  Keine Umrechnung, direkte Anzeige");
    calcedValue = sensor.value;
  } else {
    // Plausi-Prüfung
    if (sensor.value >= sensor.config.min && sensor.value <= sensor.config.max && sensor.config.max > sensor.config.min) {
      if (sensor.config.formatMin < 0 || sensor.config.formatMax < 0) {
        Serial.println("  Umrechnung in Prozentwert");
        calcedValue = (sensor.value - sensor.config.min) / (sensor.config.max - sensor.config.min) * 100;
      } else {
        Serial.println("  Umrechnung in anteiligen Wert");
        calcedValue = ((sensor.config.formatMax - sensor.config.formatMin) * (sensor.value - sensor.config.min) / (sensor.config.max - sensor.config.min)) + sensor.config.formatMin;
      }
    } else {
      Serial.println("  Umrechnung nicht möglich");
      calcedValue = sensor.value;
    }
  }
  Serial.print("  dtostrf: calcedValue=");
  Serial.print(calcedValue);
  Serial.print(" precision=");
  Serial.print(sensor.config.precision);
  dtostrf(calcedValue, 0, sensor.config.precision, stringBuffer);
  Serial.print("  stringBuffer: ");
  Serial.println(stringBuffer);
  sprintf(displayValue, sensor.config.format, stringBuffer);
  Serial.print("  displayValue: ");
  Serial.println(displayValue);
  Serial.println("sensorValueToDisplay() end");
}


byte convertHexCStringToByte(const char* hexString) {
  // Erstelle einen temporären C-String mit den ersten beiden Zeichen des Eingabe-C-Strings
  char tempString[3];
  strncpy(tempString, hexString, 2);
  // Stelle sicher, dass der temporäre C-String mit einem Null-Byte abgeschlossen ist
  tempString[2] = '\0';

  // Verwende strtol, um den hexadezimalen C-String in eine Ganzzahl (long) zu konvertieren
  long value = strtol(tempString, nullptr, 16);
  
  // Konvertiere die Ganzzahl auf einen byte-Wert (0-255)
  byte result = static_cast<byte>(value);

  return result;
}

void copyDeviceAddress(const DeviceAddress in, DeviceAddress out) {
  for (int i = 0; i < 8; i++) {
    out[i] = in[i];
  }
}

bool strToDeviceAddress(const String &str, DeviceAddress &addr) {
  // Überprüfen, ob die Zeichenkette die richtige Länge hat
  if (str.length() != 16) {
    return false; // Fehler, wenn die Länge nicht korrekt ist
  }

  for (uint8_t j = 0; j < 8; j++) {
    // Extrahiere zwei Zeichen von der Zeichenkette
    String hexStr = str.substring(j * 2, j * 2 + 2);
    
    // Konvertiere die extrahierten Zeichen in einen Hexadezimalwert
    addr[j] = strtol(hexStr.c_str(), nullptr, 16);
  }

  return true; // Konvertierung erfolgreich
}


bool getSensorTypeByAddress(const SensorAddress manufacturerCode, SensorType &sensorType) {
  char code[17];
  byte firstByte;
  // Überprüfe nur das erste Byte des char-Arrays
  strcpy(code, manufacturerCode);
  firstByte = convertHexCStringToByte(code);

  switch (firstByte) {
    case 0x28:
      sensorType = T_DS18B20; // DS18B20 Temperatursensor
      return true;
    case 0x10:
      sensorType = T_DS18S20; // DS18S20 Temperatursensor
      return true;
    case 0x22:
      sensorType = T_DS1822; // DS1822 Temperatursensor
      return true;
    case 0x26:
      sensorType = T_DS2438; // DS2438 (Smart Battery Monitor)
      return true;
    default:
      sensorType = T_UNKNOWN;
      return false;
    }
}


String deviceAddressToStr(DeviceAddress addr) {
  String returnString = "";
    for (uint8_t j = 0; j < 8; j++) {
      // if (addr[j] < 16) returnString = "0";  // War im ursprünglichen Vorschlag einer Konvertierung, scheint aber keinen Sinn zu machen, da HEX 00 legitim ist.
      if (addr[j] < 16) {
        returnString = returnString + "00";
      } else {
      returnString = returnString + String(addr[j], HEX);
      }
    }
  returnString.toUpperCase();
  return returnString;
}

void deviceAddressToStrNew(const DeviceAddress addr, String out) {
  out = "";
    for (uint8_t j = 0; j < 8; j++) {
      if (addr[j] < 16) out = "0";
      out = out + String(addr[j], HEX);
      Serial.println("deviceAddrToStrNew() Durchlauf " + String(j) + ": out = " + out);
    }
  out.toUpperCase();
  Serial.println("deviceAddrToStrNew() ende, out = " + out);
}

const char* deviceAddressToChar(DeviceAddress addr) {
  static SensorAddress result;
  String returnString = "";
    for (uint8_t j = 0; j < 8; j++) {
      if (addr[j] < 16) returnString = "0";
      returnString = returnString + String(addr[j], HEX);
    }
  returnString.toUpperCase();
  strcpy(result, returnString.c_str());
  return result;
}
