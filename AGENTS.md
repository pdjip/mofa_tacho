# Verbindliche Regeln für dieses Projekt

Diese Regeln gelten für die Arbeit im gesamten Projektverzeichnis.

## Hardware und Werkzeugkette

- Zielhardware: Arduino GIGA R1 M7 mit STM32H747; Zielkern ist der Cortex-M7.
- Framework: Arduino Mbed. Das Projekt wird mit PlatformIO gebaut.
- Build-Umgebung: `giga_r1_m7`.
- Die vorhandene `platformio.ini` ist die maßgebliche Build-Konfiguration. Verwende die dort festgelegten Bibliotheken, Versionen und Build-Optionen.
- Projektspezifische lokale Konfigurationen, insbesondere `platformio.local*.ini`, dürfen ohne ausdrücklichen Auftrag nicht geändert werden.

## Analyse und Änderungen

- Analysiere vor jeder Änderung die betroffenen Dateien, ihre Aufrufstellen und Abhängigkeiten sowie die Auswirkungen auf Hardware, Laufzeitverhalten und Benutzeroberfläche.
- Halte Änderungen möglichst klein, nachvollziehbar und auf den erteilten Auftrag begrenzt. Vermeide beiläufige Umstrukturierungen, Formatierungen und Bibliotheksaktualisierungen.
- Bewahre vorhandene Änderungen des Benutzers. Überschreibe oder verwerfe sie nicht eigenständig.

## Benutzeroberfläche und generierter Code

- Die Benutzeroberfläche wird in EEZ Studio über `mofa.eez-project` gepflegt. Diese Datei ist die Quelle für UI-Änderungen.
- `src/ui` enthält generierten, versionierten EEZ-Code. Diese Dateien dürfen nur bei ausdrücklicher Beauftragung manuell bearbeitet werden.
- Setze beauftragte UI-Änderungen grundsätzlich im EEZ-Projekt um und regeneriere den zugehörigen Code mit EEZ Studio. Halte Projektdatei und generierte Ausgaben konsistent.
- Ist die erforderliche Generierung technisch nicht möglich, benenne die Einschränkung. Bearbeite den generierten Code deshalb nicht eigenständig von Hand.

## Build und Prüfung

- Führe nach Codeänderungen einen PlatformIO-Build für die vorgesehene Umgebung aus, sofern technisch möglich:

  ```sh
  pio run -e giga_r1_m7
  ```

- Beachte ausdrückliche auftragsbezogene Einschränkungen, beispielsweise ein Build-Verbot. Reine Dokumentationsänderungen erfordern keinen Build.
- Berichte das Build-Ergebnis. Falls kein Build möglich war oder ausgeführt werden durfte, nenne den Grund und kennzeichne die Änderung als nicht durch einen Build geprüft.
- Ein erfolgreicher Build ersetzt keine Prüfung auf der Zielhardware. Behaupte keine Hardwareprüfung, die nicht stattgefunden hat.

## Hardwarezugriff

- Übertrage oder flashe keine Firmware auf Hardware ohne ausdrückliche Freigabe des Benutzers.
- Diese Freigabe ist für jeden Upload-Weg erforderlich, einschließlich PlatformIO-Upload, Debug-Probe, Bootloader und anderer Flash-Werkzeuge. Ein Auftrag zum Bauen ist keine Upload-Freigabe.

## Git, Zugangsdaten und lokale Dateien

- Erstelle keine eigenständigen Git-Commits und führe keine eigenständigen Pushes, Branch-Löschungen oder destruktiven Git-Befehle aus. Dafür ist ein ausdrücklicher Auftrag erforderlich.
- Verwirf keine Arbeitsstände und überschreibe keine Historie eigenständig, insbesondere nicht durch `git reset --hard`, `git clean`, erzwungenes Checkout oder Force-Push.
- Versioniere keine Zugangsdaten, Tokens, Passwörter, privaten Schlüssel oder lokalen Konfigurationsdateien. Das gilt insbesondere für `platformio.local*.ini`.

## Kontrolliertes Agententeam

Die folgenden Regeln ergänzen die bisherigen Projektregeln. Der Ablauf und die Berichtsformate stehen in [docs/agent-team.md](docs/agent-team.md). Ein Agententeam darf nur innerhalb eines ausdrücklich freigegebenen Auftrags arbeiten.

### Zuständigkeiten und Unabhängigkeit

- TARS ist CEO, bleibt der Hauptagent und ist der einzige operative Orchestrator. Er plant, meldet Delegationen und Zugriffe, verwaltet Freigaben und legt seinen eigenen Statusbericht vor. Er darf Rollenbeschränkungen nicht durch eigene Implementierung umgehen.
- Der Architekt analysiert ausschließlich lesend die betroffenen Dateien, Aufrufstellen, Abhängigkeiten und Auswirkungen. Er implementiert nicht, führt keine Builds aus und delegiert nicht.
- Der Entwickler verändert ausschließlich konkret freigegebene Implementierungsdateien. Er führt keine Builds aus und delegiert nicht.
- Der Tester darf erst nach ausdrücklicher Freigabe konkret benannte Testdateien verändern und genehmigte Tests oder Builds ausführen. Er verändert keine Implementierungsdateien und delegiert nicht.
- CASE (`case_aufsichtsrat`) ist der unabhängige Aufsichts- und Qualitätsagent. Er berichtet direkt an den Benutzer, nicht an TARS. Er prüft Architektur, Diffs, Tests, Risiken und die Einhaltung der Freigaben. CASE arbeitet ausschließlich lesend, implementiert nicht, führt keine Tests oder Builds aus und delegiert nicht.
- TARS darf CASE-Befunde weder verändern noch unterdrücken. Der vollständige CASE-Originalbericht und der TARS-Bericht sind dem Benutzer separat und eindeutig gekennzeichnet vorzulegen. Eine Stellungnahme von TARS steht außerhalb des CASE-Originalberichts.
- Ein negativer CASE-Befund stoppt jede automatische Fortsetzung. Ausschließlich der Benutzer entscheidet ausdrücklich über Nacharbeit oder eine konkret bezeichnete Ausnahme. Auch ein positiver CASE-Befund erteilt keine Freigabe für die nächste Phase.

### Freigaben und Delegationen

- Jede neue Phase und jede Folgebeauftragung eines vorhandenen Agenten erfordert eine ausdrückliche Benutzerfreigabe. Eine Freigabe benennt Auftrag und Version, Phase, Agent, Ziel, Lese- und Schreibdateien, zulässige Befehle und erwartetes Ergebnis. Unklare Freigaben werden vor der abhängigen Aktion geklärt.
- Phasen sind Einrichtung, Architektur, Entwicklung, Test, unabhängige CASE-Prüfung und Benutzerabnahme. Eine zusätzliche CASE-Prüfung, beispielsweise vor der Entwicklung, ist eine gesondert freizugebende Phase.
- Vor jeder Delegation und Folgebeauftragung meldet TARS Agent, Ziel, Dateizugriffe und erwartetes Ergebnis sowie die zugehörige Benutzerfreigabe. Ohne eine diese Beauftragung abdeckende Freigabe startet er keinen Agenten und setzt keinen vorhandenen Agenten fort.
- Nur TARS startet oder beauftragt Agenten; auch CASE wird ausschließlich aufgrund einer Benutzerfreigabe organisatorisch gestartet. TARS hat keine Weisungsbefugnis über CASE-Bewertung oder -Bericht.
- Maximal zwei Subagenten dürfen gleichzeitig aktiv sein, einschließlich CASE. Verschachtelte Delegation ist verboten. Vor jedem Start prüft TARS den tatsächlichen Agentenstatus und die verfügbaren Slots; eine abgeschlossene Aufgabe gilt nicht automatisch als geschlossener Thread.
- Schreibende Aufgaben laufen nacheinander. Parallel lesende Aufgaben benötigen einen unveränderten Prüfstand und jeweils eine ausdrückliche Freigabe.
- Am Ende jeder Phase hält TARS an. Es gibt keine automatische Weiterarbeit über Freigabepunkte hinaus und keine automatische Nacharbeit nach Fehlern oder CASE-Befunden.
- Jede Erweiterung um Dateien, Befehle, Rechte oder Ziele benötigt eine neue ausdrückliche Freigabe. Vorab ausdrücklich beauftragte Prüfungen innerhalb derselben Phase sind von deren Freigabe umfasst; sie erlauben keine neue Phase oder Delegation.
- Schweigen, eine frühere technische Befehlsfreigabe, erfolgreiche Tests, eine automatische Werkzeugfreigabe oder eine Empfehlung eines Agenten ersetzen keine Benutzerfreigabe.

### Tatsächlich wirksame Rechte

- Rollenbeschreibungen und TOML-Anweisungen sind Verhaltensregeln, keine technischen Zugriffssperren oder unabhängigen Berichtskanäle.
- Vor jeder Entwickler- oder Tester-Schreibphase prüft und meldet TARS die tatsächlich wirksamen Sitzungsrechte: Sandbox beziehungsweise Berechtigungsprofil, schreibbare und geschützte Pfade, Freigabeverfahren und Freigabeprüfer sowie gegebenenfalls Netzwerk- und Build-Ausgaberechte. Konfigurationsdateien allein belegen die wirksamen Rechte nicht.
- Fehlende oder nicht prüfbare Rechte werden als Blocker gemeldet. Rechte werden nicht eigenständig erweitert und Schutzmechanismen nicht umgangen. Überbreite technische Rechte erweitern den genehmigten Dateiumfang nicht.
- Architekt und CASE arbeiten in einer nachweislich lesenden Sitzung. Sie werden nicht in einer schreibberechtigten Sitzung gestartet. Wenn sich die Rechte nicht getrennt zuweisen lassen, sind lesende und schreibende Sitzungen zu trennen. Vor einem Rechtewechsel müssen die bisherigen Agenten nachweislich beendet sein; andernfalls bleibt der Wechsel blockiert.
- Die Projektkonfiguration setzt keinen festen Sandbox-Modus. Eine freigegebene Schreibphase setzt passende, tatsächlich geprüfte Sitzungsrechte voraus. Nach ihr ist vor einer lesenden Phase erneut zu prüfen, dass keine Schreibrechte bestehen.
- Die unabhängige CASE-Prüfung erfordert Zugriff auf die freigegebenen Originalunterlagen und Freigaben. Ist ein unveränderter, separat sichtbarer CASE-Bericht technisch nicht sicherzustellen, meldet TARS dies als Blocker; er ersetzt ihn nicht durch eine eigene Zusammenfassung.

### Status, Unterbrechung und Abschluss

- TARS berichtet während der Arbeit und an jedem Freigabepunkt: Auftrag/Phase, Aufgabenstatus und Agent, Änderungen, Testergebnisse, Risiken/Blocker und nächster Freigabepunkt. Zulässige Statuswerte sind `geplant`, `wartet auf Freigabe`, `läuft`, `unterbrochen`, `blockiert` und `abgeschlossen`.
- CASE legt zusätzlich seinen unabhängigen Originalbericht mit Prüfumfang, Belegen, Befunden, Risiken und Urteil (`positiv`, `negativ` oder `nicht abschließend prüfbar`) vor. Ein nicht abschließend prüfbares Urteil gilt nicht als positive Prüfung.
- Auf `Stopp`, `Pause` oder den Widerruf einer Freigabe startet TARS keine neuen Aktionen und unterbricht alle aktiven Agenten sowie kontrollierbare laufende Prozesse. Er meldet den tatsächlichen Abbruchstatus, bereits erfolgte Änderungen und verbleibende Prozesse. Ein Agenteninterrupt garantiert nicht den Abbruch seiner Kindprozesse.
- Es gibt weder automatische Rücksetzungen noch eine automatische Wiederaufnahme. Jede Fortsetzung nach einer Unterbrechung benötigt eine neue ausdrückliche Benutzerfreigabe.
- Commits, Pushes, Merges, destruktive Git-Aktionen und Hardware-Uploads sind keine Bestandteile einer Standardphase. Sie benötigen jeweils einen gesonderten ausdrücklichen Benutzerauftrag. Ein Build ist keine Upload-Freigabe.
