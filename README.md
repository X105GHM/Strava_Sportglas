# Sportglas – ESP32-Firmware

Eigenständiges PlatformIO-Projekt für die Sportglas-Platine mit
ESP32-PICO-MINI-02-N8R2. Verwendet ESP-IDF 5.5.3, PlatformIO espressif32 6.13.0
und C++20. Das Modul hat 8 MB Flash; PSRAM wird nicht verwendet.

## Öffnen und bauen

In VS Code diesen Projektordner mit PlatformIO öffnen. Alternativ im Terminal
im Projektverzeichnis ausführen:

```powershell
pio run -e esp32-pico-mini02
```

PlatformIO lädt die benötigten Werkzeuge und Komponenten beim ersten Build.
Das Projekt enthält seine eigene Build-Konfiguration und ist unabhängig von
anderen lokalen Projekten.

## Upload

Der Upload verwendet den normalen esptool-Ablauf über einen USB-UART-Adapter
mit 3,3-V-Logik. Den COM-Port an den angeschlossenen Adapter anpassen:

```powershell
pio run -e esp32-pico-mini02 -t upload --upload-port COM4
```

Ohne Auto-Reset-Beschaltung GPIO0/BOOT auf GND halten, EN/RESET kurz auslösen
und den Upload starten. GPIO0 während des Verbindungsaufbaus wieder freigeben.
Falls die Firmware nach dem Upload nicht automatisch startet, mit freiem GPIO0
noch einmal EN/RESET auslösen.

Die vorhandenen NVS-Daten bleiben beim normalen Firmware-Upload erhalten.

| Anschluss am ESP | Verbindung zum USB-UART-Adapter |
|---|---|
| GPIO1 / TX | RX |
| GPIO3 / RX | TX |
| GND | GND |
| GPIO0 | BOOT, für den Download-Modus auf GND |
| EN | RESET |

Die Platine benötigt ihre normale Versorgung. UART-Signale sind für 3,3 V ausgelegt.

## Zugangsdaten und API

Die lokalen Zugangsdaten stehen in `src/config/secrets.local.hpp` und wurden
für dieses lokale Projekt übernommen. Die Datei wird von Git ignoriert.
Bei einer frischen Kopie `src/config/secrets.example.hpp` nach
`src/config/secrets.local.hpp` kopieren und folgende Werte eintragen:

- `kSsid`: WLAN-Name
- `kPassword`: WLAN-Passwort
- `kEndpoint`: vollständige HTTPS-Adresse der Wochen-API
- `kDeviceToken`: Bearer-Gerätetoken mit 64 Hexzeichen
- `kEnabled`: `true` für die lokalen Zugangsdaten; `false` für Zugangsdaten aus NVS

Der Profilname steht in `src/config/Config.hpp` unter `kProfileName` und wird
als `X-Profile-Name` gesendet. Aktuell ist das Profil `Danilo` eingestellt.
Die API-Adresse lautet `https://sport.x105ghm.com/api/v1/glass/weekly`.
TLS prüft Zertifikat und Hostnamen. Nach Änderungen an Zugangsdaten erneut bauen
und hochladen.

## Hardware

| Funktion | Anschluss |
|---|---|
| LED-Daten, 43 WS2812 in GRB-Reihenfolge | GPIO21 |
| 5-V-Versorgung der LEDs freigeben, HIGH = ein | GPIO19 |
| Touch-Elektrode, Kanal T0 | GPIO4 |
| OE# des AHCT-Buffers | fest GND |

Die Radfahr-Anzeige verwendet LEDs 20–22, vom DIN-Ende ab 1 gezählt.
Aktuell ist nur Radfahren aktiv; die übrigen LEDs bilden den warmweißen
Hintergrund. Die Farbskala reicht von Rot über Gelb bis Grün. Die Firmware
begrenzt den rechnerischen LED-Strom auf 900 mA.

## Bedienung und Schlafbetrieb

Touch schaltet `AUS → SPORT → WARMWEISS → AUS`. Beim Einschalten das Pad
für die Kalibrierung zunächst freilassen. Ein gehaltener Finger zählt einmal;
vor der nächsten Berührung muss das Pad wieder freigegeben sein.

Umschalten zeigt die gespeicherten Werte. Ein API-Abruf erfolgt nach dem Start
und anschließend stündlich, bei Fehlern nach fünf Minuten. Fehlgeschlagene
Abrufe behalten den letzten gültigen Datensatz. Neue Werte verändern eine
bereits sichtbare Sportfarbe mit einem Fade von 800 ms.

Nach einer Berührung bleibt der ESP zwei Sekunden wach. Danach verwendet er
Light Sleep mit Touch-Wakeup. Alle 30 Sekunden prüft ein kurzer Timer-Wakeup
den Sensor; dabei wird keine zusätzliche API-Abfrage gestartet. WLAN und HTTPS
laufen in einem eigenen Task. Light Sleep benötigt mehr Ruhestrom als Deep Sleep.

Der Modus wird nach Ende der Bedienung gespeichert. Bei sofortigem Trennen der
Versorgung kann deshalb noch der vorherige Modus gespeichert sein.

Reguläre Anwendungs- und Bootloader-Logs sind ausgeschaltet. Eine ROM-Startmeldung
des ESP32 kann beim Reset weiterhin erscheinen. Den seriellen Monitor öffnet man mit:

```powershell
pio device monitor -e esp32-pico-mini02 --port COM4
```

## Dateien und Einstellungen

| Datei oder Verzeichnis | Inhalt |
|---|---|
| `platformio.ini` | Board, Toolchain, Flashgröße und Upload |
| `sdkconfig.defaults` | ESP-IDF-Vorgaben, Pins, Darstellung und Log-Einstellungen |
| `partitions.csv` | Flashaufteilung einschließlich NVS |
| `src/AppController.cpp` | Bedienung, Abrufplan, Anzeige und Sleep-Ablauf |
| `src/display/` | Farben, Übergänge und LED-Ausgabe |
| `src/input/` | Touch-Messung und Entprellung |
| `src/network/` | WLAN, HTTPS und Prüfung der API-Antwort |
| `src/storage/` | Modus, letzter Datensatz und Zugangsdaten in NVS |
| `src/power/` | LED-Versorgung und Light Sleep |
| `.clang-format`, `.vscode/settings.json` | Formatierung und VS-Code-Einstellungen |

Änderungen an den ESP-IDF-Vorgaben in `sdkconfig.defaults` vornehmen. Die Datei
`SyncDisplayConfig.cmake` übernimmt Display- und Log-Vorgaben auch in eine bereits
erzeugte sdkconfig. Bei Bedarf die Konfiguration neu erzeugen:

```powershell
pio run -e esp32-pico-mini02 -t clean
pio run -e esp32-pico-mini02
```

Formatierung: vier Leerzeichen, Klammern in eigener Zeile, eingerückte Namespaces
und Präprozessor-Anweisungen. Kurze `if`-Anweisungen dürfen einzeilig bleiben.
Das Zeilenlimit in `.clang-format` beträgt 240 Zeichen. Der VS-Code-Formatter
behält die vorhandene Einrückung der Präprozessor-Anweisungen bei.
