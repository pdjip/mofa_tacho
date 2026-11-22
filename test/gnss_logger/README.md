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
