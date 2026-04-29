# Laborversuch: Kamerabasierter Linienfolger mit ESP32-CAM

## 1. Übersicht

In diesem Versuch programmieren Sie einen autonomen Rover, der mithilfe der eingebauten Kamera des ESP32-CAM einer hellen Linie (Kreppband) auf dunklem Untergrund folgt. Sie erhalten eine funktionsfähige Basissoftware und optimieren diese für zuverlässiges Linienfolgen.

**Lernziele:**
- Embedded-Bildverarbeitung auf ressourcenbeschränkter Hardware
- Regelungstechnik (Schwellwerte, Totzone, Geschwindigkeit)
- Debugging über serielle Schnittstelle

---

## 2. Hardware-Aufbau

### 2.1 Komponenten
| Komponente | Beschreibung |
|---|---|
| ESP32-CAM | AI Thinker Modul mit OV2640 Kamera |
| ESP32-CAM-MB | Expansion Board mit CH340 USB-Serial und BOOT-Taste |
| L298N | Dual-H-Brücke Motor-Treiber |
| 2x DC-Motor | Angetrieben über L298N |
| Batterie | Externe Stromversorgung für Motoren |

### 2.2 Pin-Belegung ESP32-CAM → L298N

| ESP32 GPIO | L298N Eingang | Funktion |
|---|---|---|
| **IO2** | IN1 | Motor A (Richtung 1) |
| **IO14** | IN2 | Motor A (Richtung 2) |
| **IO15** | IN3 | Motor B (Richtung 1) |
| **IO13** | IN4 | Motor B (Richtung 2) |

**Motor-Steuerung (L298N Wahrheitstabelle):**

| Aktion | IN1 (IO2) | IN2 (IO14) | IN3 (IO15) | IN4 (IO13) |
|---|---|---|---|---|
| Vorwärts | LOW | **HIGH** | LOW | **HIGH** |
| Rückwärts | **HIGH** | LOW | **HIGH** | LOW |
| Drehen Links | LOW | LOW | LOW | **HIGH** |
| Drehen Rechts | LOW | **HIGH** | LOW | LOW |
| Stopp | LOW | LOW | LOW | LOW |

### 2.3 Wichtige Hardware-Hinweise

> **⚠ Kamera ist kopfüber montiert!**
> - Die X-Achse im Bild ist gespiegelt
> - Das "obere" Bilddrittel zeigt den Boden vor dem Rover
> - Im Code muss `x` gespiegelt werden: `IMG_W - 1 - x`

> **⚠ GPIO15 ist ein Strapping-Pin!**
> - Beim Boot ist GPIO15 automatisch HIGH → rechter Motor dreht kurz
> - Im Code werden alle Motor-Pins als **allererste Aktion** in `setup()` auf LOW gesetzt

> **⚠ GPIO4 = Flash-LED!**
> - Muss explizit ausgeschaltet werden, sonst blendet sie die Kamera

---

## 3. Software-Umgebung

### 3.1 Voraussetzungen
- **Arduino IDE 2.x** installiert
- **ESP32 Board-Paket** installiert (Version 3.x)
  - Arduino IDE → Einstellungen → Zusätzliche Boardverwalter-URLs:
    `https://espressif.github.io/arduino-esp32/package_esp32_index.json`
  - Werkzeuge → Board → Boardverwalter → "esp32" suchen und installieren

### 3.2 Board-Einstellungen
- **Board:** `AI Thinker ESP32-CAM`
- **Port:** COM-Port des CH340 (z.B. COM4)
- **Upload Speed:** 115200

### 3.3 Serial Monitor
- **Baudrate:** 115200
- Im Serial Monitor werden Debug-Informationen ausgegeben

---

## 4. Flash-Vorgang

Der ESP32-CAM hat keinen Auto-Reset. Das Flashen erfordert eine manuelle Sequenz:

### Schritt-für-Schritt:
1. **Batterie trennen** (externe Stromversorgung abklemmen)
2. **USB-Kabel abziehen**
3. **BOOT-Taste** (rechte Taste auf dem Expansion Board) **gedrückt halten**
4. **USB-Kabel einstecken** (BOOT weiter halten!)
5. **BOOT-Taste loslassen**
6. In der Arduino IDE: **Sketch → Hochladen** klicken
7. Warten bis "Done uploading" erscheint
8. **USB-Kabel kurz abziehen und wieder einstecken** (Reset)
9. **Batterie wieder anschließen**

> **Tipp:** Der Serial Monitor der Arduino IDE muss beim Flashen **geschlossen** sein, da er den COM-Port blockiert!

---

## 5. Test-Sketch: RoverRC (Fernsteuerung)

Bevor Sie am Linienfolger arbeiten, testen Sie die Hardware mit dem **RoverRC**-Sketch. Dieser ermöglicht Fernsteuerung über den Serial Monitor.

### Befehle (im Serial Monitor eingeben):
| Taste | Aktion |
|---|---|
| `w` | Vorwärts |
| `s` | Rückwärts |
| `a` | Drehen Links |
| `d` | Drehen Rechts |
| `x` | Stopp |
| `1`-`4` | Einzelnen L298N-Eingang testen |
| `0` | Alle Ausgänge LOW |

### Kamera-Offset wird automatisch alle 2 Sekunden angezeigt:
```
[Status: STOP] [CAM] avg=63 thr=93 brt=1150 c=87  --> OFFSET=7 (Linie MITTE)
```

**Prüfen Sie:**
- [ ] `w` → Rover fährt vorwärts
- [ ] `s` → Rover fährt rückwärts
- [ ] `a` → Rover dreht links
- [ ] `d` → Rover dreht rechts
- [ ] Linie mittig vor Kamera → OFFSET ≈ 0
- [ ] Linie links → OFFSET negativ
- [ ] Linie rechts → OFFSET positiv

---

## 6. Linienfolger: Funktionsweise

### 6.1 Algorithmus

```
┌─────────────┐
│ Kamerabild  │  160x120 Pixel, Graustufen
│  aufnehmen  │
└──────┬──────┘
       ▼
┌─────────────┐
│ Oberes 1/3  │  Zeilen 0-39 (kopfüber = Boden vor Rover)
│  scannen    │
└──────┬──────┘
       ▼
┌─────────────┐
│ Helle Pixel │  Pixel > Schwellwert → "Linie"
│   finden    │  X-Koordinate spiegeln (kopfüber)
└──────┬──────┘
       ▼
┌─────────────┐
│ Schwerpunkt │  centroid = Durchschnitt aller hellen X-Positionen
│  berechnen  │  Bildmitte = 80
└──────┬──────┘
       ▼
┌─────────────┐
│   Fehler    │  error = centroid - 80
│  berechnen  │  Negativ = Linie links, Positiv = rechts
└──────┬──────┘
       ▼
┌─────────────────────────────────────┐
│          Steuerung                  │
│  error < -DEAD_ZONE → Links        │
│  error >  DEAD_ZONE → Rechts       │
│  sonst              → Geradeaus    │
│  Keine Linie (brt<15) → Suche     │
└─────────────────────────────────────┘
```

### 6.2 Wichtige Parameter

| Parameter | Aktueller Wert | Beschreibung |
|---|---|---|
| `SPEED_FWD` | 77 (~30%) | Geschwindigkeit geradeaus (0-255) |
| `SPEED_TURN` | 77 (~30%) | Geschwindigkeit beim Abbiegen (0-255) |
| `SPEED_SPIN` | 90 (~35%) | Geschwindigkeit beim Suchen/Drehen |
| `DEAD_ZONE` | 20 | Toleranzbereich um Bildmitte (±20 Pixel) |
| `calThreshold` | automatisch | Helligkeitsschwelle (Kalibrierung beim Start) |

### 6.3 Kalibrierung

Beim Start misst der Rover 3 Sekunden lang die durchschnittliche Helligkeit des Bodens. Der Schwellwert wird auf `Durchschnitt + 30` gesetzt. **Der Rover muss während der Kalibrierung auf der Bahn stehen**, aber möglichst so, dass die Kamera hauptsächlich den dunklen Boden sieht.

---

## 7. Aufgabenstellung

### Aufgabe 1: Hardware-Test
Flashen Sie den **RoverRC**-Sketch und überprüfen Sie alle Funktionen gemäß Checkliste in Abschnitt 5.

### Aufgabe 2: Linienfolger testen
Flashen Sie den **CamLineFollower**-Sketch. Stellen Sie den Rover auf die Bahn und beobachten Sie sein Verhalten. Dokumentieren Sie:
- Kalibrierungswerte (avg, Schwelle)
- Verhalten auf geraden Strecken
- Verhalten in Kurven
- Probleme (z.B. Linie verloren, zu schnell, zu langsam)

### Aufgabe 3: Parameter optimieren
Optimieren Sie folgende Parameter für zuverlässiges Linienfolgen:

| Parameter | Einfluss |
|---|---|
| `SPEED_FWD` | Zu hoch → überfährt Kurven. Zu niedrig → bleibt stehen |
| `SPEED_TURN` | Zu hoch → übersteuert. Zu niedrig → reagiert zu langsam |
| `DEAD_ZONE` | Zu groß → reagiert spät. Zu klein → zittert hin und her |
| `SCAN_END` | Mehr Zeilen → sieht weiter voraus, aber unschärfer |
| Schwellwert-Offset (30) | Höher → weniger Fehlerkennungen. Niedriger → empfindlicher |

### Aufgabe 4 (optional): Regelung verbessern
Die aktuelle Steuerung ist ein einfacher Schwellwert-Regler (Bang-Bang). Implementieren Sie eine der folgenden Verbesserungen:

- **Proportionalregler:** Geschwindigkeit abhängig vom Fehler (je weiter die Linie vom Zentrum, desto stärker die Korrektur)
- **Vorausschau:** Mehrere Bildzeilen analysieren, um Kurven frühzeitig zu erkennen
- **Gedächtnis:** Letzte bekannte Linienposition merken für bessere Suche

---

## 8. Abgabe

- Optimierter Arduino-Sketch (.ino)
- Kurze Dokumentation (max. 2 Seiten):
  - Gewählte Parameterwerte und Begründung
  - Beschreibung der Optimierungen
  - Ergebnisse (fährt der Rover zuverlässig auf der Linie?)

---

## 9. Troubleshooting

| Problem | Lösung |
|---|---|
| Flash schlägt fehl | Serial Monitor schließen! BOOT-Sequenz wiederholen |
| Motoren drehen beim Boot | Normal (GPIO15 Strapping-Pin), stoppt nach <1s |
| Kein Kamerabild | Flash-LED muss AUS sein (GPIO4 = LOW) |
| Rover dreht nur im Kreis | Kalibrierung prüfen – Rover auf dunklem Boden starten |
| Rover erkennt Linie nicht | Beleuchtung prüfen, ggf. Schwellwert-Offset anpassen |
| Brownout / Reset | Batterie voll? USB allein reicht nicht für Motoren |
| COM-Port nicht gefunden | CH340-Treiber installiert? USB-Kabel mit Daten? |
