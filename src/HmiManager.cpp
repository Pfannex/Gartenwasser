#include "HmiManager.h"

#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <Wire.h>
#include <lvgl.h>

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

/**
 * @brief Zeigt einen Platzhalter-Screen, bis die eigentliche UI feststeht.
 */
void setupUi() {
  lv_obj_t *screen = lv_scr_act();
  lv_obj_set_style_bg_color(screen, lv_color_black(), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);

  lv_obj_t *label = lv_label_create(screen);
  lv_label_set_text(label, "Gartenwasser");
  lv_obj_set_style_text_color(label, lv_color_white(), 0);
  lv_obj_center(label);
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
}
