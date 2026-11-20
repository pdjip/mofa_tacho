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
