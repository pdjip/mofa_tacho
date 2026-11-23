# P3 - Display mask=1024: oeffentliche Offsetgetter richtig auswerten

Pruefdatum: **22. November 2026**. Auftrag: gezielte Korrektur der falschen Offsetablehnung (`reason=3 mask=1024`). Branch `feature/gnss-fifo-logging`, Basis `7b85488` plus bewahrte Vorarbeiten. **Testerphase abgeschlossen: neun C++-Suiten mit 188 Assertions, acht GNSS- und sieben Display-Quellvertraege, Firmwarebuild und Abschlusskontrollen erfolgreich.** Der falsche Softwaregate ist reproduziert und korrigiert; korrekte sichtbare Darstellung bleibt hardwareseitig zu bestaetigen. Alle bisherigen Berichte bleiben unten bytegleich erhalten.

## Ursache, Versionsstand und begrenzte Korrektur

Die bisherige Bit-10-Bedingung behandelte `lv_display_get_offset_x/y()` als rohe Translation. Die tatsaechlichen LVGL-Getter liefern jedoch rotationsabhaengige Werte. Bei 270 Grad und rohen Offsets `(0,0)` ergeben die **oeffentlichen Offsetgetter `(800,480)`**, entsprechend den ebenfalls gedrehten physischen Gettern `800x480`. Das ist bei Original-/DSI-Groesse `480x800` korrekt und keine reale Translation. Die alte Bedingung setzte dadurch faelschlich mask=1024 und brach den Flush vor Rotation/DMA ab.

Der produktive `zeroRawOffsetFromPublic()` vergleicht bei 0/90 Grad gegen `(0,0)`, bei 180/270 gegen die gleichnamigen gedrehten physischen Getterwerte. Er verwendet Gleichheit statt ueberlaufgefaehrlicher Ruecksubtraktion. Echte nichtnullige rohe Translation bleibt abgewiesen; ungueltige Rotation und nichtpositive physische Dimensionen ebenfalls. Bit 10 lautet jetzt `!rawOffsetZero`. Die vorhandene `lv_display_rotate_area()`-Formel, Source-/Destinationbounds, Strides, Groessenlimits, Allokation und ready-Reihenfolge bleiben unveraendert.

**Tatsaechlich installiert/gebaut: LVGL 9.6.0**, Arduino Mbed 4.6.0. Die Auftragsreferenz 9.3 stimmt nicht mit diesem lokalen Buildstand ueberein. Der Entwickler hat auch die offiziellen 9.3-Getter-/Callbackoffsetstellen verglichen; diese Testerphase kompiliert und prueft ausschliesslich die lokal installierte **9.6.0**, behauptet keinen 9.3-Build und aendert keine Bibliotheksversion. Lokal bestaetigt: `lv_refr.c` addiert die rohen `disp->offset_x/y` zur Flusharea, waehrend die oeffentlichen Getter bei 180/270 von den gedrehten physischen Aufloesungen subtrahieren.

## Umfang, Rechte und Integritaet

TARS meldete vor Testerstart: Entwickler abgeschlossen ohne Kindprozesse, Tester alleiniger Schreiber; `workspace-write` fuer Projekt/Temp, geschuetzte `.git`/`.agents`/`.codex`/`.aws`, eingeschraenktes Netzwerk, keine Rechteerweiterung. Eine fremde PlatformIO-Sitzung (gemeldete PID 9464) wurde weder beendet noch gesteuert. Eigene Builds verwenden die isolierten bisherigen Pfade unter `.pio/gnss-fifo-p3`.

Entwicklerumfang: `src/display_rotation_buffer.h` mit dem reinen Offsethelfer, `src/display_rotation.cpp` mit korrigiertem Gate, erfassten physischen Getterwerten und erweitertem Diagnosefeld. Tester aendert ausschliesslich `test/display_rotation/test_rotation.cpp`, `test/display_rotation/test_integration.py`, neu `test/display_rotation/test_mapping.cpp`, `test/gnss_logger/run_tests.py` und diesen Bericht. Keine Implementierungs-, UI-, GNSS-/USB-, Stack-, Konfigurations-, Paketcache- oder P2_P3-Aenderung durch den Tester.

Nachweise: **`.pio/gnss-fifo-p3/display-mask1024/`**. Vor Neubuild gesicherte reason3-ELF-/BIN-Hashes entsprechen dem vorigen Bericht (`reason3-firmware-hashes.json`, `reason3-sections.log`). main.cpp bleibt hashgleich zum vorigen Stand: `d8c7db83df361643a84d8a15e213cb45e7d5a508db6a8da1554dfdfdee16933d`. Stock-Arduino_H7_Video.cpp vor/nach Build unveraendert (`integration-checks.json`). Konfigurationsinhalte werden nicht ausgegeben.

## Tests mit tatsaechlichem LVGL-Mapping

Ausgefuehrt:

```powershell
& C:/Users/nicol/.platformio/penv/Scripts/python.exe test/gnss_logger/run_tests.py
```

**Abschlusslauf Exit 0**, Prozessdauer **1,264 s**. ARM GCC 7.2.1, `-std=gnu++17 -Wall -Wextra -Werror -I .pio/gnss-fifo-p3/tests -c`; danach acht GNSS- und sieben Display-Pythonmethoden bestanden. Nachweise `tests.log`, `tests-result.json`. Ein vorheriger vollstaendiger Lauf war ebenfalls erfolgreich; anschliessend wurden nur die Testobjektnamen pro C++-Datei eindeutig gemacht, damit zwei Displaydateien nicht dasselbe Ausgabeobjekt ueberschreiben. Erneuter Gesamtlauf erfolgreich, Erstlauf erhalten als `tests-before-unique-output.log`. Keine fehlgeschlagene Implementierungspruefung und keine Implementierungsnacharbeit.

| C++-Suite/Datei | Assertions | Ergebnis |
| --- | ---: | --- |
| gnss_epoch | 10 | bestanden |
| local_time | 17 | bestanden |
| log_clock | 6 | bestanden |
| rpm_filter | 4 | bestanden |
| ui_self_test | 12 | bestanden |
| gnss_log_fifo | 16 | bestanden |
| gnss_logger | 65 | bestanden |
| display_rotation/test_rotation.cpp | 45 | bestanden |
| display_rotation/test_mapping.cpp | 13 | bestanden |
| **Summe** | **188** | **bestanden** |

Alle bisherigen 170 Assertions bleiben aktiv. Fuenf neue Assertions pruefen den produktiven Offsethelfer fuer alle Rotationen, echte verschobene Werte, ungueltige Dimensionen/Rotationen und INT32_MIN/MAX ohne Ruecksubtraktion. Die Mappingdatei fuehrt **tatsaechliche installierte LVGL-Funktionskoerper** aus, keinen unabhaengig nachgebauten Mappingalgorithmus.

Der Runner extrahiert zehn unveraenderte Koerper aus `src/display/lv_display.c`: Rotation, logische/originale/physische Aufloesungen, Offsetgetter und `lv_display_rotate_area`. Nur `constexpr` wird vor die Funktionen gesetzt. Kleine kontrollierte Testshims stellen Display-/Area-Typen, Defaultdisplay, Area-Breite/-Hoehe und Argument-/Logmakros bereit. Upstream und Paketcache werden nicht geschrieben. Der Quellvertrag vergleicht die Koerper bytegleich nach Texteingabe und prueft Version, Quelldateihash und Provenienz. Vollstaendige Quelle SHA-256: **`010d404e564ec9a4498a618903bd39691b247bfc3ddbee409745b8facfda052f`**. Generierte Fixture und Einzelkoerperhashes sind als `lvgl_mapping_fixture.h/.json` im Nachweisordner gesichert.

Geprueft werden **48 gueltige Corner-/Edge-/Originfaelle**: vier Eckpunkte, vier komplette Randstreifen und vier nichtnullige Ursprungsrechtecke fuer jede Rotation. Die echte LVGL-Abbildung wird mit der produktiven Flushpolicy verbunden; Dimensionen, Grenzen, zwei Byte pro Pixel, enger Zielstride, Frameoffset und genau einmal ready werden geprueft. Dazu kommen echte rohe Offsets +/-1 auf beiden Achsen fuer jede Rotation, echte Source-/Destinationgrenzverletzungen und Allokationsabbruch ohne DMA/Rotation. Die bisherigen Padding-/Stride-/Kapazitaets-/Overflowtests bleiben bestehen.

Der konkrete 270-Grad-Fall prueft **oeffentliche Offsets 800/480 bei roher Translation null** und die echte Abbildung `(0,0,799,23) -> (456,0,479,799)`, Ziel **24x800**, Source-/Destinationstride **1600/48**, **38400 Byte**, Offset **912 Byte**. Keine Produktionsabbildung wurde zugunsten der Tests ersetzt.

Die strengen Displayquellvertraege behalten alle Initial-/Mapping-/Frameguards bei; nur Bit 10 und die zugehoerigen Getter-/Diagnosefelder sind auf die korrigierte Semantik angepasst. Die sechs bisherigen Displaymethoden bleiben erhalten; eine siebte verifiziert die Upstreamfixture samt Getterkoerpern/Provenienz. Keine GNSS-, UI-, FIFO-, USB- oder Allokationsbedingung wurde abgeschwaecht.

Grenzen: Compilezeitpruefung mit Testtypen/-makros und Fake-Transfers, kein nativer Hostlauf, keine echten Bildpixel/DMA-/Cache-/Paneltests. Die Funktionskoerper sind echt; das vollstaendige LVGL-Objektlayout, Scheduling und Nullargument-Logging werden nicht emuliert. Ein bestandener Mappingtest ist kein sichtbarer Displaynachweis.

## Build und Speicher

Einmal ausgefuehrt mit unveraenderter Konfiguration und bisherigen lokalen Pfaden:

```powershell
$env:PLATFORMIO_CORE_DIR = 'F:/Development/mofa_tacho/.pio/gnss-fifo-p3/core'
$env:PLATFORMIO_CACHE_DIR = 'F:/Development/mofa_tacho/.pio/gnss-fifo-p3/core/.cache'
$env:PLATFORMIO_WORKSPACE_DIR = 'F:/Development/mofa_tacho/.pio/gnss-fifo-p3'
$env:PLATFORMIO_BUILD_DIR = 'F:/Development/mofa_tacho/.pio/gnss-fifo-p3/build'
$env:PLATFORMIO_LIBDEPS_DIR = 'F:/Development/mofa_tacho/.pio/gnss-fifo-p3/libdeps'
$env:PLATFORMIO_SETTING_ENABLE_TELEMETRY = 'No'
$env:PLATFORMIO_SETTING_CHECK_PLATFORMIO_INTERVAL = '0'
& C:/Users/nicol/.platformio/penv/Scripts/platformio.exe run -e giga_r1_m7
```

**Exit 0, `[SUCCESS] Took 7.30 seconds`**, Prozessdauer **68,628 s** inklusive anschliessender fehlgeschlagener Upgradeabfrage. Inkrementell display_rotation.cpp und lv_blend_helium.S kompiliert, danach Link/BIN. Keine neue Codewarnung, kein Clean-Build und kein Paket-/Bibliotheksupdate. Nachweise `build.log`, `build-result.json`.

| Messgroesse | reason3-Diagnose | mask1024-Fix | Differenz |
| --- | ---: | ---: | ---: |
| RAM `.data + .bss` | 155976 | 156024 | +48 |
| Flash `.text + .data + .ARM.exidx` | 495024 | 495240 | +216 |
| firmware.bin | 497408 | 497624 | +216 |
| Linkerheapbereich | 367264 | 367232 | -32 |

Sections: `.text=486696`, `.ARM.extab=2384`, `.ARM.exidx=3656`, `.data=4888`, `.bss=151136`, `.uninitialized=8`, `.heap=367232`, `.lwip_sec=278528` Byte. `.data` +16, BSS +32, `.uninitialized` 8 statt 24 Byte; deshalb unterscheiden sich PlatformIO-RAM- und Heapbereichsdifferenz. Keine ungesicherte Objektzuordnung der uninitialized-Section. Linkerheap ist keine freie Laufzeitreserve.

Parameterkopie jetzt **148 statt 136 Byte**, statische Diagnosezeile **256 statt 224 Byte**. Der eigene Flush-Prologframe steigt von **392 auf 416 Byte** inklusive gesicherter Register; reportErrors bleibt **48 Byte**, Installer **8 Byte**, Zahlformatter **32 Byte**. Getter-/LVGL-/Allocator-/DSI-/Serialketten kommen hinzu. Kein Nachweis ausreichender Gesamtreserve auf dem unveraenderten 8192-Byte-Displaystack und keine Hardware-Stackmessung. Symbole/Frames/Sections sind in `symbols.log`, `readelf-symbols.log`, `display-disassembly.log`, `sections.log` gesichert; je ein Stock- und Projektcallback bleiben vorhanden.

Die erweiterte params2-Zeile enthaelt `offsetPublic`, `offsetPhysical` und `rawOffsetZero`. Worstcase inklusive Newline: params1 **220**, params2 **255**, params3 **192 Byte**, alle unter/gleich dem 256-Byte-Puffer; positive Zeile **132**, laufende Fehlerzeile **90 Byte** (`report-bounds.json`). Writer reserviert die letzte Position fuer Newline; Serial.write braucht keinen NUL-Terminator. Einmalige Parameter-/Transferzeilen und 5-s-Drossel laufender Fehler bleiben erhalten. Ausgabe erfolgt ausserhalb Flush und ist weiterhin nicht garantiert nichtblockierend.

ELF-SHA-256: `6b7e46da966c6117d35d5e5e3b7018e1d339e7cb3d606e6bac07d13869f57078`.
BIN-SHA-256: `17d765ef4600b9eb92b130e7205b8420d06b0cd4534de2c36feb8f1586a8e673`.

## Erwartete Hardwarediagnose und naechster Test

Nominalbeispiel, keine Hardwaremessung:

```text
[DISPLAY ROT DIAG] params1 reason=0 mask=0 policy=0 rotation=3 mode=0 cf=18 logical=800x480 original=480x800 dsi=480x800
[DISPLAY ROT DIAG] params2 area=0,0,799,23 mapped=1 rotated=456,0,479,799 offsetPublic=800,480 offsetPhysical=800x480 rawOffsetZero=1
[DISPLAY ROT DIAG] policy-ok dsi-returned w=24 h=800 pixels=19200 dma-status=unavailable visible-check=required
```

params3 muss zusaetzlich vollstaendig gesichert werden; fuer diesen Block sind Source-/Requestgroesse 800x24, Stride 1600 und 38400 verfuegbare Bytes plausibel, Zielstart 456,0. Die ersten realen Flushbereiche koennen davon abweichen. Bei erfolgreicher erstmaliger Verarbeitung des nominalen Blocks muss die Rotationskapazitaet **38400 Byte** erreichen. Die bisherige schwarze Anzeige mit Kapazitaet null und fruehem Heapwert 73744 ist keine funktionierende Vergleichsbaseline; der nun wirklich benoetigte Scratchbuffer kann die aktuelle Heapbelegung gegen diesen abgebrochenen Pfad erhoehen.

1. Nur nutzerseitig bzw. nach gesonderter Uploadfreigabe aufspielen; hier kein Upload. **115200-Baud-Monitor vor Reset verbinden**, alle drei Parameterzeilen, positive Transferzeile und Heap-/USB-Meldungen vollstaendig sichern.
2. Erwartet: `mask=0`, `mapped=1`, `rawOffsetZero=1`, korrekte gedrehte Area. Oeffentliche Offsets 800/480 sind bei Rotation 270 und null roher Translation korrekt; sie duerfen nicht wieder pauschal als Fehler gewertet werden. Echte andere rohe Translation bleibt nicht unterstuetzt.
3. **Positive Meldung und korrekt sichtbare Darstellung gemeinsam pruefen.** `dsi-returned` ist kein HAL-/DMA-Erfolgsstatus: die void-DSI-API kann interne Fehler verbergen. Selbsttest, Orientierung, Text/Arcs und Livewerte visuell pruefen; keine Darstellung allein aus Heapwerten ableiten.
4. Danach denselben problematischen Stick mit 5-Hz-GNSS testen und Heap-/Allokations-/FIFO-/USB-Diagnosen vergleichen. Bei fortbestehendem Schwarzbild trotz positiver Meldung vollstaendigen Mitschnitt und Bildbeobachtung liefern; weitere Nacharbeit erst anhand des konkreten Befunds. Kein automatisches Aufweichen weiterer Guards oder Aendern von Abbildungsformeln.

Abschlusskontrollen: `git diff --check` Exit 0, lediglich bestehende LF/CRLF-Hinweise fuer FIFOheader/Runner. Zusaetzliche Whitespacekontrolle erfasst neue unversionierte Diagnose-/Testdateien. Hashvergleich zum Testerstart findet ausschliesslich die vier freigegebenen bestehenden Testerdateien veraendert; neu ist die genehmigte Mappingtestdatei. Implementierung, main.cpp, UI, Konfigurationen, GNSS-/USB-/Heaptests und `P2_P3.bash` bleiben hashgleich. Stockvideocode unveraendert; der komplette vorherige reason3-Bericht samt aelterer Historie bleibt bytegleich als Suffix erhalten. Belege `status.log`, `final-hashes.json`, `checks.json`, `integration-checks.json`. Alle selbst gestarteten Prozesse sind beendet; die fremde PlatformIO-Sitzung wurde nicht gesteuert.

Keine Hardware-/CASE-/Dauerhaftigkeitsabnahme, kein Upload, Commit, Push, Merge oder Library-/UI-/Konfigurationspatch. Naechster Freigabepunkt: manueller Hardwaretest bzw. ausdruecklicher Folgeauftrag anhand seiner Ergebnisse.

---

## Historische reason3-/Rotations-/V3-/V2-/V1- und P3-Berichte

# P3 - Schwarzes Display / reason=3: gezielte Parameterdiagnose

Pruefdatum: **22. November 2026**. Auftrag: schwarzes Display mit `reason=3` untersuchen, sichere Nacharbeit und Test; Basis `7b85488` auf `feature/gnss-fifo-logging` mit bewahrten Vorarbeiten. **Testerphase abgeschlossen: acht C++-Suiten mit 170 Assertions, acht GNSS- und sechs Display-Quellvertraege, Build und Abschlusskontrollen erfolgreich.** Eine konkrete Hardwareursache ist **nicht nachgewiesen**. Dieser Stand ergaenzt die benoetigte Parameterdiagnose; das Schwarzbild wird **nicht als behoben** gemeldet. Die bisherige Rotations-/V3-/V2-/V1-Historie bleibt unten bytegleich erhalten.

## Umfang und gesicherter Stand

TARS meldete vor Testerstart: Entwickler abgeschlossen, keine aktiven Schreiber/Buildprozesse; `workspace-write` fuer Projekt und Temp, geschuetzte `.git`/`.agents`/`.codex`/`.aws`, eingeschraenktes Netzwerk, keine Rechteerweiterung. Tester aendert ausschliesslich `test/display_rotation/test_rotation.cpp`, `test/display_rotation/test_integration.py` und diesen Bericht. Keine Implementierungs-, Konfigurations-, UI-, Paketcache- oder P2_P3-Aenderungen durch den Tester. Kein CASE oder Upload.

Entwicklerstand dieser Phase: Parameter-/Ersttransferdiagnose in `src/display_rotation.cpp`, zusaetzlicher Policy-Untergrund und optionaler Planout in `src/display_rotation_buffer.h`. Bestehende Guards bleiben erhalten. `src/main.cpp` ist unveraendert zum vorherigen Rotationsfix, SHA-256 `d8c7db83df361643a84d8a15e213cb45e7d5a508db6a8da1554dfdfdee16933d`. Stock-Arduino_H7_Video.cpp vor/nach Build hashgleich (`integration-checks.json`). Nachweise unter **`.pio/gnss-fifo-p3/display-reason3/`**; Ausgangsfirmwarehashes und Sections wurden vor Neubuild gesichert und entsprechen dem vorherigen Rotationsbericht.

## Nachweis und neue Diagnose

Nominale Parameter **logisch 800x480, Original/DSI 480x800, RGB565, Partial, Rotation 270 Grad** werden durch die bestehenden Guards nicht grundsaetzlich abgewiesen. Neu prueft die produktive Policy den Vollbreitenblock `(0,0,799,23)` mit 800x24 Pixeln, Quellstride 1600, 38400 Quellbytes, gemapptem Ziel `(456,0,479,799)`, Zielstride 48 und Zieloffset 912 Byte erfolgreich. Der fruehere 480x40-Test bleibt als weiterer Geometriefall bestehen. Diese positiven Modellwerte erklaeren noch nicht, welche tatsaechlichen Hardwarewerte `reason=3` ausloesen.

Der Callback behaelt deshalb eine **eigene Kopie der ersten Parameter** vor Allokation/Pixelrotation/DMA. Eingangs-/gemappte Bereiche werden als Werte kopiert; die spaetere Ausgabe liest keine temporaeren LVGL-Area-Pointer. Nach Policyabschluss werden Ergebnis und Untergrund ergaenzt. Die Ausgabe erfolgt erst ausserhalb des Flush im bestehenden Displaytask nach `lv_task_handler()`.

Einmalig werden drei Zeilen `params1`, `params2`, `params3` ausgegeben; bei nicht verbundenem Serial bleibt der noch nicht ausgegebene Teil ausstehend. Die Momentaufnahme stammt nur vom ersten Flush: ein spaeterer anderer Geometriefehler ersetzt sie nicht. Vollstaendige Zeilen sind best effort; die Ausgabe prueft die zurueckgeschriebene Bytezahl von Serial.write nicht. Daher bei unvollstaendigem Mitschnitt mit schon verbundenem Monitor neu starten.

| Zeile | Felder |
| --- | --- |
| params1 | reason, mask, policy, rotation, mode, cf, logical, original, dsi |
| params2 | area, mapped, rotated, offset |
| params3 | source (Bufferdimensionen), stride, bytes (Bufferumfang), available (ab Pixelzeiger), framebuffer (Adresse dezimal), request, dest |

`rotation=3` bedeutet 270 Grad; lokal `mode=0` Partial und `cf=18` RGB565. `mapped=0` bedeutet, dass keine gueltige Bereichsumrechnung durchgefuehrt wurde; `rotated` kann dann noch die kopierte Eingangsarea enthalten. Bei frueher Ablehnung bleiben noch nicht errechnete Requestwerte null. `mask` wird dezimal ausgegeben und kann mehrere Bits enthalten:

| Bit | Wert | Adapter-Geometrieablehnung |
| --- | ---: | --- |
| 0 | 1 | x1 negativ |
| 1 | 2 | y1 negativ |
| 2 | 4 | x2 kleiner x1 |
| 3 | 8 | y2 kleiner y1 |
| 4 | 16 | logische Breite/Hoehe nicht positiv |
| 5 | 32 | x2 ausserhalb logischer Breite |
| 6 | 64 | y2 ausserhalb logischer Hoehe |
| 7 | 128 | Originalbreite/-hoehe nicht positiv |
| 8 | 256 | Originalbreite groesser DSI-Breite |
| 9 | 512 | Originalhoehe groesser DSI-Hoehe |
| 10 | 1024 | Displayoffset x oder y ungleich null |
| 11 | 2048 | gemappter Start x/y negativ |
| 12 | 4096 | gemapptes Ende kleiner Start |
| 13 | 8192 | gemapptes x2 ausserhalb DSI-Breite |
| 14 | 16384 | gemapptes y2 ausserhalb DSI-Hoehe |
| 15 | 32768 | Framebytegroesse/-adressaddition laeuft ueber |

`policy` benennt einen getrennten Untergrund in `makePlan`: **0** keiner/Adapterpreflight, **1** ungueltige Eingabedimension/Rotation, **2** Width-/Height-Striderechnung wuerde ueberlaufen, **3** Ziel ausserhalb Framebuffer, **4** Ausgangsgroesse laeuft ueber/uebersteigt INT32_MAX, **5** Framebuffer-/Offsetgroesse laeuft ueber. Nichtgeometrische Fehler bleiben bei Untergrund 0. Planout wird auch bei Abbruch vor Wachstum exportiert. Der defensive Untergrund 4 wird als Klassifizierungshelfer getestet; bei den aktuellen Sourceextent-/Tightstride-Grenzen ist seine Ausloesung nicht mit einer gueltigen Eingabe konstruiert worden.

Beim ersten tatsaechlich zurueckgekehrten DSI-Aufruf wird separat Breite/Hoehe/Pixelzahl gesichert. Erst nach allen drei Parameterzeilen erscheint einmalig:

```text
[DISPLAY ROT DIAG] policy-ok dsi-returned w=24 h=800 pixels=19200 dma-status=unavailable visible-check=required
```

Diese Meldung bestaetigt **Policyannahme und Rueckkehr aus `dsi_lcdDrawImage()`**, keine erfolgreiche DMA oder sichtbare Darstellung. Die vorhandene void-DSI-API verschluckt HAL-Status; auch bei intern fehlgeschlagenem DMAstart kann sie zurueckkehren. Positiver End-to-End-Nachweis erfordert diese Meldung **und ein korrekt sichtbares Bild**. Fruehe Heapwerte wie 73744 Byte bei Rotationskapazitaet null belegen keinen funktionierenden Displaypfad.

## Tests und Aussagegrenzen

Einmal ausgefuehrt, **Exit 0**, Prozessdauer **1,197 s**:

```powershell
& C:/Users/nicol/.platformio/penv/Scripts/python.exe test/gnss_logger/run_tests.py
```

Alle acht C++-Suiten mit ARM GCC 7.2.1, `-std=gnu++17 -Wall -Wextra -Werror -c`, bestanden. Die bisherigen 160 Assertions bleiben aktiv; zehn neue betreffen Untergruende, Planout bei Preflight/Format/Source/Allokationsfehler und nominale 800x24-Abbildung. GNSS-130 Assertions plus Display-40 ergeben **170**. Alle acht bisherigen GNSS- und sechs Display-Pythonmethoden bestanden (`tests.log`, `tests-result.json`). unittest-Stderr kann im PowerShell-Log als `NativeCommandError` dargestellt sein; Exit 0 und zweimal `OK` belegen Erfolg.

Die Displayvertraege wurden strukturell an die unveraenderten nun maskierten Koordinatengates angepasst: alle elf Initialbedingungen sind mit ihren eindeutigen Bits und Aufrufreihenfolge geprueft, ebenso die fuenf spaeteren Mapping-/Frameguards. Die bisherige Ausgabe direkt in reportErrors wurde auf die gemeinsame Message-Sendestelle nachverfolgt; keine Freigabe fuer Ausgabe im Flush. Hinzu kommen eigene Snapshotwerte/Erstaufnahme, Ausgabe genau der drei Teile, Ergebnisergaenzung nach Policy, positiver Transferbeleg erst nach DSI-Rueckkehr und ausdrueckliche Kennzeichnung des fehlenden DMAstatus. Alte Sicherheitsgates wurden nicht entfernt oder pauschal abgeschwaecht.

Keine Ausfuehrung privater Adapterfunktionen mit echter LVGL-/DSI-Hardware wird behauptet. Initialmaske und Adapterrouting sind Quellvertraege; C++ fuehrt die gemeinsame Produktionspolicy zur Compilezeit mit Fakes aus. Die Fakes beobachten Geometrie, Pointerbesitz und Aufrufreihenfolge, keine Bildpixel oder DMA. Auch die erfolgreiche Nominalpruefung ist keine Reproduktion des Schwarzbilds.

## Ausgabegrenzen und Laufzeitrisiken

Die gemeinsame statische Zeile hat **224 Byte**. Text-/Zahlwriter begrenzen die Nutzlaenge auf 223, Send fuegt ein Newline hinzu; Serial.write nutzt explizite Laenge ohne NUL. Selbst unbeschraenkte uint32-/int32-Extrema der aktuellen Felder ergeben inklusive Newline hoechstens **220/185/192 Byte** fuer params1/2/3 und **132 Byte** fuer die positive Meldung. Laufende reason/count/need/capacity bleiben maximal **90 Byte**. Beleg `report-bounds.json`.

Drei Parameterzeilen und eine positive Zeile sind einmalige Zusatzmeldungen, nicht der 5-s-Fehlerdrossel unterstellt. Laufende Fehlerzaehler bleiben auf eine Meldung pro fuenf Sekunden begrenzt. Keine neuen Heap-/printf-/String-Aufrufe fuer die Ausgabe, kein Register-/Framebuffer-/Cachescan. Serial.write ausserhalb Flush bleibt **nicht garantiert nichtblockierend** und kann die Anzeige zeitlich beeinflussen. Ein blockierter DSI-Aufruf verhindert die nachtraegliche Ausgabe; das Ausbleiben einer Diagnose beweist keinen fehlerfreien Pfad.

## Build, Speicher und Stack

Einmal ausgefuehrt mit denselben lokalen Pfaden und unveraenderter Konfiguration:

```powershell
$env:PLATFORMIO_CORE_DIR = 'F:/Development/mofa_tacho/.pio/gnss-fifo-p3/core'
$env:PLATFORMIO_CACHE_DIR = 'F:/Development/mofa_tacho/.pio/gnss-fifo-p3/core/.cache'
$env:PLATFORMIO_WORKSPACE_DIR = 'F:/Development/mofa_tacho/.pio/gnss-fifo-p3'
$env:PLATFORMIO_BUILD_DIR = 'F:/Development/mofa_tacho/.pio/gnss-fifo-p3/build'
$env:PLATFORMIO_LIBDEPS_DIR = 'F:/Development/mofa_tacho/.pio/gnss-fifo-p3/libdeps'
$env:PLATFORMIO_SETTING_ENABLE_TELEMETRY = 'No'
$env:PLATFORMIO_SETTING_CHECK_PLATFORMIO_INTERVAL = '0'
& C:/Users/nicol/.platformio/penv/Scripts/platformio.exe run -e giga_r1_m7
```

**Exit 0, `[SUCCESS] Took 7.24 seconds`**, gesamter Prozess **68,620 s** einschliesslich anschliessender fehlgeschlagener Upgradeabfrage. Inkrementeller Build mit Neukompilierung von display_rotation.cpp und lv_blend_helium.S; kein Clean-Build oder Paketupdate. Keine neue Codewarnung. Nachweise `build.log`, `build-result.json`.

| Messgroesse | Voriger Rotationsfix | Parameterdiagnose | Differenz |
| --- | ---: | ---: | ---: |
| RAM `.data + .bss` | 155688 | 155976 | +288 |
| Flash `.text + .data + .ARM.exidx` | 492928 | 495024 | +2096 |
| firmware.bin | 495312 | 497408 | +2096 |
| Linkerheapbereich | 367576 | 367264 | -312 |

Sections: `.text=486496`, `.ARM.extab=2384`, `.ARM.exidx=3656`, `.data=4872`, `.bss=151104`, `.uninitialized=24`, `.heap=367264`, `.lwip_sec=278528` Byte. `.data` nimmt um 136, BSS um 152 Byte zu. Zusaetzlich erscheint die **24-Byte-.uninitialized-Section**; sie wird hier keinem konkreten Objekt/Ursprung zugeschrieben. Deshalb weicht die Heapbereichsverminderung von der PlatformIO-RAM-Differenz ab. Linkerheap ist keine gemessene freie Laufzeitreserve.

Neue Symbolgroessen: erste Parameterkopie **136 Byte**, erster Transferbeleg **16 Byte**, gemeinsamer Diagnosepuffer **224 Byte** statt vorheriger 96-Byte-Zeile; weitere Teil-/Ausgabeflags und Zaehler. Der Projektflush benoetigt jetzt einen **eigenen Frame von 392 Byte statt 104 Byte** inklusive gesicherter Register, unter anderem fuer die lokale Diagnosekopie. `reportErrors` eigener Frame **48 Byte** (vorher 56), Zahlformatter 32, Texthelfer 8, Area-/Dimensions-/Signed-/Sendhelfer jeweils 16 Byte. LVGL-/Allocator-/DSI-/Serial-Aufrufketten kommen hinzu. Das ist **kein Nachweis ausreichender Reserve** auf dem unveraenderten 8192-Byte-Displaystack und keine Hardware-Stackmessung. Artefakte `size.log`, `sections.log`, `symbols.log`, `readelf-symbols.log`, `display-disassembly.log`.

ELF-SHA-256: `c7bce4f097d543402cd1f31708ec387c9752abcc8d3c9b61914e98f98c354c3f`.
BIN-SHA-256: `a0b9a849e8344436f6b97ca76205ba9b0f2b7c70f17f1f19abebbf1b54b5516e`.

## Naechster manueller Hardwaretest

1. Erst nach gesonderter Uploadfreigabe bzw. nutzerseitig Diagnose-BIN aufspielen; hier wurde nichts geflasht. Monitor **115200 Baud vor Reset verbinden**, dann neu starten. Alle drei kompletten `params1`, `params2`, `params3`-Zeilen und gegebenenfalls die positive `policy-ok dsi-returned`-Meldung sichern. Bei fehlenden Teilen Monitor verbunden lassen und erneut starten.
2. Erste Werte mit den nominalen 800x480/480x800/270/Partial/RGB565 vergleichen. reason=3 anhand mask und policy auf einen konkreten Gatezweig eingrenzen; area/mapped/rotated/offset und die Buffer-/DSI-Dimensionen gemeinsam beurteilen. Nicht aus der unveraenderten Nullkapazitaet oder einem einzelnen Heapwert auf DMAerfolg schliessen.
3. Selbsttest und Liveanzeige visuell pruefen. Positiver Uebertragungsnachweis erfordert Policy-/DSI-Rueckkehrmeldung **plus korrekte sichtbare Darstellung**. Bei weiterhin schwarzem Bild trotz Meldung kann die void-DSI-API einen internen HAL-/Panelpfadfehler weiterhin verbergen; keine weiteren Guardlockerungen ohne konkrete Daten.
4. Vollstaendigen Mitschnitt samt V1-/V2-/Heapdiagnosen, LED-/Fatalzustand und beobachtetem Bildzustand zurueckgeben. Erst mit diesen tatsaechlichen Parametern ueber gezielte weitere Nacharbeit entscheiden. Ein USB-/GNSS-Test ersetzt den Displaypfadnachweis nicht; kein automatischer Upload oder weiterer Hardwareeingriff.

Abschlusskontrollen: `git diff --check` Exit 0; lediglich bestehende LF/CRLF-Hinweise fuer FIFOheader/Runner. Zusaetzliche Whitespacekontrolle umfasst unversionierte Diagnosequellen und Tests. Hashvergleich zum Testerstart findet ausschliesslich die beiden genehmigten Rotationstestdateien und diesen Bericht geaendert. Gesamte Implementierung, main.cpp, UI, Konfigurationen, bisherige GNSS-/Heaptests, Runner und `P2_P3.bash` bleiben hashgleich; Stockvideocode vor/nach Build unveraendert. Der vorherige Rotationsbericht samt aller Historie bleibt bytegleich als Suffix erhalten. Gitstatus, Hash-/Integritaetsbelege in `status.log`, `final-hashes.json`, `checks.json`, `integration-checks.json`. Alle selbst gestarteten Prozesse sind beendet.

Keine nachgewiesene Schwarzbildbehebung oder Hardware-/CASE-Abnahme; kein Upload, Gitmutation, EEZ-/UI-/Librarypatch. Naechster Freigabepunkt: Benutzerentscheidung anhand der neuen Hardwarediagnose.

---

## Historische Rotations-/V3-/V2-/V1- und P3-Berichte

# P3 - Display-Rotationspuffer korrigieren

Pruefdatum: **22. November 2026**. Benutzerauftrag: Display-Rotationspuffer korrigieren, Fortsetzung ausdruecklich freigegeben. Branch **`feature/gnss-fifo-logging`**, Ausgangscommit **`7b85488`** plus bewahrte GNSS-/V1-/V2-/V3-Arbeit. **Testerstatus: abgeschlossen - acht C++-Suiten mit 160 Assertions, acht bisherige und vier neue Pythonvertraege, Firmwarebuild und Abschlusskontrollen erfolgreich.** Dieser Bericht ist aktuell; die vollstaendige bisherige V3-/V2-/V1-/P3-Historie bleibt unten bytegleich erhalten.

## Umfang und Integritaet

TARS meldete vor Testerstart: Entwickler beendet, kein aktiver Schreiber und keine laufenden Buildprozesse; `workspace-write` fuer Projekt und Temp, geschuetzte `.git`/`.agents`/`.codex`/`.aws`, eingeschraenktes Netzwerk, Werkzeugfreigaben ueber Benutzer. Keine Rechteerweiterung oder CASE-Pruefung. Tester-Schreibdateien: neu `test/display_rotation/test_rotation.cpp` und `test/display_rotation/test_integration.py`, Runner `test/gnss_logger/run_tests.py`, dieser Bericht sowie die gezielt angepasste Vergleichsstelle in `test/gnss_logger/test_integration.py`. `test_logger.cpp` bleibt unveraendert.

Implementierung vom Entwickler: neu `src/display_rotation_buffer.h`, `src/display_rotation.h`, `src/display_rotation.cpp`; in `src/main.cpp` ausschliesslich Include, Installation nach erfolgreichem `Display.begin()` und Diagnoseaufruf nach `lv_task_handler()`. Die Ruecknahme exakt dieser drei Aenderungen ergibt bytegleich den V3-main-Hash **`3f0cda434f738123182107b6488da31861120f3075dce765624b3cf171acc1c3`**. Damit bleibt der gesamte uebrige GNSS-/Logger-/Displaytaskbestand nachweislich erhalten (`integration-checks.json`).

Nachweise unter **`.pio/gnss-fifo-p3/display-rotation-fix/`**. V3-ELF-/BIN-Hashes und Sections wurden vor Neubuild gesichert. Der tatsaechlich verwendete Stock-Quelltext `core/packages/framework-arduino-mbed/libraries/Arduino_H7_Video/src/Arduino_H7_Video.cpp` ist vor/nach Build hashgleich: **`a0f26203b563883756cdbeeeb74cd804829c271d2a5dd92173798f5b092c25de0`**. Keine Paket-/Bibliotheksquelle oder Konfiguration wurde bearbeitet; lokale Konfigurationsinhalte werden nicht ausgegeben. `P2_P3.bash` bleibt bewahrt.

## Befund und integrierter Pfad

Im unveraenderten Stockcallback ist `rotated_buf = realloc(rotated_buf, w * h * 4)` belegt: vier Byte pro Pixel trotz RGB565 und Verlust des einzigen alten Zeigers bei fehlgeschlagener Reallokation. Das sind nachgewiesene Quelltextprobleme; ihre konkrete Beteiligung an USB-ENOMEM/Absturz bleibt hardwareseitig zu pruefen.

Der neue projektinterne Callback wird nach erfolgreichem Displaybegin und vor `ui_init()`/Anwendungsthreadstarts ueber den **oeffentlichen LVGL-Setter** registriert. Kein Librarypatch und kein gleichnamiger Symbolersatz. ELF/nm/readelf enthalten genau einen Stockcallback und genau einen anders benannten Projektcallback. `callback-install.log` belegt die Relokation des Projektcallbacks als Argument fuer `lv_display_set_flush_cb`; `setup-handler-calls.log` belegt die bedingte Setupinstallation und den Diagnoseaufruf nach dem Handler. Der alte Callback bleibt wegen der Stockinitialisierung im ELF enthalten; der neue Installer ersetzt den registrierten Zeiger.

Gepruefte lokale Grundlagen: LVGL **9.6.0**, Arduino Mbed **4.6.0** einschliesslich Arduino_H7_Video und DSI. Die oeffentlichen LVGL-Funktionen fuer Rotation, Originalaufloesung und Drawbufferstride sind vorhanden und erfolgreich gelinkt. RGB565 benoetigt **zwei Byte pro Pixel**. Die Produktionspolicy prueft Formate, Partialmodus, Koordinaten, Sourceextent, signed-LVGL-Grenzen, Frameextent, Stride und Groessenueberlaeufe, bevor Allokation/Rotation/DMA stattfinden. Sie uebergibt den **wirklichen Quellstride** und verlangt einen dicht gepackten Zielstride. Das ist notwendig, weil die unveraenderte DSI-DMA-Konfiguration `InputOffset=0` verwendet. Auch bei Rotation 0 wird gepaddeter Direktinput abgewiesen.

Vorhandene Bufferkapazitaet wird wiederverwendet; Wachstum wird erst nach erfolgreichem realloc uebernommen. Bei Allokationsfehler bleiben alter Zeiger und Kapazitaet erhalten. Jeder Policyabbruch meldet Fehler und genau einmal Flush-ready, ohne Rotation/DMA. Bei erfolgreichem Rotationsflush lautet die Reihenfolge Validierung, gegebenenfalls Wachstum, Rotation, DMA, ready. Normalheap/Reallokator und die vorhandene DSI-Cachebehandlung bleiben erhalten; keine Verlagerung nach SDRAM und keine neue DMA-/Cachepolitik.

Beim heutigen 480x40-Pixel-Partialblock werden fuer das Rotationsziel **38400 Byte statt bisher bis zu 76800 Byte** angefordert. Der urspruengliche Drawbuffer bleibt zusaetzlich bestehen. Diese Groessen gelten fuer den aktuellen RGB565-Partialpfad, nicht fuer beliebige kuenftige Formate/Modi. Eine halbierte Anfrage garantiert weder eine erfolgreiche Allokation bei Fragmentierung noch eine behobene USB-Ursache.

## Tests

Einmal ausgefuehrt, **Exit 0**, Prozessdauer **1,203 s**:

```powershell
& C:/Users/nicol/.platformio/penv/Scripts/python.exe test/gnss_logger/run_tests.py
```

ARM GCC 7.2.1, `-std=gnu++17 -Wall -Wextra -Werror -c`, danach beide Python-Vertragssuiten. Nachweise `tests.log`, `tests-result.json`. PowerShell kann unittest-Stderr als `NativeCommandError` darstellen; Exit 0 und beide abschliessenden `OK` sind massgeblich.

| C++-Suite | Assertions | Ergebnis |
| --- | ---: | --- |
| gnss_epoch | 10 | bestanden |
| local_time | 17 | bestanden |
| log_clock | 6 | bestanden |
| rpm_filter | 4 | bestanden |
| ui_self_test | 12 | bestanden |
| gnss_log_fifo | 16 | bestanden |
| gnss_logger | 65 | bestanden |
| display_rotation | 30 | bestanden |
| **Summe** | **160** | **bestanden** |

Alle bisherigen sieben Suiten/130 Assertions und acht Pythonvertraege bestehen fort. Der bestehende Displaytask-Originalvergleich erlaubt ausschliesslich die exakt einmalige neue `reportErrors()`-Zeile unmittelbar nach `lv_task_handler()`; nach deren Entfernung wird weiterhin die gesamte Funktion gegen das Original verglichen. Keine bisherige Bedingung wurde entfernt oder pauschal abgeschwaecht.

Die neue Suite verwendet die **produktive constexpr-Policy** mit typisierten Fakeoperationen. Geprueft: initialer Aufbau, unveraenderte/kleinere Wiederverwendung ohne realloc, erfolgreicher Growcommit, initialer Fehlschlag, fehlgeschlagenes Wachstum mit altem Zeiger/Kapazitaet und spaetere sichere Wiederverwendung; RGB565 mit 0/90/180/270, Dimensionen/Offset/zwei Byte pro Pixel; gepaddeter Quellstride mit genauer Extentgrenze und Ablehnung gepaddeter DMA-Zielzeilen; ungueltiger Modus/Format/Rotation/Geometrie, Alignment, Byteextent und Groessenueberlauf vor Stride/Allokation. Alle Abbruchfaelle pruefen kein Rotate/DMA und genau einmal ready.

Vier neue Pythonvertraege pruefen Adapterklassifizierung einschliesslich Format-vor-Sourcefehler, oeffentliche APIaufrufe, Koordinaten-/Source-/Framebounds, Setupreihenfolge/eindeutigen Callbacknamen, Allokation ausschliesslich im ensure-Pfad, keine serielle Ausgabe im Flush und Diagnose nach dem Handler. Die feste Meldung ist selbst bei vier maximalen uint32-Zahlen **90 Byte inklusive Newline**, unter 96 Byte; `Serial.write` nutzt die Laenge und braucht keinen NUL-Terminator. Ausgabe hoechstens eine Zeile pro fuenf Sekunden bei verbundenem Serial.

Pruefgrenze: Die C++-Fakes beobachten Entscheidungen, Zeigerbesitz, Geometrie und Aufrufreihenfolge; sie berechnen **keine gedrehten Bildpixel** und fuehren keine echte DMA aus. Tests sind Compilezeitauswertungen plus statische Quellvertraege, keine native Hostlaufzeit- oder Zielhardwarepruefung. Bildorientierung, Cache-/DMA-Kohärenz, Darstellung und RTOS-Timing bleiben Hardwarepruefpunkte.

## Build und Speichervergleich

Einmal ausgefuehrt mit unveraenderter massgeblicher Konfiguration und vorhandenen Paketen:

```powershell
$env:PLATFORMIO_CORE_DIR = 'F:/Development/mofa_tacho/.pio/gnss-fifo-p3/core'
$env:PLATFORMIO_CACHE_DIR = 'F:/Development/mofa_tacho/.pio/gnss-fifo-p3/core/.cache'
$env:PLATFORMIO_WORKSPACE_DIR = 'F:/Development/mofa_tacho/.pio/gnss-fifo-p3'
$env:PLATFORMIO_BUILD_DIR = 'F:/Development/mofa_tacho/.pio/gnss-fifo-p3/build'
$env:PLATFORMIO_LIBDEPS_DIR = 'F:/Development/mofa_tacho/.pio/gnss-fifo-p3/libdeps'
$env:PLATFORMIO_SETTING_ENABLE_TELEMETRY = 'No'
$env:PLATFORMIO_SETTING_CHECK_PLATFORMIO_INTERVAL = '0'
& C:/Users/nicol/.platformio/penv/Scripts/platformio.exe run -e giga_r1_m7
```

**Exit 0, `[SUCCESS] Took 34.15 seconds`**, gesamte Prozessdauer **95,673 s** inklusive anschliessender fehlgeschlagener Upgradeabfrage. Zahlreiche vorhandene Framework-/Libraryobjekte wurden neu kompiliert; kein Clean-Build beauftragt. Bekannte Warnungen aus Framework/Bibliotheken/generierter UI, keine Warnung aus dem neuen Rotationscode. Kein Paket-/Bibliotheksupdate. `build.log` und `build-result.json` dokumentieren den Lauf.

| Messgroesse | V3 Byte | Rotationsfix Byte | Differenz |
| --- | ---: | ---: | ---: |
| RAM `.data + .bss` | 155488 | 155688 | +200 |
| Flash `.text + .data + .ARM.exidx` | 491112 | 492928 | +1816 |
| firmware.bin | 493484 | 495312 | +1828 |
| Linkerheapbereich | 367776 | 367576 | -200 |

PlatformIO: RAM **29,7 %** von 523624, Flash **62,7 %** von 786432 Byte; BIN **63,0 %**. Sections: `.text=484544`, `.ARM.extab=2384`, `.ARM.exidx=3648`, `.data=4736`, `.bss=150952`, `.heap=367576`, `.lwip_sec=278528` Byte. Linkerheap ist keine gemessene freie Laufzeitreserve. Die dynamische Rotationskapazitaet ist darin nicht als bereits belegter statischer Speicher enthalten.

Neue rotationseigene BSS **200 Byte inklusive Alignment**: Bufferzeiger/Kapazitaet 8, Fehler-/Bedarfs-/gemeldete Zaehler je 28, letzter Ausgabezeitpunkt 4, Flag 1, Ausgabelinie 96 Byte. Der alte Stock-`rotated_buf`-Zeiger bleibt als eigenes Symbol erhalten; nach Installation benutzt der neue Flush seinen eigenen Buffer. Angewandte Werkzeuge: ARM size/-A, nm/-S/-C, readelf/-s/-W und objdump/-d/-C/-r. Nachweise `size.log`, `sections.log`, `symbols.log`, `readelf-symbols.log`, `display-disassembly.log`, `main-disassembly.log`.

Eigene Prologframes inklusive gesicherter Register: Projektflush **104 Byte**, `reportErrors` **56 Byte**, Installer **8 Byte**, interner uint32-Zahlformatter **32 Byte**, Textkopierhilfe **8 Byte**, Displaytask **160 Byte**. Library-/LVGL-/realloc-/DSI-/Serial-Aufrufketten kommen hinzu; keine Gesamtstackspitzen behauptet. Der vorhandene Displaystack von 8192 Byte und alle anderen Stackgroessen bleiben unveraendert. `Serial.write` ausserhalb des Flush ist **nicht garantiert nichtblockierend**; der Core liefert ueber das geerbte `availableForWrite()` lediglich null. Die seltene begrenzte Ausgabe kann Displaytiming trotzdem beeinflussen. Keine Hardware-Latenzmessung.

ELF-SHA-256: `6cff099f8d4db481f55d1af0a815fb349ba9eee416f455e906c8da6a6b63a1fc`.
BIN-SHA-256: `eb53591781cb195e47a86b9f1c637e2ef8919c2f069086d818e08f0b6f03e589`.

## Diagnose und naechster manueller A/B-Test

Im Erfolgsfall gibt der neue Rotationscode keine Meldung aus. Beispiel bei fehlgeschlagenem Wachstum (illustrative Werte):

```text
[DISPLAY ROT DIAG] reason=6 count=1 need=38400 capacity=19200
```

Grundnummern: 1 Display/Modus, 2 Format, 3 Geometrie/Overflow, 4 Stride, 5 Quellbuffer/Extent, 6 Allokation. `capacity` bleibt bei fehlgeschlagenem Wachstum die alte gueltige Kapazitaet. Der betroffene Flush wird dann ohne DMA quittiert; die Anzeige kann dadurch voruebergehend einen alten Bereich behalten. Zaehler saettigen bei UINT32_MAX. Die Ausgabe ist begrenzt und bei getrenntem Serial nicht sofort sichtbar; fehlende Diagnose ist kein Hardware-Erfolgsbeweis.

1. A/B ausschliesslich nutzerseitig bzw. nach gesonderter Uploadfreigabe ausfuehren; dieser Auftrag hat keinen Upload vorgenommen. Beide Firmwarestaende jeweils **neu starten**, um alte Heapbelegung/Lecks nicht mitzunehmen. Gleiche Hardware, gleicher USB-Stick und gleiche 5-Hz-GNSS-Last; Serial ab Reset mit **115200 Baud** vollstaendig sichern.
2. Zuerst Selbsttest und Liveanzeige pruefen: Text/Arcs/Zahlen an richtigen Positionen, richtige Orientierung, keine vertauschten/reissenden Bildteile oder neuen Artefakte. Der aktuelle Partial-/RGB565-Pfad muss funktionieren; andere Formate/Render-Modi werden bewusst abgewiesen.
3. Den zuvor problematischen Stick einstecken und mehrere Minuten einschliesslich 30-s-Heap-Paarperiode laufen lassen. Heap-current/alloc/fail, Allokations-null-Trace, V2-I/O-Erstfehler und verbleibende Displayrotation-Fehler vergleichen. Erwarteter Scratchhoechstbedarf im heutigen Pfad 38400 statt 76800 Byte; genaue Gesamtpeaks koennen wegen anderer Threads abweichen.
4. Bei Fehler reason/need/capacity sowie angrenzende Heap-/USB-Diagnose und Fatal-Dump sichern. Wiederholtes fehlgeschlagenes Wachstum darf den alten Besitzer nicht verlieren; dies ist softwareseitig getestet, eine Fehlerprovokation auf Hardware bedarf eines gesonderten Auftrags. Kein absichtliches Heapfuellen in diesem Auftrag.
5. GNSS-Dateien am PC pruefen: 17 TSV-Spalten, FIFO-Bilanz, aktuelle Statistik und unveraenderte Legacyformate. Ob USB-ENOMEM/Absturz nach dem Rotationsfix verschwindet, bleibt erst nach diesem Hardwarevergleich entscheidbar.

Abschluss: `git diff --check` Exit 0; lediglich LF/CRLF-Hinweise fuer den vorhandenen FIFOheader und Runner. Zusaetzliche Whitespacekontrolle erfasst auch neue unversionierte Quellen/Tests. Hashvergleich zum Testerstart findet ausschliesslich die drei genehmigten bestehenden Testerdateien veraendert, zusaetzlich wurden die zwei freigegebenen Rotationstestdateien angelegt. Alle Implementierungs-/UI-/Konfigurationsdateien, `test_logger.cpp` und `P2_P3.bash` sind hashgleich geblieben; Stockvideocode vor/nach Build unveraendert. Komplette V3-Berichtshistorie bytegleich erhalten. Gitstatus und Abschlussbelege in `status.log`, `final-hashes.json`, `checks.json`, `integration-checks.json`. Alle selbst gestarteten Prozesse sind beendet.

Keine Hardware-/CASE-/Dauerhaftigkeitsabnahme, kein Upload, Commit, Push, Merge, UI-Code-/EEZ-Eingriff oder Librarypatch. Naechster Freigabepunkt ist der manuelle Hardwaretest oder konkret beauftragte Nacharbeit anhand seines Befunds.

---

## Historische Diagnose-V3-/V2-/V1- und P3-Berichte

# GNSS-FIFO P3 - Diagnose V3: Heap und Allokationsfehler

Pruefdatum: **22. November 2026**. Auftrag: ausdruecklich freigegebene Diagnose V3, Heapverbrauch und Allokationsfehler erfassen; anschliessende Testerphase. Branch **`feature/gnss-fifo-logging`**, Basis **`7b85488`** mit bewahrten V1-/V2-Aenderungen. **Status: abgeschlossen - alle sieben C++-Suiten mit 130 Assertions, alle acht Python-Quellvertraege, Firmwarebuild und Abschlusskontrollen erfolgreich.** Dieser Abschnitt ist aktuell; der komplette bisherige V2-/V1-/P3-Bericht bleibt unten historisch bytegleich erhalten.

## Umfang und bewahrter Stand

TARS meldete vor Beginn den Entwickler als abgeschlossen ohne Prozesse und den Tester als einzigen Schreiber. Wirksame Rechte laut Sitzung: `workspace-write` fuer Projekt und Temp, geschuetzte `.git`/`.agents`/`.codex`/`.aws`, eingeschraenktes Netzwerk und werkzeugseitiges Benutzerfreigabeverfahren. Keine Rechteerweiterung. Freigegebene Tester-Schreibdateien: `test/gnss_logger/test_logger.cpp`, `test/gnss_logger/test_integration.py` und dieser Bericht. Implementierung, UI, Konfigurationen, Bibliotheken und `P2_P3.bash` werden vom Tester nicht geaendert. V3-Implementierung: `src/main.cpp`, neu `src/heap_diag.h` und `src/heap_diag.cpp`; V1-FIFO und V2-USB-Diagnoseheader bleiben unveraendert.

Nachweise unter **`.pio/gnss-fifo-p3/diagnose-v3/`**: `initial-hashes.json`, `final-hashes.json`, `checks.json`, Tests-/Buildlogs, Sections/Symbole/Disassemblierung und Firmwarehashes. Vor Neubuild wurden V2-ELF-/BIN-Hashes, Sections und vorhandene malloc-/Newlib-/FatFs-/Trace-Ketten gesichert (`v2-firmware-hashes.json`, `v2-sections.log`, `v2-trace-symbols-expanded.log`, `v2-trace-chain.log`). V2-Hashes stimmen mit dem V2-Bericht ueberein. Lokale Konfigurationsinhalte werden nicht ausgegeben.

## Messwerte, Getter und Callback

Die reale API ist `void mbed_stats_heap_get(mbed_stats_heap_t *)`. Header und neuer Linkstand bestaetigen die Verfuegbarkeit. `linked-heap-apis.log` zeigt eine **28-Byte-Kopie** der bestehenden Mbed-Statistik unter dem vorhandenen Heapstatistik-Mutex; `reserved_size` wird aus `mbed_heap_size` gesetzt. Der Getter durchsucht keine Freiliste. Er ist **nicht lockfrei**: Wartezeit auf diesen bestehenden Mutex und Zeitaufwand wurden auf Hardware nicht gemessen. `readStats()` ist im neuen Objekt in `capture()` integriert; kein eigener isolierter readStats-Frame wird behauptet.

| Ausgabefeld | Quelle und Bedeutung |
| --- | --- |
| `current` | `current_size`: aktuell belegte Nutzbytes des erfassten Mbed-Heaps |
| `max` | `max_size`: bisheriger Hoechststand dieser Nutzbytes |
| `alloc` | `alloc_cnt`: aktuell noch nicht freigegebene Allokationen, kein kumulativer Allokationszaehler |
| `fail` | `alloc_fail_cnt`: bisher gezaehlte fehlgeschlagene Allokationen |
| `overhead` | `overhead_size`: vom Mbed-Wrapper gezaehlter Verwaltungs-/Allokatoroverhead |
| `reserved` | `reserved_size`: Heapbudget aus `mbed_heap_size`, **kein gemessener freier Block** |

`total_size` ist im tatsaechlichen Wrapper kumulativ und wird nicht als Leckindikator verwendet. Kein `mallinfo`, keine internen Freilistenzugriffe, kein groesster-freier-Block-Wert und keine geaenderten Heap-/LFN-/Bibliotheksflags. Metriken betreffen den erfassten Mbed-Heap; fremde Speicherpools oder SDRAM sind damit nicht vollstaendig beschrieben. Auch `reserved-current` waere ohne Overhead und Fragmentierung keine zuverlaessige maximal moegliche Allokation.

Bereits V2 besitzt die Wrapper-/Trace-Infrastruktur: `malloc -> __wrap__malloc_r -> malloc_wrapper`; `ff_memalloc` erreicht diesen Pfad. Der alte Tracecallback-Zeiger liegt in BSS, ein bestehender Setterbesitzer war im untersuchten Quell-/Linkstand nicht vorhanden. V3 registriert `allocationTrace` unter dem **bestehenden Mbed-Tracemutex**, nach Peripherie-/UI-Initialisierung und vor den unveraenderten Anwendungsthreadstarts. Bereits existierende Peripheriethreads werden durch diesen Registrierungsschutz beruecksichtigt. Die neue Disassemblierung zeigt: der Mallocwrapper gibt den Heapstatistik-Mutex vor dem Tracecallback frei; der vorhandene Tracemutex serialisiert die Produzenten und sein Tiefenzaehler unterdrueckt verschachtelte Traces.

Der Callback erfasst nur NULL-Ergebnisse mit positiver angeforderter Groesse fuer malloc/realloc/calloc; Free und legale Nullgroessen werden ignoriert. Er liest keine Statistik/Uhr, formatiert nicht, allokiert nicht, gibt nichts seriell aus und nimmt keinen neuen Mutex. Gesichert werden Operation, count, size und eine bestmoegliche Calleradresse. **`callerReliable=0`** bleibt ausdruecklich sichtbar; die Adresse ist kein verlaesslicher Stacktrace. malloc=0, realloc=1, calloc=2 nach dem lokal bestaetigten Mbed-Enum. Ein NULL-Ergebnis ist Allokationsfehler-Evidenz fuer die positive Anfrage, aber der Callback sichert keinen errno-Code.

Der acht Eintraege grosse Fehlerring besitzt einen durch Mbed serialisierten Produzentenpfad und den Logger als einzigen Konsumenten. Release/Acquire publiziert Slots; beim Lesen wird erst kopiert und dann der Tail freigegeben. Ueberlauf erhoeht einen bei UINT32_MAX saettigenden `lost`-Zaehler. Das ist eine begrenzte Zusatzspur, keine Garantie jeder Allokationsbeobachtung. Die SPSC-Annahme stuetzt sich auf die untersuchte Mbed-Wrapper-/Traceserialisierung; Zielhardware-Nebenlaeufigkeit wurde nicht ausgefuehrt.

## Messpunkte, Historie und Ausgabegrenzen

Passive Momentaufnahmen erscheinen nach Initialisierung vor USB-/Loggerarbeit, vor fopen/exklusivem open, nach **tatsaechlichem** fclose/close sowie vor/nach Mount und nach Unmount. Jedes Dateipaar behaelt eine ID. Fehlgeschlagenes Open erhaelt `open-failed-no-close`; es wird kein Close erfunden. `delta` und `deltaAlloc` sind signed64-Differenzen zur passenden Vorheraufnahme, ohne uint32-Unterlauf. Andere Threads koennen zwischen den Messpunkten allokieren: **ein Dateipaar beweist kein dateispezifisches Leck**.

Die laufende Historie umfasst acht kopierte Snapshots. Beim ersten I/O-Fehler und unabhaengig beim ersten errno=ENOMEM werden eigene Kopien **vor Cleanup** eingefroren; spaetere Ringumlaeufe veraendern sie nicht. Ausgabe erfolgt erst nach zurueckgekehrtem Cleanup. Dazu kommt jeweils eine separate `after-error-cleanup`-Aufnahme. Blockiert Cleanup dauerhaft, kann der intern gesicherte Befund seriell ausbleiben; die V1-before/after-Spur bleibt zusaetzlich wichtig. Ein fataler new-/Allokationsfehler kann ebenfalls den Logger stoppen, bevor die verzogerte Ausgabe des RAM-Fehlerrings erfolgt. Allokations-NULL-Ereignisse und errno=ENOMEM sind getrennte Quellen; das eine erzwingt nicht automatisch die andere Meldung.

Die Baseline wird genau nach der ersten vollstaendig erfolgreichen Loggerdateirunde mit mindestens einem echten Close und gueltigen Datei-/Mediumgates gesetzt. `baselineDelta` vergleicht die Snapshot-Nutzbytes mit dieser Baseline. `baseline=1` bedeutet, dass sie beim **Ausgeben** verfuegbar ist; historische eingefrorene Aufnahmen koennen zeitlich davor liegen. Wachsende Peaks oder kumulative Failzaehler allein beweisen kein Leck; bleibende aktuelle Nutzbytes/Allokationen nach wiederholtem Cleanup und gegen dieselbe Baseline sind die relevantere Beobachtung.

Normale Dateipaare: zuerst zwei Paare, danach hoechstens ein Paar pro 30 s. Erfolgreiche Mount-/Unmount-Meilensteine: zuerst zwei, danach hoechstens alle 5 s je Kategorie. Eingefrorene Erstfehlerhistorien: jeweils einmal. Allokationsfehlerspur: hoechstens acht Eintraege pro Ausgabe, hoechstens alle 5 s. Puffer sind statisch. Konservative Formatobergrenzen einschliesslich NUL: Heap-Snapshot **363 Byte**, Allokationsmeldung **151 Byte**, jeweils unter 384 Byte (`report-bounds.json`). signed64-Deltas und das uint64-Produkt count*size werden ueber die V1-Dezimalhelfer als Strings ausgegeben. Die V2-Erstfehlergrenze bleibt 328 Byte, GNSS-Datensatz/Statistik bleiben 185/255 Byte mit 17 Datenspalten.

## Tests und Pruefgrenzen

Einmal ausgefuehrt, **Exit 0**, Dauer **1,057 s**:

```powershell
& C:/Users/nicol/.platformio/penv/Scripts/python.exe test/gnss_logger/run_tests.py
```

ARM GCC 7.2.1, `-std=gnu++17 -Wall -Wextra -Werror -c`; danach alle acht Pythonmethoden bestanden. Nachweise `tests.log`, `tests-result.json`. PowerShell kann unittest-Stderr als `NativeCommandError` darstellen; Exit 0 und abschliessendes `OK` belegen den Erfolg.

| Suite | Assertions | Ergebnis |
| --- | ---: | --- |
| gnss_epoch | 10 | bestanden |
| local_time | 17 | bestanden |
| log_clock | 6 | bestanden |
| rpm_filter | 4 | bestanden |
| ui_self_test | 12 | bestanden |
| gnss_log_fifo | 16 | bestanden |
| gnss_logger | 65 | bestanden |
| **Summe** | **130** | **bestanden** |

Zehn neue Compilezeitregressionen fuehren produktive `History`-/`difference`-Policies aus: leer/teilvoll/voll, Chronologie ueber mehrfache Ringumlaeufe, Kapazitaeten eins und acht, gefrorene unabhaengige Kopien mit eigenem Pfad, allen sechs Statistikfeldern und signed64-Deltas sowie uint32-Differenzen jenseits INT32_MAX bis +/-4294967295. Alle V1-/V2-120-Assertions bleiben aktiv.

Die weiterhin acht Python-Vertraege pruefen zusaetzlich positive Messpunkte/Tokenpaarung, fehlgeschlagenes Open ohne Close, Init vor Anwendungsthreads, Freeze vor Cleanup und getrennte spaetere Ausgabe, Baselinegates, sechs API-Felder, zulassungsfreie direkte Callbackarbeit ohne Stats/Uhr/Serial/Allokation/neue Locks, Nullgroessenfilter, Release/Acquire-Reihenfolge, Lostsaettigung, Raten und Ausgabeobergrenzen. V1-/V2-errno-/FILE-/Recovery-/Legacy-/FIFO-/UI-Vertraege bleiben aktiv. Keine Tests mussten abgeschwaecht oder Implementierungsfehler repariert werden.

Grenze: Compilezeitpolicies und statische Quellvertraege ersetzen keine Ausfuehrung von malloc-Fault-Injection, SPSC-Stress unter RTOS, realem FAT-/USB-Cleanup, zeitlichem Callbackablauf oder Hardware-Stackmessung. Das erfolgreiche Linken und die Disassemblierung bestaetigen API/ABI und den untersuchten Kontrollfluss, keine gemessene Laufzeitlatenz oder Hardware-Absturzursache.

## Build, Speicher und eigene Stackframes

Einmal ausgefuehrt mit unveraenderter Konfiguration und vorhandenen Paketen:

```powershell
$env:PLATFORMIO_CORE_DIR = 'F:/Development/mofa_tacho/.pio/gnss-fifo-p3/core'
$env:PLATFORMIO_CACHE_DIR = 'F:/Development/mofa_tacho/.pio/gnss-fifo-p3/core/.cache'
$env:PLATFORMIO_WORKSPACE_DIR = 'F:/Development/mofa_tacho/.pio/gnss-fifo-p3'
$env:PLATFORMIO_BUILD_DIR = 'F:/Development/mofa_tacho/.pio/gnss-fifo-p3/build'
$env:PLATFORMIO_LIBDEPS_DIR = 'F:/Development/mofa_tacho/.pio/gnss-fifo-p3/libdeps'
$env:PLATFORMIO_SETTING_ENABLE_TELEMETRY = 'No'
$env:PLATFORMIO_SETTING_CHECK_PLATFORMIO_INTERVAL = '0'
& C:/Users/nicol/.platformio/penv/Scripts/platformio.exe run -e giga_r1_m7
```

**Exit 0, `[SUCCESS] Took 32.71 seconds`**, gesamte Prozessdauer **94,136 s** einschliesslich anschliessender fehlgeschlagener Upgradeabfrage. Zahlreiche vorhandene Framework-/Bibliotheksobjekte wurden neu kompiliert; kein Clean-Build beauftragt. Bekannte Warnungen aus Bibliotheken/Framework/generierter UI im Log; keine Warnung aus dem neuen Diagnosecode. Keine Paket-/Bibliotheksupdates. `build.log`, `build-result.json` dokumentieren den Lauf.

| Messgroesse | V2 Byte | V3 Byte | Differenz | Nutzung V3 |
| --- | ---: | ---: | ---: | ---: |
| RAM `.data + .bss` | 152680 | 155488 | +2808 | 29,7 % von 523624 |
| Flash `.text + .data + .ARM.exidx` | 487448 | 491112 | +3664 | 62,4 % von 786432 |
| firmware.bin | 489476 | 493484 | +4008 | 62,7 % von 786432 |

Sections: `.text=482760`, `.ARM.extab=2372`, `.ARM.exidx=3616`, `.data=4736`, `.bss=150752`, `.heap=367776`, `.lwip_sec=278528` Byte. Der Linkerheapbereich ist keine gemessene freie Laufzeitreserve. Artefakte `size.log`, `sections.log`, `symbols.log`, `main-disassembly.log`, `heap-disassembly.log`, `linked-heap-apis.log`.

Snapshot-ABI **88 Byte**, je History **712 Byte** (8*88 plus Indizes), drei Historien zusammen **2136 Byte** statisch. Allokationsfehlerring **128 Byte** (acht Eintraege a 16 Byte), Heap-Ausgabepuffer 384 Byte plus vier 21-Byte-Dezimalstrings, zusaetzliche Zustands-/Token-/Atomikwerte und Alignment. Die RAM-Zunahme von 2808 Byte liegt vollstaendig in BSS; `.data` ist unveraendert. FIFO und grosse Loggerarrays bleiben statisch. Die Diagnose implementiert keine eigenen malloc/new-Aufrufe und aendert keine Stackgroesse.

Eigene Prologframes inklusive gesicherter Register, **keine Gesamtstackspitzen**:

| Funktion | Byte |
| --- | ---: |
| allocationTrace | 56 |
| heap capture (mit integrierter readStats-Aufrufstelle) | 72 |
| heap ioError | 224 |
| heap afterClose | 160 |
| heap reportPending | 192 |
| heap printSnapshot | 88 |
| mbed_stats_heap_get | 16 |
| loggingTask | 408 |
| ensureLegacyHeader | 304 |
| FileOps::capture | 168 |
| reportGnssStats | 256 |

Mutex-/RTOS-/Serial-/printf-/Dateisystem-/USB-Aufrufketten kommen hinzu. Der Tracecallback belastet den Stack des jeweils allokierenden Threads, nicht nur den Logger. Lokale Snapshotkopien sind in den aufgefuehrten Frames beruecksichtigt. Der 4096-Byte-Loggerstack bleibt unveraendert; ausreichende Gesamtreserve oder Ueberlauf sind weiterhin nicht nachgewiesen. Die neue Instrumentierung kann Scheduling, Stackspitzen und Timing beeinflussen; reale V1-Wasserzeichen unter derselben Last sind erforderlich.

ELF-SHA-256: `7009a0079e989018fc45fe30786625fdd3f7dc5360c094a0468b2dc1c091380a`.
BIN-SHA-256: `ba78dd8e10cd50c2ed2b117194634981defc152e869e4d5e97f390e8749046e2`.

## Erwartete Ausgabe und naechster manueller Hardwaretest

Illustrative Werte, keine Hardwaremessung:

```text
[HEAP DIAG] pair id=7 point=before-open path=/usb/DRZ.txt ms=2000 current=50000 max=60000 alloc=100 fail=0 overhead=1000 reserved=367776 delta=0 deltaAlloc=0 baseline=1 baselineDelta=0 errno=0
[HEAP DIAG] pair id=7 point=after-close path=/usb/DRZ.txt ms=2005 current=50000 max=61000 alloc=100 fail=0 overhead=1000 reserved=367776 delta=0 deltaAlloc=0 baseline=1 baselineDelta=0 errno=0
[HEAP DIAG] allocation-null op=0 requested=4096 count=1 size=4096 caller=0x8000001 callerReliable=0 lost=0
```

Bei Erstfehler erscheinen maximal acht `first-error-frozen`-Aufnahmen und separat `first-error-post-cleanup`; beim ersten ENOMEM entsprechend `first-ENOMEM-frozen`/`first-ENOMEM-post-cleanup`. Die Frozenzeilen besitzen ihre urspruenglichen IDs und Zeiten, nicht den Zeitpunkt der spaeteren Ausgabe. `init-before-usb`, `first-successful-round` und erfolgreiche Mount-/Unmountpunkte ergaenzen die Dateipaare.

1. Diagnose-BIN nur nutzerseitig bzw. nach gesonderter Uploadfreigabe aufspielen; dieser Auftrag fuehrt keinen Upload aus. Seriellen Mitschnitt **115200 Baud vor Reset** beginnen und V1-/V2-/Heapmeldungen vollstaendig sichern.
2. GNSS etwa 5 Hz ohne Stick stabil anlaufen lassen, dann denselben problematischen Stick einstecken. Init-/Mountwerte, die ersten beiden Dateipaare und `first-successful-round` vergleichen. Beobachten, ob aktuelle Nutzbytes/aktive Allokationen nach Close wieder zurueckgehen; ein hoeherer Peak allein ist normal moeglich.
3. Mindestens mehrere Minuten bis ueber die 30-s-Paarperiode laufen lassen. Bei erneutem I/O-Fehler die erste komplette V2-Fehlergruppe, beide getrennten Frozen-/Post-Cleanup-Gruppen, Allokations-NULL-Spur und V1-Stackreserve sichern. `delta` ist systemweit fuer den erfassten Heap; eine wachsende Folge muss auch GNSS/UI/USB-Hintergrundarbeit beruecksichtigen.
4. Bei `allocation-null` requested/count/size, op, callerReliable und lost pruefen. Mit gleichzeitigem Anstieg von fail oder errno=ENOMEM korrelieren, keine Gleichheit erzwingen. Die Calleradresse nur gegen genau dieses ELF symbolisieren; selbst ein Symbolfund ist bei callerReliable=0 kein verlaesslicher Aufrufernachweis. Keine absichtliche Speichererschoepfung ohne gesonderten Testauftrag.
5. Bei Freeze/Fatal Error den vollstaendigen Fatal-Dump und letzte V1-Paare sichern. Ausbleibende Heapfehlerausgabe bei blockiertem Cleanup beweist keinen fehlerfreien Heap. Anschliessend Dateien am PC sichern: GNSS exakt 17 Datenspalten, unveraenderte Legacyformate und konsistente FIFO-Bilanz pruefen. Wiederanschluss/Abziehen nur im dafuer freigegebenen Hardwaretest.

Abschlusskontrollen: `git diff --check` Exit 0, lediglich bestehender Git-Hinweis auf LF/CRLF-Normalisierung in `src/gnss_log_fifo.h`; zusaetzliche Whitespacekontrolle umfasst auch alle neuen unversionierten Diagnosequellen. Hashvergleich meldet ausschliesslich die drei freigegebenen Testerdateien als waehrend dieser Phase geaendert. Implementierung, UI, Konfigurationen und `P2_P3.bash` sind zum Testerstart hashgleich. Der bisherige V2-Bericht samt kompletter V1-/P3-Historie bleibt bytegleich erhalten. Gitstatus: die bisherigen fuenf versionierten Dateien modifiziert sowie `?? src/heap_diag.cpp`, `?? src/heap_diag.h`, `?? src/usb_io_diag.h` und das bewahrte `?? P2_P3.bash`. Alle selbst gestarteten Prozesse sind beendet.

Keine bestaetigte neue Hardwareursache, Hardware-/Dauerhaftigkeits-/CASE-Abnahme, kein Upload, Commit, Push, Merge, Bibliotheksupdate oder UI-Eingriff. Naechster Freigabepunkt: Benutzerentscheidung zum manuellen Hardwaretest bzw. konkret beauftragter Nacharbeit anhand seiner Ergebnisse.

---

## Historische Diagnose-V2-/V1- und P3-Berichte

# GNSS-FIFO P3 - Diagnose V2: USB/FAT-Erstfehler

Pruefdatum: **22. November 2026**. Benutzerfreigabe: Diagnose V2, gezielte USB/FAT-I/O-Erstfehlererfassung und anschliessende Testerphase. Basis **`7b85488`**, Branch **`feature/gnss-fifo-logging`**, einschliesslich bewahrter V1-Aenderungen. **Status: abgeschlossen - sieben C++-Suiten mit 120 Assertions, alle acht Python-Quellvertraege und Firmwarebuild erfolgreich.** V2 ist der aktuelle Pruefstand. Alle folgenden Berichte bleiben historisch unveraendert erhalten.

## Umfang und Nachweise

TARS meldete vor Testerstart: Entwickler abgeschlossen ohne Prozesse, Tester einziger Schreiber; `workspace-write` fuer Projekt und Temp, geschuetzte `.git`/`.agents`/`.codex`/`.aws`, eingeschraenktes Netzwerk, werkzeugseitiges Freigabeverfahren. Keine Rechteerweiterung. Der aktuelle Benutzerauftrag erlaubt Tests und Build. Keine CASE-Pruefung oder Hardwareaktion.

V2-Entwicklerdateien: `src/main.cpp` und neu `src/usb_io_diag.h`. V1-Dezimalhelfer und FIFO bleiben unveraendert. Testerdateien ausschliesslich `test/gnss_logger/test_logger.cpp`, `test/gnss_logger/test_integration.py` und dieser Bericht. Neue Nachweise liegen unter **`.pio/gnss-fifo-p3/diagnose-v2/`**. Vor Neubuild wurden V1-ELF-/BIN-Hashes in `v1-firmware-hashes.json` gesichert; sie stimmen mit dem V1-Bericht ueberein. Ausgangs-/Abschlussdateihashes und Status liegen in `initial-hashes.json`, `final-hashes.json`, `checks.json`, `initial-status.log` und `status.log`. Konfigurationsinhalte werden nicht ausgegeben.

## Gepruefte Diagnosesemantik und Grenzen

`FileOps` erfasst `errno` unmittelbar nach `fopen`, `fwrite`, `fflush` und `fclose`. `ferror` wird bei Write/Flush danach auf dem gueltigen Stream gelesen. Bei Close wird es **vor** `fclose` gesichert; danach erfolgt kein Zugriff auf das geschlossene `FILE*`. `rc=0` bedeutet bei `fopen` einen Nullzeiger; bei Write ist `rc` die tatsaechliche Bytezahl, bei Flush/Close der rohe Rueckgabewert. `requested`/`actual` gelten fuer Write. `row` ist beim Write der **nullbasierte Zeilenindex**; bei Flush/Close ist es die Zahl bereits versuchter Zeilen, keine nachtraeglich bekannte fehlerhafte Zeile.

Der erste Fehler bleibt bis nach Flush/Close erhalten. Pfad und Modus sind eigene begrenzte Kopien (`path[32]`, `mode[4]`); das spaetere Leeren von `gnssPath` verliert den Fehlerpfad nicht. Ein Cleanupfehler ueberschreibt keinen frueheren Write-/Openfehler. Rohes `open(O_EXCL)`, `fdopen` und notwendiges `close(fd)` sind ebenfalls instrumentiert. Erwartetes `EEXIST` bleibt normale Namenssuche. Die Diagnosen bewahren den vorhandenen errno-Wert; sie fuehren keine zusaetzlichen Datei-, Mount-, Unmount- oder Connectversuche aus.

Headerzweige sind getrennt sichtbar: `existing-nonempty` bei erfolgreichem Stat mit nichtleerer Datei, `stat-error` bei Fehler ausser ENOENT, `header-write` bei leerer oder fehlender Datei. ENOENT bleibt regulaerer Anlagepfad. `stat=1` kennzeichnet gueltige `size`/`type` aus `st_size` und `st_mode & S_IFMT`; `stat=0` macht diese Felder unverbindlich. `io` und `medium` unterscheiden Dateiergebnis und Mediumgeneration. Bei reinem Mediumwechsel lautet `ctx=medium-changed`; `branch` bewahrt den urspruenglichen Zweig.

`errno` ist ein unmittelbar gesicherter Wert, **kein automatisch zuverlaessiger Fehlercode bei Erfolg**: erfolgreiche Aufrufe muessen ihn nicht nullen. Bei Mbed-Mount/Unmount ist der rohe `rc` massgeblich; `errno` bleibt eine zusaetzliche Momentaufnahme. `ferror=-1 ferrPhase=n/a` bedeutet, dass kein gueltiger FILE-Status vorlag; `pre-close` kennzeichnet den vor Close erfassten Status und `after-call` den Write-/Flushstatus.

Jede ausgegebene Ereignisgruppe enthaelt `pre` und `post` mit allen sieben Feldern: attached, mounted, unmountPending, fault, detachGeneration, mountedGeneration, headersReady. Bei Dateifehlern ist `pre` der **Fehlermoment**, keine Aufnahme vor dem Systemaufruf; `post` folgt dem Datei-Cleanup. Mount/Unmount sichern ihren Zustand vor dem Aufruf und nach den bestehenden Flagupdates. Spaetere Logger-Recovery kann die Flags erneut aendern. `headersReady` wird ueber einen Zeiger auf das unveraenderte lokale Loggergate gelesen. Die einzelnen atomaren Felder ergeben **keine atomare Gesamtaufnahme**; USB-Callbacks koennen zwischen den Lesezugriffen eintreten. Die Dateifehlerausgabe erfolgt erst nach Cleanup; blockiert Flush/Close dauerhaft, bleibt der Erstfehler intern gesichert, kann aber seriell noch nicht erscheinen. Die V1-before/after-Diagnosen bleiben dafuer ein zusaetzlicher Hinweis.

Neue Fehleridentitaeten erscheinen sofort. Identische Ereignisse erscheinen hoechstens alle fuenf Sekunden. `repeat` zaehlt die Vorkommen der gespeicherten Identitaet **kumulativ einschliesslich unterdrueckter Meldungen**, saettigt bei UINT32_MAX und wird durch eine Ausgabe nicht zurueckgesetzt. Acht Identitaeten werden gespeichert; eine neunte neue Identitaet verdraengt zyklisch einen Eintrag. Ein danach wiederkehrender verdraengter Fehler erscheint erneut sofort mit `repeat=1`. Bytezahlen, Zeile, Statgroesse, Flags und Generationen sind Beobachtungen und veraendern die Identitaet nicht. Bei Mount/Unmount/Medium gehoert beilaufiges errno ebenfalls nicht zur Identitaet. Daher ist die Ausgabe begrenzt fuer wiederkehrende Ursachen, aber keine lueckenlose Ereignisaufzeichnung und keine globale Begrenzung vieler neuer Ursachen.

Konservativ berechnete laengste Erstfehlerzeile **328 Byte einschliesslich NUL**, unter dem 384-Byte-Puffer. Dabei wurden maximale Pfad-/Moduslaengen, laengste Namen, volle ARM-32-Bit-Zahlen und INT64_MIN fuer Dateigroesse eingesetzt (`report-bound.log`). Die Zustandszeilen sind kuerzer. GNSS-Datensatz/Statistik behalten die V1-Grenzen 185/255 Byte einschliesslich NUL, 17 TSV-Spalten und sichere 64-Bit-Dezimalstrings.

## Tests

Ausgefuehrt:

```powershell
& C:/Users/nicol/.platformio/penv/Scripts/python.exe test/gnss_logger/run_tests.py
```

Abschlusslauf **Exit 0**, **1,030 s**. ARM GCC 7.2.1, `-std=gnu++17 -Wall -Wextra -Werror -c`, danach alle acht Pythonmethoden erfolgreich (`tests.log`, `tests-result.json`). Die unittest-Stderr kann im PowerShell-Log als `NativeCommandError` dargestellt sein; massgeblich sind Exit 0 und abschliessendes `OK`.

| Suite | Assertions | Ergebnis |
| --- | ---: | --- |
| gnss_epoch | 10 | bestanden |
| local_time | 17 | bestanden |
| log_clock | 6 | bestanden |
| rpm_filter | 4 | bestanden |
| ui_self_test | 12 | bestanden |
| gnss_log_fifo | 16 | bestanden |
| gnss_logger | 55 | bestanden |
| **Summe** | **120** | **bestanden** |

15 neue produktive constexpr-Regressionen pruefen Erstfehler/Cleanup/Reset, unveraenderliche Pfadkopie, begrenzte NUL-terminierte Kopien samt Canaries, sofortige neue Ursachen, kumulative Wiederholungszaehler und 5-s-Grenze, ignorierte Beobachtungswerte, alle Identitaetsfelder, Mbed-Rueckgabecode gegen errno, acht alternierende Ursachen mit neunter Verdraengung sowie millis-Umlauf. Die V1-105-Assertions einschliesslich 64-Bit- und negativer UTC-Grenzen bleiben aktiv. Zaehler-Saettigung wird durch den Quellvertrag zur expliziten UINT32_MAX-Schranke geprueft; kein Milliarden-Iterations-Lauf behauptet.

Die weiterhin acht Python-Vertraege pruefen zusaetzlich unmittelbare errno-Sicherung vor ferror/Diagnose/weiterem I/O, gueltige ferror-Phase, keine FILE-Verwendung nach Close, Headerzweige, Zustandfelder/-zeiger, Pufferobergrenze, Erstfehler-/Nach-Cleanup-Reihenfolge und unveraenderte Recovery-/Segment-/FIFO-/Legacygates. Textgebundene V1-Gates wurden auf die aequivalenten `mediumOk`-Variablen angepasst. Die Anzahl realer File-/Mount-/Connect-Aufrufstellen bleibt gegen `7b85488` gleich.

Der erste Lauf bestand alle C++-Suiten und sieben Pythonmethoden. Eine neue Aufrufstellenzaehlung verglich irrtuemlich mit dem historischen Vor-FIFO-Commit `2673b455` (zwei fopen-Stellen) statt mit dem Auftragsausgang `7b85488` (eine Stelle). Ausschliesslich diese neue Testbaseline wurde korrigiert; der erneute Gesamtlauf ist erfolgreich. Beide Erstlaufnachweise bleiben als `tests-initial-baseline-error.log` und `tests-initial-result.json` erhalten. Kein Implementierungsfehler wurde dabei festgestellt oder repariert.

Grenzen: C++-Tests sind Compilezeitauswertungen produktiver Policies, Python sind statische Quellvertraege. Der reale FILE-/FAT-/RTOS-Stack wird dadurch nicht ausgefuehrt. errno- und Stream-Semantik der Zielbibliothek sowie Verhalten beim realen Abziehen bleiben Hardwarepruefpunkte. Kein Hardwareerfolg und keine neue nachgewiesene USB-Absturzursache werden behauptet.

## Firmwarebuild, Speicher und Stackrisiko

Einmal ausgefuehrt mit unveraenderter massgeblicher Konfiguration und vorhandenen Paketen:

```powershell
$env:PLATFORMIO_CORE_DIR = 'F:/Development/mofa_tacho/.pio/gnss-fifo-p3/core'
$env:PLATFORMIO_CACHE_DIR = 'F:/Development/mofa_tacho/.pio/gnss-fifo-p3/core/.cache'
$env:PLATFORMIO_WORKSPACE_DIR = 'F:/Development/mofa_tacho/.pio/gnss-fifo-p3'
$env:PLATFORMIO_BUILD_DIR = 'F:/Development/mofa_tacho/.pio/gnss-fifo-p3/build'
$env:PLATFORMIO_LIBDEPS_DIR = 'F:/Development/mofa_tacho/.pio/gnss-fifo-p3/libdeps'
$env:PLATFORMIO_SETTING_ENABLE_TELEMETRY = 'No'
$env:PLATFORMIO_SETTING_CHECK_PLATFORMIO_INTERVAL = '0'
& C:/Users/nicol/.platformio/penv/Scripts/platformio.exe run -e giga_r1_m7
```

**Exit 0**, Buildmeldung **`[SUCCESS] Took 37.62 seconds`**, gesamte Prozessdauer **99,029 s** einschliesslich anschliessender fehlgeschlagener Upgradeabfrage. Neu kompiliert wurden auch zahlreiche Bibliotheks-/Frameworkobjekte; kein Clean-Build wurde beauftragt oder ausgefuehrt. Vorhandene Warnungen aus USBHostMbed5, Arduino-/Mbed-Code und generierter UI sind im Buildlog sichtbar, insbesondere deprecated APIs, ungenutzte Werte, Vergleichs-/Rueckgabewarnungen und ignorierte Pragmas. Keine davon stammt aus dem neuen Diagnosecode. Pakete/Bibliotheken wurden nicht aktualisiert. Nachweise: `build.log`, `build-result.json`.

| Messgroesse | V1 Byte | V2 Byte | Differenz | V2 Nutzung |
| --- | ---: | ---: | ---: | ---: |
| RAM `.data + .bss` | 150880 | 152680 | +1800 | 29,2 % von 523624 |
| Flash `.text + .data + .ARM.exidx` | 483336 | 487448 | +4112 | 62,0 % von 786432 |
| firmware.bin | 485272 | 489476 | +4204 | 62,2 % von 786432 |

Sections: `.text=479224`, `.ARM.extab=2028`, `.ARM.exidx=3488`, `.data=4736`, `.bss=147944`, `.heap=370584`, `.lwip_sec=278528`, `.uninitialized=0` Byte (`size.log`, `sections.log`). Der Linkerheapbereich ist keine gemessene freie Heapreserve. Neues Ereignis-ABI aus den tatsaechlichen Symbolgroessen: `Event=112`, `FirstFailure=120`, `Repeats=1032` Byte; die beiden statischen Mount-/Unmount-Ereignisse sind je 112 Byte. Reportpuffer 384 Byte plus 21 Byte Groessenstring und 4 Byte Headerzustandszeiger liegen statisch. Ring/Arbeitsblock/Zeilenpuffer bleiben statisch mit `logFifo=20528`, `gnssWork=1280`, `gnssLine=384` Byte. Keine zusaetzlichen Heap-Allokationen im Diagnosecode.

Eigene Prologframes aus `objdump -d -C` auf dem gebauten `main.cpp.o`, jeweils inklusive gesicherter Register:

| Funktion | Byte |
| --- | ---: |
| loggingTask | 408 |
| ensureLegacyHeader | 296 |
| FileOps::capture | 160 |
| usbIoReport | 112 |
| reportGnssStats | 240 |
| formatGnssRecord | 248 |
| FileOps::finish | 64 |
| loggerDiagnostic | 56 |

Stat-/Headerereignis und Capture besitzen lokale Ereigniskopien; diese Kosten sind in den jeweiligen Frames enthalten. FILE-/FAT-/USB-/printf-/RTOS-Aufrufketten kommen hinzu. Es handelt sich **nicht um Gesamtstackspitzen**. Der 4096-Byte-Loggerstack wurde nicht veraendert; weder ausreichende Gesamtreserve noch Ueberlauf sind nachgewiesen. Die vorhandenen `[LOGGER DIAG]`-Wasserzeichen unter realer Last bleiben erforderlich. Belege: `symbols.log`, `main-disassembly.log`.

ELF-SHA-256: `91c05fe4b3eb54c8a31c6faec98c230ff43725e7110f85e59f16bc1bd4b3189e`.
BIN-SHA-256: `1921fd84b8a5e39cacd17d67623837db271211ab1af97affefa006ee8324bc87`.

## Erwartete Ausgabe und naechster manueller Test

Beispiel eines Header-Writefehlers, Werte illustrativ:

```text
[USB IO DIAG] op=fwrite path=/usb/DRZ.txt mode=a ctx=header-write branch=header-write rc=7 errno=5 ferror=1 ferrPhase=after-call row=0 requested=20 actual=7 repeat=1 stat=1 size=0 type=32768 io=0 medium=1
[USB IO DIAG] pre attached=1 mounted=1 unmountPending=0 fault=0 detachGeneration=0 mountedGeneration=0 headersReady=0
[USB IO DIAG] post attached=1 mounted=1 unmountPending=0 fault=0 detachGeneration=0 mountedGeneration=0 headersReady=0
```

Ein spaeterer Flush-/Closefehler ersetzt diese erste Writeursache nicht. Bei reinem Closefehler erscheint `op=fclose ferrPhase=pre-close`. Bei Statfehler `op=stat ctx=stat-error`; bei Mountfehler `op=mount ctx=mount rc=<roher Mbed-Code> ferror=-1 ferrPhase=n/a`. Erfolgreiche Headerentscheidungen sind ebenfalls als `existing-nonempty` oder `header-write` sichtbar, daher ist `[USB IO DIAG]` allein kein Fehlerurteil.

1. Den Diagnosebuild nur nutzerseitig bzw. nach gesonderter Uploadfreigabe aufspielen. Dieser Auftrag enthaelt keinen Upload. Vollstaendigen seriellen Mitschnitt mit **115200 Baud vor Reset** beginnen; V1-Stackdiagnosen und Statistik mit aufzeichnen.
2. GNSS mit etwa 5 Hz ohne Stick anlaufen lassen, dann denselben problematischen Stick einstecken. Mindestens zwei Minuten beobachten. Die **erste komplette `[USB IO DIAG]`-Gruppe mit `pre`/`post`** sichern, insbesondere op/path/ctx/branch/rc/errno/ferror/row/Bytezahlen. Zusaetzlich die angrenzenden `[LOGGER DIAG]`-Paare und Wasserzeichenreserve sichern.
3. Bei Header-/Statfehlern pruefen, ob DRZ/SENSOR vorher existierten und leer oder nichtleer waren. `stat=1 size/type` und den beobachteten Zweig vergleichen. Ein nicht-null errno bei erfolgreichem Stat ist allein kein Fehler. Bei `medium=0` Generationen/attached vergleichen; mehrere getrennte Zustandsreads koennen einen laufenden Detach zeigen.
4. Bei Freeze/Fatal Error seriellen Fatal-Dump, letzte vollstaendige Erstfehlergruppe, fehlendes V1-Gegenstueck, LED-Muster und Dateibestand sichern. Keine nachtraegliche Ursache aus einer einzelnen zuletzt gepufferten Zeile ableiten. Danach Dateien am PC sichern und GNSS-17-Spalten, unveraenderte Legacy-Header sowie `accepted = queued + written + overflow + uncertain` pro Statistikzeile pruefen.
5. Einen gesonderten Wiederanschluss-/Abziehtest erst im freigegebenen Hardwaretest ausfuehren. Darauf achten, dass eine neue Ursache sofort erscheint, eine identische Ursache innerhalb fuenf Sekunden unterdrueckt bleibt und die naechste Ausgabe einen weitergelaufenen Repeatzaehler zeigt. Keine zusaetzliche Recoveryfunktion wurde eingebaut.

Abschlusskontrollen: `git diff --check` Exit 0; Git meldet lediglich die bestehende LF/CRLF-Normalisierung in `src/gnss_log_fifo.h`. Die zusaetzliche Whitespacepruefung bezieht auch die neue unversionierte Diagnosedatei ein. Hashvergleich: In der Testerphase wurden ausschliesslich die drei freigegebenen Testerdateien geaendert. Implementierung, UI, Konfigurationen und `P2_P3.bash` sind zum Testerstart hashgleich; der komplette V1-Bericht samt aelterer Historie bleibt bytegleich als Suffix erhalten. Gitstatus: fuenf bereits modifizierte versionierte Dateien (`src/main.cpp`, `src/gnss_log_fifo.h`, die drei Testerdateien) sowie `?? src/usb_io_diag.h` und das bewahrte `?? P2_P3.bash`. Alle selbst gestarteten Test-/Build-/Analyseprozesse sind beendet.

Keine Hardware-/Dauerhaftigkeits-/CASE-Abnahme, kein Upload, Commit, Push, Merge, Bibliotheksupdate oder UI-Eingriff. Naechster Freigabepunkt: Benutzerentscheidung zum manuellen Hardwaretest oder konkret beauftragter Nacharbeit anhand seines Befunds.

---

## Historische Diagnose-V1- und P3-Berichte

# GNSS-FIFO P3 - Hardwarediagnose V1

Pruefdatum: **22. November 2026**. Benutzerauftrag: TARS Hardwarefehleranalyse GNSS-FIFO, A/B/C und vollstaendige Testphase. Basis **`7b85488`**, Branch **`feature/gnss-fifo-logging`**. **Status: abgeschlossen - sieben C++-Suiten mit 105 `static_assert`-Pruefungen, alle acht Python-Quellvertraege, Firmwarebuild und Diffpruefung erfolgreich.** Dies ist der aktuelle Pruefstand; alle Berichte unterhalb der historischen Trennlinie gelten fuer ihre damaligen Staende.

## Umfang, Rechte und Bewahrung

TARS meldete vor der Testerphase den Entwickler als abgeschlossen ohne eigene Prozesse. Wirksame Sitzungsrechte laut Laufzeitvorgaben: `workspace-write` fuer Projekt und Temp, geschuetzte `.git`/`.agents`/`.codex`/`.aws`, eingeschraenktes Netzwerk und werkzeugseitiges Eskalationsverfahren. Keine Rechteerweiterung vorgenommen. Der ausdrueckliche Benutzerauftrag umfasst Entwicklung und anschliessenden Test ohne weitere Zwischenfreigaben; CASE ist ausgeschlossen.

Entwicklerdiff ausschliesslich `src/main.cpp` und `src/gnss_log_fifo.h`. Tester schreibend ausschliesslich `test/gnss_logger/test_logger.cpp`, `test/gnss_logger/test_integration.py` und dieser Bericht. Nachweise liegen unter `.pio/gnss-fifo-p3/diagnose-v1/`; Testobjekte unter `tests/`, Firmware unter `build/giga_r1_m7/`. Bereits vorhandene Nachweise bleiben erhalten. Ausgangsstatus beim Tester: die zwei Entwicklerdateien modifiziert und `?? P2_P3.bash`. Hashvergleich (`initial-hashes.json`, `final-hashes.json`, `checks.json`) prueft insbesondere, dass in der Testerphase keine Implementierungsdatei, UI, Konfiguration oder `P2_P3.bash` veraendert wurde. Lokale Konfigurationsinhalte werden nicht protokolliert.

## Nachgewiesener Fehler und offene Hardwareursache

Der bisher verlinkte Formatter unterstuetzt die verwendeten `%llu`-/`%lld`-Formate nicht. Vor dem Neubuild gesicherte Belege: `old-printf-symbols.log` zeigt identische Adressen fuer `snprintf`/`sniprintf` und `_svfprintf_r`/`_svfiprintf_r`; `old-printf-parser.log` zeigt den Parser bei `0x0808e0b2` mit genau einem Modifier aus `hlL` (Literal bei `0x080b3892`). Alter ELF-Hash: `59b9ea8eb536b7149f87aa3691ce42fd71bbd361c24d7ed31f896c0053812330`, gesichert in `old-elf-hash.log`. Das Mbed-Minimal-printf-Konfigurationsflag fuer 64 Bit beweist keine Unterstuetzung durch diesen tatsaechlich verlinkten Newlib-Nano-Pfad.

Dieser Befund erklaert `accepted=lu` und die falsche Zuordnung nachfolgender Varargs; `highWater=812` war damit kein belastbarer FIFO-Messwert. In `formatGnssRecord()` folgen Stringargumente: die Verschiebung kann einen Zahlenwert als `%s`-Zeiger interpretieren. Das ist ein konkreter plausibler Absturzmechanismus, aber ohne Hardwarelauf oder Fatal-Error-Dump **keine bestaetigte Ursache des beobachteten Absturzes**.

Die produktiven heapfreien `decimalUnsigned()`/`decimalSigned()` ersetzen alle 64-Bit-printf-Formate in den beiden Aufrufstellen durch `%s`. Kein 32-Bit-Cast, kein Heap, kein `-INT64_MIN`. Unzureichende Zielpuffer werden abgewiesen; beschreibbare Puffer werden leer zurueckgegeben. Die 17 GNSS-Spalten samt Flags/leeren ungueltigen Werten und die Legacy-Messwertausdruecke bleiben erhalten. Konservative maximale Zeilenlaengen **einschliesslich NUL**: Datensatz **185 Byte**, Statistik **255 Byte**, jeweils unter dem vorhandenen 384-Byte-Puffer. Die Datensatzgrenze nimmt fuer alle vier uint8-Flags drei Dezimalstellen an; sie ist fuer binaere Gueltigkeitsflags konservativ.

Anwendungsseitig besitzt weiterhin allein der Logger Mount/Unmount und Dateioperationen. `writeFile()` oeffnet einen Stream und beendet Schreiben/Flush/Close vor Rueckkehr; keine rekursive gleichzeitige Dateiöffnung festgestellt. Der USB-Host verarbeitet parallel Geraeteereignisse. Mehrstufige SCSI-Transfers, Endpoint-Wartephasen, Detach waehrend I/O, Speicherbeschaedigung und Stackueberlauf bleiben unbestaetigte weitere Verdachtsmomente. USB-Treiber und Stackgroessen wurden nicht geaendert.

## Regressionen und alle Tests

Einmal ausgefuehrt, **Exit 0**, Prozessdauer **0,961 s**:

```powershell
& C:/Users/nicol/.platformio/penv/Scripts/python.exe test/gnss_logger/run_tests.py
```

Der unveraenderte Runner kompiliert mit ARM GCC 7.2.1, `-std=gnu++17 -Wall -Wextra -Werror -c`, danach laufen alle acht Python-Methoden. Nachweise: `tests.log`, `tests-result.json`. Die Darstellung der unittest-Stderr als PowerShell-`NativeCommandError` ist eine Ausgabeformatierung; Exit 0, acht `ok`, abschliessend `OK`.

| C++-Suite | Assertions | Ergebnis |
| --- | ---: | --- |
| gnss_epoch | 10 | bestanden |
| local_time | 17 | bestanden |
| log_clock | 6 | bestanden |
| rpm_filter | 4 | bestanden |
| ui_self_test | 12 | bestanden |
| gnss_log_fifo | 16 | bestanden |
| gnss_logger | 40 | bestanden |
| **Summe** | **105** | **bestanden** |

15 neue Compilezeit-Regressionen fuehren die produktiven Dezimalhelfer aus: Null, UINT32_MAX und +1, unsigned High-Bit, UINT64_MAX, INT64_MIN/MAX, UTC `-1`, `-1001`, `-4294967297` und `4294967296`, exakte kleine Puffer, Nullzeiger und Kapazitaet null. Fuer beide 64-Bit-Extrema werden alle Kapazitaeten 0 bis 21 mit direkt angrenzenden Canaries geprueft. Bestehende FIFO-/Unsicherheits-/Drop-newest-/Wiederanlauftests bleiben aktiv; die Bilanz `accepted = queued + written + overflow + uncertain` bleibt unveraendert.

Die acht statischen Python-Vertraege wurden erweitert, ohne bestehende Anforderungen zu entfernen: sichere Helferuebergaben, keine 64-Bit-printf-Modifier, exakt 17 Datensatz-Formatspezifikatoren, leere Ausgangspuffer ungueltiger Werte, Zeilenobergrenzen, Statistikfelder, Diagnosepaare, Ratebegrenzung und Stack-API-Aufrufe. Callback-, ISR-, GNSS-Akzeptanz-, Legacy-, UI- und FIFO-Mutexvertraege bestehen fort. **Grenze:** C++ ist Compilezeitpruefung; Python prueft Quellvertraege. Kein Ausfuehren der gesamten RTOS-Loggerschleife oder des Ziel-Newlib-/USB-/FAT-Stacks und kein Hardwaretest.

## Firmwarebuild und Speicher

Einmal ausgefuehrt mit vorhandenen Paketen und unveraenderter `platformio.ini`:

```powershell
$env:PLATFORMIO_CORE_DIR = 'F:/Development/mofa_tacho/.pio/gnss-fifo-p3/core'
$env:PLATFORMIO_CACHE_DIR = 'F:/Development/mofa_tacho/.pio/gnss-fifo-p3/core/.cache'
$env:PLATFORMIO_WORKSPACE_DIR = 'F:/Development/mofa_tacho/.pio/gnss-fifo-p3'
$env:PLATFORMIO_BUILD_DIR = 'F:/Development/mofa_tacho/.pio/gnss-fifo-p3/build'
$env:PLATFORMIO_LIBDEPS_DIR = 'F:/Development/mofa_tacho/.pio/gnss-fifo-p3/libdeps'
$env:PLATFORMIO_SETTING_ENABLE_TELEMETRY = 'No'
$env:PLATFORMIO_SETTING_CHECK_PLATFORMIO_INTERVAL = '0'
& C:/Users/nicol/.platformio/penv/Scripts/platformio.exe run -e giga_r1_m7
```

**Exit 0, `[SUCCESS] Took 14.31 seconds`**. Nach der erfolgreichen BIN-Erzeugung nur die bekannte fehlgeschlagene PlatformIO-Upgradeabfrage; kein erneuter Versuch, kein Paketdownload/-update. ST STM32 20.0.0, Arduino Mbed 4.6.0, ARM GCC-Paket 1.70201.0; Bibliotheksversionen unveraendert. Inkrementeller Build, kein Clean-Build behauptet. Nachweise: `build.log`, `build-result.json`.

| Messgroesse | Byte | Limit | Nutzung | Gegen Ausgangsbuild |
| --- | ---: | ---: | ---: | ---: |
| RAM `.data + .bss` | 150880 | 523624 | 28,8 % | +224 |
| Flash `.text + .data + .ARM.exidx` | 483336 | 786432 | 61,5 % | +1224 |
| firmware.bin | 485272 | 786432 | 61,7 % | +1224 |

`arm-none-eabi-size`, `size -A`, `nm -S --size-sort -C` auf dem neuen ELF und `objdump -d -C` auf `src/main.cpp.o` sind in `size.log`, `sections.log`, `symbols.log`, `main-disassembly.log` gesichert. `.text=476552`, `.data=3352`, `.bss=147528`, `.ARM.exidx=3432`, `.ARM.extab=1936`, `.heap=372376`, `.lwip_sec=278528` Byte. Der Heap-Linkerbereich ist keine gemessene freie Heapreserve; Dateisystem und Treiber benoetigen dynamischen Speicher.

Unveraendert statisch: `logFifo=20528`, `gnssWork=1280`, `gnssLine=384` Byte. Neu: 160 Byte statischer Diagnosepuffer und acht Tracezustaende zu je 8 Byte, zusammen genau **224 Byte BSS**. Keine zusaetzlichen Heap-Allokationen im neuen Formatierungs-/Diagnosecode.

Neue Prologe aus dem tatsaechlich gebauten Objekt, inklusive gesicherter Register: `loggingTask` **408 Byte**, `formatGnssRecord` **248 Byte**, `reportGnssStats` **224 Byte**, `loggerDiagnostic` **56 Byte**. Das sind eigene Frames, **keine Gesamtstackspitzen**; printf-, FAT-, USB-, RTOS- und String-Aufrufketten kommen hinzu. Der bisherige 4096-Byte-Loggerstack bleibt unveraendert; ein Ueberlauf ist bisher nicht nachgewiesen. Lokales `Thread.h` bestaetigt die Signaturen `uint32_t stack_size() const`, `free_stack() const`, `max_stack() const`; erfolgreiches Linken bestaetigt die Verfuegbarkeit. `max_stack` scannt das Stackfuellmuster; `reserve=stack-peak` ist eine bisherige Wasserzeichenreserve. `free_stack` beruht auf dem gespeicherten Stackpointer und ist beim laufenden Thread nur ein Schaetzwert. Die Messung und serielle Ausgabe selbst beeinflussen Stack und Timing.

ELF-SHA-256: `57bf5437ca1a01008471cf128fe6a411b753952d885ee485d10b2939c1cc6a30`.
BIN-SHA-256: `7f93513b2d63e6d5f72c35e19b9a34f4b43bb02d59b9f384802067891e76d691`.

## Erwartete Diagnose und naechster manueller Hardwaretest

Diagnosen werden nur vom Logger ausgegeben: erste sechs Aufrufpaare je Kategorie, danach hoechstens ein Paar pro fuenf Sekunden. Eine gemeinsame Kategorie deckt jeweils beide Legacy-Pfade ab. Normale Statistik bleibt separat einmal pro Loggersekunde sichtbar. Beispiele (Stackwerte sind Platzhalter):

```text
[LOGGER DIAG] segment before result=0 stack=4096 free=... peak=... reserve=...
[LOGGER DIAG] segment after result=0 stack=4096 free=... peak=... reserve=...
[LOGGER DIAG] stats-write before result=0 stack=4096 free=... peak=... reserve=...
[LOGGER DIAG] stats-write after result=1 stack=4096 free=... peak=... reserve=...
[LOGGER DIAG] /usb/DRZ.txt before-write result=... stack=4096 free=... peak=... reserve=...
[LOGGER DIAG] /usb/DRZ.txt after-write result=1 stack=4096 free=... peak=... reserve=...
[LOGGER DIAG] fifo-reserve before result=0 stack=4096 free=... peak=... reserve=...
[LOGGER DIAG] fifo-reserve after result=... stack=4096 free=... peak=... reserve=...
[LOGGER DIAG] gnss-block before result=... stack=4096 free=... peak=... reserve=...
[LOGGER DIAG] gnss-block after result=1 stack=4096 free=... peak=... reserve=...
```

Weitere Kategorien: Legacy `before-header`/`after-header`, `mount`/`unmount` mit `before`, `after` oder `error`; ohne erkanntes Medium `mount connect-unavailable result=-1`. Bei Segment-`after` ist das Ergebnis der Enumwert (`Ready=0`, `Exists=1`, `Pending=2`, `Exhausted=3`, `IoError=4`); bei Schreib-`after` bedeutet 1 Erfolg und 0 Fehler. Die Meldungen umklammern Dateiaufrufgruppen, nicht jeden internen FAT-/USB-Treiberaufruf. Die letzte Meldung ist wegen Ratebegrenzung und serieller Pufferung ein Hinweis, keine lueckenlose Ablaufspur.

1. Nutzerseitig Diagnose-BIN nur nach gesonderter Uploadfreigabe aufspielen; dieser Auftrag hat keinen Upload ausgefuehrt. Seriellen Mitschnitt mit **115200 Baud** bereits vor Reset starten und durchgehend bis ueber den Fehler hinaus speichern; moeglichen Mbed-Fatal-Error-Text vollstaendig sichern.
2. Ohne Stick starten, stabilen 5-Hz-GNSS-Empfang abwarten und etwa 10 Sekunden FIFO-Rueckstand aufbauen. `# STATS` muss vollstaendige Dezimalzahlen zeigen; `queued` und `highWater` duerfen 320 nicht ueberschreiten.
3. Den bisher abstuerzenden Stick einstecken und mindestens zwei Minuten laufen lassen. `segment`, `stats-write`, Legacy, `fifo-reserve` und `gnss-block` vor/nach vergleichen; Stack-Wasserzeichenreserve festhalten. Bei erneutem Freeze letzte vollstaendige Diagnosepaare, fehlendes Gegenstueck, LED-Muster und Fatal-Dump sichern. Ein fehlendes `after` grenzt nur bei einem tatsaechlich ausgegebenen Paar ein.
4. Nach Ablauf aller Dateioperationen bzw. im gesondert beauftragten Abziehtest den Stick entfernen und wieder anschliessen. Neue GNSS-Segmente, weiterlaufende Anzeige und Fehler-/Unsicherheitszaehler beobachten. Anschliessend Dateien am PC sichern: GNSS-Datenzeilen exakt 17 TSV-Spalten; UTC der gespeicherten Epochen, steigende Sequenzen, unveraenderte Legacy-Header/-Spalten pruefen. Kommentar-/Headerzeilen nicht als GNSS-Daten zaehlen.
5. Statistikbilanz pro konsistenter `# STATS`-Zeile pruefen: `accepted = queued + written + overflow + uncertain`. Bei kurzen Ausfaellen unter freier FIFO-Reserve sind ohne I/O-Fehler keine Overflow-/Unsicherheitsverluste zu erwarten. Ein gesonderter Lauf ueber 64 Sekunden ohne Stick prueft den begrenzten Drop-newest-Verlust; bei 5 Hz und zuvor leerem FIFO sind maximal 64 Sekunden Puffer vorhanden. Negative UTC-Grenzen sind hier Compilezeitregressionen und muessen nicht durch Manipulation des realen GNSS erzeugt werden.

Abschluss: `git diff --check` Exit 0, ausschliesslich Git-Hinweis auf spaetere LF/CRLF-Normalisierung in `src/gnss_log_fifo.h`, keine Whitespacefehler. Git-Status: die zwei Entwicklerdateien und drei Testerdateien modifiziert; `?? P2_P3.bash` weiterhin vorhanden und hashgleich. Der Hashvergleich findet in der Testerphase ausschliesslich die drei genehmigten Testerdateien veraendert. Der Buildprozess endete nach insgesamt 75,698 Sekunden einschliesslich Upgradeabfrage; alle selbst gestarteten Test-/Build-/Analyseprozesse sind beendet.

Keine Hardware-, Laufzeit-, physische Dauerhaftigkeits- oder CASE-Abnahme. Kein Commit, Push, Merge, Upload, UI-Eingriff oder Bibliotheksupdate. Naechster Freigabepunkt: Benutzerentscheidung zum manuellen Hardwaretest bzw. gesonderter Nacharbeit nach dessen Befund.

---

## Historische P3-Berichte vor Hardwarediagnose V1

# GNSS-FIFO P3 - B1: GNSS-Segmentfehler isolieren

Pruefdatum: **22. November 2026**. Auftrag: ausdruecklich beauftragte B1-Nacharbeit der GNSS-FIFO-Variante A mit Regression, gemeinsamem Medienfehlerpfad und vollstaendigen P3-Pruefungen. **Status: abgeschlossen - sieben C++-Suiten mit 90 `static_assert`-Pruefungen, acht Python-Quellvertraege, PlatformIO-Build sowie Diff- und Speicherpruefung erfolgreich.** Dieser Abschnitt ist der aktuelle Pruefstand. Alle folgenden P3-Berichte bleiben als historische Nachweise unveraendert erhalten.

## Stand, Rechte und Dateiumfang

Basiscommit `2673b4559fcc56c4f883bb2d2d4791dec234ae77`, Branch `feature/gnss-fifo-logging`, vorhandener uncommitteter Benutzerstand. Der Entwickler aenderte ausschliesslich `src/main.cpp`, `src/gnss_log_fifo.h` und `docs/architecture.md`; TARS meldete ihn vor der Testerphase als abgeschlossen ohne eigene Prozesse. Der Tester aenderte ausschliesslich `test_logger.cpp`, `test_integration.py`, diesen Bericht und unmittelbar betroffene Pruefstand-/Fehlerstatusangaben in `docs/architecture.md`. Keine Implementierungsdatei wurde in der Testerphase veraendert.

TARS meldete vor der Schreibphase: `workspace-write` fuer Projekt und Temp, geschuetzte `.git`/`.agents`/`.codex`/`.aws`, `approval_policy=never`, eingeschraenktes Netzwerk. Keine Rechteerweiterung. Tests schreiben nach `.pio/gnss-fifo-p3/tests`; neue Nachweise liegen unter `.pio/gnss-fifo-p3/b1-rework/`. Build-, Bibliotheks- und Cachepfade entsprechen dem vorherigen erfolgreichen P3-Build. `platformio.ini`, `platformio.local*.ini`, UI und P1 bleiben unveraendert.

## B1-Verhalten und Regressionen

`sharedSegmentFailure(result, mediumAvailable)` behandelt einen GNSS-Segment-`IoError` nur bei einem erkannten Mediumwechsel als gemeinsamen Fehler. Bei unveraendertem Medium bleiben Legacy-Sampling und faellige gepruefte Schreibversuche aktiv. Ein fehlendes oder fehlerhaftes GNSS-Segment sperrt nur GNSS-Verbrauch. Auch GNSS-Statistik-/Blockfehler legen das Segment still; ein versuchter Fehlerblock bleibt genau einmal als unsicher bilanziert. Wiederaufnahme erfolgt exklusiv im naechsten Namen. Mount-/Unmount-Fehler, Detach-Generation, Legacy-I/O-Fehler und FIFO-Abschlussinvarianten behalten den konservativen gemeinsamen Pfad.

Neue C++-Regressionen verwenden die produktiven `findSegment`, `sharedSegmentFailure`, `writeFile` und `Fifo` mit injizierbaren Dateioperationen:

- **GNSS-Anlage liefert `IoError`, Legacy bleibt beschreibbar:** 400 simulierte 100-ms-Runden, in jeder Runde genau ein fehlgeschlagener Anlagecallback und ein neues RPM-/SENSOR-Sample. Nach der produktiven 1-s-Frist werden je **396 Zeilen in 36 Bloecken** durch den geprueften Writer verarbeitet; je vier Zeilen bleiben regulaer gepuffert. Geprueft sind Callback-Verarbeitung, Bytezahlen (1980 RPM, 2376 SENSOR), Open, Flush und Close fuer beide Dateien. GNSS bleibt unbestaetigt: 400 akzeptiert = 320 gepuffert + 80 Drop-newest, keine geschriebenen oder unsicheren GNSS-Datensaetze.
- **Begrenzung bei dauerhaftem `IoError`:** genau 10000 Anlagecallbacks; weitere Aufrufe liefern `Exhausted` ohne Callback. Bestehende Pruefungen fuer 32 Kandidaten je Runde, EEXIST und geordneten Suchabbruch bleiben aktiv.
- **Gemeinsamer Medien-/Dateisystemausfall ohne Detach-Callback:** Nach einem isolierten Segmentfehler scheitert der faellige RPM-Block getrennt bei Open, Write, Flush oder Close. Jeder Fall loest den gemeinsamen Fehler aus, zaehlt den versuchten 11er-Block einmal unsicher und behaelt den unversuchten SENSOR-Block; keine weitere SENSOR-Dateioperation. Die vorhandenen GNSS-Fehlerblocktests sichern die FIFO-Bilanz und Wiederaufnahme ohne Replay.
- **Erkannter Mediumwechsel:** Segment-`IoError` bei ungueltigem Mediumzustand verhindert beide Legacy-Schreibversuche sofort. Der bestehende Detach-invalidierte GNSS-Blocktest bleibt aktiv.

Die acht Python-Quellvertraege wurden gezielt erweitert: produktiver Aufruf mit `mediumUnchanged()`, Legacy-Aufnahme und Schreibreihenfolge nach Segmentfehlern, GNSS-Stats-/Blockrotation, gemeinsame Legacy-Fehlerweitergabe, Detach-Generation, Mount-Erfolg als Voraussetzung fuer Dateizugriffe, Mountabstand und Unmount-Sperre. O_EXCL sowie bisherige Akzeptanz-, Format-, Zeit-, UI- und Mutexvertraege bleiben aktiv.

**Pruefgrenze:** Der C++-Testtreiber bildet Aufnahme-/Frist-/Fehlergates nach und fuehrt die produktiven constexpr-Policies und geprueften Schreiboperationen zur Compile-Zeit aus. Die statischen Quellvertraege verbinden dies mit den realen Logger-Aufrufstellen. Es ist keine Ausfuehrung der vollstaendigen RTOS-Loggerschleife, kein echter FAT-/USB-Zugriff und kein Hardware-Medienausfalltest. Gemeinsame Fehler ohne Detach werden durch den naechsten faelligen geprueften Legacy-Dateizugriff erkannt, sofern Treiberaufrufe zurueckkehren.

## Alle Tests

Genau einmal ausgefuehrt:

```powershell
python test/gnss_logger/run_tests.py
```

**Exit 0**, Dauer 1,140 s. Sieben C++-Suiten mit ARM GCC 7.2.1, `-std=gnu++17 -Wall -Wextra -Werror -c`, anschliessend alle acht Python-Quellvertraege erfolgreich. Logs: `b1-rework/tests.log`, `tests-result.json`.

| C++-Suite | `static_assert` | Ergebnis |
| --- | ---: | --- |
| `gnss_epoch` | 10 | bestanden |
| `local_time` | 17 | bestanden |
| `log_clock` | 6 | bestanden |
| `rpm_filter` | 4 | bestanden |
| `ui_self_test` | 12 | bestanden |
| `gnss_log_fifo` | 16 | bestanden |
| `gnss_logger` | 25 | bestanden |
| **Summe** | **90** | **bestanden** |

## Firmwarebuild und Speicher

Genau einmal ausgefuehrt mit unveraenderter massgeblicher Konfiguration und vorhandenen Paketen, nur prozesslokalen Ausgabepfaden:

```powershell
$env:PLATFORMIO_CORE_DIR = 'F:/Development/mofa_tacho/.pio/gnss-fifo-p3/core'
$env:PLATFORMIO_CACHE_DIR = 'F:/Development/mofa_tacho/.pio/gnss-fifo-p3/core/.cache'
$env:PLATFORMIO_WORKSPACE_DIR = 'F:/Development/mofa_tacho/.pio/gnss-fifo-p3'
$env:PLATFORMIO_BUILD_DIR = 'F:/Development/mofa_tacho/.pio/gnss-fifo-p3/build'
$env:PLATFORMIO_LIBDEPS_DIR = 'F:/Development/mofa_tacho/.pio/gnss-fifo-p3/libdeps'
$env:PLATFORMIO_SETTING_ENABLE_TELEMETRY = 'No'
$env:PLATFORMIO_SETTING_CHECK_PLATFORMIO_INTERVAL = '0'
pio run -e giga_r1_m7
```

**Exit 0, `[SUCCESS] Took 15.81 seconds`**, gesamte Prozessdauer 77,157 s. Nach erfolgreicher BIN-Erzeugung nur die bekannte Meldung `Failed to check for PlatformIO upgrades. Please check your Internet connection.` Kein erneuter Versuch, keine Downloads oder Paket-/Bibliotheksupdates. ST STM32 20.0.0, Arduino Mbed 4.6.0, ARM GCC-Paket 1.70201.0; Bibliotheken unveraendert wie im vorherigen Bericht. Inkrementelle Neukompilierung von `main.cpp`, `gnss_service.cpp`, `vars.cpp` und `lv_blend_helium.S`, danach Link und BIN; kein Clean-Build behauptet. Logs: `b1-rework/build.log`, `build-result.json`.

| Messgroesse | Byte | Limit | Nutzung | Gegen vorheriges P3 |
| --- | ---: | ---: | ---: | ---: |
| RAM, `.data + .bss` | 150656 | 523624 | 28,8 % | unveraendert |
| Flash, `.text + .data + .ARM.exidx` | 482112 | 786432 | 61,3 % | +168 |
| `firmware.bin` | 484048 | 786432 | 61,6 % | +164 |

`arm-none-eabi-size.exe`, `size -A` und `nm -S --size-sort -C` liefen jeweils mit Exit 0 auf `build/giga_r1_m7/firmware.elf`; Nachweise `b1-rework/size.log`, `sections.log`, `symbols.log`, `sizes-result.json`. Sections: `.text=475352`, `.ARM.exidx=3408`, `.ARM.extab=1936`, `.data=3352`, `.bss=147304`, `.uninitialized=8`, `.lwip_sec=278528`, `.heap=372600` Byte. Die BIN enthaelt zusaetzlich `.ARM.extab`. Einfache size-Summe: `text=480696`, `data=3352`, `bss=425840`; sie ersetzt nicht die PlatformIO-RAM-Messung.

Symbole unveraendert: `logFifo=20528` Byte (20480 Ringdaten + Verwaltung), `gnssWork=1280` Byte, `gnssLine=384` Byte, statisch in BSS. Keine neuen grossen Puffer oder Heap-Allokationen durch B1. Heap-/Stackreserven sind nicht gemessen; die alten Hardware-Wasserzeichen belegen keine aktuelle Reserve.

ELF-SHA-256: `59b9ea8eb536b7149f87aa3691ce42fd71bbd361c24d7ed31f896c0053812330`.
BIN-SHA-256: `d026aa35a29acc4dadb442b90fc9c87e06828dea2d5fb0b411465a51f8c9b3c0`.

## Abschlusskontrollen und Grenzen

57 initiale SHA-256-Dateiwerte stehen in `b1-rework/initial-hashes.json`; Abschlussvergleich in `final-hashes.json` und `checks.json`. Gegen den abgeschlossenen Entwicklerstand sind ausschliesslich die vier genehmigten Tester-/Dokumentationsdateien veraendert. Alle Implementierungsdateien, UI, P1, Konfigurationen und vorhandenen Benutzerdateien sind in dieser Phase bytegleich geblieben. Die vorigen P3-Nachweise wurden erhalten.

Nach Berichtsaktualisierung: `git diff --check` Exit 0 ohne Ausgabe, keine nachlaufenden Leerzeichen in den genehmigten Test-/Dokumentationsdateien einschliesslich unversionierter Dateien. Git-Status protokolliert; dieselben bereits anfangs veraenderten/unversionierten Pfade. Nachweise: `b1-rework/diff-check.log`, `status.log`, `checks.json`. Der Git-Diff enthaelt auch den bereits vorhandenen GNSS-FIFO-Benutzerstand; er ist kein isolierter B1-Diff.

Keine Fehler bei den beauftragten Pruefungen. Keine Hardware-, Last-, Laufzeit-Stack- oder physische Dauerhaftigkeitspruefung. Keine CASE-Pruefung, Commits, Pushes, Merges oder Uploads. Alle gestarteten Test-/Build-/Analyseprozesse sind beendet. Naechster Freigabepunkt: Benutzerentscheidung ueber Abnahme oder gesonderte weitere Pruefung.

---

## Historischer P3-Bericht F1-F3 vor B1

Die folgenden Angaben einschliesslich alter Status-, Groessen- und Hashwerte gelten ausschliesslich fuer den frueheren Pruefstand.

# GNSS-FIFO P3 - gezielte CASE-Nacharbeit F1-F3

Pruefdatum: **21. November 2026**. Auftrag: GNSS-FIFO Variante A, gezielte Nacharbeit der drei vom Benutzer genannten CASE-Befunde einschliesslich Regressionen und vollstaendiger P3-Pruefung. **Status: abgeschlossen - sieben C++-Suiten mit 83 `static_assert`-Pruefungen, acht Python-Quellvertraege und `giga_r1_m7`-Build erfolgreich.** Dies ist der aktuelle Pruefstand. Der fruehere P3-Bericht folgt unten als ausdruecklich historische Dokumentation; seine alten Groessen und Hashes gelten nicht fuer diese Nacharbeit.

## Aktueller Pruefstand und Umfang

Branch `feature/gnss-fifo-logging`, Basiscommit `2673b4559fcc56c4f883bb2d2d4791dec234ae77`, mit bereits vorhandenen uncommitteten Aenderungen. Der aktuelle Benutzerauftrag erlaubt die gezielte Nacharbeit und alle benannten Pruefungen in einem Durchgang. TARS meldete vor dem Testerstart: Entwickler abgeschlossen, keine eigenen Prozesse; `workspace-write` fuer Projekt und Temp, geschuetzte `.git`/`.agents`/`.codex`/`.aws`, `approval_policy=never`, eingeschraenktes Netzwerk, keine Rechteerweiterung. Build-, Bibliotheks- und Cacheausgaben bleiben unter `.pio/gnss-fifo-p3`; lokale Konfigurationen bleiben unveraendert.

Entwickleraenderungen ausschliesslich:

- `src/main.cpp`: GNSS-Suche/-Erschoepfung vom gemeinsamen Fehlerpfad getrennt; Legacy laeuft weiter. GNSS-Verbrauch nur bei aktivem Segment. Frisches `millis()` unmittelbar vor FIFO-Reservierung. Serielle Such-/Erschoepfungsdiagnose.
- `src/gnss_log_fifo.h`: produktive injizierbare begrenzte Suchpolicy mit getrennten Ergebnissen, konservativer gemeinsamer I/O-Fehlerbewertung und sicherer modularer Teilblockfrist.
- `docs/architecture.md`: sieben C++-Suiten und acht Python-Quellvertraege sowie korrigierte Fehler-/Zeitsemantik.

Testeraenderungen ausschliesslich `test/gnss_log_fifo/test_fifo.cpp`, `test/gnss_logger/test_logger.cpp`, `test/gnss_logger/test_integration.py` und dieser Bericht. Kein Implementierungseingriff durch den Tester. Neue Nachweise liegen unter `.pio/gnss-fifo-p3/case-rework/`; urspruengliche `*.log` und `repeat-*`-Nachweise werden bewahrt.

## Aktuelle Regressionen und alle Tests

Ausgefuehrt genau einmal:

```powershell
python test/gnss_logger/run_tests.py
```

**Exit 0**, alle sieben C++-Suiten und alle acht Pythonpruefungen bestanden. Vollstaendige Ausgabe: `case-rework/tests.log`, Befehl/Exit/Dauer: `case-rework/tests-result.json`. Der unveraenderte Runner kompiliert mit dem vorhandenen ARM-GCC 7.2.1, `-std=gnu++17 -Wall -Wextra -Werror -c`, nach `.pio/gnss-fifo-p3/tests/` und startet anschliessend `test_integration.py` mit demselben Python-Interpreter.

| C++-Suite | `static_assert` | Ergebnis |
| --- | ---: | --- |
| `gnss_epoch` | 10 | bestanden |
| `local_time` | 17 | bestanden |
| `log_clock` | 6 | bestanden |
| `rpm_filter` | 4 | bestanden |
| `ui_self_test` | 12 | bestanden |
| `gnss_log_fifo` | 16 | bestanden |
| `gnss_logger` | 18 | bestanden |
| **Summe** | **83** | **bestanden** |

**F1:** Der neue constexpr-Regressionstest verwendet die tatsaechlichen Firmwarefunktionen `findSegment`, `sharedSegmentFailure`, `writeFile` und `Fifo`. Alle 10000 Namen werden in Reihenfolge als existent gemeldet; geprueft sind hoechstens 32 Versuche pro Runde und kein weiterer Anlagecallback nach Erschoepfung. Ueber 400 Runden bleiben je 400 RPM- und SENSOR-Schreibaufrufe erfolgreich, waehrend der GNSS-FIFO seine ersten 320 Eintraege behaelt und exakt 80 Drop-newest-Verluste zaehlt. Es gibt keine falschen Schreibbestaetigungen oder unsicheren GNSS-Bloecke. Zusaetzliche Faelle pruefen erfolgreichen Suchabbruch und sofortigen gemeinsamen Fehler bei `IoError`. Die vorhandenen Fehlerpruefungen fuer Open/Write/Flush/Close und unsichere Bloecke bleiben aktiv.

Der erweiterte statische Quellvertrag verbindet diese Policy mit `loggingTask`: Suchfortsetzung/-Erschoepfung setzt nicht mehr `failed = !newSegment`; nur `sharedSegmentFailure` setzt den gemeinsamen Segmentfehler. Die Erschoepfungsgrenze sperrt neue Suchaufrufe; `gnssPath[0]` sperrt ausschliesslich den GNSS-Verbrauch, waehrend beide Legacy-Schreibpfade davor bleiben. Exklusive Anlage mittels `O_EXCL`, unveraenderte Legacy-Messwertausdruecke, Mediumpruefung und konservativer gemeinsamer I/O-Wiederanlauf werden weiter geprueft. Dies ist eine Policypruefung plus statische Verdrahtungspruefung, keine Ausfuehrung der vollstaendigen RTOS-Loggerschleife oder des Ziel-FAT-Dateisystems.

**F2:** Neue FIFO-Faelle pruefen `receivedMs=1500` bei veraltetem `now=1000`, einen zukuenftigen Empfangswert Ueber den Zaehlerueberlauf sowie einen vor dem Ueberlauf empfangenen Eintrag mit Ablauf danach. Jeder Teilblock bleibt bei veraltetem Zeitpunkt, Alter null und 999 ms gesperrt und wird erst bei 1000 ms freigegeben; gespeicherte Zeit und Bilanz bleiben korrekt. Weitere Faelle pruefen sofortige Vollblockfreigabe und konservative Sperre bei exakt `2^31 ms`. Der Quellvertrag fordert frisches `millis()` direkt an `reserveGnssLog` nach den Legacy-Dateizugriffen. Die modulare Zeitordnung ist nur fuer eindeutige Abstaende unter `2^31 ms` (etwa 24,9 Tage) zugesagt.

**F3:** Die Architektur nennt jetzt alle sieben C++-Suiten und acht Python-Quellvertraege samt gemeinsamem Runner und den Pruefgrenzen. Die Zahl der Python-Testmethoden bleibt acht; vorhandene Vertraege wurden gezielt erweitert.

## Aktueller Firmwarebuild und Groessen

Ausgefuehrt genau einmal mit vorhandenen lokalen Paketen und der massgeblichen, unveraenderten `platformio.ini`; prozesslokale Pfade wie in der historischen P3-Wiederholung:

```powershell
$env:PLATFORMIO_CORE_DIR = 'F:/Development/mofa_tacho/.pio/gnss-fifo-p3/core'
$env:PLATFORMIO_CACHE_DIR = 'F:/Development/mofa_tacho/.pio/gnss-fifo-p3/core/.cache'
$env:PLATFORMIO_WORKSPACE_DIR = 'F:/Development/mofa_tacho/.pio/gnss-fifo-p3'
$env:PLATFORMIO_BUILD_DIR = 'F:/Development/mofa_tacho/.pio/gnss-fifo-p3/build'
$env:PLATFORMIO_LIBDEPS_DIR = 'F:/Development/mofa_tacho/.pio/gnss-fifo-p3/libdeps'
$env:PLATFORMIO_SETTING_ENABLE_TELEMETRY = 'No'
$env:PLATFORMIO_SETTING_CHECK_PLATFORMIO_INTERVAL = '0'
pio run -e giga_r1_m7
```

**Exit 0, `[SUCCESS] Took 8.28 seconds`.** Der gesamte gestartete Prozess dauerte 69,484 s. Nach der erfolgreichen Firmwareerzeugung meldete PlatformIO lediglich `Failed to check for PlatformIO upgrades. Please check your Internet connection.` Das Log zeigt keine Downloads, Paketinstallationen oder Bibliotheksupdates. Kein erneuter Versuch. Verwendet wurden unveraendert ST STM32 20.0.0, Arduino Mbed 4.6.0, ARM GCC-Paket 1.70201.0 und die unten historisch aufgelisteten Bibliotheksversionen.

Inkrementeller Build mit Neukompilierung von `gnss_service.cpp`, `main.cpp`, `vars.cpp` sowie `lv_blend_helium.S`; danach Link, Groessenpruefung und BIN-Erzeugung. Kein Clean-Build oder Beseitigen alter Bibliothekswarnungen behauptet. Nachweise: `case-rework/build.log`, `build-result.json`, `size.log`, `sizes-result.json`.

| Messgroesse | Byte | Limit | Nutzung |
| --- | ---: | ---: | ---: |
| RAM, PlatformIO `.data + .bss` | 150656 | 523624 | 28,8 % |
| Flash, PlatformIO `.text + .data + .ARM.exidx` | 481944 | 786432 | 61,3 % |
| Tatsaechliche `firmware.bin` | 483884 | 786432 | 61,5 % |

`arm-none-eabi-size.exe` und `arm-none-eabi-size.exe -A` wurden aus `.pio/gnss-fifo-p3/core/packages/toolchain-gccarmnoneeabi/bin` auf `.pio/gnss-fifo-p3/build/giga_r1_m7/firmware.elf` angewandt. Sections: `.text=475184`, `.ARM.exidx=3408`, `.ARM.extab=1940`, `.data=3352`, `.bss=147304`, `.uninitialized=8`, `.lwip_sec=278528`, `.heap=372600` Byte. Die BIN enthaelt zusaetzlich den von PlatformIO nicht gezaehlten `.ARM.extab`-Anteil. Die einfache size-Summe ist `text=480532`, `data=3352`, `bss=425840`; diese BSS-Zahl enthaelt weitere Bereiche und ist keine andere RAM-Nutzungsmessung. Heap- und Stackreserve sind nicht gemessen. Gegenueber dem historischen Wiederholungsbuild: RAM unveraendert, PlatformIO-Flash +264 Byte, BIN +268 Byte.

ELF-SHA-256: `c6186642cab8fb0944b20b0f4c5c31d57287f8e6a0caca3a991acc3363b41512`.
BIN-SHA-256: `5293c947a8c4be4274c327ce6e10b39034a4ce60fadb0bc796e5ebef2e1ee854`.

## Aktuelle Abschlusskontrollen und Grenzen

Die 60 zu Arbeitsbeginn erfassten Dateien stehen in `case-rework/initial-hashes.json`; `tester-initial-hashes.json` haelt den abgeschlossenen Entwicklerstand fest. Die Abschlusskontrolle in `final-hashes.json` und `checks.json` vergleicht beide Staende. Nur die drei genehmigten Entwicklerdateien und vier Testerdateien unterscheiden sich von Beginn. In der Testerphase bleibt der gesamte Implementierungs-/Konfigurations-/UI-Stand identisch. Vorhandene Benutzerdateien, `platformio.ini`, lokale Konfigurationen und die bisherigen P3-Logs wurden erhalten.

Nach der Berichtsaktualisierung wurden `git diff --check`, zusaetzliche Whitespacekontrolle auch der unversionierten Testdateien und `git status --short` erfolgreich protokolliert: `git diff --check` Exit 0 ohne Ausgabe, keine Whitespacebefunde auch in den unversionierten Testdateien (`case-rework/diff-check.log`, `status.log`, `checks.json`). Der Git-Status hat weiterhin dieselben Pfade wie zu Auftragsbeginn; Aenderungen an unversionierten Testdateien bleiben als Verzeichniseintraege sichtbar.

Alle C++-Pruefungen sind Compile-only-Auswertungen der produktiven constexpr-Policies; die acht Pythonpruefungen sind statische Quellvertraege. Keine vollstaendige RTOS-/USB-/RTC-Laufzeitpruefung, reale USB-Latenz-/Durchsatzpruefung, Stack-Watermark- oder Hardwarepruefung. Fehlerhafte Ziel-FAT-/Treiberaufrufe und tatsaechliche physische Dauerhaftigkeit bleiben ungeprueft. Keine neuen wesentlichen Fehler waehrend der beauftragten Pruefungen festgestellt. Kein CASE, Commit, Push, Merge, Firmware-Upload oder Rechteausbau. Gestartete Test- und Buildprozesse sind beendet. Naechster Freigabepunkt: Benutzerentscheidung Ueber Abnahme oder gesonderte weitere Pruefung; keine automatische Folgephase.

---

## Historische P3-Pruefung vor der aktuellen CASE-Nacharbeit

Die folgenden Angaben sind die bewahrte Dokumentation des frueheren Pruefstands. Insbesondere 75 Assertions, Flash 481680 Byte, BIN 483616 Byte und die alten Hashes sind historische Werte.

# GNSS-FIFO P3-Prüfbericht

Prüfdatum: 21. November 2026. Testerphase zum ausdrücklich freigegebenen P1-Entwurf Variante A, einschließlich der ausdrücklich beauftragten gezielten Fehlernacharbeit und P3-Wiederholung. **Status: abgeschlossen – alle sieben C++-Suiten, acht statischen Quellvertragsprüfungen und der Firmwarebuild erfolgreich.** Der ursprüngliche Buildfehler und die Wiederholung sind unten getrennt dokumentiert. Kein CASE, Commit/Push/Merge, Hardwarezugriff, Upload oder Rechteausbau.

## Prüfstand und Dateiumfang

Basis: `feature/gnss-fifo-logging`, Commit `2673b4559fcc56c4f883bb2d2d4791dec234ae77`, mit dem abgeschlossenen P2-Diff in `src/gnss_service.h`, `src/gnss_service.cpp`, `src/gnss_log_fifo.h`, `src/main.cpp` und `docs/architecture.md`.

In der ursprünglichen P3-Phase hat der Tester ausschließlich die folgenden Dateien neu angelegt:

- `test/gnss_log_fifo/test_fifo.cpp`
- `test/gnss_logger/test_logger.cpp`
- `test/gnss_logger/test_integration.py`
- `test/gnss_logger/run_tests.py`
- `test/gnss_logger/README.md`

Der anfängliche SHA-256-Prüfstand liegt unverändert unter `.pio/gnss-fifo-p3/initial-hashes.json`. In der freigegebenen Entwicklernacharbeit wurde danach ausschließlich eine Include-Zeile in `src/main.cpp` geändert. Der Tester hat bei der Wiederholung nur diesen Prüfbericht aktualisiert und Nachweise unter `.pio/gnss-fifo-p3` geschrieben; keine Test- oder Implementierungsdateien verändert. Dokumentation aus P2, `platformio.ini`, `platformio.local.ini` sowie die bereits vorhandenen Benutzerdateien `P2_P3.bash` und `docs/gnss-fifo-p1.md` bleiben erhalten.

TARS prüfte und meldete vor der Schreibphase die tatsächlich wirksamen Sitzungsrechte: `workspace-write` für Projekt und Temp, geschützte `.git`-/`.agents`-/`.codex`-/`.aws`-Pfade, `approval_policy=never`, eingeschränktes Netzwerk und keine Rechteerweiterung. Der Entwickler war vor dem Testerstart abgeschlossen und meldete keine laufenden eigenen Prozesse. Der aktuelle Prüfstand mit 63 Datei-Hashes ist in `.pio/gnss-fifo-p3/repeat-initial-hashes.json` erfasst; Abschlussvergleich und Hashes stehen in `repeat-final-hashes.json` und `repeat-checks.json` im selben Verzeichnis.

## Ausgeführte Tests

```powershell
python test/gnss_logger/run_tests.py
```

Ergebnis der Wiederholung: **Exit 0; alle sieben C++-Suiten mit insgesamt 75 `static_assert`-Prüfungen und acht Python-Quellvertragsprüfungen bestanden.** Neues vollständiges Log: `.pio/gnss-fifo-p3/repeat-tests.log`; das ursprüngliche erfolgreiche Log `.pio/gnss-fifo-p3/tests.log` bleibt erhalten. Bei der Wiederholung wurden stdout und stderr gemeinsam über den gestarteten Python-Prozess erfasst; alle Prozesse endeten erfolgreich.

Der Runner verwendet für jede C++-Datei den bereits vorhandenen Compiler:

```text
C:/Users/nicol/.platformio/packages/toolchain-gccarmnoneeabi/bin/arm-none-eabi-g++.exe
  -std=gnu++17 -Wall -Wextra -Werror -c <Testdatei>
  -o F:/Development/mofa_tacho/.pio/gnss-fifo-p3/tests/<Suite>.o
```

Anschließend führt er `python test/gnss_logger/test_integration.py` mit demselben Python-Interpreter aus.

| C++-Suite | `static_assert`-Prüfungen | Ergebnis |
| --- | ---: | --- |
| Vorhanden: `gnss_epoch` | 10 | bestanden |
| Vorhanden: `local_time` | 17 | bestanden |
| Vorhanden: `log_clock` | 6 | bestanden |
| Vorhanden: `rpm_filter` | 4 | bestanden |
| Vorhanden: `ui_self_test` | 12 | bestanden |
| Neu: `gnss_log_fifo` | 11 | bestanden |
| Neu: `gnss_logger` | 15 | bestanden |
| **Summe** | **75** | **bestanden** |

`static_assert` wertet die tatsächlichen constexpr-Funktionen der Firmware aus. Die neuen Suiten prüfen insbesondere:

- 320 nutzbare Plätze; 350 Einreihungsversuche über 70 Sekunden mit exakt 30 Drop-newest-Verlusten; Reihenfolge, Sequenzlücken und Bilanz bei jedem Schritt.
- Ringumlauf über 800 Einträge; geschützte Reservierung, falsche oder doppelte Bestätigung, Verbraucherlimit und höchstens 20 Einträge je Block.
- Exakte Teilblockfrist einschließlich `millis()`-Umlauf; unveränderliche Messzeit über Rückstau, ungültige Flags und unterschiedliche Sitzungen hinweg.
- Begrenzte Abbaurunden mit 200 Bestätigungen bei fünf neu angenommenen Einträgen in einer simulierten Sekunde. Das belegt die Policy-Kapazität, keinen realen USB-Durchsatz.
- Erfolgreiche vollständige Schreiboperation; Fehler beim Öffnen, ersten/mittleren/letzten Schreiben, Flush und Close; Formatierungsfehler; Close auch nach vorherigem Fehler.
- Bilanzierung eines versuchten 20er-Fehlerblocks als unsicher, Erhalt der 20 noch unversuchten Einträge und Wiederaufnahme ohne Replay des unsicheren Blocks. Auch eine nachträglich durch Detach entwertete Schreibbestätigung wird im Testaufrufer so behandelt.

Die acht Python-Prüfungen sind **statische Quellverträge**: Sie vergleichen den Akzeptanzpfad nach Entfernung der FIFO-Einfügung und unveränderte UTC-/Rate-/Snapshot-/UI-Funktionen mit dem Basiscommit; prüfen Legacy-Kopfzeilen und Messwertausdrücke, das additive 17-Spalten-Format, atomare Callback-Ereignisse, Mount-Gates, exklusive begrenzte Dateinamensuche, Statistiken ohne Folgeepoche und I/O-freie FIFO-Mutexbereiche. Sie führen weder den vollständigen Logger noch Mbed-RTC, USB oder RTOS aus.

## Ursprünglicher PlatformIO-Buildversuch

Die vorhandene lokale Konfiguration verwendet `F:/PlatformIO/mofa_tacho` als Workspace. Dieser Pfad liegt außerhalb der wirksamen Schreibrechte. Die Konfiguration wurde nicht geändert. Weil PlatformIO auch bei vorhandenen Paketen Sperrdateien anlegt, wurden die installierte Plattform `ststm32`, benötigte Pakete und die aktiven Projektbibliotheken unverändert in den genehmigten Ausgabebereich kopiert. Keine Symlinks, Downloads, Paketupdates oder Rechteerweiterungen.

Für den ursprünglichen Versuch und den anschließend ausdrücklich freigegebenen Wiederholungsbuild galten ausschließlich im jeweils gestarteten Prozess diese unveränderten Ausgabepfade und Variablen:

```powershell
$env:PLATFORMIO_CORE_DIR = 'F:/Development/mofa_tacho/.pio/gnss-fifo-p3/core'
$env:PLATFORMIO_CACHE_DIR = 'F:/Development/mofa_tacho/.pio/gnss-fifo-p3/core/.cache'
$env:PLATFORMIO_WORKSPACE_DIR = 'F:/Development/mofa_tacho/.pio/gnss-fifo-p3'
$env:PLATFORMIO_BUILD_DIR = 'F:/Development/mofa_tacho/.pio/gnss-fifo-p3/build'
$env:PLATFORMIO_LIBDEPS_DIR = 'F:/Development/mofa_tacho/.pio/gnss-fifo-p3/libdeps'
$env:PLATFORMIO_SETTING_ENABLE_TELEMETRY = 'No'
$env:PLATFORMIO_SETTING_CHECK_PLATFORMIO_INTERVAL = '0'
pio run -e giga_r1_m7
```

Ursprüngliches Ergebnis vor der Korrektur: **Exit 1, `[FAILED] Took 40.96 seconds`**. Der neue Buildpfad war zuvor leer; es erfolgte eine vollständige Neukompilierung bis zum Compilerfehler, kein erfolgreicher Link. Das vollständige ursprüngliche Log `.pio/gnss-fifo-p3/build.log` bleibt unverändert erhalten.

Ursprünglicher blockierender Fehler: Das in `src/main.cpp:25` eingebundene `<unistd.h>` deklariert über Newlib `unsigned int sleep(unsigned int)`; dies kollidiert mit Mbed `static inline void sleep(void)` in `platform/mbed_power_mgmt.h:195`.

```text
sys/unistd.h:209:10: error: conflicting declaration of C function
  'unsigned int sleep(unsigned int)'
mbed_power_mgmt.h:195:20: note: previous declaration 'void sleep()'
*** [.../src/main.cpp.o] Error 1
```

Zusätzlich meldete dieser ursprüngliche Build Warnungen in vorhandenen Bibliotheks-, Framework- und generierten UI-Dateien, darunter veraltete Mbed-/LVGL-APIs. Diese Dateien wurden nicht verändert.

## Gezielte Korrektur und erfolgreicher Wiederholungsbuild

Die Entwicklernacharbeit ersetzt ausschließlich in `src/main.cpp:25`:

```diff
-#include <unistd.h>
+#include <platform/mbed_retarget.h>
```

Newlib `sys/unistd.h:209` und Mbed `platform/mbed_power_mgmt.h:195` deklarieren unterschiedliche `sleep`-Signaturen unter C-Linkage; C++-Überladung kann diesen Konflikt nicht lösen. Tatsächlich benötigt `main.cpp` aus `unistd.h` nur `::close(fd)` im Fehlerpfad nach fehlgeschlagenem `fdopen`. Der vorhandene Mbed-Header deklariert `close` in Zeile 749 sowie `open`/`fdopen` in Zeilen 730–735, ohne Newlibs `unistd.h` einzubinden. Der Deskriptor wird im Fehlerpfad weiterhin geschlossen; exklusive Segmentanlage mit `O_EXCL` bleibt erhalten. Es wurden weder eigene Funktionsdeklarationen noch Makroumbenennungen oder Bibliothekspatches eingeführt.

Der neue SHA-256 von `src/main.cpp` ist `5623be3731725640a4437ac6f39257bb828253b0fa1e7b67006273a62ef12ba4`. Wird nur diese Include-Zeile auf den ursprünglichen Text zurückgerechnet, ergibt sich exakt der ursprüngliche P3-Hash `d2177543aa1fb1625b6dc9fe48a2561d7ff8235a19227134a451036e46069df4`. Damit bleibt der übrige bestehende Benutzerstand dieser Datei erhalten.

Die USB-Schreibsemantik ist unverändert: `writeFile` prüft `fopen`; `FileOps::write` verwendet bereits `fwrite` statt `fputs` und verlangt die vollständige Bytezahl sowie `!ferror(f)`. Für die nullterminierten Logzeilen erfüllt dies die geforderte vollständige, fehlergeprüfte Ausgabe und erkennt kurze Schreiboperationen ausdrücklich. `fflush(f) == 0 && !ferror(f)` sowie `fclose(f) == 0` bleiben erhalten. `writeOpened` führt Flush und Close auch nach einem vorangehenden Schreibfehler aus und bestätigt nur das Gesamtergebnis aller Operationen. Keine Fehlerprüfung wurde entfernt oder abgeschwächt.

`fsync` wurde bisher nicht verwendet und ist für den vereinbarten P1-Vertrag aus geprüfter Öffnung, vollständigem Schreiben, Flush und Close ohne physische Dauerhaftigkeitsgarantie nicht zusätzlich erforderlich. Für einen künftig gesondert verlangten Synchronisationsschritt stellt derselbe kompatible Header bereits `int fsync(int fildes)` in Zeile 741 bereit. Hier wurde kein zusätzlicher Synchronisationsschritt eingeführt oder eine physische Dauerhaftigkeitsgarantie behauptet.

Der Tester führte nach der Korrektur genau einen Wiederholungsbuild mit dem oben dokumentierten `pio run -e giga_r1_m7` aus: **Exit 0, `[SUCCESS] Took 8.44 seconds`**. Vollständiges neues Log: `.pio/gnss-fifo-p3/repeat-build.log`; Befehl, Prozessvariablen, Exitcode und gemessene Prozessdauer (8,881 Sekunden einschließlich Startaufwand) stehen in `repeat-build-result.json` im selben Verzeichnis.

Dies war ein inkrementeller Wiederholungsbuild auf den unveränderten Objekten des ursprünglichen vollständigen Buildversuchs. `main.cpp` wurde neu kompiliert, die Firmware erfolgreich verlinkt, die Größenprüfung bestanden und `firmware.bin` erzeugt. Kein Clean-Build wurde zusätzlich behauptet oder gestartet. Der Wiederholungsbuild gab keine neuen Warnungen aus; die bereits dokumentierten Warnungen aus wiederverwendeten Bibliotheks-/Framework-/UI-Objekten sind damit nicht beseitigt. Keine weiteren wesentlichen Fehler traten auf.

| Tatsächlich verwendete Komponente | Version |
| --- | --- |
| ST STM32 | 20.0.0 |
| Arduino Mbed | 4.6.0 |
| ARM GCC-Paket / Compiler | 1.70201.0 / 7.2.1 |
| Arduino_GigaDisplayTouch | 1.1.2 |
| LVGL | 9.6.0 |
| Arduino_USBHostMbed5 | 0.3.1 |
| SparkFun u-blox GNSS | 2.2.29 |
| OneWire | 2.3.8 |
| DallasTemperature | 4.0.6 |

## RAM, Flash und Stack

Das neu verlinkte `.pio/gnss-fifo-p3/build/giga_r1_m7/firmware.elf` liefert folgende erfolgreiche PlatformIO-Größenprüfung:

| PlatformIO-Auswertung | Belegt | Konfiguriertes Limit | Nutzung |
| --- | ---: | ---: | ---: |
| RAM (`.data + .bss`) | 150656 Byte | 523624 Byte | 28,8 % |
| Flash (PlatformIO-Sectionauswahl) | 481680 Byte | 786432 Byte | 61,2 % |

Die Zählweise ist durch `core/platforms/ststm32/builder/main.py:61–63` und `arm-none-eabi-size -A` belegt: PlatformIO zählt RAM aus `.data=3352` und `.bss=147304`; `.uninitialized=8` Byte und die linkerreservierte `.lwip_sec=278528` Byte im separaten RAM-D2-Bereich sind darin nicht enthalten. `.heap=372600` beschreibt einen im Linkerskript vorgesehenen Bereich, keine gemessene freie Laufzeitreserve. Die einfache `size`-Zusammenfassung meldet `text=480264`, `data=3352`, `bss=425840`; wegen der zusätzlichen Sections ist deren BSS-Zahl nicht mit dem PlatformIO-RAM-Zähler gleichzusetzen.

PlatformIO zählt Flash aus `.text + .data + .ARM.exidx` und lässt `.ARM.extab=1936` Byte aus. Die tatsächlich erzeugte `firmware.bin` umfasst **483616 Byte (61,5 % von 786432 Byte)** und enthält diesen zusätzlichen Flash-Anteil. Damit sind sowohl der konkrete PlatformIO-Zähler als auch die Binärgröße dokumentiert. ELF-SHA-256: `b824992584b53760cb75cf46aa1ce08d47eb3ef151ca853f63a534a8f37e5d86`. Größen-, Section- und Symbolnachweise stehen in `.pio/gnss-fifo-p3/repeat-size-symbols.log`.

Verfügbare Teilnachweise:

```powershell
arm-none-eabi-nm.exe -S -C --size-sort .pio/gnss-fifo-p3/build/giga_r1_m7/src/gnss_service.cpp.o
arm-none-eabi-size.exe .pio/gnss-fifo-p3/build/giga_r1_m7/src/gnss_service.cpp.o
```

Die ursprünglichen Objektprüfungen verwendeten die vorhandenen Werkzeuge unter `C:/Users/nicol/.platformio/packages/toolchain-gccarmnoneeabi/bin`. Bei der Wiederholung wurden `arm-none-eabi-size` und `arm-none-eabi-nm -S -C --size-sort` zusätzlich auf das neue ELF angewandt, mit den identischen lokalen Werkzeugkopien unter `.pio/gnss-fifo-p3/core/packages/toolchain-gccarmnoneeabi/bin`. Die folgenden Symbolgrößen wurden im neuen ELF bestätigt:

| Statische Größe | Beleg | Byte |
| --- | --- | ---: |
| Ein GNSS-Datensatz | ARM-`static_assert` | 64 |
| Nutzdatenring | 320 × 64 | 20480 |
| Gesamtes `logFifo`-Objekt | `nm`: `0x5030` | 20528 |
| Separater FIFO-Mutex | `nm`: `0x24` | 36 |
| Arbeitskopie | 20 × 64, globale Deklaration | 1280 |
| Formatpuffer | globale Deklaration | 384 |
| Ring + Arbeitskopie + Formatpuffer | Arraybudget | 22144 |
| FIFO-Objekt + Mutex + Arbeitskopie + Formatpuffer | Teilbudget inkl. Ringverwaltung | 22228 |

Das letzte Teilbudget enthält noch nicht den zusätzlichen Pfad-, Atomik- und sonstigen Loggerzustand oder dynamische Dateisystemkosten. Arbeitskopie und Formatpuffer liegen nicht auf dem Loggerstack. Die erfolgreiche Service-Objektdatei meldet `text=4344`, `data=4`, `bss=25356` Byte; das sind **unverlinkte Teilobjektwerte**, keine Firmwaregrößen und kein gemessener Mehrverbrauch gegenüber dem Basisstand.

Die Logger-/GNSS-Stackreserve und dynamische Heapreserve wurden nicht gemessen. Auch das nun erfolgreich erzeugte ELF ersetzt weder eine vollständige statische Aufrufkettenbetrachtung noch die ausstehende Hardware-Watermark-/Lastprüfung.

## Prüflücken und offene Risiken

- Der aktuelle Firmwarestand ist erfolgreich kompiliert und verlinkt. Die C++-Suiten sind Compile-only-Prüfungen per `static_assert`; die acht Python-Prüfungen sind statische Quellverträge. Damit sind weder der vollständige Logger noch die Geräte-/RTOS-Integration zur Laufzeit geprüft.
- Kalenderumrechnung einschließlich negativer Nanosekunden ist per Quellvergleich unverändert. Die Mbed-RTC-Funktionen wurden in dieser Phase nicht ausgeführt; die FIFO-Tests prüfen das unveränderte Speichern bereits gebildeter UTC-Werte.
- Fehlendes Medium und Mountfehler sind durch Policy-Erhalt beziehungsweise statische Gates abgedeckt. Tatsächliche Mount-/Unmountfehler, Detach während blockierender Treiberaufrufe, Reconnect und exklusives Anlegen auf dem Ziel-FAT-Dateisystem sind nicht zur Laufzeit geprüft.
- Keine nachgewiesene reale 5-Hz-/USB-Abbaurate, keine RTOS-Nebenläufigkeits-, I²C-Latenz-, Stack-Watermark-, Dauertest- oder Hardwareprüfung. Kein Host-C++-Compiler oder ARM-Emulator verfügbar; WSL wurde nicht als Ausführungsweg verwendet.
- 320 Einträge überbrücken bei zuvor leerem FIFO 64 Sekunden mit 5 Hz. Danach gilt Drop-newest. Roh-PVT-Verluste vor der bestehenden Akzeptanzstelle bleiben außerhalb von Variante A.
- Die Dateinamensuche ist auf 10000 Kandidaten pro Boot und 32 Versuche pro Runde begrenzt. Stromausfall kann RAM und noch nicht physisch dauerhaft gespeicherte FAT-Daten verlieren; geprüftes Flush/Close ist keine externe Dauerhaftigkeitsbestätigung.

## Abschlusskontrollen und Halt

`git diff --check` wurde nach der Berichtsaktualisierung ausgeführt: Exit 0, ohne Ausgabe. `git status --short` wurde ebenfalls ausgeführt; der Status bleibt gegenüber dem Beginn der Nacharbeit unverändert:

```text
 M docs/architecture.md
 M src/gnss_service.cpp
 M src/gnss_service.h
 M src/main.cpp
?? P2_P3.bash
?? docs/gnss-fifo-p1.md
?? src/gnss_log_fifo.h
?? test/gnss_log_fifo/
?? test/gnss_logger/
```

Die Hashkontrolle der 63 zu Beginn der Wiederholung erfassten Dateien bestätigt: Nur der freigegebene Prüfbericht wurde in der Testerphase verändert; alle 62 übrigen Dateien einschließlich der Implementierung, Tests, Konfigurationen, Benutzerdateien und ursprünglichen P3-Logs blieben identisch. Gegenüber `initial-hashes.json` unterscheidet sich ausschließlich `src/main.cpp` durch die oben belegte Include-Korrektur. Kein Konfigurations-/UI-/Bibliotheksquelltext wurde verändert. Neue Nachweise liegen nur unter `.pio/gnss-fifo-p3`. Die gestarteten Test- und Buildprozesse sind beendet.

Die genehmigte Fehlernacharbeit und P3-Wiederholung sind abgeschlossen. Nächster Freigabepunkt ist die Benutzerentscheidung über weitere Arbeit; keine automatische Folgephase oder Nacharbeit. CASE wurde auftragsgemäß nicht gestartet. Der Tester hält an.
