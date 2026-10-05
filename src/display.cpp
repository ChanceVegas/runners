// display.cpp — LGFX device config for CrowPanel 4.3" Basic (pins/timings from board_config.h).
#include "display.h"
#include "board_config.h"

namespace {

class LGFX_CrowPanel43 : public lgfx::LGFX_Device {
  lgfx::Bus_RGB        _bus;
  lgfx::Panel_RGB      _panel;
  lgfx::Light_PWM      _light;
  lgfx::Touch_XPT2046  _touch;

public:
  LGFX_CrowPanel43() {
    {
      auto cfg = _panel.config();
      cfg.memory_width  = LCD_WIDTH;
      cfg.memory_height = LCD_HEIGHT;
      cfg.panel_width   = LCD_WIDTH;
      cfg.panel_height  = LCD_HEIGHT;
      cfg.offset_x = 0;
      cfg.offset_y = 0;
      _panel.config(cfg);
    }
    {
      auto cfg = _bus.config();
      cfg.panel = &_panel;

      cfg.pin_d0  = LCD_PIN_B0;  cfg.pin_d1  = LCD_PIN_B1;
      cfg.pin_d2  = LCD_PIN_B2;  cfg.pin_d3  = LCD_PIN_B3;
      cfg.pin_d4  = LCD_PIN_B4;
      cfg.pin_d5  = LCD_PIN_G0;  cfg.pin_d6  = LCD_PIN_G1;
      cfg.pin_d7  = LCD_PIN_G2;  cfg.pin_d8  = LCD_PIN_G3;
      cfg.pin_d9  = LCD_PIN_G4;  cfg.pin_d10 = LCD_PIN_G5;
      cfg.pin_d11 = LCD_PIN_R0;  cfg.pin_d12 = LCD_PIN_R1;
      cfg.pin_d13 = LCD_PIN_R2;  cfg.pin_d14 = LCD_PIN_R3;
      cfg.pin_d15 = LCD_PIN_R4;

      cfg.pin_henable = LCD_PIN_DE;
      cfg.pin_vsync   = LCD_PIN_VSYNC;
      cfg.pin_hsync   = LCD_PIN_HSYNC;
      cfg.pin_pclk    = LCD_PIN_PCLK;
      cfg.freq_write  = LCD_PCLK_HZ;

      cfg.hsync_polarity    = LCD_HSYNC_POLARITY;
      cfg.hsync_front_porch = LCD_HSYNC_FRONT_PORCH;
      cfg.hsync_pulse_width = LCD_HSYNC_PULSE_WIDTH;
      cfg.hsync_back_porch  = LCD_HSYNC_BACK_PORCH;
      cfg.vsync_polarity    = LCD_VSYNC_POLARITY;
      cfg.vsync_front_porch = LCD_VSYNC_FRONT_PORCH;
      cfg.vsync_pulse_width = LCD_VSYNC_PULSE_WIDTH;
      cfg.vsync_back_porch  = LCD_VSYNC_BACK_PORCH;
      cfg.pclk_active_neg   = LCD_PCLK_ACTIVE_NEG;
      cfg.de_idle_high      = LCD_DE_IDLE_HIGH;
      cfg.pclk_idle_high    = LCD_PCLK_IDLE_HIGH;

      _bus.config(cfg);
      _panel.setBus(&_bus);
    }
    {
      auto cfg = _light.config();
      cfg.pin_bl = LCD_PIN_BL;
      _light.config(cfg);
      _panel.light(&_light);
    }
    {
      // XPT2046 resistive touch, own SPI host (pins shared with unused TF slot).
      auto cfg = _touch.config();
      cfg.spi_host = SPI2_HOST;
      cfg.freq     = TOUCH_SPI_HZ;
      cfg.pin_sclk = TOUCH_PIN_SCK;
      cfg.pin_mosi = TOUCH_PIN_MOSI;
      cfg.pin_miso = TOUCH_PIN_MISO;
      cfg.pin_cs   = TOUCH_PIN_CS;
      cfg.pin_int  = TOUCH_PIN_INT;
      cfg.bus_shared = false;
      cfg.x_min = TOUCH_X_MIN;  cfg.x_max = TOUCH_X_MAX;
      cfg.y_min = TOUCH_Y_MIN;  cfg.y_max = TOUCH_Y_MAX;
      // offset_rotation is empirical per panel; 0 first, fix from the M2a
      // raw-coordinate test if axes come out mirrored/swapped.
      cfg.offset_rotation = 0;
      _touch.config(cfg);
      _panel.setTouch(&_touch);
    }
    setPanel(&_panel);
  }
};

LGFX_CrowPanel43 s_lcd;

} // namespace

namespace display {

bool init() {
  if (!s_lcd.init()) return false;
  s_lcd.setBrightness(255);
  s_lcd.fillScreen(TFT_BLACK);
  return true;
}

lgfx::LGFX_Device& lcd() { return s_lcd; }

} // namespace display
