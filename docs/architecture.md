# Technische Übersicht der Firmware

## Grundlage und Geltungsbereich

Diese Übersicht beschreibt den Quelltext im Repository, keine geplante Architektur. Grundlage sind die Projektdateien, insbesondere [AGENTS.md](../AGENTS.md), [platformio.ini](../platformio.ini), [src/main.cpp](../src/main.cpp), [src/gnss_service.cpp](../src/gnss_service.cpp), [src/vars.cpp](../src/vars.cpp) und [mofa.eez-project](../mofa.eez-project) einschließlich der generierten Dateien unter `src/ui`.

Pruefstand dieser Fassung: **GNSS-FIFO Variante A, gezielte CASE-Nacharbeit B1 auf Basis `2673b455`**. Aktuelle RAM-/Flashwerte und Pruefergebnisse stehen im [P3-Pruefbericht](../test/gnss_logger/README.md). Fruehere Messungen unten gelten nicht als Nachweis fuer diese Aenderung.

- **Früherer Buildbefund (für `0e174bb` nicht erneut geprüft):** `pio run -e giga_r1_m7` läuft fehlerfrei durch (Flash 60,6 % von 786 432 Byte, RAM 24,6 %).
- **Pruefungen:** Sieben C++-Compile-only-Suiten (`gnss_epoch`, `local_time`, `log_clock`, `rpm_filter`, `ui_self_test`, `gnss_log_fifo`, `gnss_logger`) pruefen `constexpr`-Richtlinien mit `static_assert`. Dazu kommen acht Python-Quellvertragspruefungen in `test/gnss_logger/test_integration.py`. `python test/gnss_logger/run_tests.py` fuehrt beide Gruppen aus; der PlatformIO-Firmwarebuild fuehrt sie nicht aus. Ergebnisse und Grenzen sind im P3-Pruefbericht dokumentiert.
- **Frühere Hardwaremessung (für `0e174bb` nicht wiederholt):** Gemessen wurden nur die Stackspitzen der Threads auf dem Board (Abschnitt 2). Während dieser Messung lieferte der GNSS-Empfänger keine Epochen. Alle anderen Aussagen über das Laufzeitverhalten sind aus dem Code abgeleitet; unklare oder nicht nachgewiesene Eigenschaften sind ausdrücklich gekennzeichnet.

Die Zielhardware ist laut `AGENTS.md` der Arduino GIGA R1 M7 mit STM32H747 und Cortex-M7. Das Framework ist Arduino Mbed; `platformio.ini` verwendet die Plattform `ststm32`, das Board `giga_r1_m7`, `framework = arduino` und die Build-Umgebung `giga_r1_m7`. Eine Firmware für den zweiten Prozessorkern oder eine Kommunikation mit ihm ist nicht enthalten.

## 1. Quelldateien und Aufgaben

| Datei oder Verzeichnis | Aufgabe |
| --- | --- |
| [src/main.cpp](../src/main.cpp) | Hardwareobjekte, Konfiguration, Arduino-Einstiegspunkte, Drehzahlinterrupt, die RTOS-Threads für Drehzahl, Temperatur, Logging und Display sowie USB-Mount und Dateizugriffe. Der GNSS-Thread wird aus `setup()` gestartet, liegt aber in `gnss_service.cpp`. |
| [src/gnss_service.h](../src/gnss_service.h), [gnss_service.cpp](../src/gnss_service.cpp) | Eigener GNSS-Thread (`gnssThread`, statischer 4-KiB-Stack). Konfiguriert den Empfänger, liest PVT-Epochen, prüft Reihenfolge und Rate, setzt einmal pro Start die RTC und veröffentlicht den jeweils letzten Stand als `GnssSnapshot`. `readGnssSnapshot()` liefert eine Kopie und markiert Daten nach 1,5 s ohne neue Epoche als veraltet. |
| [src/gnss_log_fifo.h](../src/gnss_log_fifo.h) | Hardwareunabhaengiger statischer GNSS-Ring, 64-Byte-Logdatensatz, Statistik/Reservierung und injizierbare gepruefte Dateioperationen; constexpr-Policy erlaubt Compile-time-Fehlerinjektionstests ohne Hostcompiler. |
| [src/gnss_epoch.h](../src/gnss_epoch.h) | Reine Richtlinienlogik ohne Hardwarebezug: `EpochTracker` (Reihenfolge, Duplikate, Zeitsprünge, neue Basis nach drei regelmäßigen Schritten eines abgelehnten Stroms) und `RateMonitor` (Abstand der Epochen gegen 200 ms). |
| [src/rpm_display_filter.h](../src/rpm_display_filter.h) | Glättung der angezeigten Drehzahl mit Totzone und Hysterese. Wirkt nur auf die Anzeige; Rohdrehzahl, Logging und Warnausgang nutzen sie nicht. |
| [src/ui_self_test.h](../src/ui_self_test.h) | Startdurchlauf von 3 s: schiebt Drehzahl, Geschwindigkeit, Temperatur, Warnsymbol, Uhr und alle GPS-Statustexte durch ihren Wertebereich. |
| [src/local_time.h](../src/local_time.h) | Umrechnung UTC in Wanduhrzeit: UTC+1 (MEZ) beziehungsweise UTC+2 (MESZ) nach der EU-Regel (letzter Sonntag im März/Oktober, 01:00 UTC). |
| [src/log_clock.h](../src/log_clock.h) | Eine monoton laufende Uhr für die Logzeitstempel, einmalig auf die erste gültige GNSS-Zeit verankert. |
| [src/vars.h](../src/vars.h), [vars.cpp](../src/vars.cpp) | UI-Datenmodell und Textpuffer. Helfer: `set_ui_velocity()`, `set_ui_temperature()`, `set_ui_time(int64_t utcSeconds)`, `set_ui_time_unavailable()`, `set_ui_rpm_warning()`, `set_ui_gps_status()`, `set_ui_self_test()`. Implementiert zudem die von EEZ erwarteten C-Getter und -Setter. |
| [src/ui/ui.c](../src/ui/ui.c), [ui.h](../src/ui/ui.h) | Generierter UI-Einstieg mit `ui_init()`, `ui_tick()` und `loadScreen()`. |
| [src/ui/screens.c](../src/ui/screens.c), [screens.h](../src/ui/screens.h) | Erzeugt den einzigen Bildschirm `Main`, hält LVGL-Objektzeiger in `objects_t` und aktualisiert die Widgets über Getter. |
| [src/ui/vars.h](../src/ui/vars.h) | Generierte Schnittstelle mit 15 UI-Variablen; von `src/vars.h` zu unterscheiden. |
| [src/ui/images.c](../src/ui/images.c), [images.h](../src/ui/images.h), `src/ui/ui_image_*.c` | Bildtabelle und eingebettete LVGL-Bilddaten für `icon`, `rpm_warning` und `gps`. |
| [src/ui/fonts.h](../src/ui/fonts.h), `src/ui/ui_font_*.c` | Eingebettete Fontdaten für die vier projektspezifischen Schriftgrößen. |
| [src/ui/actions.h](../src/ui/actions.h), [styles.c](../src/ui/styles.c), [styles.h](../src/ui/styles.h), [structs.h](../src/ui/structs.h) | Generierte Gerüste ohne fachliche Inhalte. Die verwendeten Widget-Stile stehen direkt in `screens.c`. |
| [mofa.eez-project](../mofa.eez-project) | EEZ-Studio-Quelldatei für Layout, Variablen, Fonts und Assets; Quelle für `src/ui`. |
| [fonts](../fonts) | `aurebesh.ttf` und `digital.ttf`. Die konfigurierten EEZ-Fonts verwenden `aurebesh.ttf`; `digital.ttf` wird von dieser UI nicht genutzt. |
| [test](../test) | Sieben C++-Compile-only-Suiten (`gnss_epoch`, `local_time`, `log_clock`, `rpm_filter`, `ui_self_test`, `gnss_log_fifo`, `gnss_logger`) und acht statische Python-Quellvertragspruefungen. Gemeinsamer Runner: `python test/gnss_logger/run_tests.py`; er kompiliert die `static_assert`-Suiten mit dem vorhandenen ARM-Compiler und startet `test_integration.py`. Keine Laufzeitsimulation von RTOS, USB oder RTC. `pio run` uebersetzt `test/` nicht. `include` und `lib` enthalten nur die PlatformIO-README. |
| [platformio.ini](../platformio.ini) | Build-Umgebung, Bibliotheksvorgaben, LVGL-Buildflags, Monitorrate und Einbindung lokaler Zusatzkonfigurationen. |

Die [Projekt-README](../README.md) beschreibt den Ablauf EEZ-Generierung vor PlatformIO-Kompilierung. Für Uploads gilt unabhängig davon die ausdrückliche Freigabepflicht aus `AGENTS.md`. `src/ui` ist generierter, versionierter EEZ-Code und wird nur über EEZ Studio geändert.

## 2. Initialisierung und Hauptprogrammablauf

### Startreihenfolge

`setup()` in [src/main.cpp](../src/main.cpp) führt der Reihe nach aus:

1. `Serial.begin(115200)`, `Wire.begin()` und I²C-Takt 400 kHz.
2. Pin 3 als Drehzahleingang mit `INPUT_PULLDOWN`, Pin 2 als Ausgang, `rpmISR()` auf steigende Flanken.
3. `sensors_DS.begin()` für den OneWire-Temperatursensor.
4. `Display.begin()` und `TouchDetector.begin()`, danach `lv_tick_set_cb(millis)` ohne separaten `lv_init()`-Aufruf.
5. Anfangszustand der UI (`set_var_gps_online(false)`, `set_ui_velocity(-1)`, `set_ui_time_unavailable()`) und `ui_init()`.
6. USB-Callbacks registrieren; connect(), Mount, Unmount und Dateizugriffe erfolgen ausschliesslich im Loggerthread.
7. Threads starten: `displayThread`, `sensorThread`, `loggingThread`, `rpmSensorThread`, zuletzt `startGnss()`.

Der GNSS-Empfänger blockiert den Start nicht mehr. Fehlt er, wiederholt der GNSS-Thread die Verbindung mit wachsender Pause (1 s, 2 s, 4 s, höchstens 8 s); Display, Drehzahl, Temperatur und Logging laufen weiter, und die UI zeigt „GPS: searching“ beziehungsweise (antwortet das Modul nicht am I²C) „GPS: no module“ in Rot. Schlägt der Start des GNSS-Threads selbst fehl, meldet die UI „GPS: thread error“.

`Display.begin()` kommt aus der Bibliothek `Arduino_H7_Video`. Deren Symbole enthalten den LVGL-Flush-Callback (`lvgl_displayFlushing`), den Touch-Callback (`_lvglTouchCb`) und einen eigenen Tick-Thread. Im aktuellen `setup()` wird `lv_init()` nicht zusätzlich aufgerufen. Die Initialisierung erfolgt über `Display.begin()`; das Bibliotheksverhalten wurde hier nicht erneut auf Hardware geprüft.

### Threads und Stacks

`loop()` schläft lediglich jeweils zehn Sekunden. Die Arbeit erfolgt in Mbed-RTOS-Threads und im Interrupt. Die Stackspitzen stammen aus einer Messung auf dem Board über die RTX-Wasserzeichen (Selbsttest und laufende UI, einige Dutzend Sekunden); sie sind Momentaufnahmen, keine Garantien. Der Messcode ist nicht im Repository.

| Thread / Funktion | Aufgabe und zeitliche Steuerung | Stack (Spitze / reserviert) |
| --- | --- | --- |
| `rpmISR()` | Übernimmt `micros()` und den Abstand zum letzten Puls nur, wenn er größer als 1000 µs ist. Zwei `volatile uint32_t`-Werte. | Interruptstack: 472 / 1024 B |
| `rpmSensorTask()` | Liest das Wertepaar des Interrupts im kritischen Abschnitt, berechnet die Drehzahl (zwei Pulse pro Umdrehung), setzt sie nach 150 ms ohne Puls auf null. Warnschwelle mit Hysterese: ein ab 15000 min⁻¹, aus unter 14500 min⁻¹; sie schaltet Pin 2 und `curRpmWarning`. Veröffentlicht unter `dataMutex`. Pause 1 ms. | 200 / 4096 B |
| `sensorTask()` | Nur Temperatur: einmal pro Sekunde `requestTemperatures()` und Sensor 0 lesen, Ergebnis unter `dataMutex` in `curTempC`. Pause 10 ms. | 232 / 4096 B |
| `loggingTask()` | Alleiniger USB-/Dateibesitzer; Legacy-Sampling nominal 10 Hz, gepruefte Bloecke mit 1-s-Teilblockfrist, separate GNSS-Segmente mit bis zu 20 Epochen je Runde und periodischer Statistik. Kein Leeren vorhandener Dateien. | 4096 B reserviert; historische Spitze 520 B gilt nicht fuer FIFO-Stand. |
| `displayTask()` | Besitzt alle UI-Zustände: liest Messwerte und GNSS-Stand, setzt die Modellvariablen, führt den Selbsttest aus, ruft `lv_task_handler()` und `ui_tick()` auf. Pause 2 bis 20 ms. | **4608 / 8192 B** |
| `gnssThread` | Siehe unten. | 1176 / 4096 B (ohne Epochen gemessen) |
| `main` (`setup()`/`loop()`) | | 1128 / 32768 B |
| Bibliotheksthreads | USB-Host (zugeordnet über die Stackgröße 3072): 208 / 3072 B; LVGL-Tick: 112 / 4096 B; `USBevt` des Arduino-Cores: 136 / 256 B; RTX-Idle- und Timer-Thread. | |

Der Displaythread braucht deutlich mehr als der Mbed-Standardstack von 4096 B: LVGL zeichnet jedes Element synchron im Zeichenereignis seines Widgets, und der Software-Renderer läuft am Ende der Rekursion über den Objektbaum. Er hat deshalb 8 KiB. Eine statische Aufrufgraph-Analyse der Firmware ergab etwa 4,3 KiB zuzüglich Ausnahmerahmen. Wer Verschachtelung oder Stile in EEZ Studio erhöht, sollte die Spitze erneut messen.

Die genannten Raten sind aus Konstanten, Zeitvergleichen und Schlafpausen abgeleitet. Sie sind keine nachgewiesenen festen Abtastraten: Bibliotheksaufrufe, Temperaturmessung, Dateizugriffe und Scheduling können zusätzliche Zeit beanspruchen.

### GNSS-Thread

`gnssTask()` in [src/gnss_service.cpp](../src/gnss_service.cpp) läuft in einer Dauerschleife:

1. `gnss.begin(Wire, 0x42, 250 ms)`, danach Konfiguration: UBX auf I²C, Messperiode 200 ms, Navigationsrate 1, anschließend eine frische CFG-RATE-Abfrage; nur bei 200 ms und Rate 1 geht es weiter, dann Auto-PVT an. `setNavigationFrequency(1)` wird bewusst nicht verwendet, weil es 200 ms auf 1000 ms überschriebe.
2. Alle 10 ms wird mit `getPVT(0)` auf eine neue Epoche geprüft. `EpochTracker` ordnet sie ein (Basis, neu, Duplikat, alt, ungültig); `RateMonitor` prüft den Abstand der Epochen.
3. Eine neue Sitzung veroeffentlicht erst nach drei aufeinanderfolgenden 200-ms-Schritten. Jede danach akzeptierte Epoche aktualisiert den Snapshot und wird genau einmal im separaten FIFO eingereiht oder als Ueberlauf gezaehlt, auch ohne gueltige Position/UTC. Andere Zustandsveroeffentlichungen werden nicht eingereiht.
4. Ohne neue Epoche für 1,5 s, bei dauerhaft falscher Rate oder zwei fehlgeschlagenen CFG-RATE-Prüfungen (alle 10 s) wird die Verbindung beendet und neu aufgebaut.
5. Wird die erste gültige UTC-Zeit erkannt, nachdem schon Epochen veröffentlicht wurden, beginnt der Tracker neu; die Sitzung wird dafür beendet. Die UI zeigt kurz „GPS: reconnecting“.
6. Die RTC wird einmal pro Start mit der ersten gültigen UTC-Zeit gesetzt (`set_time`, auf die nächste ganze Sekunde gerundet). Spätere Sitzungen verändern sie nicht.
7. Alle 10 s schreibt der Thread eine Zeile `GNSS DIAG …` auf die serielle Schnittstelle.

Der Snapshot enthält Verbindungszustand, Fix-Gültigkeit, Position, Höhe, Geschwindigkeit, Kurs, Genauigkeit, UTC-Zeit und `clockSynchronized`. `positionValid` verlangt `gnssFixOK`, keine ungültige Position und Fixtyp 2, 3 oder 4; `altitudeValid` zusätzlich Fixtyp ungleich 2.

### USB-Dateien und Messdaten

`USBHostMSD msd` und `mbed::FATFileSystem fs("usb")` stellen den Massenspeicherzugriff bereit. Nur `loggingTask()` ruft `connect()`, Mount, Unmount und Dateioperationen auf. Callbacks setzen ausschliesslich lockfreie atomare Zustaende; eine atomare Detach-Generation erkennt auch ein Entfernen mit raschem Wiederanschluss zwischen zwei Loggerrunden. Kein Unmount laeuft gleichzeitig mit einer Logger-Dateioperation. Ein fehlgeschlagenes Unmount blockiert erneute Mounts und Writes bis zum erfolgreichen Unmount. Der Logger unternimmt hoechstens einen Mountversuch pro Sekunde, ohne enge Wiederholungsschleife.

Die bestehenden Dateien werden beim Mount **nicht mehr geleert**. Nur eine neue oder leere Datei erhaelt ihre bisherige Kopfzeile; existierende Daten bleiben erhalten:

- `/usb/DRZ.txt`: `Time_UTC`, `RPM_Umin-1`.
- `/usb/SENSOR.txt`: unveraendert zehn Spalten fuer Zeit, Temperatur, Breite, Laenge, Hoehe, Geschwindigkeit, Kurs, horizontale Genauigkeit, `gpsOK` und `gpsFix3D`.

Diese Legacy-Zeilen bleiben aktuelle Snapshots, nominal 10 Hz, mit unveraenderter RPM-/Temperatursemantik und leeren ungueltigen Messwerten. Sie werden spaetestens nach einer Sekunde oder bei 20 Eintraegen geschrieben (sofern das Medium verfuegbar ist und Dateiaufrufe zurueckkehren). Nicht versuchte Restpuffer bleiben bei Ausfall erhalten; volle Puffer verhindern weiteres Legacy-Sampling, es gibt keine nachtraegliche Rekonstruktion. Ein versuchter fehlerhafter Legacy-Block wird vollstaendig in `legacyUncertain` gezaehlt und nicht wiederholt. Fehler in einer Legacy-Datei verhindern weitere Dateiversuche derselben Runde. Eine unbegrenzte, verlustfreie Legacy-Pufferung ist damit nicht zugesagt.

#### Separates GNSS-Epochenprotokoll (Variante A)

Unmittelbar nach der bisherigen Akzeptanz/Veroeffentlichung kopiert `gnssTask()` einen `GnssLogRecord` mit urspruenglichen UBX-Ganzzahleinheiten. Er enthaelt Sequenz (`uint64_t`), bereits normalisierte UTC-Millisekunden (`int64_t`), Sitzung, Sample, iTOW, Host-Empfangszeit, Epochendelta, Position, Hoehe, Geschwindigkeit, Kurs, horizontale Genauigkeit und Gueltigkeitsflags/Fixtyp. `static_assert` verlangt genau 64 Byte. Ungueltige UTC bleibt ungueltig; `receivedMs` ersetzt keine Messzeit. Insbesondere wird eine alte Epoche niemals an die aktuelle Legacy-Hostuhr zurueckgefuettert.

Der eigene FIFO-Mutex schuetzt nur Kopien, Indizes und Statistik; Snapshot- und FIFO-Mutex werden nie gleichzeitig gehalten. Einreihung fuehrt keine Formatierung, serielle Ausgabe oder Dateizugriffe aus. Der Ring hat genau **320 nutzbare Plaetze**, haelt bei vollem Ring bereits gespeicherte Daten und verwirft den neuen Datensatz (**Drop-newest**). Die bootweite Akzeptanzsequenz steigt auch bei diesem Verlust. GNSS-Reconnects loeschen weder FIFO noch Zaehler; Sitzung/Sample und Sequenz unterscheiden historische Epochen.

Der einzige Konsument reserviert hoechstens 20 Eintraege als statische Arbeitskopie. Diese bleiben bis zur Bestaetigung im FIFO enthalten und vor Ueberlauf geschuetzt. Ein voller Block ist sofort faellig, ein Teilblock nach einer Sekunde ab dem `receivedMs` des aeltesten Eintrags. Der Logger nimmt unmittelbar vor der Reservierung nach den Dateioperationen eine frische `millis()`-Zeit auf. Die modulare Alterspruefung weist zukunftsliegende Empfangszeiten zurueck, auch wenn der Produzent zwischen Zeitaufnahme und FIFO-Sperre einreiht. Sie beruecksichtigt einen `millis()`-Umlauf fuer eindeutige Zeitabstaende unter einem Halbzyklus (2^31 ms, etwa 24,9 Tage); groessere Abstaende lassen sich ohne zusaetzlichen Zeitanker nicht sicher von zukuenftigen Werten unterscheiden und geben keinen Teilblock frei. Die Frist setzt Rueckkehr der Dateisystemaufrufe voraus. Je 100-ms-Runde wird hoechstens ein GNSS-Block geschrieben; rechnerisch bis zu 200 Epochen/s vor I/O-/Schedulingkosten, also mehr als der 5-Hz-Zulauf. Legacy-Arbeit erhaelt jede Runde Vorrang. Treiberaufrufe koennen das zeitliche Budget trotzdem ueberschreiten.

Neue Dateien heissen `/usb/GN000000.tsv` bis `/usb/GN009999.tsv`. `O_CREAT | O_EXCL` reserviert jeden Namen exklusiv; bestehende Dateien werden nicht ueberschrieben. Die Suche prueft maximal 32 Kandidaten je Runde und insgesamt 10000 Nummern pro Boot. Namenskollisionen (`EEXIST`), eine noch laufende Suche und die Erschoepfung des Namensraums sind GNSS-spezifische Zustaende: Sie setzen keinen gemeinsamen USB-/Loggerfehler und unterbrechen weder RPM- noch Temperaturlogging. Ohne Segment wird kein FIFO-Block reserviert oder bestaetigt; der FIFO bleibt bis zum regulaer bilanzierten Drop-newest-Ueberlauf erhalten. Nach 10000 Kandidaten gibt es in diesem Boot keine weiteren Anlageversuche; die Erschoepfung wird einmal seriell gemeldet.

Auch `IoError` bei der GNSS-Segmentanlage beweist fuer sich keinen gemeinsamen Dateisystemausfall. Solange Attached-Flag und Detach-Generation unveraendert sind, bleiben Legacy-Sampling und die faelligen, vollstaendig geprueften Legacy-Schreibversuche aktiv. Fehler beim GNSS-Statistik- oder Blockschreiben verwerfen nur das aktive Segment; ein versuchter GNSS-Block wird weiterhin einmalig als unsicher bilanziert. Der naechste begrenzte Anlageversuch verwendet den naechsten Namen, ohne Replay oder Anhaengen hinter eine moeglicherweise unvollstaendige Zeile. GNSS-Fehler werden seriell gemeldet. Ein Mediumwechsel, fehlgeschlagener Legacy-Header/-Schreibzugriff oder verletzte FIFO-Abschlussinvariante loest weiterhin den konservativen gemeinsamen Fehlerpfad mit Unmount und erneut geprueftem Mount aus. Ein gemeinsamer Dateisystemausfall ohne Detach-Ereignis wird somit spaetestens am naechsten faelligen Legacy-Dateizugriff erkannt, sofern die Treiberaufrufe zurueckkehren; GNSS-Anlagefehler allein verhindern diesen Nachweis nicht mehr. Fehlgeschlagenes Mount beziehungsweise noch nicht abgeschlossenes Unmount sperrt weiterhin alle Dateizugriffe. Jeder Wiederanschluss erzeugt ein neues GNSS-Segment.

Die versionierte Kopfzeile lautet `# GNSS_LOG_V1`. Die 17 tabulatorgetrennten Spalten sind:

`sequence, sessionId, sampleId, iTOW_ms, receivedMs, epochDeltaMs, UTC_ms, timeValid, positionValid, altitudeValid, fixType, lat_e7, lon_e7, hMSL_mm, gSpeed_mm_s, headMot_e5, hAcc_mm`.

Ungueltige UTC-, Positions- und Hoehenwerte bleiben entsprechend ihren Flags leer. Kommentarzeilen `# STATS` erscheinen beim Oeffnen eines Segments und mindestens einmal pro Loggersekunde unabhaengig von weiteren Epochen. Sie enthalten bootweite `accepted`, `written`, `queued`, `highWater`, `overflow`, `uncertain` und separat `legacyUncertain`. `gnssSegmentNext` zeigt den Fortschritt der begrenzten Namenssuche, `gnssSegmentExhausted=1` die Erschoepfung ohne aktives Segment; dieser GNSS-Zustand setzt nicht das gemeinsame USB-Fehlerflag. Die Statistik wird auch ohne Medium seriell ausgegeben; beim Wiederanschluss wird die aktuelle Bilanz im neuen Segment sichtbar. Dadurch bleiben auch Verluste ohne einen nachfolgenden GNSS-Datensatz erkennbar, solange die Firmware weiterlaeuft.

Erfolg erfordert Oeffnen, vollstaendige `fwrite`-Operationen, `fflush`, `fclose` und eine unveraenderte Detach-Generation bei weiterhin erkanntem Medium. Auch nach Schreib-/Flushfehler wird geschlossen. Ein versuchter fehlerhafter GNSS-Block wird **vollstaendig als unsicher** gezaehlt und aus dem Ring genommen, ohne automatisches Replay; unversuchte Eintraege bleiben erhalten. Die einzelne Unsicherheitsgruppe ist auf 20 Epochen begrenzt. Das gilt konservativ auch bei Oeffnungsfehler, obwohl dann keine Nutzdaten geschrieben wurden. Die Anzahl tatsaechlich gespeicherter Zeilen eines Fehlerblocks ist unbekannt. Erfolgreiche Dateisystemaufrufe beweisen keinen physischen Flash-Commit und bieten keine Exactly-once-Garantie.

Die unter dem FIFO-Mutex konsistente Bilanz lautet:

`accepted = queued (einschliesslich reservierter Eintraege) + written + overflow + uncertain`.

#### Speicherbudget und verbleibende Verifikation

- Ringdaten: **320 x 64 = 20480 Byte** statisch; zusaetzlich Statistik, zwei Indizes und FIFO-Mutex.
- Logger-Arbeitskopie: **20 x 64 = 1280 Byte** statisch, Formatpuffer **384 Byte** statisch. Zusammen sind das **22144 Byte** fuer diese drei Arrays, zuzueglich Verwaltungs-/Atomikzustand, Pfadpuffer und Mutex. Die fruehere Header-`String`-Arrayreserve entfaellt.
- Der Ring und die Arbeitskopie liegen nicht auf dem bestehenden 4-KiB-Loggerstack. Kleine Formatierhilfen und printf-/Dateisystemaufrufe benoetigen weiterhin Stack. Die alten Stackmessungen ohne GNSS-Epochen sind kein aktueller Reservenachweis. Eine Laufzeit-Wasserzeichenpruefung unter GNSS/USB-Last bleibt offen.
- B1-P3-Build vom 22. November 2026: RAM **150656 Byte** (28,8 %), Flash **482112 Byte** (61,3 %), BIN **484048 Byte**; RAM unveraendert, Flash +168 Byte gegen den vorigen P3-Stand. Sieben C++-Suiten mit 90 Assertions und acht Python-Quellvertraege erfolgreich; Einzelheiten und Grenzen im P3-Pruefbericht. Heap-/FAT-/USB-Treiberbedarf und physische Dauerhaftigkeit sind dadurch nicht vollstaendig erfasst.
- Bei leerem FIFO entsprechen 320 Epochen **64 Sekunden bei 5 Hz**; bei Fuellstand q verbleiben `(320-q)/5` Sekunden. Ein beliebig langer USB-Ausfall oder Stromausfall kann nicht verlustfrei ueberbrueckt werden.
- Erfasst werden ausschliesslich bisher akzeptierte/veroeffentlichte Epochen. SparkFun kann davor mehrere PVT-Nachrichten eines I2C-Bursts auf den letzten Bibliotheksdatensatz reduzieren. Weder Callback noch `flushPVT()` bilden einen zusaetzlichen FIFO; Roh-PVT-Erfassung (Variante B) wurde nicht aktiviert.
- Reale I2C-/USB-Latenzen, FAT-Fehlermodi, Callback-/Treiberkonkurrenz und Durchsatz sowie Stackreserven sind auf Zielhardware ungeprueft. P2 hat keine Tests/Builds oder Hardwarezugriffe ausgefuehrt. Kein Upload gehoert zur P3-Buildfreigabe.

**Legacy-Zeitstempel:** Die Spalte heißt `Time_UTC`. Sekunden und Millisekunden stammen aus einer einzigen Uhr (`log_clock.h`): Die erste gültige GNSS-Zeit, die der Logger sieht, verankert sie, danach läuft sie mit `millis()` weiter und kann dadurch nicht rückwärts springen. Vor der ersten gültigen GNSS-Zeit steht 1970 plus Betriebszeit als ausdrücklicher Platzhalter, kein vertrauenswürdiges UTC. Die Uhr wird nicht nachsynchronisiert und driftet mit dem Takt von `millis()`.

## 3. Hardware-Schnittstellen und Bibliotheken

### Im Anwendungscode belegte Schnittstellen

| Schnittstelle | Konfiguration und Nutzung |
| --- | --- |
| GPIO 3 | Drehzahlimpulse, `INPUT_PULLDOWN`, Interrupt auf `RISING`. Das Signal erzeugende Bauteil und die Eingangsbeschaltung sind nicht dokumentiert. |
| GPIO 2 | Schaltausgang: HIGH bei Drehzahl über 15000, LOW unter 14500; dazwischen bleibt der Zustand. Ein angeschlossener Aktor ist aus dem Code nicht bestimmbar. |
| GPIO 4 / OneWire | `OneWire` und `DallasTemperature`; gelesen wird der Sensor mit Index 0. Sensormodell und Anzahl sind nicht festgelegt. |
| I²C über `Wire` | 400 kHz; der GNSS-Empfänger wird auf Adresse 0x42 angesprochen. Empfängermodell und physische I²C-Pins nennt der Anwendungscode nicht. |
| Display und Touch | `Arduino_H7_Video Display(800, 480, GigaDisplayShield)` und `Arduino_GigaDisplayTouch TouchDetector`. Die Anwendung definiert keine eigenen Display- oder Touch-Pins. |
| USB-Massenspeicher | `Arduino_USBHostMbed5`, `USBHostMSD` und Mbed-FAT-Dateisystem unter `/usb`. |
| Serielle Diagnose | `Serial.begin(115200)`; `monitor_speed = 115200`. Ausgaben unter anderem von GNSS (`GNSS DIAG`, `GNSS CFG-RATE`) und USB-Mount. |
| Systemzeit | Die RTC wird einmal mit GNSS-UTC gesetzt. Die Anzeige liest sie über `time()` und rechnet nach MEZ/MESZ um; USB-Dateien erhalten ihre FAT-Zeitstempel aus ihr. Eine eigene Konfiguration der RTC-Hardware ist nicht enthalten. |

Quelle: Hardwaredefinitionen, globale Objekte, `setup()` und Task-Funktionen in [src/main.cpp](../src/main.cpp) sowie [src/gnss_service.cpp](../src/gnss_service.cpp).

### Explizite PlatformIO-Abhängigkeiten

Die Angaben sind Versionsvorgaben aus [platformio.ini](../platformio.ini), keine nachgewiesenen installierten Versionsstände:

| Bibliothek | Vorgabe | Nachgewiesene Verwendung |
| --- | --- | --- |
| `arduino-libraries/Arduino_GigaDisplayTouch` | `^1.1.2` | Touchobjekt und Initialisierung in `main.cpp`. |
| `lvgl/lvgl` | `^9.6.0` | GUI-Laufzeit, Widgets, Tick und Ereignisverarbeitung. |
| `facchinm/Arduino_USBHostMbed5` | `^0.3.1` | USB-Massenspeicher und Erkennungs-/Entfernungs-Callbacks. |
| `sparkfun/SparkFun u-blox GNSS Arduino Library` | `^2.2.29` | GNSS-Konfiguration und PVT-Abfrage im GNSS-Thread. |
| `paulstoffregen/OneWire` | `^2.3.8` | OneWire-Bus an Pin 4. |
| `milesburton/DallasTemperature` | `^4.0.6` | Temperaturanforderung und Auslesen des ersten Sensors. |

`Arduino_H7_Video` steht nicht in `lib_deps`; es stammt aus dem Arduino-Mbed-Framework. Die Version der Plattform `ststm32` ist nicht ausdrücklich festgelegt.

`platformio.ini` setzt `lib_archive = false` und die Flags `LV_CONF_SKIP`, `LV_CONF_INCLUDE_SIMPLE` und `LV_FONT_MONTSERRAT_20=1`. Dazu kommen Flags, die ungenutzte LVGL-Widgets (Diagramm, Kalender, Tastatur, Tabelle, Liste, Schieber, Schalter, Button, Flex, Grid und weitere) und ungenutzte Software-Zeichenformate abschalten; das spart etwa 105 KB Flash. Die UI verwendet nur Bogen, Bild, Label und Skala. **Wird in EEZ Studio ein anderes Widget oder Bildformat eingesetzt, muss das passende Flag wieder aktiviert werden.** Das gilt auch für Stile, die eine Ebene mit Alpha brauchen (Ebenentransparenz, Drehung, Skalierung, abgerundete Clip-Ecken, Bitmap-Masken, Blend-Modi): ohne `LV_DRAW_SW_SUPPORT_ARGB8888` können sie ohne Fehlermeldung leer erscheinen. Eine eigene `lv_conf.h` gibt es nicht. Lokale Dateien nach dem Muster `platformio.local*.ini` werden zusätzlich eingebunden und von [.gitignore](../.gitignore) ausgeschlossen.

## 4. LVGL und EEZ Studio

### Generierung und GUI-Laufzeit

[mofa.eez-project](../mofa.eez-project) konfiguriert ein LVGL-Projekt der EEZ-Projektversion `v3` mit 800 × 480 Pixeln, `colorFormat = BGR`, `lvglVersion = 9.0` und `flowSupport = false`. Das Ausgabeziel ist `src\ui`; Bilder und Fonts werden als C-Quellcode exportiert. Die vorhandene `ui.c` verwendet direkte LVGL-Aufrufe und keine EEZ-Flow-Runtime. EEZ ist damit auf LVGL 9.0 eingestellt, PlatformIO fordert `^9.6.0`; Auffälligkeiten durch den Unterschied sind bisher nicht bekannt, aber auch nicht systematisch geprüft. Die Generierung geht auch ohne Oberfläche mit `EEZ-Studio-….AppImage --no-sandbox -build <Pfad>/mofa.eez-project`; der Prozess endet nicht von selbst und braucht ein `timeout`.

`ui_init()` erzeugt über `create_screens()` das LVGL-Standardtheme im hellen Modus (die Anzeige ist für Sonnenlicht ausgelegt: heller Hintergrund, schwarze Schrift, kräftige Farben) und den Bildschirm `Main`. `loadScreen(SCREEN_ID_MAIN)` lädt ihn mit einer 200-ms-Fade-in-Animation. `ui_tick()` ruft `tick_screen_main()` auf; diese Funktion vergleicht die UI-Variablen mit den Widgetwerten und ändert Texte, Drehzahlbogen oder Sichtbarkeit nur bei Abweichung. Die laufende GUI-Verarbeitung erfolgt im Displaythread.

### Datenanbindung und Widgets

Die generierte [src/ui/vars.h](../src/ui/vars.h) deklariert C-Getter und -Setter; [src/vars.cpp](../src/vars.cpp) implementiert sie mit `extern "C"` und ergänzt die Helfer aus `src/vars.h`. Die Textvariablen und `hide_*`-Flags schreibt nur der Displaythread (nach dem Start der Threads), die Setter für Textvariablen sind leer.

| Variable oder Helfer | Darstellung und Semantik |
| --- | --- |
| `motor_rpm`, `motor_rpm_text` | Drehzahlbogen 0 bis 20000 und numerisches Label (mindestens fünf Stellen, nach oben auf 999999 begrenzt). Der Displaythread übergibt den Ausgang von `RpmDisplayFilter`, nicht den Rohwert. |
| `hide_rpm_warning` | Steuert das Warnbild. `set_ui_rpm_warning()` übernimmt den Zustand aus `curRpmWarning`, also dieselbe Hysterese (15000 ein, 14500 aus) wie der Schaltausgang. |
| `set_ui_velocity()` / `velocity_text` | Ganzzahl, nach oben auf 9999 begrenzt; bei negativem Wert ein leerer Text (kein gültiger Fix). Der Font enthält nur Ziffern. |
| `set_ui_temperature()` / `temperature_text` | Eine Nachkommastelle, auch negativ. Bei Werten `<= -120` (ein fehlender Sensor liefert -127) steht `--.-`. |
| `set_ui_time()` / `time_text` | `HH:MM:SS` in MEZ oder MESZ aus UTC-Sekunden (`local_time.h`), ohne feste Offset-Annahme. Solange keine GNSS-Zeit die RTC gesetzt hat, steht `...`. |
| `gps_online` | Der Setter speichert `!value`; der Getter steuert das Hidden-Flag des Satellitensymbols. Ein Aufruf mit `true` macht das Symbol sichtbar. |
| `usb_status_text`, `hide_usb_ok/busy/error` | Zweite Statuszeile mit Punkt unter der GPS-Zeile, gesetzt von `set_ui_usb_status()`: gelb „USB: mounting“, grün „USB: logging“, rot „USB: no stick“ (es wird nicht geloggt) und „USB: error“ (Mount oder Schreibzugriff fehlgeschlagen; verschwindet nach dem nächsten Erfolg oder Abziehen). Der Zustand wird im Displaythread aus `usbAttachedFlag`, `usbMountedFlag` und `usbFaultFlag` gebildet; Legacy-Open/Write/Flush/Close-Fehler und gemeinsame Mediumfehler setzen den USB-Fehlerstatus. Isolierte GNSS-Segmentfehler rotieren nur das Segment und werden seriell gemeldet; die Zustandsflags sind atomar. |
| `gps_status_text`, `hide_gps_ok/busy/error` | Statuszeile mit Punkt: grün bei Fix („GPS: 3D fix“, „GPS: 2D fix“), gelb bei laufendem Aufbau („searching“, „starting“, „no fix“, „reconnecting“), rot bei Fehlern („no module“, „config error“, „rate error“, „thread error“). Gesetzt von `set_ui_gps_status()`. |

Die vier generierten Fonts (`DIGITAL_72`, `DIGITAL_128`, `DIGITA_32`, `DIGITAL_92`) basieren auf `fonts/aurebesh.ttf`. Nur `DIGITAL_72` enthält neben den Ziffern auch Punkt, Doppelpunkt und Bindestrich (Bereich `48-57,58,46,45`); sie bildet Uhr und Temperatur. Die anderen enthalten nur Ziffern. Zusätzlich nutzt `screens.c` Montserrat 14 und 20. Die Bilder `icon`, `rpm_warning` und `gps` liegen als eingebettete `RGB565A8`-Daten vor.

### Startdurchlauf („Selbsttest“)

Wie bei Armaturen älterer Fahrzeuge fährt die Anzeige nach dem Einschalten alle Elemente durch ihren Bereich, damit tote Segmente, fehlende Zeichen oder defekte Widgets vor der ersten Messung auffallen. `ui_self_test.h` liefert dafür aus der verstrichenen Zeit einen Satz Werte, den `set_ui_self_test()` in die UI schreibt: 3 s lang Drehzahl 0 → 20000 → 0, Geschwindigkeit 0 → 199 → 0, Temperatur −40 → 120 → −40 °C (so ist das Minuszeichen gleich zu sehen), Warnsymbol zwischen 1 s und 2 s, Uhr `88:88:88` und nacheinander alle zehn GPS-Statustexte. Der Durchlauf läuft nur einmal nach dem Start des Displaythreads und überschreibt in dieser Zeit die Livewerte; er bricht sofort ab, sobald die Drehzahlwarnung echt anliegt, damit ein Reset während der Fahrt eine Überdrehzahl nicht verdeckt.

## 5. Abhängigkeiten und Datenfluss

```mermaid
flowchart TD
    GPIO["GPIO 3 / rpmISR()"] --> RPM["rpmSensorTask()"]
    RPM --> OUT["GPIO 2"]
    RPM --> RAW["curRPM, curRpmWarning (dataMutex)"]
    TEMP["Temperatur über OneWire"] --> SENSOR["sensorTask()"]
    SENSOR --> RAWT["curTempC (dataMutex)"]
    GNSS["GNSS über Wire"] --> GTASK["gnssTask()"]
    GTASK --> SNAP["GnssSnapshot (Mutex)"]
    GTASK --> RTC["RTC einmal pro Start"]
    GTASK --> FIFO["GNSS FIFO: 320 Epochen / eigener Mutex"]
    FIFO --> LOG
    RAW --> LOG["loggingTask()"]
    RAWT --> LOG
    SNAP --> LOG
    LOG --> USB["USB / FAT / DRZ.txt, SENSOR.txt, GNnnnnnn.tsv"]
    RAW --> DISPLAY["displayTask()"]
    RAWT --> DISPLAY
    SNAP --> DISPLAY
    RTC --> DISPLAY
    TEST["ui_self_test"] --> DISPLAY
    DISPLAY --> MODEL["UI-Zustand in vars.cpp"]
    MODEL --> TICK["ui_tick() / tick_screen_main()"]
    DISPLAY --> LVGL["lv_task_handler()"]
    TICK --> LVGL
    LVGL --> VIDEO["Display-/Touch-Anbindung"]
```

- **Hardware und Ablauf:** `main.cpp` erzeugt die Hardwareobjekte und die RTOS-Threads. GNSS ist als eigene Einheit (`gnss_service`) mit einer kleinen Schnittstelle (`startGnss()`, `readGnssSnapshot()` und separate FIFO-Reservierung/Bestaetigung/Statistik) herausgelöst, ebenso die Richtlinienlogik in den Headern.
- **Zustandsbereiche:** Die `cur*`-Variablen in `main.cpp` und der GNSS-Snapshot versorgen Logging und Anzeige gleichermaßen; die Zustände und Textpuffer in `vars.cpp` gehören dem Displaythread. Die Anzeige liest nicht aus den Logpuffern.
- **Synchronisierung:** `dataMutex` schützt `curRPM`, `curRpmWarning` und `curTempC` bei jedem Schreiben und Lesen. Der GNSS-Snapshot hat einen eigenen Mutex; Leser erhalten eine Kopie. Der Log-FIFO hat einen separaten Mutex ohne I/O oder gleichzeitig gehaltenen Snapshot-Mutex. Die UI-Modellvariablen werden nur im Displaythread geschrieben, die Getter von LVGL ebenfalls dort aufgerufen.
- **Interruptgrenze:** Der Drehzahlthread liest das vom Interrupt geschriebene Wertepaar (`lastPulseTime`, `delta_rpm`, beide `uint32_t`) in einem kritischen Abschnitt und rechnet danach außerhalb davon.
- **Generierungsabhängigkeit:** EEZ-Variablennamen und generierte Deklarationen müssen zu den Implementierungen in `vars.cpp` passen. Neue UI-Variablen brauchen sowohl einen Eintrag im EEZ-Projekt (und eine neue Generierung) als auch Getter und Setter in `vars.cpp`.
- **Startabhängigkeit:** `setup()` muss die GUI erzeugen, bevor `displayTask()` sie aktualisiert. Die Systemzeit kommt erst, wenn der GNSS-Thread eine gültige Zeit gesehen hat; bis dahin zeigt die Uhr `...` und das Log den Platzhalter.

## 6. Unklarheiten und erkennbare Einschränkungen

Die folgenden Punkte sind aus dem Quelltext erkennbar. Soweit nichts anderes steht, wurden ihre Auswirkungen weder in einem Test noch auf Hardware geprüft.

| Bereich | Beleg und Grenze der Aussage |
| --- | --- |
| GNSS-Zeit und RTC | Die RTC wird einmal pro Start mit ganzen UTC-Sekunden gesetzt; Die Nanosekunden werden auf ganze Sekunden gerundet und die Verarbeitungszeit verworfen, sie kann deshalb um etwa eine halbe Sekunde abweichen und wird später nicht korrigiert. Anzeigeuhr (RTC) und Logzeit (eigene Verankerung) können sich um weniger als eine Sekunde unterscheiden. Die Zeitumrechnung ist für 2000 bis 2099 ausgelegt und hat als feste Zone Mitteleuropa. |
| GNSS-Neustart bei erster UTC-Zeit | Sobald der Empfänger erstmals gültige UTC meldet, nachdem schon Epochen veröffentlicht wurden, wird die Sitzung bewusst neu aufgebaut. Für etwa 2 bis 3 s fehlen dann Position und Geschwindigkeit, die UI zeigt „GPS: reconnecting“, die serielle Ausgabe „stream stale“. |
| GNSS-Status | Ein nicht antwortendes Modul meldet „no module“ in Rot. `gpsOK` im Log und das Satellitensymbol folgen `connection == Receiving`, auch wenn noch keine Epoche veröffentlicht wurde. |
| Logdateien | Der Legacy-Spaltenname `Acc_ms-1` bleibt trotz horizontaler Genauigkeit in Metern erhalten. Ungueltige Werte bleiben leer. Kopfzeilen werden nur nach erfolgreichem Mount fuer neue/leere Dateien geschrieben. |
| USB-Erfolgserkennung und Fehler | Mount-Rueckgabewert sowie Open/Write/Flush/Close werden geprueft. Fehlerbloecke werden als unsicher gezaehlt. Keine Garantie physischer Dauerhaftigkeit. |
| USB-Abziehen und Restpuffer | Nur der Logger unmountet nach abgeschlossenen Dateioperationen. Atomare Detach-Generationen entwerten betroffene Schreibbestaetigungen. Nicht versuchte Restpuffer bleiben erhalten; GNSS rotiert Segmente. Stromausfall und endlicher RAM bleiben Verlustquellen. |
| Anzeige | Der Geschwindigkeitsfont enthält nur Ziffern; ohne Fix bleibt das Feld leer. Die angezeigte Drehzahl liegt bei konstanter Eingangsdrehzahl innerhalb von 0,6 des gefilterten Werts (Hysterese des Anzeigefilters). |
| Temperatur | `tempOK` prüft `-55 < tC < 125`, wird aber sonst nirgends ausgewertet. Die Anzeige unterscheidet nur zwischen gültigem Wert und Werten `<= -120`. |
| Tests | Der Runner `python test/gnss_logger/run_tests.py` umfasst sieben C++-Compile-only-Suiten und acht statische Python-Quellvertraege; der Firmwarebuild fuehrt diese nicht aus. Diese Pruefungen ersetzen keine RTOS-/USB-/RTC-Laufzeitpruefung. Eine `native`-Testumgebung existiert nicht; `pio test` auf der Hardware waere ausserdem ein Upload und braeuchte die Freigabe aus `AGENTS.md`. |
| Hardware | Konkrete Sensor-/GNSS-Modelle, elektrische Beschaltung und tatsächlich angeschlossene Geräte sind aus den Projektdateien nicht bestimmbar. |

Belegstellen für diese Grenzen sind insbesondere `gnssTask()` in [src/gnss_service.cpp](../src/gnss_service.cpp), `mountUSB()`, `safeFileWrite()`, `loggingTask()`, `displayTask()` und `rpmSensorTask()` in [src/main.cpp](../src/main.cpp), die Funktionen in [src/vars.cpp](../src/vars.cpp) und [src/rpm_display_filter.h](../src/rpm_display_filter.h) sowie die Bindungen in [src/ui/screens.c](../src/ui/screens.c).
