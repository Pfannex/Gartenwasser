#include "HmiManager.h"

#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <Wire.h>
#include <lvgl.h>

#include "ConfigStore.h"
#include "Diagnostics.h"
#include "Logger.h"
#include "MqttManager.h"
#include "Sequencer.h"
#include "ValveController.h"
#include "ValveTimer.h"
#include "esp_lcd_touch_axs5106l.h"

namespace {

// --- Pin-Definitionen ------------------------------------------------------
constexpr int kBacklightPin = 23;
constexpr int kRotation = 2;
constexpr int kTouchSda = 18;
constexpr int kTouchScl = 19;
constexpr int kTouchRst = 20;
constexpr int kTouchInt = 21;

constexpr int16_t kDisplayWidth = 172;
constexpr int16_t kDisplayHeight = 320;

// --- Display (SPI/ST7789) ---------------------------------------------------
Arduino_DataBus *bus = new Arduino_HWSPI(15 /* DC */, 14 /* CS */, 1 /* SCK */, 2 /* MOSI */);
Arduino_GFX *gfx = new Arduino_ST7789(bus, 22 /* RST */, 0 /* rotation */, false /* IPS */,
                                      kDisplayWidth, kDisplayHeight, 34, 0, 34, 0);

// --- LVGL Display-Puffer & Treiber -----------------------------------------
lv_disp_draw_buf_t draw_buf;
lv_color_t disp_draw_buf[kDisplayWidth * 40];
lv_disp_drv_t disp_drv;
lv_indev_drv_t indev_drv;

/**
 * @brief Initialisiert die Display-Register des ST7789 per Batch-Kommando.
 * @note  Herstellerspezifische Registerkonfiguration, 1:1 aus basic-pio übernommen.
 */
void lcdRegInit() {
  static const uint8_t init_operations[] = {
    BEGIN_WRITE,
    WRITE_COMMAND_8, 0x11,  // Sleep Out (kein weiterer Argument, 120ms Delay nötig)
    END_WRITE,
    DELAY, 120,

    BEGIN_WRITE,
    WRITE_C8_D16, 0xDF, 0x98, 0x53,  // Herstellerspezifisch (Init-Sequenz)
    WRITE_C8_D8, 0xB2, 0x23,

    WRITE_COMMAND_8, 0xB7,  // Gamma-Einstellung
    WRITE_BYTES, 4,
    0x00, 0x47, 0x00, 0x6F,

    WRITE_COMMAND_8, 0xBB,  // VCOM-Einstellungen
    WRITE_BYTES, 6,
    0x1C, 0x1A, 0x55, 0x73, 0x63, 0xF0,

    WRITE_C8_D16, 0xC0, 0x44, 0xA4,  // Power Control
    WRITE_C8_D8, 0xC1, 0x16,

    WRITE_COMMAND_8, 0xC3,  // Power Control 2
    WRITE_BYTES, 8,
    0x7D, 0x07, 0x14, 0x06, 0xCF, 0x71, 0x72, 0x77,

    WRITE_COMMAND_8, 0xC4,  // Power Control 3
    WRITE_BYTES, 12,
    0x00, 0x00, 0xA0, 0x79, 0x0B, 0x0A, 0x16, 0x79, 0x0B, 0x0A, 0x16, 0x82,

    WRITE_COMMAND_8, 0xC8,  // Gamma-Kurve (positive + negative, 16 Werte je)
    WRITE_BYTES, 32,
    0x3F, 0x32, 0x29, 0x29, 0x27, 0x2B, 0x27, 0x28,
    0x28, 0x26, 0x25, 0x17, 0x12, 0x0D, 0x04, 0x00,
    0x3F, 0x32, 0x29, 0x29, 0x27, 0x2B, 0x27, 0x28,
    0x28, 0x26, 0x25, 0x17, 0x12, 0x0D, 0x04, 0x00,

    WRITE_COMMAND_8, 0xD0,  // Power-Sequenz
    WRITE_BYTES, 5,
    0x04, 0x06, 0x6B, 0x0F, 0x00,

    WRITE_C8_D16, 0xD7, 0x00, 0x30,
    WRITE_C8_D8, 0xE6, 0x14,
    WRITE_C8_D8, 0xDE, 0x01,  // Page-Switch (interne Register-Bank)

    WRITE_COMMAND_8, 0xB7,
    WRITE_BYTES, 5,
    0x03, 0x13, 0xEF, 0x35, 0x35,

    WRITE_COMMAND_8, 0xC1,
    WRITE_BYTES, 3,
    0x14, 0x15, 0xC0,

    WRITE_C8_D16, 0xC2, 0x06, 0x3A,
    WRITE_C8_D16, 0xC4, 0x72, 0x12,
    WRITE_C8_D8, 0xBE, 0x00,
    WRITE_C8_D8, 0xDE, 0x02,

    WRITE_COMMAND_8, 0xE5,  // TE-Einstellungen (Page 2)
    WRITE_BYTES, 3,
    0x00, 0x02, 0x00,

    WRITE_COMMAND_8, 0xE5,
    WRITE_BYTES, 3,
    0x01, 0x02, 0x00,

    WRITE_C8_D8, 0xDE, 0x00,  // Zurück zu Page 0
    WRITE_C8_D8, 0x35, 0x00,  // Tearing Effect Line: ON
    WRITE_C8_D8, 0x3A, 0x05,  // Pixel Format: RGB565 (16-Bit)

    WRITE_COMMAND_8, 0x2A,  // Column Address Set
    WRITE_BYTES, 4,
    0x00, 0x22, 0x00, 0xCD,  //  → Spalten 34 bis 205 (172px mit Offset)

    WRITE_COMMAND_8, 0x2B,  // Row Address Set
    WRITE_BYTES, 4,
    0x00, 0x00, 0x01, 0x3F,  //  → Zeilen 0 bis 319 (320px)

    WRITE_C8_D8, 0xDE, 0x02,
    WRITE_COMMAND_8, 0xE5,
    WRITE_BYTES, 3,
    0x00, 0x02, 0x00,

    WRITE_C8_D8, 0xDE, 0x00,
    WRITE_C8_D8, 0x36, 0x00,  // Memory Access Control
    WRITE_COMMAND_8, 0x21,    // Display Inversion ON
    END_WRITE,

    DELAY, 10,

    BEGIN_WRITE,
    WRITE_COMMAND_8, 0x29,  // Display ON
    END_WRITE
  };

  bus->batchOperation(init_operations, sizeof(init_operations));
}

/**
 * @brief LVGL Flush-Callback: Überträgt einen Bildbereich vom LVGL-Puffer auf das Display.
 */
void dispFlush(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_p) {
  const uint32_t w = (area->x2 - area->x1 + 1);
  const uint32_t h = (area->y2 - area->y1 + 1);
  gfx->draw16bitRGBBitmap(area->x1, area->y1, (uint16_t *)&color_p->full, w, h);
  lv_disp_flush_ready(drv);
}

/**
 * @brief LVGL Touchpad-Callback: Liest die aktuellen Berührungskoordinaten.
 */
void touchpadRead(lv_indev_drv_t *drv, lv_indev_data_t *data) {
  touch_data_t touch_data;
  bsp_touch_read();

  if (bsp_touch_get_coordinates(&touch_data)) {
    data->point.x = touch_data.coords[0].x;
    data->point.y = touch_data.coords[0].y;
    data->state = LV_INDEV_STATE_PRESSED;
  } else {
    data->state = LV_INDEV_STATE_RELEASED;
  }
}

// --- Touch-UI: Start/Stop-Toggle & Ventil-Statusanzeige ---------------------
// Liest den Zustand direkt aus ValveController/Sequencer (kein MQTT-Umweg fuer
// die lokale Anzeige); der Toggle-Button loest ueber MqttManager::requestMainCmd()
// denselben Pfad wie main/cmd per MQTT aus (inkl. aller Publishes, keine
// Sonderlogik), siehe docs/spec/13-touch-ui.md.

constexpr uint8_t kValveCount = 6;  // V0..V5, siehe ValveController::kValveCount

// Ventil-Statusmatrix (Touch-UI-Neugestaltung): 4x4 Anzeigefelder, V0..V5 belegen die ersten 6
// (zeilenweise), der Rest bleibt als reiner Platzhalter sichtbar - Testaufbau fuer
// eine spaetere Erweiterung auf 16 Ventile (volle MCP23017-Kapazitaet), siehe
// docs/spec/13-touch-ui.md. V1..V5 sind per Tap direkt schaltbar (V{n}/cmd,
// siehe valveCellEventHandler()); V0 hat wie bei MQTT keinen eigenen cmd
// (Kopplung an V1-V5, siehe MqttManager) und bleibt ohne Funktion.
constexpr uint8_t kMatrixCols = 4;
constexpr uint8_t kMatrixRows = 4;
constexpr uint8_t kMatrixCellCount = kMatrixCols * kMatrixRows;

constexpr unsigned long kUiRefreshIntervalMs = 250;

// Layout: graue Titelzeile + START/STOP-Button oben, darunter die Ventil-Statusmatrix,
// direkt darunter der Programme-Button (zeigt das aktive Programm als Buttontext), den
// Rest nimmt die zweizeilige Statuszeile als Fussleiste ein (Box-Position fix an der
// unteren Displaykante, nur der Text darin sitzt hoeher - siehe statusLine1/2 in setupUi()).
constexpr lv_coord_t kHeaderHeight = 26;
constexpr lv_coord_t kMainButtonY = 30;
constexpr lv_coord_t kMainButtonHeight = 34;
constexpr lv_coord_t kMatrixTopY = 70;
constexpr lv_coord_t kMatrixCellSize = 38;
constexpr lv_coord_t kMatrixCellGap = 4;
constexpr lv_coord_t kMatrixLeftX = 4;
constexpr lv_coord_t kProgramsButtonY = 236;
constexpr lv_coord_t kProgramsButtonHeight = 34;  // gleichmaessig mit START aufgeteilt
constexpr lv_coord_t kStatusBoxHeight = 42;  // START/Programme leicht gekuerzt (37->34), damit die
                                              // Statuszeile mehr Hoehe fuer weiter oben sitzenden
                                              // Text hat, ohne den Text stark ins Negative zu schieben.
constexpr lv_coord_t kStatusBoxBottomMargin = 4;  // Abstand zur unteren Displaykante
constexpr lv_coord_t kSideMargin = 8;  // volle Breite = kDisplayWidth - kSideMargin

lv_obj_t *mainScreen = nullptr;
lv_obj_t *mainButton = nullptr;
lv_obj_t *mainButtonLabel = nullptr;
lv_obj_t *valveCells[kMatrixCellCount] = {nullptr};
lv_obj_t *valveCellLabels[kMatrixCellCount] = {nullptr};
lv_obj_t *programsButton = nullptr;
lv_obj_t *programsButtonLabel = nullptr;
lv_obj_t *statusBox = nullptr;
lv_obj_t *statusLine1 = nullptr;
lv_obj_t *statusLine2 = nullptr;
unsigned long lastUiRefreshMs = 0;

// Programme-Unterseite (Touch-UI-Neugestaltung): eigener LVGL-Screen zum Durchblaettern aller
// Programme (</>), OK wendet nur an (identisch main/program/cmd), startet nichts -
// siehe docs/spec/13-touch-ui.md.
lv_obj_t *programScreen = nullptr;
lv_obj_t *programNameLabel = nullptr;
uint8_t browseProgramIndex = 0;  // 0 = "Kein Programm", 1..getProgramCount() = Programm-Index

// Der eingebaute LVGL-Font (Montserrat) enthaelt keine Umlaute - fuer die
// lokale Anzeige auf ASCII transliterieren. Die eigentlichen Alias-Werte
// (MQTT/ConfigStore) bleiben unveraendert UTF-8, das betrifft nur das Display.
void toDisplayAscii(const char *utf8, char *out, size_t outSize) {
  size_t o = 0;
  for (size_t i = 0; utf8[i] != '\0' && o + 1 < outSize;) {
    const unsigned char c0 = static_cast<unsigned char>(utf8[i]);
    if (c0 == 0xC3 && utf8[i + 1] != '\0') {
      const unsigned char c1 = static_cast<unsigned char>(utf8[i + 1]);
      const char *replacement = nullptr;
      switch (c1) {
        case 0xA4: replacement = "ae"; break;  // ä
        case 0xB6: replacement = "oe"; break;  // ö
        case 0xBC: replacement = "ue"; break;  // ü
        case 0x84: replacement = "Ae"; break;  // Ä
        case 0x96: replacement = "Oe"; break;  // Ö
        case 0x9C: replacement = "Ue"; break;  // Ü
        case 0x9F: replacement = "ss"; break;  // ß
        default: break;
      }
      if (replacement != nullptr) {
        for (size_t r = 0; replacement[r] != '\0' && o + 1 < outSize; r++) {
          out[o++] = replacement[r];
        }
        i += 2;
        continue;
      }
    }
    out[o++] = utf8[i];
    i++;
  }
  out[o] = '\0';
}

// Faerbt einen Button beim Antippen (LV_STATE_PRESSED) hell ein, unabhaengig von seiner
// sonstigen (teils dynamisch wechselnden) Hintergrundfarbe - reines visuelles Feedback,
// damit auf einen Blick erkennbar ist, ob der Touch getroffen hat (Nutzer-Feedback
// 2026-08-17: "dann sieht man ob man den Button erwischt hat").
void addPressHighlight(lv_obj_t *btn) {
  lv_obj_set_style_bg_color(btn, lv_color_hex(0xFFFFFF), LV_STATE_PRESSED);
}

// Ventil-Matrixzelle (V1..V5): schaltet direkt per V{n}/cmd (siehe
// MqttManager::requestValveCmd()) - identisches Verhalten wie MQTT (waehrend die
// Automatik laeuft, wird ein manuelles ON ignoriert, siehe applyValveCmd()). V0
// bekommt bewusst keinen Handler (kein eigener cmd, siehe kMatrixCols-Kommentar).
void valveCellEventHandler(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
    return;
  }
  const uint8_t index = static_cast<uint8_t>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(e)));
  const bool targetOn = !ValveController::getValve(index);
  Logger::logf(Logger::Type::INFO, Logger::Source::HMI, "Touch: V%u manuell %s", index, targetOn ? "EIN" : "AUS");
  MqttManager::requestValveCmd(index, targetOn);
}

// Ohne gewaehltes Programm ist der Button laut refreshMainButton() bereits gesperrt
// (LV_STATE_DISABLED + nicht klickbar) - dieser Handler feuert dann gar nicht erst,
// braucht also keinen eigenen Guard mehr (Nachtrag 2026-08-18, vorher: transienter
// "Kein Programm vorgewaehlt!"-Hinweis statt Sperren).
void mainButtonEventHandler(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
    return;
  }
  const bool starting = !Sequencer::isRunning();
  Logger::log(Logger::Type::INFO, Logger::Source::HMI, starting ? "Touch: START gedrueckt" : "Touch: STOP gedrueckt");
  MqttManager::requestMainCmd(starting);
}

void refreshProgramNameLabel() {
  char nameAscii[40];
  if (browseProgramIndex == 0) {
    lv_label_set_text(programNameLabel, "Kein Programm");
    return;
  }
  toDisplayAscii(ConfigStore::getProgramName(browseProgramIndex), nameAscii, sizeof(nameAscii));
  lv_label_set_text(programNameLabel, nameAscii);
}

// Programme-Button (Hauptseite): oeffnet die Unterseite, startend beim aktuell aktiven
// Programm (0 = "Kein Programm"), damit </> von dort aus weiterblaettern.
void programsButtonEventHandler(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
    return;
  }
  browseProgramIndex = ConfigStore::getActiveProgram();
  refreshProgramNameLabel();
  lv_scr_load(programScreen);
}

void programPrevButtonEventHandler(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
    return;
  }
  const uint8_t count = ConfigStore::getProgramCount();
  browseProgramIndex = (browseProgramIndex == 0) ? count : browseProgramIndex - 1;
  refreshProgramNameLabel();
}

void programNextButtonEventHandler(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
    return;
  }
  const uint8_t count = ConfigStore::getProgramCount();
  browseProgramIndex = (browseProgramIndex >= count) ? 0 : browseProgramIndex + 1;
  refreshProgramNameLabel();
}

// OK: wendet nur das durchblaetterte Programm an (identisch main/program/cmd), startet
// nichts - der Start bleibt bewusst ein separater Schritt ueber START auf der Hauptseite.
void programOkButtonEventHandler(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
    return;
  }
  Logger::logf(Logger::Type::INFO, Logger::Source::HMI, "Touch: Programm-Auswahl Index %u bestaetigt",
               browseProgramIndex);
  MqttManager::requestProgramSelect(browseProgramIndex);
  lv_scr_load(mainScreen);
}

void programCancelButtonEventHandler(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
    return;
  }
  lv_scr_load(mainScreen);
}

void refreshMainButton() {
  const bool running = Sequencer::isRunning();
  lv_label_set_text(mainButtonLabel, running ? "STOP" : "START");
  lv_obj_set_style_bg_color(mainButton, running ? lv_color_hex(0xAA0000) : lv_color_hex(0x008800), 0);

  // Ohne gewaehltes Programm kann START ohnehin nicht wirken (siehe
  // MqttManager::startSequence(), Guard activeProgram==0) - Button daher gesperrt statt
  // nur per Hinweis zu erklaeren, warum ein Tap nichts bewirkt (Nachtrag 2026-08-18, analog
  // zum Web-Dashboard). STOP bleibt davon unberuehrt (running-Check zuerst), eine waehrend
  // einer laufenden Sequenz durch main/config/set ausgeloeste MANUELL-Ruecksetzung (siehe
  // MqttManager::publishConfigStateAndClearProgram()) darf STOP nicht sperren.
  if (!running && ConfigStore::getActiveProgram() == 0) {
    lv_obj_add_state(mainButton, LV_STATE_DISABLED);
    lv_obj_clear_flag(mainButton, LV_OBJ_FLAG_CLICKABLE);
  } else {
    lv_obj_clear_state(mainButton, LV_STATE_DISABLED);
    lv_obj_add_flag(mainButton, LV_OBJ_FLAG_CLICKABLE);
  }
}

// Ventil-Statusmatrix: gruen = auto AN + state AUS, dunkelgrau = auto AUS + state AUS,
// rot = state AN (ueberschreibt die anderen Faelle - macht das aktive Ventil bereits
// ausreichend sichtbar, kein zusaetzlicher Rahmen noetig). V0 hat kein eigenes
// Automatik-Flag und wird nie gedimmt. Die Zellen sind Buttons (Backlog-Test, ob sie
// gut antippbar sind), aber (noch) ohne Funktion - siehe docs/spec/13-touch-ui.md.
void refreshValveStatus() {
  const lv_color_t dimGray = lv_color_hex(0x555555);

  for (uint8_t i = 0; i < kValveCount; i++) {
    const bool on = ValveController::getValve(i);
    const bool dimmed = (i != 0) && !on && !ValveController::getAuto(i);
    lv_obj_set_style_bg_color(valveCells[i], on ? lv_color_hex(0xCC0000) : (dimmed ? dimGray : lv_color_hex(0x00AA00)),
                               0);
  }
}

// Statuszeile (2 Zeilen), Fussleiste: Zeile 1 in Prioritaet I2C-Fehler (roter
// Hintergrund, gelbe Schrift - deutlich auffaelliger als der Rest) > waehrend die
// Automatik laeuft "V{n} mm:ss | mm:ss" (aktives Ventil | Restlaufzeit der gesamten
// Sequenz, gelb) > manuell (per Matrix-Tap) geschaltetes Ventil "MANUELL" (hellblau) >
// sonst leer (START ist dann ohnehin gesperrt, siehe refreshMainButton()). Zeile 2 zeigt dazu jeweils den
// Alias-Namen des betroffenen Ventils (nur bei den beiden Ventil-Faellen, sonst leer) -
// ValveTimer liefert fuer noch ausstehende (nicht gestartete) Ventile bereits deren
// volle konfigurierte Zeit, daher genuegt fuer "gesamt" die Summe ueber aktives +
// wartende Ventile (Sequencer::getPendingValve()).
void refreshStatusLine() {
  char line1[40];
  char line2[40] = "";
  lv_color_t color1 = lv_color_white();
  lv_color_t boxColor = lv_color_hex(0x333333);  // wie die graue Kopfzeile

  if (!Diagnostics::isI2cOk()) {
    snprintf(line1, sizeof(line1), "I2C-Fehler!");
    color1 = lv_color_hex(0xFFFF00);
    boxColor = lv_color_hex(0xCC0000);
  } else if (Sequencer::isRunning()) {
    const uint8_t activeValve = Sequencer::getActiveValve();
    const uint16_t activeRemaining = ValveTimer::getRemainingSeconds(activeValve);
    uint32_t totalRemaining = activeRemaining;
    const uint8_t pendingCount = Sequencer::getPendingCount();
    for (uint8_t i = 0; i < pendingCount; i++) {
      totalRemaining += ValveTimer::getRemainingSeconds(Sequencer::getPendingValve(i));
    }
    snprintf(line1, sizeof(line1), "V%u  %02u:%02u | %02u:%02u", activeValve, activeRemaining / 60,
             activeRemaining % 60, static_cast<unsigned>(totalRemaining / 60),
             static_cast<unsigned>(totalRemaining % 60));
    color1 = lv_color_hex(0xFFFF00);

    char aliasAscii[40];
    toDisplayAscii(ValveController::getAlias(activeValve), aliasAscii, sizeof(aliasAscii));
    if (aliasAscii[0] != '\0') {
      snprintf(line2, sizeof(line2), "%s", aliasAscii);
    }
  } else {
    // Manuell (per Matrix-Tap) geschaltetes Ventil, ausserhalb einer Automatik-Sequenz -
    // erstes laufende Ventil in V1..V5-Reihenfolge, falls mehrere gleichzeitig an sind.
    uint8_t manualValve = 0;
    for (uint8_t i = 1; i <= 5; i++) {
      if (ValveController::getValve(i)) {
        manualValve = i;
        break;
      }
    }
    if (manualValve != 0) {
      snprintf(line1, sizeof(line1), "MANUELL");
      color1 = lv_color_hex(0x33CCFF);
      char aliasAscii[40];
      toDisplayAscii(ValveController::getAlias(manualValve), aliasAscii, sizeof(aliasAscii));
      if (aliasAscii[0] != '\0') {
        snprintf(line2, sizeof(line2), "%s", aliasAscii);
      }
    } else {
      line1[0] = '\0';
    }
  }

  lv_obj_set_style_bg_color(statusBox, boxColor, 0);
  lv_label_set_text(statusLine1, line1);
  lv_obj_set_style_text_color(statusLine1, color1, 0);
  lv_label_set_text(statusLine2, line2);
}

// Programme-Button (Hauptseite): zeigt das aktive Programm als eigenen Buttontext
// (ersetzt die fruehere separate Anzeigezeile) - liest ConfigStore::getActiveProgram()
// direkt, egal ob die Auswahl per Touch (Programme-Unterseite), MQTT main/program/cmd
// oder main/programs/set zustande kam. Waehrend die Automatik laeuft, wird der Button
// gesperrt (disabled + nicht klickbar) - ein Programmwechsel mitten in einer laufenden
// Sequenz wuerde sonst Chaos anrichten (Nutzer-Feedback 2026-08-17).
void refreshProgramsButtonLabel() {
  if (Sequencer::isRunning()) {
    lv_obj_add_state(programsButton, LV_STATE_DISABLED);
    lv_obj_clear_flag(programsButton, LV_OBJ_FLAG_CLICKABLE);
  } else {
    lv_obj_clear_state(programsButton, LV_STATE_DISABLED);
    lv_obj_add_flag(programsButton, LV_OBJ_FLAG_CLICKABLE);
  }

  const uint8_t active = ConfigStore::getActiveProgram();
  if (active == 0) {
    // Button UND Status in einem - zeigt bei keinem gewaehlten Programm bewusst "Manueller
    // Modus" statt "Kein Programm" (Nachtrag 2026-08-18, analog zum Web-Dashboard), da eine
    // manuelle time/auto-Aenderung die Programmwahl jetzt automatisch zuruecksetzt (siehe
    // MqttManager::publishConfigStateAndClearProgram()) - der Button muss diesen Zustand also
    // regelmaessig anzeigen, nicht nur direkt nach dem Boot.
    lv_label_set_text(programsButtonLabel, "Manueller Modus");
  } else {
    char nameAscii[40];
    toDisplayAscii(ConfigStore::getProgramName(active), nameAscii, sizeof(nameAscii));
    lv_label_set_text(programsButtonLabel, nameAscii);
  }
}

/**
 * @brief Baut die Touch-UI-Hauptseite auf: Titel, START/STOP-Button, Ventil-
 *        Statusmatrix (V1..V5 direkt schaltbar), Programme-Button (zeigt das
 *        aktive Programm), zweizeilige Statuszeile als Fussleiste.
 */
void setupUi() {
  mainScreen = lv_scr_act();
  lv_obj_set_style_bg_color(mainScreen, lv_color_black(), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(mainScreen, LV_OPA_COVER, LV_PART_MAIN);

  // Titelzeile grau hinterlegt, wie die Menuezeile der Programme-Unterseite.
  lv_obj_t *headerBar = lv_obj_create(mainScreen);
  lv_obj_clear_flag(headerBar, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(headerBar, kDisplayWidth, kHeaderHeight);
  lv_obj_align(headerBar, LV_ALIGN_TOP_MID, 0, 0);
  lv_obj_set_style_radius(headerBar, 0, 0);
  lv_obj_set_style_border_width(headerBar, 0, 0);
  lv_obj_set_style_bg_color(headerBar, lv_color_hex(0x333333), 0);
  lv_obj_set_style_bg_opa(headerBar, LV_OPA_COVER, 0);

  lv_obj_t *title = lv_label_create(headerBar);
  lv_label_set_text(title, "Gartenwasser");
  lv_obj_set_style_text_color(title, lv_color_white(), 0);
  lv_obj_center(title);

  mainButton = lv_btn_create(mainScreen);
  lv_obj_set_size(mainButton, kDisplayWidth - kSideMargin, kMainButtonHeight);
  lv_obj_align(mainButton, LV_ALIGN_TOP_MID, 0, kMainButtonY);
  lv_obj_add_event_cb(mainButton, mainButtonEventHandler, LV_EVENT_CLICKED, nullptr);
  addPressHighlight(mainButton);
  mainButtonLabel = lv_label_create(mainButton);
  lv_obj_center(mainButtonLabel);

  // Ventil-Statusmatrix: 4x4 Felder, V0..V5 auf die ersten 6 (zeilenweise), der Rest
  // bleibt als Platzhalter fuer eine spaetere Erweiterung sichtbar (dunkler Rahmen,
  // kein Fuellstand). V1..V5 sind per Tap direkt schaltbar, V0 ohne Handler (siehe
  // valveCellEventHandler()).
  for (uint8_t i = 0; i < kMatrixCellCount; i++) {
    const uint8_t row = i / kMatrixCols;
    const uint8_t col = i % kMatrixCols;
    const lv_coord_t x = kMatrixLeftX + col * (kMatrixCellSize + kMatrixCellGap);
    const lv_coord_t y = kMatrixTopY + row * (kMatrixCellSize + kMatrixCellGap);

    if (i < kValveCount) {
      valveCells[i] = lv_btn_create(mainScreen);
      lv_obj_set_size(valveCells[i], kMatrixCellSize, kMatrixCellSize);
      lv_obj_align(valveCells[i], LV_ALIGN_TOP_LEFT, x, y);
      lv_obj_set_style_radius(valveCells[i], 4, 0);
      if (i >= 1) {
        lv_obj_add_event_cb(valveCells[i], valveCellEventHandler, LV_EVENT_CLICKED,
                             reinterpret_cast<void *>(static_cast<uintptr_t>(i)));
        addPressHighlight(valveCells[i]);
      }
      valveCellLabels[i] = lv_label_create(valveCells[i]);
      char text[4];
      snprintf(text, sizeof(text), "V%u", i);
      lv_label_set_text(valveCellLabels[i], text);
      lv_obj_set_style_text_color(valveCellLabels[i], lv_color_white(), 0);
      lv_obj_center(valveCellLabels[i]);
    } else {
      // Reservierte Zelle (kuenftige Erweiterung) - kein Fuellstand, nur schwacher Rahmen.
      valveCells[i] = lv_obj_create(mainScreen);
      lv_obj_clear_flag(valveCells[i], LV_OBJ_FLAG_SCROLLABLE);
      lv_obj_set_size(valveCells[i], kMatrixCellSize, kMatrixCellSize);
      lv_obj_align(valveCells[i], LV_ALIGN_TOP_LEFT, x, y);
      lv_obj_set_style_radius(valveCells[i], 4, 0);
      lv_obj_set_style_bg_opa(valveCells[i], LV_OPA_TRANSP, 0);
      lv_obj_set_style_border_width(valveCells[i], 1, 0);
      lv_obj_set_style_border_color(valveCells[i], lv_color_hex(0x333333), 0);
    }
  }

  programsButton = lv_btn_create(mainScreen);
  lv_obj_set_size(programsButton, kDisplayWidth - kSideMargin, kProgramsButtonHeight);
  lv_obj_align(programsButton, LV_ALIGN_TOP_MID, 0, kProgramsButtonY);
  lv_obj_add_event_cb(programsButton, programsButtonEventHandler, LV_EVENT_CLICKED, nullptr);
  addPressHighlight(programsButton);
  programsButtonLabel = lv_label_create(programsButton);
  lv_obj_center(programsButtonLabel);

  // Statuszeile, abgesetzt mit dunkelgrauem Hintergrund, an der unteren Displaykante
  // verankert (kStatusBoxBottomMargin) - die Box-Position bleibt fix, nur der Text
  // darin sitzt weiter oben (statusLine1/2-Offsets unten).
  statusBox = lv_obj_create(mainScreen);
  lv_obj_clear_flag(statusBox, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(statusBox, kDisplayWidth, kStatusBoxHeight);
  lv_obj_align(statusBox, LV_ALIGN_BOTTOM_MID, 0, -kStatusBoxBottomMargin);
  lv_obj_set_style_radius(statusBox, 0, 0);
  lv_obj_set_style_border_width(statusBox, 0, 0);
  lv_obj_set_style_bg_color(statusBox, lv_color_hex(0x333333), 0);
  lv_obj_set_style_bg_opa(statusBox, LV_OPA_COVER, 0);

  statusLine1 = lv_label_create(statusBox);
  lv_obj_set_width(statusLine1, kDisplayWidth - 16);
  lv_obj_set_style_text_align(statusLine1, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(statusLine1, LV_ALIGN_TOP_MID, 0, -9);

  statusLine2 = lv_label_create(statusBox);
  lv_obj_set_width(statusLine2, kDisplayWidth - 16);
  lv_obj_set_style_text_align(statusLine2, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_color(statusLine2, lv_color_hex(0xAAAAAA), 0);
  lv_obj_align(statusLine2, LV_ALIGN_TOP_MID, 0, 9);

  refreshMainButton();
  refreshValveStatus();
  refreshStatusLine();
  refreshProgramsButtonLabel();
}

/**
 * @brief Baut die Programme-Unterseite auf: </> blaettert durch alle Programme
 *        (inkl. virtuellem Eintrag "Kein Programm"), OK wendet nur an (kein Start),
 *        Abbrechen kehrt ohne Aenderung zurueck. Siehe docs/spec/13-touch-ui.md.
 */
void setupProgramScreen() {
  programScreen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(programScreen, lv_color_black(), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(programScreen, LV_OPA_COVER, LV_PART_MAIN);

  // Menuezeile grau hinterlegt, hebt sich vom schwarzen Hintergrund ab.
  lv_obj_t *headerBar = lv_obj_create(programScreen);
  lv_obj_clear_flag(headerBar, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(headerBar, kDisplayWidth, 30);
  lv_obj_align(headerBar, LV_ALIGN_TOP_MID, 0, 0);
  lv_obj_set_style_radius(headerBar, 0, 0);
  lv_obj_set_style_border_width(headerBar, 0, 0);
  lv_obj_set_style_bg_color(headerBar, lv_color_hex(0x333333), 0);
  lv_obj_set_style_bg_opa(headerBar, LV_OPA_COVER, 0);

  lv_obj_t *title = lv_label_create(headerBar);
  lv_label_set_text(title, "Programm waehlen");
  lv_obj_set_style_text_color(title, lv_color_white(), 0);
  lv_obj_center(title);

  programNameLabel = lv_label_create(programScreen);
  lv_obj_set_width(programNameLabel, kDisplayWidth - 32);
  lv_label_set_long_mode(programNameLabel, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_align(programNameLabel, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_color(programNameLabel, lv_color_white(), 0);
  lv_obj_align(programNameLabel, LV_ALIGN_TOP_MID, 0, 60);

  // </> auf ca. halbe Displaybreite vergroessert, fuer bessere Treffsicherheit.
  lv_obj_t *prevButton = lv_btn_create(programScreen);
  lv_obj_set_size(prevButton, 80, 54);
  lv_obj_align(prevButton, LV_ALIGN_TOP_LEFT, 4, 150);
  lv_obj_add_event_cb(prevButton, programPrevButtonEventHandler, LV_EVENT_CLICKED, nullptr);
  addPressHighlight(prevButton);
  lv_obj_t *prevButtonLabel = lv_label_create(prevButton);
  lv_label_set_text(prevButtonLabel, "<");
  lv_obj_center(prevButtonLabel);

  lv_obj_t *nextButton = lv_btn_create(programScreen);
  lv_obj_set_size(nextButton, 80, 54);
  lv_obj_align(nextButton, LV_ALIGN_TOP_RIGHT, -4, 150);
  lv_obj_add_event_cb(nextButton, programNextButtonEventHandler, LV_EVENT_CLICKED, nullptr);
  addPressHighlight(nextButton);
  lv_obj_t *nextButtonLabel = lv_label_create(nextButton);
  lv_label_set_text(nextButtonLabel, ">");
  lv_obj_center(nextButtonLabel);

  // OK/Abbrechen untereinander, unten buendig (Cancel ganz unten, OK direkt darueber),
  // volle Breite, beide grau (keine Gruen/Rot-Signalfarbe mehr).
  lv_obj_t *cancelButton = lv_btn_create(programScreen);
  lv_obj_set_size(cancelButton, kDisplayWidth - kSideMargin, 40);
  lv_obj_align(cancelButton, LV_ALIGN_BOTTOM_MID, 0, -6);
  lv_obj_set_style_bg_color(cancelButton, lv_color_hex(0x444444), 0);
  lv_obj_add_event_cb(cancelButton, programCancelButtonEventHandler, LV_EVENT_CLICKED, nullptr);
  addPressHighlight(cancelButton);
  lv_obj_t *cancelButtonLabel = lv_label_create(cancelButton);
  lv_label_set_text(cancelButtonLabel, "Abbrechen");
  lv_obj_center(cancelButtonLabel);

  lv_obj_t *okButton = lv_btn_create(programScreen);
  lv_obj_set_size(okButton, kDisplayWidth - kSideMargin, 40);
  lv_obj_align_to(okButton, cancelButton, LV_ALIGN_OUT_TOP_MID, 0, -6);
  lv_obj_set_style_bg_color(okButton, lv_color_hex(0x444444), 0);
  lv_obj_add_event_cb(okButton, programOkButtonEventHandler, LV_EVENT_CLICKED, nullptr);
  addPressHighlight(okButton);
  lv_obj_t *okButtonLabel = lv_label_create(okButton);
  lv_label_set_text(okButtonLabel, "OK");
  lv_obj_center(okButtonLabel);
}

}  // namespace

void HmiManager::begin() {
  // Display initialisieren
  if (!gfx->begin()) {
    Logger::log(Logger::Type::ERROR, Logger::Source::HMI, "Display-Init fehlgeschlagen.");
  }
  lcdRegInit();  // Herstellerspezifische Registersequenz
  gfx->setRotation(kRotation);
  gfx->fillScreen(RGB565_BLACK);  // Schwarzen Hintergrund setzen
  pinMode(kBacklightPin, OUTPUT);
  digitalWrite(kBacklightPin, HIGH);  // Backlight einschalten

  // Touchscreen (I2C) - MCP23017 hängt am selben Bus (siehe I2CManager)
  Wire.begin(kTouchSda, kTouchScl);
  bsp_touch_init(&Wire, kTouchRst, kTouchInt, gfx->getRotation(), gfx->width(), gfx->height());

  // LVGL initialisieren
  lv_init();
  lv_disp_draw_buf_init(&draw_buf, disp_draw_buf, NULL, kDisplayWidth * 40);

  lv_disp_drv_init(&disp_drv);
  disp_drv.hor_res = kDisplayWidth;
  disp_drv.ver_res = kDisplayHeight;
  disp_drv.flush_cb = dispFlush;
  disp_drv.draw_buf = &draw_buf;
  lv_disp_drv_register(&disp_drv);

  lv_indev_drv_init(&indev_drv);
  indev_drv.type = LV_INDEV_TYPE_POINTER;
  indev_drv.read_cb = touchpadRead;
  lv_indev_drv_register(&indev_drv);

  // UI aufbauen (Haupt- + Programme-Seite) und einmalig rendern
  setupUi();
  setupProgramScreen();
  lv_scr_load(mainScreen);
  lv_timer_handler();
}

void HmiManager::loop() {
  lv_timer_handler();  // LVGL-Verarbeitung (muss regelmäßig aufgerufen werden)

  const unsigned long now = millis();
  if (now - lastUiRefreshMs >= kUiRefreshIntervalMs) {
    lastUiRefreshMs = now;
    refreshMainButton();
    refreshValveStatus();
    refreshStatusLine();
    refreshProgramsButtonLabel();
  }
}
