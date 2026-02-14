1-Wire Busmaster, der die Temperaturen (und Füllstände) angeschlossener 1-Wire Sensoren abfragt und auf einem Display anzeigt sowie per WiFi an einen MQTT Host überträgt.
Durch geringen Stromverbrauch ideal fürs Wohnmobil geeignet.

<img width="1572" height="876" alt="grafik" src="https://github.com/user-attachments/assets/093f617e-85d0-4e78-815d-8d39a0f4f616" />

# Hardware
## Busmaster mit Display
+ Arduino Nano 33 IoT
+ Display Joy-IT SBC-LCD02 (ILI9163C 1.44" TFT 128x128)
+ Sensor-Button
+ Streifenplatine
+ Gehäuse (Bedieneinheit Gehäuse-Basis.stl / Bedieneinheit Gehäuse-Front.stl)
+ Optional: Temperatursensor DS18B20

![Streifenplatinenlayout](https://github.com/mrapnx/1wireTemp/blob/a99a4a109de7bc7a8899633e41c8733ef497bb1b/1WireTemp%20-%20Streifenplatine.fzz.png)
  
## Adapter für Tanksonde
+ [Votronic Tanksonde 12-24k](https://www.votronic.de/tankgeber/tankelektrode-14-24-k-und-15-50-k/)
+ Streifenplatine
+ 1-Wire DS2438 Sensor
+ RJ45 Buchse
+ 3 Port-Klemme
+ Gehäuse (DS2438 Sondenadapter Gehäuse-Unterteil.stl)
  
![Streifenplatinenlayout](https://github.com/mrapnx/1wireTemp/blob/a99a4a109de7bc7a8899633e41c8733ef497bb1b/DS2438%20Sondenadapter.fzz.png)

## 12V Injektor
+ 2x RJ 45 Buchsen
+ 2 Port-Klemme
+ Downstepper

![Streifenplatinenlayout](https://github.com/mrapnx/1wireTemp/blob/a99a4a109de7bc7a8899633e41c8733ef497bb1b/1Wire%20Injektor.fzz.png)

# Software
+ VS Code
+ Plaftorm.IO
=> Einfach Repo in Platform.IO öffnen
