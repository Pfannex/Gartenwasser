/**
 * @file    ConfigStore.h
 * @brief   Persistenz der Einstellwerte (config.json: Ventil-Laufzeiten/Automatik/Alias/maxTime;
 *          programs.json: Bewaesserungsprogramme, Phase 14) im SPIFFS. Siehe docs/requirements.md,
 *          Abschnitt "Konfiguration" (config/programs/schedule als getrennte Bereiche).
 */

#pragma once

#include <cstddef>
#include <cstdint>

class ConfigStore {
 public:
  /// Maximale Laenge (ohne Nullterminator) fuer einen Ventil-Alias bzw. Programmnamen.
  static constexpr size_t kAliasMaxLength = 32;

  /// Puffer-/Speicherpool-Groesse fuer die config-JSON (SPIFFS-Persistenz und
  /// main/config/set|state teilen sich diese Konstante).
  static constexpr size_t kJsonCapacity = 768;

  /// Maximale Anzahl Bewaesserungsprogramme (Phase 14).
  static constexpr uint8_t kMaxPrograms = 8;

  /// Puffer-/Speicherpool-Groesse fuer die programs-JSON (SPIFFS-Persistenz und
  /// main/programs/set|state teilen sich diese Konstante).
  static constexpr size_t kProgramsJsonCapacity = 2048;

  /// Ein (Teil-)Eintrag zum Setzen eines Programms ueber setPrograms(). `time`/`autoFlag`
  /// sind nur gueltig, wenn das zugehoerige `timeSet[i]`/`autoSet[i]` gesetzt ist (Index 1..5,
  /// 0 unbenutzt) - Programme sind Teilmengen wie main/config/set, siehe docs/spec/14-programme.md.
  struct ProgramInput {
    const char *name;
    uint8_t shortcut;  ///< 0 = keiner, 1..4 = P1..P4 (Touch-UI-Button-Bindung, Phase 13).
    uint16_t time[6];
    bool timeSet[6];
    bool autoFlag[6];
    bool autoSet[6];
  };

  ConfigStore() = delete;

  /// Mountet SPIFFS und laedt /config.json + /programs.json (falls vorhanden), sonst gelten Defaultwerte.
  static void begin();

  /// Konfigurierte Laufzeit in Minuten fuer Ventil `index` (1..5).
  static uint16_t getValveTime(uint8_t index);
  static void setValveTime(uint8_t index, uint16_t minutes);

  /// Globale Obergrenze in Minuten (main/time/maxTime, effektive Laufzeit = min(time, maxTime)).
  static uint16_t getMaxTime();
  static void setMaxTime(uint16_t minutes);

  /// Automatik-Flag fuer Ventil `index` (1..5): Teilnahme an der Automatik-Sequenz (Phase 7).
  static bool getValveAuto(uint8_t index);
  static void setValveAuto(uint8_t index, bool on);

  /// Klartextname (Alias) fuer Ventil `index` (0..5, inkl. V0/Hauptventil). Leerer String,
  /// falls nicht gesetzt. `alias` wird auf kAliasMaxLength Zeichen gekuerzt uebernommen
  /// (Aufrufer validiert vorher).
  static const char *getValveAlias(uint8_t index);
  static void setValveAlias(uint8_t index, const char *alias);

  /// Serialisiert die aktuelle Gesamt-Konfiguration (time/auto/alias/maxTime) als JSON
  /// nach `buffer` (dieselbe Struktur wie /config.json). Liefert die Anzahl geschriebener
  /// Bytes (wie serializeJson()), oder 0 bei Fehler.
  static size_t toJson(char *buffer, size_t bufferSize);

  /// Ersetzt das komplette Programme-Array (main/programs/set, Key "programs") und speichert
  /// sofort. Mehr als kMaxPrograms Eintraege werden ignoriert + geloggt.
  static void setPrograms(const ProgramInput *entries, uint8_t count);

  /// Anzahl aktuell gespeicherter Programme.
  static uint8_t getProgramCount();

  /// Programmname, 1-basierter Index (1..getProgramCount()). Leerer String bei ungueltigem Index.
  static const char *getProgramName(uint8_t programIndex);

  /// Ob Programm `programIndex` (1-basiert) einen Laufzeit-/Automatik-Wert fuer Ventil
  /// `valveIndex` (1..5) enthaelt, bzw. dessen Wert. Ungueltige Indizes liefern false/0.
  static bool programHasTime(uint8_t programIndex, uint8_t valveIndex);
  static uint16_t getProgramTime(uint8_t programIndex, uint8_t valveIndex);
  static bool programHasAuto(uint8_t programIndex, uint8_t valveIndex);
  static bool getProgramAuto(uint8_t programIndex, uint8_t valveIndex);

  /// Aktuell gewaehltes Programm, 1-basiert (0 = keins). Persistiert in /programs.json.
  static uint8_t getActiveProgram();
  static void setActiveProgram(uint8_t programIndex);

  /// Programm-Index (1-basiert), dessen `shortcut`-Feld dem physischen Button `shortcut`
  /// (1..4 fuer P1..P4) entspricht - bei Duplikaten gewinnt der erste Treffer in
  /// Array-Reihenfolge (siehe docs/spec/14-programme.md, Kernentscheidung 8). 0, wenn
  /// kein Programm diesen Shortcut hat.
  static uint8_t getProgramIndexForShortcut(uint8_t shortcut);

  /// Serialisiert programs+activeProgram als JSON (main/programs/state-Struktur). Liefert
  /// die Anzahl geschriebener Bytes (wie serializeJson()), oder 0 bei Fehler.
  static size_t programsToJson(char *buffer, size_t bufferSize);
};
