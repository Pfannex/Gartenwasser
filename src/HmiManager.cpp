#include "HmiManager.h"

#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <Wire.h>
#include <lvgl.h>

#include "ConfigStore.h"
#include "Diagnostics.h"
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

// --- Touch-UI: Automatik-Toggle & Ventil-Statusanzeige ----------------------
// Liest den Zustand direkt aus ValveController/Sequencer (kein MQTT-Umweg fuer
// die lokale Anzeige); der Toggle-Button loest ueber MqttManager::requestMainCmd()
// denselben Pfad wie main/cmd per MQTT aus (inkl. aller Publishes, keine
// Sonderlogik), siehe docs/spec/13-touch-ui.md.

constexpr uint8_t kValveCount = 6;  // V0..V5, siehe ValveController::kValveCount
constexpr uint8_t kProgramButtonCount = 4;  // P1..P4, wenden das Programm mit passendem "shortcut" an
constexpr unsigned long kUiRefreshIntervalMs = 250;
constexpr unsigned long kProgramHintDurationMs = 2000;  // Anzeigedauer fuer "P{n} nicht konfiguriert!"

// Layout: Titel + AUTO/OFF-Button oben (bis y=76), darunter Ventil-Liste/
// Programm-Buttons, die Statuszeile nimmt als Fussleiste die komplette
// verbleibende Hoehe unten ein.
constexpr lv_coord_t kContentTopY = 84;
constexpr lv_coord_t kValveRowHeight = 30;
constexpr lv_coord_t kProgramButtonStep = 40;
constexpr lv_coord_t kStatusBoxHeight = 64;

lv_obj_t *mainButton = nullptr;
lv_obj_t *mainButtonLabel = nullptr;
lv_obj_t *statusLabel = nullptr;
lv_obj_t *valveLeds[kValveCount] = {nullptr};
lv_obj_t *valveNameLabels[kValveCount] = {nullptr};
lv_obj_t *programButtons[kProgramButtonCount] = {nullptr};
unsigned long lastUiRefreshMs = 0;

// Transienter Hinweis "P{n} nicht konfiguriert!" nach Druck auf einen Button ohne
// gebundenes Programm - blendet sich nach kProgramHintDurationMs von selbst wieder aus
// (0 = kein aktiver Hinweis), siehe refreshStatusLine().
char programHintText[24] = "";
unsigned long programHintUntilMs = 0;

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

void mainButtonEventHandler(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
    return;
  }
  const bool startingSequence = !Sequencer::isRunning();
  // Nur bewusst als Hinweis, nicht blockierend: main/cmd nutzt ohnehin die aktuell
  // gesetzten auto-Flags, unabhaengig davon, ob/welches Programm "aktiv" markiert ist -
  // rein manuelle Konfiguration ohne Programm ist ein vollwertiger, unveraenderter Weg.
  if (startingSequence && ConfigStore::getActiveProgram() == 0) {
    snprintf(programHintText, sizeof(programHintText), "Kein Programm vorgewaehlt!");
    programHintUntilMs = millis() + kProgramHintDurationMs;
  }
  MqttManager::requestMainCmd(startingSequence);
}

// P1-P4 (Phase 13/14): wendet das Programm an, dessen "shortcut"-Feld dem gedrueckten
// Button entspricht - ueber MqttManager::requestProgramByShortcut(), analog zu
// requestMainCmd() (kein MQTT-Umweg). Der sichtbare Checked-State der Buttons wird
// nicht hier gesetzt, sondern periodisch in refreshProgramButtons() aus dem tatsaechlichen
// ConfigStore::getActiveProgram() abgeleitet - so bleibt die Anzeige korrekt, auch wenn
// die Auswahl ueber MQTT (main/program/cmd, main/programs/set) geaendert wird.
// Ist keinem Programm dieser Shortcut zugeordnet, wird nichts angewendet, sondern nur
// ein kurzer Hinweis in der Statuszeile eingeblendet. Ist das gebundene Programm bereits
// aktiv, wird es abgewaehlt (Toggle-Verhalten, wie main/program/cmd 0).
void programButtonEventHandler(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
    return;
  }
  const uint8_t index = static_cast<uint8_t>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(e)));
  const uint8_t shortcut = index + 1;
  const uint8_t boundProgram = ConfigStore::getProgramIndexForShortcut(shortcut);
  if (boundProgram == 0) {
    snprintf(programHintText, sizeof(programHintText), "P%u nicht konfiguriert!", shortcut);
    programHintUntilMs = millis() + kProgramHintDurationMs;
    return;
  }
  programHintUntilMs = 0;  // frischer Programmwechsel/-abwahl verdraengt einen evtl. sichtbaren Hinweis
  if (boundProgram == ConfigStore::getActiveProgram()) {
    MqttManager::requestProgramClear();
    return;
  }
  MqttManager::requestProgramByShortcut(shortcut);
}

void refreshMainButton() {
  const bool running = Sequencer::isRunning();
  lv_label_set_text(mainButtonLabel, running ? "OFF" : "AUTO");
  lv_obj_set_style_bg_color(mainButton, running ? lv_color_hex(0xAA0000) : lv_color_hex(0x008800), 0);
}

// Ventile als runde Status-Indikatoren (radio-button-artig): gruen = AUS, rot = AN.
// Ventile mit auto=OFF werden im AUS-Zustand gedimmt (dunkelgrau) dargestellt, um
// sichtbar zu machen, dass sie nicht Teil der Automatik-Sequenz sind - sobald
// manuell eingeschaltet, wieder ganz normal (rot). V0 hat kein eigenes
// Automatik-Flag und wird nie gedimmt.
void refreshValveStatus() {
  const bool sequenceRunning = Sequencer::isRunning();
  const uint8_t activeValve = sequenceRunning ? Sequencer::getActiveValve() : 0;

  for (uint8_t i = 0; i < kValveCount; i++) {
    const bool on = ValveController::getValve(i);
    const bool dimmed = (i != 0) && !on && !ValveController::getAuto(i);
    const lv_color_t dimGray = lv_color_hex(0x555555);

    lv_led_set_color(valveLeds[i], on ? lv_color_hex(0xFF0000) : (dimmed ? dimGray : lv_color_hex(0x00CC00)));

    char text[4];
    snprintf(text, sizeof(text), "V%u", i);
    lv_label_set_text(valveNameLabels[i], text);

    lv_color_t textColor;
    if (dimmed) {
      textColor = dimGray;
    } else if (sequenceRunning && activeValve == i) {
      // Aktives Sequenz-Ventil zusaetzlich hervorheben (siehe docs/spec/13-touch-ui.md, Test 4).
      textColor = lv_color_hex(0xFFFF00);
    } else {
      textColor = lv_color_white();
    }
    lv_obj_set_style_text_color(valveNameLabels[i], textColor, 0);
  }
}

// Spiegelt den Checked-State der P1-P4-Buttons aus dem tatsaechlichen aktiven Programm
// (ConfigStore::getActiveProgram()) - laeuft an, egal ob die Auswahl per Touch, MQTT
// main/program/cmd oder main/programs/set zustande kam (kein lokaler Klick-Zustand).
void refreshProgramButtons() {
  const uint8_t active = ConfigStore::getActiveProgram();
  for (uint8_t i = 0; i < kProgramButtonCount; i++) {
    const uint8_t boundProgram = ConfigStore::getProgramIndexForShortcut(i + 1);
    const bool checked = (active != 0) && (boundProgram == active);
    if (checked) {
      lv_obj_add_state(programButtons[i], LV_STATE_CHECKED);
    } else {
      lv_obj_clear_state(programButtons[i], LV_STATE_CHECKED);
    }
  }
}

// Formatiert "<Alias oder V{n}>  mm:ss[ +N weitere]" fuer die Statuszeile.
void formatValveActivity(char *out, size_t outSize, uint8_t valveIndex, uint8_t extraCount) {
  char aliasAscii[40];
  toDisplayAscii(ValveController::getAlias(valveIndex), aliasAscii, sizeof(aliasAscii));
  const uint16_t remaining = ValveTimer::getRemainingSeconds(valveIndex);
  char extra[16] = "";
  if (extraCount > 0) {
    snprintf(extra, sizeof(extra), " +%u weitere", extraCount);
  }
  if (aliasAscii[0] != '\0') {
    snprintf(out, outSize, "%s  %02u:%02u%s", aliasAscii, remaining / 60, remaining % 60, extra);
  } else {
    snprintf(out, outSize, "V%u  %02u:%02u%s", valveIndex, remaining / 60, remaining % 60, extra);
  }
}

// Statuszeile: zeigt an, was das Geraet gerade tut - in Prioritaet
// Fehler > transienter Programm-Hinweis (2s) > laufende Automatik > manuell
// laufende Ventile > gewaehltes Programm > "MANUELL" (kein Programm gewaehlt).
// Der Hinweis steht bewusst
// ueber "laufende Automatik": er wird genau beim Start ausgeloest (siehe
// mainButtonEventHandler()), zu dem Zeitpunkt ist die Sequenz bereits aktiv -
// mit niedrigerer Prioritaet waere er sofort wieder verdeckt.
void refreshStatusLine() {
  char text[64];
  lv_color_t color = lv_color_white();

  if (!Diagnostics::isI2cOk()) {
    snprintf(text, sizeof(text), "I2C-Fehler!");
    color = lv_color_hex(0xFF3333);
  } else if (millis() < programHintUntilMs) {
    snprintf(text, sizeof(text), "%s", programHintText);
    color = lv_color_hex(0xFF8800);  // Hinweis-Orange, unterscheidet sich von Fehler-Rot/Programm-Blau
  } else if (Sequencer::isRunning()) {
    formatValveActivity(text, sizeof(text), Sequencer::getActiveValve(), 0);
    color = lv_color_hex(0xFFFF00);  // wie die Hervorhebung des aktiven Ventils
  } else {
    uint8_t runningCount = 0;
    uint8_t firstRunning = 0;
    for (uint8_t i = 1; i <= 5; i++) {
      if (ValveController::getValve(i)) {
        if (runningCount == 0) {
          firstRunning = i;
        }
        runningCount++;
      }
    }
    if (runningCount > 0) {
      formatValveActivity(text, sizeof(text), firstRunning, runningCount - 1);
      color = lv_color_hex(0x33CCFF);  // manuell (nicht Teil der Automatik-Sequenz)
    } else {
      const uint8_t activeProgram = ConfigStore::getActiveProgram();
      if (activeProgram != 0) {
        char nameAscii[40];
        toDisplayAscii(ConfigStore::getProgramName(activeProgram), nameAscii, sizeof(nameAscii));
        snprintf(text, sizeof(text), "Programm: %s", nameAscii);
        color = lv_color_hex(0x66CCFF);
      } else {
        // Kein Programm gewaehlt: main/cmd ON ist blockiert (siehe MqttManager::startSequence()),
        // "Bereit" waere hier irrefuehrend - MANUELL beschreibt den tatsaechlichen Modus
        // (Ventile direkt schaltbar, aber keine Automatik moeglich), bleibt neutral/grau wie zuvor.
        snprintf(text, sizeof(text), "MANUELL");
        color = lv_color_hex(0x888888);
      }
    }
  }

  lv_label_set_text(statusLabel, text);
  lv_obj_set_style_text_color(statusLabel, color, 0);
}

/**
 * @brief Baut die Touch-UI auf: Automatik-Toggle + Statuszeile + Ventil-Statusanzeige.
 */
void setupUi() {
  lv_obj_t *screen = lv_scr_act();
  lv_obj_set_style_bg_color(screen, lv_color_black(), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);

  lv_obj_t *title = lv_label_create(screen);
  lv_label_set_text(title, "Gartenwasser");
  lv_obj_set_style_text_color(title, lv_color_white(), 0);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 8);

  mainButton = lv_btn_create(screen);
  lv_obj_set_size(mainButton, kDisplayWidth - 20, 44);
  lv_obj_align(mainButton, LV_ALIGN_TOP_MID, 0, 32);
  lv_obj_add_event_cb(mainButton, mainButtonEventHandler, LV_EVENT_CLICKED, nullptr);
  mainButtonLabel = lv_label_create(mainButton);
  lv_obj_center(mainButtonLabel);

  // Statuszeile als eigene Fussleiste ganz unten, abgesetzt mit dunkelgrauem
  // Hintergrund, nutzt die komplette verbleibende Hoehe unterhalb der
  // Ventil-/Programm-Spalten.
  lv_obj_t *statusBox = lv_obj_create(screen);
  lv_obj_clear_flag(statusBox, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(statusBox, kDisplayWidth, kStatusBoxHeight);
  lv_obj_align(statusBox, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_set_style_radius(statusBox, 0, 0);
  lv_obj_set_style_border_width(statusBox, 0, 0);
  lv_obj_set_style_bg_color(statusBox, lv_color_hex(0x1A1A1A), 0);
  lv_obj_set_style_bg_opa(statusBox, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_all(statusBox, 6, 0);

  statusLabel = lv_label_create(statusBox);
  lv_obj_set_width(statusLabel, kDisplayWidth - 16);
  lv_label_set_long_mode(statusLabel, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_align(statusLabel, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_center(statusLabel);

  for (uint8_t i = 0; i < kValveCount; i++) {
    const lv_coord_t y = kContentTopY + i * kValveRowHeight;

    valveLeds[i] = lv_led_create(screen);
    lv_obj_set_size(valveLeds[i], 16, 16);
    lv_obj_align(valveLeds[i], LV_ALIGN_TOP_LEFT, 10, y);
    lv_led_set_brightness(valveLeds[i], 255);

    valveNameLabels[i] = lv_label_create(screen);
    lv_obj_align(valveNameLabels[i], LV_ALIGN_TOP_LEFT, 34, y);
  }

  for (uint8_t i = 0; i < kProgramButtonCount; i++) {
    programButtons[i] = lv_btn_create(screen);
    lv_obj_add_flag(programButtons[i], LV_OBJ_FLAG_CHECKABLE);
    lv_obj_set_size(programButtons[i], 56, 34);
    lv_obj_align(programButtons[i], LV_ALIGN_TOP_RIGHT, -8, kContentTopY + i * kProgramButtonStep);
    lv_obj_add_event_cb(programButtons[i], programButtonEventHandler, LV_EVENT_CLICKED,
                         reinterpret_cast<void *>(static_cast<uintptr_t>(i)));

    char label[4];
    snprintf(label, sizeof(label), "P%u", i + 1);
    lv_obj_t *programButtonLabel = lv_label_create(programButtons[i]);
    lv_label_set_text(programButtonLabel, label);
    lv_obj_center(programButtonLabel);
  }

  refreshMainButton();
  refreshStatusLine();
  refreshValveStatus();
  refreshProgramButtons();
}

}  // namespace

void HmiManager::begin() {
  // Display initialisieren
  gfx->begin();
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

  // UI aufbauen und einmalig rendern
  setupUi();
  lv_timer_handler();
}

void HmiManager::loop() {
  lv_timer_handler();  // LVGL-Verarbeitung (muss regelmäßig aufgerufen werden)

  const unsigned long now = millis();
  if (now - lastUiRefreshMs >= kUiRefreshIntervalMs) {
    lastUiRefreshMs = now;
    refreshMainButton();
    refreshStatusLine();
    refreshValveStatus();
    refreshProgramButtons();
  }
}
