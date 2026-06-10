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


typedef enum {
	T_DS18B20,
  T_DS18S20,
  T_DS1822,
  T_DS2438,
  T_UNKNOWN
} SensorType;

const char* typeNames[] = {
	"DS18B20",
  "DS18S20",
  "DS1822",
  "DS2438",
  "UNKNOWN"  
};

typedef enum {
	C_DS18B20 = 't',
  C_DS18S20 = 't',
  C_DS1822  = 't',
  C_DS2438  = 'b',
  C_UNKNOWN = 'u'
} SensorCategory;

struct SensorConfig {
  SensorName            name            = "";        // Name zur Anzeige
  SensorValueFormat     format          = "%s";      // Format-String zur Darstellung des Wertes
  SensorValueFormatMin  formatMin       = -1;        // Minimum des Anzeige-Wertes
  SensorValueFormatMax  formatMax       = -1;        // Maximum des Anzeige-Wertes
  SensorValuePrecision  precision       = 0;         // Dezimalstellen des Wertes
  SensorValueMin        min             = -1;        // Minimum des Messwertes 
  SensorValueMax        max             = -1;        // Minimum des Messwertes
  char                  interpolation   [101] = "";  // Interpolationspunkte: "sensorval1=level1;sensorval2=level2"
};

struct PersistantSensorConfig {
  SensorAddress         address         = "";        // Addresse des zu konfigurierenden Sensors
  SensorConfig          config;                      // Anzuwendende Konfig
};

struct Sensor {
  SensorAddress         address         = "";         // Adresse des Sensors userfriendly
  DeviceAddress         deviceAddress;                // Adresse des Sensors als HEX
  SensorCategory        category        = C_UNKNOWN;  // Kategorie, derzeit werden nur t, b und u unterstützt
  SensorType            type            = T_UNKNOWN;  // Typ gemäß Dallas/Maxim
  SensorConfig          config;
  float                 value;
};

struct Sensors {
  Sensor*               sensorList      = nullptr;    // Zeiger auf das Array von SensorData
  int                   count           = 0;          // Aktuelle Anzahl von Sensoren
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
   min = 0 / max = 3 / formatMin = 0 / fformatMax = 120 / format = "%s l"   (Implizit: precision = 0)
   Bei einem Messwert von 1,5 wird nun "60 l" angezeigt

*/

// *************** Deklaration der Funktionen
byte convertHexCStringToByte(const char* hexString);
String deviceAddressToStr(DeviceAddress addr);
void deviceAddressToStrNew(const DeviceAddress addr, String out);
const char* deviceAddressToChar(DeviceAddress addr); 
bool strToDeviceAddress(const String &str, DeviceAddress &addr);
bool getSensorCategoryByAddress(const SensorAddress manufacturerCode, SensorCategory &sensorCategory);
bool getSensorTypeByAddress(const SensorAddress manufacturerCode, SensorType &sensorType);
void copyDeviceAddress(const DeviceAddress in, DeviceAddress out);
float interpolateFromPairs(const char* interpolationString, const float sensorValue);
void sensorValueToDisplay(const float sensorValue, const SensorValueFormat formatString, const SensorValueFormatMin formatMin, const SensorValueFormatMax formatMax, const SensorValuePrecision precision, const SensorValueMin min, const SensorValueMax max, char displayValue[30]);
void sensorValueToDisplay(const Sensor sensor, char displayValue[30]);

// ***************  Funktionen

// Interpoliert einen Wert basierend auf Paaren im Format "val1=level1;val2=level2;..."
// Beispiel: "0=0;50=120;100=240" mit sensorValue=75 => 180
float interpolateFromPairs(const char* interpolationString, const float sensorValue) {
  Serial.println("interpolateFromPairs() begin");
  Serial.print("  interpolationString: ");
  Serial.println(interpolationString);
  Serial.print("  sensorValue: ");
  Serial.println(sensorValue);

  if (strlen(interpolationString) == 0) {
    Serial.println("  Interpolationstring ist leer, gebe sensorValue zurück");
    Serial.println("interpolateFromPairs() end");
    return sensorValue;
  }

  // Erstelle eine lokale Kopie des Strings zum Parsen
  char tempString[101];
  strncpy(tempString, interpolationString, 100);
  tempString[100] = '\0';

  // Parse alle Paare
  float pairValues[50][2];      // Max. 50 Paare
  int pairCount = 0;
  char* token = strtok(tempString, ";");
  
  while (token != nullptr && pairCount < 50) {
    // Token ist im Format "sensorval=level"
    char* eqPos = strchr(token, '=');
    if (eqPos != nullptr) {
      *eqPos = '\0';  // Trenne die beiden Teile
      float sensorVal = atof(token);
      float level = atof(eqPos + 1);
      pairValues[pairCount][0] = sensorVal;
      pairValues[pairCount][1] = level;
      pairCount++;
      Serial.print("  Pair ");
      Serial.print(pairCount - 1);
      Serial.print(": sensor=");
      Serial.print(sensorVal);
      Serial.print(" level=");
      Serial.println(level);
    }
    token = strtok(nullptr, ";");
  }

  if (pairCount < 2) {
    Serial.println("  Weniger als 2 Paare gefunden, gebe sensorValue zurück");
    Serial.println("interpolateFromPairs() end");
    return sensorValue;
  }

  // Finde die zwei Punkte, zwischen denen sensorValue liegt
  if (sensorValue <= pairValues[0][0]) {
    Serial.print("  sensorValue <= first point, return ");
    Serial.println(pairValues[0][1]);
    Serial.println("interpolateFromPairs() end");
    return pairValues[0][1];
  }
  if (sensorValue >= pairValues[pairCount - 1][0]) {
    Serial.print("  sensorValue >= last point, return ");
    Serial.println(pairValues[pairCount - 1][1]);
    Serial.println("interpolateFromPairs() end");
    return pairValues[pairCount - 1][1];
  }

  // Lineare Interpolation zwischen zwei Punkten
  for (int i = 0; i < pairCount - 1; i++) {
    if (sensorValue >= pairValues[i][0] && sensorValue <= pairValues[i + 1][0]) {
      float x1 = pairValues[i][0];
      float y1 = pairValues[i][1];
      float x2 = pairValues[i + 1][0];
      float y2 = pairValues[i + 1][1];
      
      // Lineare Interpolation: y = y1 + (y2-y1) * (x-x1) / (x2-x1)
      float interpolated = y1 + (y2 - y1) * (sensorValue - x1) / (x2 - x1);
      Serial.print("  Interpoliert zwischen Punkt ");
      Serial.print(i);
      Serial.print(" und ");
      Serial.print(i + 1);
      Serial.print(", result=");
      Serial.println(interpolated);
      Serial.println("interpolateFromPairs() end");
      return interpolated;
    }
  }

  Serial.println("  Keine passenden Punkte gefunden, gebe sensorValue zurück");
  Serial.println("interpolateFromPairs() end");
  return sensorValue;
}

void sensorValueToDisplay(const Sensor sensor, char displayValue[30]) {
    char stringBuffer[30] = "";
  float calcedValue = -1;
  Serial.println("sensorValueToDisplay() begin");

  // Überprüfe ob Interpolation konfiguriert ist (interpolation string nicht leer)
  if (strlen(sensor.config.interpolation) > 0) {
    Serial.println("  Interpolationsmodus");
    calcedValue = interpolateFromPairs(sensor.config.interpolation, sensor.value);
  }
  // Wenn min oder max nicht gesetzt sind
  else if (sensor.config.min < 0 || sensor.config.max < 0) {
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

[[deprecated("Diese Funktion wird eigentlich nicht mehr gebraucht, da es eine Version gibt, die eine Sensor-Struct annimmt")]]
void sensorValueToDisplay(const float sensorValue, const SensorValueFormat formatString, const SensorValueFormatMin formatMin, const SensorValueFormatMax formatMax, const SensorValuePrecision precision, const SensorValueMin min, const SensorValueMax max, char displayValue[30]) {
  char stringBuffer[30] = "";
  float calcedValue = -1;
  Serial.println("sensorValueToDisplay() begin");

  // Wenn min oder max nicht gesetzt sind
  if (min < 0 || max < 0) {
    // Erfolgt keine Umrechnung, sondern die Übernahme des float Wertes
    Serial.println("  Keine Umrechnung, direkte Anzeige");
    calcedValue = sensorValue;
  } else {
    // Plausi-Prüfung
    if (sensorValue >= min && sensorValue <= max && max > min) {
      if (formatMin < 0 || formatMax < 0) {
        Serial.println("  Umrechnung in Prozentwert");
        calcedValue = (sensorValue - min) / (max - min) * 100;
      } else {
        Serial.println("  Umrechnung in anteiligen Wert");
        calcedValue = ((formatMax - formatMin) * (sensorValue - min) / (max - min)) + formatMin;
      }
    } else {
      Serial.println("  Umrechnung nicht möglich");
      calcedValue = sensorValue;
    }
  }
  Serial.print("  dtostrf: calcedValue=");
  Serial.print(calcedValue);
  Serial.print(" precision=");
  Serial.print(precision);
  dtostrf(calcedValue, 0, precision, stringBuffer);
  Serial.print("  stringBuffer: ");
  Serial.println(stringBuffer);
  sprintf(displayValue, formatString, stringBuffer);
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

bool getSensorCategoryByAddress(const SensorAddress manufacturerCode, SensorCategory &sensorCategory) {
  char code[17];
  byte firstByte;
  // Überprüfe nur das erste Byte des char-Arrays
  strcpy(code, manufacturerCode);
  firstByte = convertHexCStringToByte(code);

  switch (firstByte) {
    case 0x28:
      sensorCategory = C_DS18B20; // DS18B20 Temperatursensor
      return true;
    case 0x10:
      sensorCategory = C_DS18S20; // DS18S20 Temperatursensor
      return true;
    case 0x22:
      sensorCategory = C_DS1822; // DS1822 Temperatursensor
      return true;
    case 0x26:
      sensorCategory = C_DS2438; // DS2438 (Smart Battery Monitor)
      return true;
    default:
      sensorCategory = C_UNKNOWN;
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
