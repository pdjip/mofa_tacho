**Architektenbericht – GNSS-FIFO-Logging, P1**

Aufgabenstatus: `abgeschlossen`. Agent: nativer `architekt`. Prüfstand: lokales `develop`, Commit `2673b4559fcc56c4f883bb2d2d4791dec234ae77`, sauberer Arbeitsbaum vor und nach der Analyse. Kein Remote-Abgleich. Die Sitzung ist technisch schreibgeschützt; `approval_policy = never`. Projektregeln aus `AGENTS.md` und `docs/agent-team.md` wurden gelesen.

Änderungen: keine. Builds, Tests, Uploads und weitere Delegationen: nicht ausgeführt, gemäß Auftrag. Dieser Bericht beschreibt einen Entwurf, keine verifizierte Implementierung.

**Ergebnis und Geltungsbereich**

Ich empfehle einen separaten statischen FIFO unmittelbar an der bestehenden GNSS-Akzeptanzstelle. `gnssTask()` bleibt alleiniger Produzent; `loggingTask()` wird alleiniger Konsument. Die Anzeige erhält weiterhin ausschließlich den aktuellen `GnssSnapshot`.

Damit lassen sich alle **von der bestehenden Firmware akzeptierten und veröffentlichten** Epochen geordnet übernehmen oder bei erschöpfter Kapazität ausdrücklich als Verlust zählen. Eine Garantie für jede vom Empfänger erzeugte 5-Hz-Epoche ergibt sich daraus nicht: Bereits vor der Akzeptanzstelle kann die Bibliothek mehrere empfangene PVT-Nachrichten auf ihren letzten Datensatz reduzieren.

Ein endlicher RAM-Puffer kann außerdem einen beliebig langen USB-Ausfall nicht verlustfrei überbrücken. „Verlustkontrolliert“ muss deshalb konkret bedeuten: begrenzte Pufferung, definierte Überlaufregel, Sequenznummern, getrennte Fehlerzähler und keine stillschweigende Erfolgsmeldung nach fehlgeschlagenem Dateizugriff.

**Bestehender Datenfluss und Akzeptanz**

[gnss_service.cpp](/F:/Development/mofa_tacho/src/gnss_service.cpp:20) besitzt GNSS-Gerät und Bibliothekszustand ausschließlich im GNSS-Thread. Der Empfänger wird auf 200 ms Messperiode und Navigationsrate 1 konfiguriert; eine frische CFG-RATE-Antwort bestätigt diese Einstellung. Alle zehn Sekunden wird sie erneut geprüft.

Eine akzeptierte Epoche ist im vorliegenden Entwurf exakt ein Datensatz, der die bestehende Verarbeitung bis `publish(current)` erreicht:

- `EpochTracker` liefert `New`.
- Die anfängliche Bestätigung durch drei aufeinanderfolgende 200-ms-Schritte ist erfüllt.
- Der `RateMonitor` hat keinen Abbruch ausgelöst.
- `sampleId` wurde erhöht und der vollständige Datensatz aufgebaut.

Diese Grenze liegt in [gnss_service.cpp](/F:/Development/mofa_tacho/src/gnss_service.cpp:216), insbesondere bei Zeilen 229–265. Ein `New` vor abgeschlossener Startbestätigung zählt ausdrücklich noch nicht dazu. `Baseline`, `Duplicate`, `Old` und `Invalid` werden nicht nachträglich geloggt.

Positions- und Zeitgültigkeit sind unabhängig von dieser Akzeptanz: Auch eine akzeptierte Epoche ohne gültigen Fix oder ohne gültige UTC gehört in den FIFO. Andernfalls wäre die Forderung „alle akzeptierten Epochen“ nicht erfüllt.

Der bestehende [EpochTracker und RateMonitor](/F:/Development/mofa_tacho/src/gnss_epoch.h:23) bleiben erhalten, einschließlich Wochenwechsel, Hostzeitvergleich, Re-Ankerung und UTC-Übergang mit Sitzungsneustart.

**`getPVT(0)`, `flushPVT()` und Bibliothekspuffer**

Die lokal vorhandene SparkFun-Bibliothek meldet Version **2.2.29**, passend zur Vorgabe `^2.2.29` in `platformio.ini`.

Die Quellprüfung zeigt:

- `getPVT(0)` ruft bei eingeschaltetem Auto-PVT mit impliziter Aktualisierung `checkUbloxInternal()` auf und liefert anschließend den Freshness-Status des einen PVT-Datensatzes. Beleg: Bibliotheksdatei `SparkFun_u-blox_GNSS_Arduino_Library.cpp`, Zeilen 10950–10965.
- `checkUbloxI2C()` verarbeitet sämtliche zuvor ermittelten verfügbaren Bytes in Transaktionsblöcken. Ein Aufruf kann deshalb mehrere vollständige PVT-Nachrichten verarbeiten. Beleg: Zeilen 1201–1265.
- Jede PVT-Nachricht überschreibt `packetUBXNAVPVT->data`. Nach einem Burst sieht die Anwendung den letzten Datensatz. Beleg: Zeilen 3353–3393.
- `flushPVT()` setzt lediglich die beiden Freshness-Bitfelder auf null. Es leert weder den Empfängerpuffer noch einen Epochenspeicher und entfernt keine I²C-Bytes. Beleg: Zeilen 11151–11156.

`maxWait = 0` bedeutet hier somit keinen garantierten zeitlich begrenzten I²C-Aufruf. Transport-, Treiber- und Schedulingverzögerungen bleiben möglich. Auch die periodische CFG-RATE-Abfrage und serielle Ausgaben können den GNSS-Thread verzögern.

Der Standardcallback löst das Burstproblem ebenfalls nicht zuverlässig: Die Bibliothek besitzt nur eine ausstehende Callback-Kopie. Solange `callbackCopyValid` gesetzt ist, wird diese nicht durch nachfolgende PVTs ersetzt; erst `checkCallbacks()` gibt den Slot wieder frei. Belege: Zeilen 3395–3400 und 5410–5426. Ein Callback ist hier kein FIFO.

Die Bibliothek bietet zusätzlich einen optionalen Byte-Ringpuffer:

- Standardgröße null; im Projekt derzeit nicht aktiviert.
- Aktivierung durch `setFileBufferSize()` vor `begin()` und `logNAVPVT()` nach vorhandener PVT-Speicherinitialisierung.
- Eine NAV-PVT-Nachricht belegt 92 Byte Nutzdaten plus 8 Byte UBX-Rahmen, insgesamt 100 Byte.
- Bei Platzmangel wird die ganze Nachricht verworfen. `storePacket()` liefert `false`, dessen Rückgabe der NAV-PVT-Verarbeitungszweig allerdings nicht auswertet. Eine Debugausgabe ersetzt keinen zuverlässigen Verlustzähler.
- Bibliothekszugriffe auf diesen Puffer müssten weiterhin im GNSS-Thread verbleiben.

Belege: Bibliotheksdatei Zeilen 3403–3406, 6982–7030, 7040–7064, 7111–7159 und 11160–11164.

**Bestehendes Logging, UTC und USB**

[loggingTask()](/F:/Development/mofa_tacho/src/main.cpp:298) liest derzeit alle etwa 100 ms den neuesten Snapshot. Dadurch werden 5-Hz-GNSS-Werte mehrfach geloggt; während USB-Verzögerungen können Zwischenepochen vollständig fehlen. Die Schleife schläft nach ihrer Arbeit, weshalb 10 Hz keine garantierte Ausführungsrate sind.

Die bestehenden Dateien sind tabulatorgetrennt:

| Datei | Bestehendes Format und Verhalten |
|---|---|
| `DRZ.txt` | `Time_UTC`, Drehzahl; aktuelle Rohdrehzahl, nominell 10 Hz |
| `SENSOR.txt` | Zehn Spalten: Zeit, Temperatur, Position, Höhe, Geschwindigkeit, Kurs, Genauigkeit, `gpsOK`, `gpsFix3D`; nominell 10 Hz |

Ungültige Messwerte bleiben leer. `Acc_ms-1` enthält tatsächlich horizontale Genauigkeit in Metern. Diese bestehende Benennung sollte nicht beiläufig korrigiert werden.

[log_clock.h](/F:/Development/mofa_tacho/src/log_clock.h:21) verankert eine Hostuhr einmalig auf GNSS-UTC. Danach folgen die Legacy-Zeilen der Hostzeit. Das eignet sich für aktuelle RPM-/Temperaturzeilen, aber nicht unmittelbar für nachträglich abgearbeitete GNSS-Epochen.

Insbesondere darf nach `timeMs(now)` nicht `timeMs(altesReceivedMs)` auf derselben Uhr aufgerufen werden: Die vorzeichenlose Differenz würde als nahezu vollständiger `millis()`-Umlauf interpretiert.

Die UTC jeder GNSS-Epoche muss deshalb bereits im Datensatz gespeichert werden. Vorhandenes `pvtUtc()` prüft Datum, Zeit und Gültigkeitsflags und normalisiert negative Nanosekunden; Schaltsekunden werden abgelehnt. Beleg: [gnss_service.cpp](/F:/Development/mofa_tacho/src/gnss_service.cpp:86). Für Millisekundenauflösung ist `utcSeconds * 1000 + utcNanoseconds / 1000000` geeignet. Ungültige UTC bleibt ausdrücklich ungültig, etwa durch leere UTC-Spalten plus `timeValid=0`. `receivedMs` ist Verarbeitungszeit und kein Ersatz für Messzeit.

Die USB-Seite hat bereits heute relevante Lücken:

- [safeFileWrite()](/F:/Development/mofa_tacho/src/main.cpp:241) gibt keinen Erfolg zurück und prüft weder `fputs()` noch `fclose()`.
- Die Logger-Pufferzähler werden nach jedem Schreibversuch zurückgesetzt.
- Beim Mount werden die beiden Legacy-Dateien geleert.
- Nach fehlgeschlagenem Mount werden trotzdem Kopfzeilenschreibversuche ausgeführt.
- Der Detach-Callback ruft `fs.unmount()` auf, während der Logger ohne gemeinsame Serialisierung schreiben kann.
- Teilgefüllte Puffer besitzen keine explizite Abschlussfrist.

Ein FIFO allein behebt diese Fehler nicht.

**Vergleich der Lösungsvarianten**

| Aspekt | A: FIFO nach bestehender Akzeptanz | B: Roh-PVT-Erfassung über Bibliotheks-Dateipuffer, danach Akzeptanz und FIFO |
|---|---|---|
| Erfasster Umfang | Jede bisher akzeptierte/veröffentlichte Epoche | Zusätzlich einzeln verfügbare PVTs eines Bibliotheksbursts |
| Eingriff | Klein; bestehender Akzeptanzpfad bleibt bestehen | Zusätzlicher UBX-Decoder und Initialisierungs-/Pufferverwaltung |
| Anzeige | Snapshot-Verhalten unverändert | Latest-Value-Schnittstelle kann bleiben; akzeptierte Eingabefolge kann sich ändern |
| Speicher | Vorgeschlagen etwa 22 KiB zusätzlich | Zusätzlich beispielsweise 4096 Byte Rohpuffer und Decoderzustand |
| Verlustnachweis | Ab Akzeptanzgrenze exakt zählbar | Bibliotheksoverflow nicht direkt zuverlässig gezählt; Vorstufen bleiben begrenzt |
| Hauptrisiko | Verwechslung mit vollständiger Empfängererfassung | Veränderte Burst-, Hostzeit- und RateMonitor-Wechselwirkungen |

Ein 4096-Byte-Bibliothekspuffer fasst bei ausschließlich 100-Byte-PVT-Paketen maximal 40 vollständige Nachrichten, entsprechend acht Sekunden bei 5 Hz. Das ist keine USB-Ausfallreserve, solange der GNSS-Thread ihn regelmäßig leert.

Bei Variante B erhalten mehrere nachträglich dekodierte Nachrichten nahezu dieselbe Host-Verarbeitungszeit. Die vorhandenen Zeitplausibilitäts- und Rateprüfungen müssen dann sorgfältig bewertet werden. Ein einfacher Austausch der Eingabeschleife wäre kein nachgewiesener Erhalt des bisherigen Laufzeitverhaltens.

**Empfehlung: Variante A**

Sie erfüllt den ausdrücklich genannten Umfang „alle akzeptierten Epochen“ mit dem kleinsten Eingriff. Variante B ist eine mögliche spätere Erweiterung, falls die Anforderung auf jede korrekt empfangene oder jede erzeugte Empfängerepoche ausgeweitet wird.

Vorgeschlagener Entwurf:

1. `gnssTask()` erzeugt unmittelbar an der bisherigen Veröffentlichung einen unveränderlichen Logdatensatz. Eine bootweit fortlaufende 64-Bit-Akzeptanzsequenz wird auch bei anschließendem FIFO-Überlauf erhöht.
2. Ein separater statischer Ring mit **320 nutzbaren Einträgen** erhält einen eigenen Mutex. Snapshot- und FIFO-Mutex werden nie gleichzeitig gehalten.
3. Unter dem FIFO-Mutex erfolgen ausschließlich Kopieren, Indexänderungen und Statistikzugriffe. Keine Formatierung, Ausgabe, Dateioperation oder Wartephase.
4. `loggingTask()` holt höchstens 20 Einträge je Arbeitsrunde als Kopie. Der FIFO-Anfang bleibt bis zur Ergebnisbewertung reserviert. Nur dieser eine Konsument darf ihn bestätigen.
5. Bei vollem FIFO wird der neue Eintrag verworfen: **Drop-newest**. Bereits gepufferte Reihenfolge und reservierte Schreibblöcke bleiben stabil. Produzent und Anzeige warten nicht auf USB.
6. Nach GNSS-Reconnect wird der FIFO nicht gelöscht. `sessionId`, `sampleId` und Sequenz unterscheiden historische Datensätze vom aktuellen Anzeigenzustand.
7. Der Logger arbeitet Rückstände schneller als mit nur einem Eintrag je 100-ms-Runde ab. Ein begrenztes Arbeitsbudget verhindert, dass Legacy-Logging durch große Rückstände vollständig verdrängt wird.

Ein möglicher 64-Byte-Datensatz besteht aus:

| Felder | Speicher |
|---|---:|
| Akzeptanzsequenz `uint64_t`, UTC-Millisekunden `int64_t` | 16 Byte |
| Sitzung, Sample-ID, iTOW, Empfangszeit, Epochendelta: fünf 32-Bit-Felder | 20 Byte |
| Latitude, Longitude, Höhe MSL, Geschwindigkeit, Kurs, horizontale Genauigkeit in ursprünglichen skalierten Ganzzahleinheiten | 24 Byte |
| Gültigkeitsflags, Fixtyp, Reserve | 4 Byte |
| **Summe** | **64 Byte** |

Die Festlegung auf Ganzzahlfelder vermeidet zusätzliche Rundung und eine Abhängigkeit von `sizeof(time_t)`. Sie ist ein vorgeschlagener Typ, kein gemessenes ABI-Ergebnis.

320 × 64 Byte ergeben **20 480 Byte beziehungsweise 20 KiB**. Bei leerem FIFO sind das 64 Sekunden bei 5 Hz. Mit aktuellem Füllstand `q` beträgt die freie Reserve `(320 − q) / 5` Sekunden.

Zusätzlich sind etwa 1280 Byte für eine statische 20-Einträge-Arbeitskopie, ein begrenzter Formatpuffer sowie Indizes, Zähler und Mutex nötig. Planungswert: **etwa 22 KiB zusätzlich**, zuzüglich nicht bestimmter Bibliotheks-/Dateisystemkosten. Ein großes Arbeitsarray gehört nicht auf den bestehenden 4-KiB-Loggerstack. Exakte Größen und Stackreserven müssen später geprüft werden; alte RAM-/Stackangaben aus `docs/architecture.md` sind dafür kein aktueller Nachweis.

**Logformat und Fehlervertrag**

Ich empfehle eine zusätzliche GNSS-Datei mit versionierter Kopfzeile. Damit bleiben die vorhandenen Zehn-Spalten-SENSOR-Dateien und die RPM-Zeitsemantik kompatibel. Eine Umstellung von `SENSOR.txt` auf Epochentakt würde dessen Rate, Temperaturzuordnung und Verhalten ohne GNSS verändern.

Die neue Datei sollte mindestens enthalten: Sequenz, Sitzung, Sample-ID, iTOW, `receivedMs`, UTC beziehungsweise UTC-Gültigkeit, Positions-/Höhengültigkeit, Fixtyp und GNSS-Messwerte. Eine feste Temperaturzuordnung ist dort nicht erforderlich; das bestehende Temperatur-Snapshot-Logging bleibt bestehen.

Notwendige Statistiken: akzeptiert, erfolgreich geschriebene Einträge, FIFO-Füllstand, Höchststand, Überlaufverluste und nach Schreibfehler unsicher gespeicherte Einträge. Sie müssen seriell sowie bei wieder verfügbarem Medium in der neuen Datei sichtbar sein. Auch abschließende Verluste ohne späteren Datensatz müssen so erkennbar bleiben.

Für USB empfehle ich folgenden ausdrücklichen Fehlervertrag:

- Der Logger besitzt Mount, Unmount und Dateizugriffe allein. Callbacks melden lediglich Ereignisse über geeignete threadsichere Zustände.
- Ohne Medium bleiben Einträge im RAM; bei Überlauf gilt Drop-newest.
- Erfolg setzt geprüfte Öffnung, vollständige Schreiboperationen, Flush und Schließen voraus.
- Bei teilweise fehlgeschlagenem Block ist die tatsächlich gespeicherte Zeilenzahl unbekannt. Ohne zusätzliches Transaktions-/Wiederanlaufprotokoll darf kein „genau einmal“ behauptet werden.
- Als begrenzte Erstlösung wird ein versuchter Fehlerblock vollständig als **Speicherstatus unsicher** gezählt und aus der Warteschlange genommen; er wird nicht automatisch erneut angehängt. Noch unversuchte Einträge bleiben erhalten. Das begrenzt einen einzelnen solchen Unsicherheitsbereich auf höchstens 20 Epochen.
- Wiederaufnahme erfolgt in einem neuen, vorhandene Dateien nicht überschreibenden GNSS-Dateisegment. Dafür ist eine begrenzte, geprüfte Vergabe freier Dateinamen vorzusehen.
- Teilblöcke erhalten eine zeitliche Schreibfrist, beispielsweise eine Sekunde. Sonst können bei ausbleibendem GNSS-Verkehr die letzten Einträge unbegrenzt im RAM verbleiben.

Das ist kontrollierter möglicher Verlust, keine Garantie physischer Dauerhaftigkeit. Stromausfall kann RAM und noch nicht dauerhaft gespeicherte FAT-Daten verlieren.

Eine sinnvolle Bilanz lautet außerhalb einer gerade abgeschlossenen Zustandsänderung:

`akzeptiert = FIFO einschließlich reservierter Einträge + Schreibbestätigungen + Überlaufverluste + unsichere Fehlerblöcke`

„Schreibbestätigung“ bezeichnet dabei den geprüften Dateisystemaufruf, nicht einen unabhängig belegten Flash-Commit des USB-Mediums.

**Konkreter späterer Dateiumfang**

| Datei | Vorgeschlagene Änderung |
|---|---|
| `src/gnss_service.h` | Separater Logdatentyp, begrenzte Lese-/Bestätigungsschnittstelle, Statistiktyp |
| `src/gnss_service.cpp` | Statischer FIFO, eigener Mutex, Sequenzierung, Einreihung an bestehender Akzeptanzstelle |
| Neu: `src/gnss_log_fifo.h` | Kleine hardwareunabhängige Ringrichtlinie für gezielte spätere Prüfungen |
| `src/main.cpp` | FIFO-Konsument, additive GNSS-Datei, geprüfte Schreibresultate, USB-Ereignisbesitz und Fehlerbilanz |
| `docs/architecture.md` | Neuer Datenfluss, Speicherbudget und ausdrücklich begrenzte Verlustgarantien |

`src/gnss_epoch.h`, `src/log_clock.h` und `platformio.ini` benötigen für Variante A voraussichtlich keine Änderung. Bibliotheken werden weder aktualisiert noch gepatcht. Lokale PlatformIO-Konfigurationen bleiben geschützt.

Die Anzeige bleibt bei `readGnssSnapshot()` mit bestehender 1,5-s-Alterung, RTC-Holdover und aktuellen Zuständen. [displayTask()](/F:/Development/mofa_tacho/src/main.cpp:384) und die UI-Bindungen benötigen keine Änderung. Es entstehen keine EEZ-Änderungen und keine manuell zu bearbeitenden generierten Dateien.

**Vorgeschlagener Prüfplan für eine gesondert freizugebende Testphase**

- FIFO-Richtlinie: leer/voll, exakt 320 nutzbare Plätze, Indexumlauf, Reihenfolge, Drop-newest, Reservierung/Bestätigung und Bilanz nach Überlauf.
- Akzeptanzintegration: Baseline, Startbestätigung, Duplikate, alte/ungültige Pakete, Ratefehler, UTC-Ersterwerb und Reconnect; genau ein Einreihungsversuch pro bisher veröffentlichter Epoche.
- Zeitstempel: 200-ms-Folge, Sekunden-/Tageswechsel, negative Nanosekunden, ungültige UTC und `millis()`-Umlauf; Rückstau verändert gespeicherte Epochentimestamps nicht.
- Logger mit kontrollierten Fehlern: fehlendes Medium, Mountfehler, Fehler bei Öffnen/Schreiben/Flush/Schließen, Teilblockfrist, Detach während des Schreibens und Wiederanschluss.
- Kapazität: Rückstau unterhalb der Reserve ohne Verlust; über 64 Sekunden bei zuvor leerem FIFO mit exakt erklärter Verlustzahl; Abbau mit mehr als fünf Einträgen pro Sekunde.
- Formatvergleich: Legacy-Kopfzeilen, Spaltenzahl, leere ungültige Werte und RPM-/Temperatursemantik bleiben erhalten.
- Nach ausdrücklicher Freigabe: `pio run -e giga_r1_m7`, Größen-/Stackprüfung sowie getrennt freizugebende Hardwareprüfung mit realem 5-Hz-GNSS und langsamen/entfernten USB-Medien. Ein Upload ist gesondert freigabepflichtig.

Für mögliche Teständerungen wären neue FIFO-/Logger-Tests und gegebenenfalls Ergänzungen unter `test/gnss_epoch` und `test/log_clock` konkret zu genehmigen. Die vorhandenen Compile-only-Tests werden vom Firmwarebuild nicht ausgeführt.

**Offene Annahmen und Risiken**

- Die Auslegung „akzeptiert = bisher veröffentlicht“ ist die Grundlage dieser Empfehlung. Eine vollständige Roh-PVT-Erfassung wäre ein größerer Auftrag.
- Die additive GNSS-Datei und die vorgeschlagene Behandlung unsicherer Schreibblöcke sind Entwurfsentscheidungen zur Benutzerfreigabe.
- 64 Sekunden sind eine gewählte Reserve; eine verlangte maximale USB-Ausfalldauer wurde nicht genannt.
- Empfängermodell, dessen Ausgabepuffer, reale I²C-Latenzen und garantierte USB-Schreibzeiten sind nicht bestimmt.
- Lokale SparkFun-Version 2.2.29 wurde gelesen; vollständige Übereinstimmung lokaler Bibliotheken mit unveränderten Originalpaketen wurde nicht nachgewiesen.
- Flüchtige Ereignisflags allein sind keine ausreichende Synchronisierungsstrategie. Die genaue Callback-Ausführung und Atomik müssen bei der späteren Umsetzung berücksichtigt werden.
- Änderungen am gemeinsamen USB-Schreibpfad betreffen auch Legacy-Dateien und benötigen deshalb gezielte Regressionstests.
- Keine aktuelle Build-, Last-, Nebenläufigkeits- oder Hardwareverifikation liegt vor.

P1 endet hier. Nächster Freigabepunkt: Benutzerentscheidung über den konkreten P2-Entwurf oder eine gesonderte unabhängige CASE-Prüfung. Keine automatische Fortsetzung.
