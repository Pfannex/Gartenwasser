/**
 * @file    Logger.h
 * @brief   Einheitliches Log-Format fuer alle Subsysteme.
 *
 * Zeilenformat: hh:mm:ss:mmm CLASS TYPE logtext
 * hh:mm:ss:mmm ist boot-relative Laufzeit (millis()-basiert), bis enableRealTime()
 * nach erfolgreicher NTP-Synchronisierung auf Echtzeit umschaltet.
 */

#pragma once

#include <cstddef>
#include <cstdint>

class Logger {
 public:
  enum class Type : uint8_t { ERROR, INFO, DEBUG, PUB, SUB };
  // VALVE (2026-08-18 ergaenzt): tatsaechliche Ventilschaltungen (ValveController/ValveTimer) -
  // vorher faelschlich unter I2C mitgelaufen, das jetzt ausschliesslich Bus-Gesundheit meint
  // (Scan, MCP23017-Erreichbarkeit). SEQ: Automatik-Sequenz-Lebenszyklus (Sequencer), vorher
  // komplett ungeloggt.
  enum class Source : uint8_t { WIFI, MQTT, I2C, HMI, WEB, SYSTEM, VALVE, SEQ };

  using ErrorCallback = void (*)(const char *message);
  using LineCallback = void (*)(const char *line);

  // Maximale Laenge einer formatierten Log-Zeile (inkl. Zeitstempel/CLASS/TYPE-Praefix,
  // exkl. Nullterminator) - oeffentlich, damit MqttManager seinen Live-Log-Ringpuffer
  // (ein `char[kMaxLineLength]` pro Zeile) garantiert genauso gross dimensioniert wie das,
  // was hier tatsaechlich hineinpasst (Nachtrag 2026-08-18: vorher zwei unabhaengige "224"-
  // Literale in Logger.cpp/MqttManager.cpp, nur zufaellig gleich - siehe Log.md).
  static constexpr size_t kMaxLineLength = 560;

  Logger() = delete;

  static void log(Type type, Source source, const char *message);
  static void logf(Type type, Source source, const char *format, ...);

  /// Schaltet den Zeitstempel von boot-relativer Zeit auf Echtzeit um (nach erfolgreicher NTP-Synchronisierung).
  static void enableRealTime();

  /// Liefert true, wenn die Systemzeit per NTP synchronisiert ist (siehe enableRealTime()).
  /// Fuer Code, der echtes Datum/Uhrzeit braucht (z.B. Scheduler, Phase 15) statt nur eines
  /// Log-Zeitstempels, der auch boot-relativ sinnvoll ist.
  static bool isRealTimeEnabled();

  /// Formatiert den aktuellen Zeitstempel (wie am Anfang jeder Logzeile) nach `buffer`.
  static void currentTimestamp(char *buffer, size_t bufferSize);

  /// Registriert einen Callback, der bei jedem ERROR-Log-Eintrag mit der reinen
  /// (unformatierten) Meldung aufgerufen wird - Grundlage fuer diagnostics/lastError
  /// (Diagnostics), ohne dass Logger die Diagnostics-Klasse kennen muss.
  static void setErrorCallback(ErrorCallback callback);

  /// Registriert einen Callback, der bei JEDER Log-Zeile (inkl. PUB/SUB) mit der komplett
  /// formatierten Zeile (wie auf Serial) aufgerufen wird - Grundlage fuer diagnostics/livelog
  /// (MqttManager), ohne dass Logger MqttManager kennen muss. Keine Rueckkopplungsgefahr durch
  /// das Weiterleiten selbst, solange der Callback dafuer rohes MQTT-Publish statt einer
  /// geloggten Publish-Funktion nutzt (2026-08-18 klargestellt, siehe MqttManager::
  /// onLoggerLine()). Zu haeufige Einzeltopics (z.B. sekuendliche Restlaufzeit-Ticks) filtert
  /// bei Bedarf gezielt der Aufrufer selbst.
  static void setLineCallback(LineCallback callback);
};
