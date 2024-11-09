/*******************************************************************
    A touch screen test for the ESP32 Cheap Yellow Display.

    https://github.com/witnessmenow/ESP32-Cheap-Yellow-Display

    If you find what I do useful and would like to support me,
    please consider becoming a sponsor on Github
    https://github.com/sponsors/witnessmenow/

    Written by Brian Lough
    YouTube: https://www.youtube.com/brianlough
    Twitter: https://twitter.com/witnessmenow
 *******************************************************************/

// Make sure to copy the UserSetup.h file into the library as
// per the Github Instructions. The pins are defined in there.

// ----------------------------
// Standard Libraries
// ----------------------------

#include <SPI.h>

// ----------------------------
// Additional Libraries - each one of these will need to be installed.
// ----------------------------

#include <XPT2046_Touchscreen.h>
// A library for interfacing with the touch screen
//
// Can be installed from the library manager (Search for "XPT2046")
//https://github.com/PaulStoffregen/XPT2046_Touchscreen

#include <TFT_eSPI.h>
// A library for interfacing with LCD displays
//
// Can be installed from the library manager (Search for "TFT_eSPI")
//https://github.com/Bodmer/TFT_eSPI

#include "SD.h"
#include "Audio.h"

// ----------------------------
// Touch Screen pins
// ----------------------------

// The CYD touch uses some non default
// SPI pins

#define XPT2046_IRQ 36
#define XPT2046_MOSI 32
#define XPT2046_MISO 39
#define XPT2046_CLK 25
#define XPT2046_CS 33

// ----------------------------

SPIClass mySpi = SPIClass(VSPI);
XPT2046_Touchscreen ts(XPT2046_CS, XPT2046_IRQ);

TFT_eSPI tft = TFT_eSPI();

uint8_t n_home = 3
uint8_t n_dryness = 5;
uint8_t n_settings = 4;
uint8_t n_temperature = 6;
uint8_t n_humidity = 6;

TFT_eSPI_Button btn_home[n_home];
TFT_eSPI_Button btn_dryness[n_dryness];
TFT_eSPI_Button btn_settings[n_settings];
TFT_eSPI_Button btn_temperature[n_temperature];
TFT_eSPI_Button btn_humidity[n_humidity];

Audio audio(true, I2S_DAC_CHANNEL_LEFT_EN);

static readonly string[] lbl_home = { "Lighting", "Biltong Timer", "Settings" };
static readonly string[] lbl_dryness = { "Wet", "Medium", "Dry", "Karoo Dry", "Home" };
static readonly string[] lbl_settings = { "Temperature", "Humidity", "Wifi", "Home" };
static readonly string[] lbl_temperature = { "^", "v", "^", "v", "Default", "Save" };
static readonly string[] lbl_humidity = { "^", "v", "^", "v", "Default", "Save" };

// The timing for the different dryness levels of the biltong.
static readonly float[] dryness_timing = { 48.0, 60.0, 84.0, 96.0 };

uint16_t bWidth = TFT_HEIGHT/3;
uint16_t bHeight = TFT_WIDTH/2;

unsigned long last_touch = 0;
unsigned long last_timing_update = 0;
unsigned long last_timing_start = 0;
unsigned long last_refresh = 0;
float current_total_timing = 0.0;

static readonly float DEFAULT_TEMPERATURE_MIN = 30.0;
static readonly float DEFAULT_TEMPERATURE_MAX = 33.0;
static readonly float DEFAULT_HUMIDITY_MIN = 50.0;
static readonly float DEFAULT_HUMIDITY_MAX = 80.0;

float temperature_min = DEFAULT_TEMPERATURE_MIN;
float temperature_max = DEFAULT_TEMPERATURE_MAX;
float humidity_min = DEFAULT_HUMIDITY_MIN;
float humidity_max = DEFAULT_HUMIDITY_MAX;

bool lighting_on = false;

int center_x = 320 / 2; // center of display
int center_y = 100;
int fontSize = 2;

/*
Enum defining which of the menu screens we are on; affects the button selections
and corresponding event handling.
*/
enum DumeniMenu {
  HOME,
  DRYNESS,
  SETTINGS,
  TEMPERATURE,
  HUMIDITIY
}

enum DumeniMenu active_screen = HOME;

#define LCD_BACK_LIGHT_PIN 21
// use first channel of 16 channels (started from zero)
#define LEDC_CHANNEL_0     0
// use 12 bit precission for LEDC timer
#define LEDC_TIMER_12_BIT  12
// use 5000 Hz as a LEDC base frequency
#define LEDC_BASE_FREQ     5000

// Arduino like analogWrite
// value has to be between 0 and valueMax
void ledcAnalogWrite(uint8_t channel, uint32_t value, uint32_t valueMax = 255) {
  // calculate duty, 4095 from 2 ^ 12 - 1
  uint32_t duty = (4095 / valueMax) * min(value, valueMax);

  // write duty to LEDC
  ledcWrite(channel, duty);
}

void initialize_sd() {
  if (!SD.begin(SS, spi, 80000000)) {
    Serial.println("Card Mount Failed");
    return;
  }
  uint8_t cardType = SD.cardType();

  if (cardType == CARD_NONE) {
    Serial.println("No SD card attached");
    return;
  }

  Serial.print("SD Card Type: ");
  if (cardType == CARD_MMC) {
    Serial.println("MMC");
  } else if (cardType == CARD_SD) {
    Serial.println("SDSC");
  } else if (cardType == CARD_SDHC) {
    Serial.println("SDHC");
  } else {
    Serial.println("UNKNOWN");
  }

  uint64_t cardSize = SD.cardSize() / (1024 * 1024);
  Serial.printf("SD Card Size: %lluMB\n", cardSize);

}

void readFile(fs::FS &fs, const char * path) {
  Serial.printf("Reading file: %s\n", path);

  File file = fs.open(path);
  if (!file) {
    Serial.println("Failed to open file for reading");
    return;
  }

  Serial.print("Read from file: ");
  while (file.available()) {
    Serial.write(file.read());
  }
  file.close();
}

void writeFile(fs::FS &fs, const char * path, const char * message) {
  Serial.printf("Writing file: %s\n", path);

  File file = fs.open(path, FILE_WRITE);
  if (!file) {
    Serial.println("Failed to open file for writing");
    return;
  }
  if (file.print(message)) {
    Serial.println("File written");
  } else {
    Serial.println("Write failed");
  }
  file.close();
}


/* Draws the interface for selecting the biltong dryness to a cleared screen.
*/
void draw_dryness_buttons() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);

  // Generate buttons with different size X deltas
  for (int i = 0; i < n_dryness; i++) {
    uint16_t btn_index = i;
    if i == 4 {
      // Leave a gap between the dryness buttons and the final home button.
      btn_index += 1;
    }

    btn_dryness[i].initButton(&tft,
                      bWidth * (btn_index % 3) + bWidth/2,
                      bHeight * (btn_index / 3) + bHeight/2,
                      bWidth,
                      bHeight,
                      TFT_BLACK, // Outline
                      TFT_BLUE, // Fill
                      TFT_BLACK, // Text
                      "",
                      1);

    btn_dryness[i].drawButton(false, lbl_dryness[i]);
  }
}

/*
Button handler for the dryness menu button click.
*/
void dryness_menu_click() {
  draw_dryness_buttons();
  active_screen = DRYNESS;
}


/*
Button click handler for the various biltong dryness buttons.
*/
void dryness_button_handler(uint8_t index) {
  if index == 4 {
    // Home
    home_menu_click();
    return;
  }

  current_total_timing = dryness_timing[index];
  writeFile(SD, "/timing.txt", String(current_total_timing));
  last_timing_update = getLocalEpoch();
  writeFile(SD, "/lastrun.txt", String(last_timing_update));
}

void lighting_button_click(bool on) {
  // Turn on/off the lighting
}

void load_temperature() {
  char *s;
  readFile(SD, "/temperature.txt");
  sscanf(s, "%f,%f", &temperature_min, &temperature_max);
}

/* Draws the interface for selecting the biltong dryness to a cleared screen.
*/
void draw_temperature_buttons() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);

  uint8_t[] xs = { 130, 130, 290, 290, 80, 240 };
  uint8_t[] ys = { 90, 150, 90, 150, 220, 220 };
  uint8_t width = 30;
  uint8_t height = 30;

  // Generate buttons with different size X deltas
  for (int i = 0; i < n_temperature; i++) {
    uint16_t btn_index = i;
    if i > 3 {
      // Make the Default/Save buttons wider.
      width = 60;
    }

    btn_temperature[i].initButton(&tft,
                      xs[i],
                      ys[i]
                      width,
                      width,
                      TFT_BLACK, // Outline
                      TFT_BLUE, // Fill
                      TFT_BLACK, // Text
                      "",
                      1);

    btn_temperature[i].drawButton(false, lbl_temperature[i]);
  }

  char *st;
  sprintf(st, "%.1f", temperature_min);
  tft.drawCentreString(st, 80, 120, fontSize);

  char *sh;
  sprintf(sh, "%.1f", temperature_max);
  tft.drawCentreString(sh, 240, 120, fontSize);

  tft.drawCentreString("Temperature", center_x, 10, fontSize);
  tft.drawCentreString("Min", 80, 40, fontSize);
  tft.drawCentreString("Max", 240, 40, fontSize);
}

/*
Button handler for the dryness menu button click.
*/
void temperature_menu_click() {
  draw_temperature_buttons();
  active_screen = TEMPERATURE;
}

void set_temperature_mosfet() {
  // Set the temperature mosfet to the correct value.
  // TODO: Implement this.
  return;
}

void save_tempearature() {
  char *s;
  sprintf(s, "%f,%f", temperature_min, temperature_max);
  writeFile(SD, "/temperature.txt", );
  set_temperature_mosfet();
}

/*
Button click handler for the various biltong dryness buttons.
*/
void temperature_button_handler(uint8_t index) {
  switch (index) {
    case 0:
      // Temperature Min Up
      temperature_min += 0.5;
      if temperature_min > temperature_max; {
        temperature_min = temperature_max;
      }
      break;
    case 1:
      // Temperature Min Down
      temperature_min -= 0.5;
      if temperature_min < 1.0: {
        temperature_min = 1.0;
      }
      break;
    case 2:
      // Temperature Max Up
      temperature_max += 0.5;
      if temperature_min > 40.0: {
        temperature_min = 40.0;
      }
      break;
    case 3:
      // Temperature Max Down
      temperature_max -= 0.5;
      if temperature_min < temperature_min: {
        temperature_min = temperature_min;
      }
      break;
    case 4:
      // Default values.
      temperature_min = DEFAULT_TEMPERATURE_MIN;
      temperature_max = DEFAULT_TEMPERATURE_MAX;
      break;
    case 5:
      // Home/Save
      save_tempearature();
      home_menu_click();
      return;
  }

  draw_temperature_buttons();
}


void load_humidity() {
  char *s;
  readFile(SD, "/humidity.txt");
  sscanf(s, "%f,%f", &humidity_min, &humidity_max);
}

/* Draws the interface for selecting the biltong dryness to a cleared screen.
*/
void draw_humidity_buttons() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);

  uint8_t[] xs = { 130, 130, 290, 290, 80, 240 };
  uint8_t[] ys = { 90, 150, 90, 150, 220, 220 };
  uint8_t width = 30;
  uint8_t height = 30;

  // Generate buttons with different size X deltas
  for (int i = 0; i < n_humidity; i++) {
    uint16_t btn_index = i;
    if i > 3 {
      // Make the Default/Save buttons wider.
      width = 60;
    }

    btn_humidity[i].initButton(&tft,
                      xs[i],
                      ys[i]
                      width,
                      height,
                      TFT_BLACK, // Outline
                      TFT_BLUE, // Fill
                      TFT_BLACK, // Text
                      "",
                      1);

    btn_humidity[i].drawButton(false, lbl_humidity[i]);
  }

  char *st;
  sprintf(st, "%.1f", humidity_min);
  tft.drawCentreString(st, 80, 120, fontSize);

  char *sh;
  sprintf(sh, "%.1f", humidity_max);
  tft.drawCentreString(sh, 240, 120, fontSize);

  tft.drawCentreString("Humidity", center_x, 10, fontSize);
  tft.drawCentreString("Min", 80, 40, fontSize);
  tft.drawCentreString("Max", 240, 40, fontSize);
}

/*
Button handler for the dryness menu button click.
*/
void humidity_menu_click() {
  draw_humidity_buttons();
  active_screen = HUMIDITIY;
}

void set_humidity_mosfet() {
  // Set the humidity mosfet to the correct value.
  // TODO: Implement this.
  return;
}

void save_humidity() {
  char *s;
  sprintf(s, "%f,%f", humidity_min, humidity_max);
  writeFile(SD, "/humidity.txt", );
  set_humidity_mosfet();
}

/*
Button click handler for the various biltong dryness buttons.
*/
void humidity_button_handler(uint8_t index) {
  switch (index) {
    case 0:
      // humidity Min Up
      humidity_min += 0.5;
      if humidity_min > humidity_max; {
        humidity_min = humidity_max;
      }
      break;
    case 1:
      // humidity Min Down
      humidity_min -= 0.5;
      if humidity_min < 10.0: {
        humidity_min = 10.0;
      }
      break;
    case 2:
      // humidity Max Up
      humidity_max += 0.5;
      if humidity_min > 80.0: {
        humidity_min = 80.0;
      }
      break;
    case 3:
      // humidity Max Down
      humidity_max -= 0.5;
      if humidity_min < humidity_min: {
        humidity_min = humidity_min;
      }
      break;
    case 4:
      // Default values.
      humidity_min = DEFAULT_HUMIDITY_MIN;
      humidity_max = DEFAULT_HUMIDITY_MAX;
      break;
    case 5:
      // Home/Save
      save_humidity();
      home_menu_click();
      return;
  }

  draw_humidity_buttons();
}


/* Draws the interface for selecting the biltong dryness to a cleared screen.
*/
void draw_settings_buttons() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);

  // Generate buttons with different size X deltas
  for (int i = 0; i < n_settings; i++) {
    uint16_t btn_index = i;
    btn_settings[i].initButton(&tft,
                      bWidth * (btn_index % 3) + bWidth/2,
                      bHeight * (btn_index / 3) + bHeight/2,
                      bWidth,
                      bHeight,
                      TFT_BLACK, // Outline
                      TFT_BLUE, // Fill
                      TFT_BLACK, // Text
                      "",
                      1);

    btn_settings[i].drawButton(false, lbl_settings[i]);
  }
}

/*
Button handler for the dryness menu button click.
*/
void settings_menu_click() {
  draw_settings_buttons();
  active_screen = SETTINGS;
}


/*
Button click handler for the various biltong dryness buttons.
*/
void settings_button_handler(uint8_t index) {
  switch (index) {
    case 0:
      // Temperature
      draw_temperature_buttons();
      break;
    case 1:
      // Humidity
      draw_humidity_buttons();
      break;
    case 2:
      // Wifi
      break;
    case 3:
      // Home
      home_menu_click();
      break;
  }
}

/*
Gets the current temperature from the sensor.
*/
float get_temperature() {
  // TODO: Implement this
  return 0.0;
}

/*
Gets the current humidity from the sensor.
*/
float get_humidity() {
  // TODO: Implement this
  return 0.0;
}

void update_temperature() {
  unsigned long current_time = getLocalEpoch();
  if (current_time - last_refresh > 1) {
    draw_home_buttons();
  }
}

/* Draws the interface for selecting the biltong dryness to a cleared screen.
*/
void draw_home_buttons() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);

  // Generate buttons with different size X deltas
  for (int i = 0; i < n_home; i++) {
    uint16_t btn_index = i;
    btn_home[i].initButton(&tft,
                      bWidth * (btn_index % 3) + bWidth/2,
                      bHeight * (btn_index / 3) + bHeight/2,
                      bWidth,
                      bHeight,
                      TFT_BLACK, // Outline
                      TFT_BLUE, // Fill
                      TFT_BLACK, // Text
                      "",
                      1);

    btn_home[i].drawButton(false, lbl_home[i]);
  }

  char *s;
  sprintf(s, "Temperature: %.1f    Humidity: %.1f", get_temperature(), get_humidity());
  String label = String.strftime("%H:%M:%S", current_time);
  tft.drawCentreString(String(s), center_x, center_y, fontSize);
}

/*
Button handler for the home menu button click. This button exists on all the
sub-menus and returns the user to the home screen.
*/
void home_menu_click() {
  draw_home_buttons();
  active_screen = HOME;
}

/*
Button click handler for the various biltong dryness buttons.
*/
void home_button_handler(uint8_t index) {
  switch (index) {
    case 0:
      // Lighting
      lighting_button_click(!lighting_on);
      break;
    case 1:
      // Biltong Timer
      dryness_menu_click();
      break;
    case 2:
      // Settings
      settings_menu_click();
      break;
  }
}

/*
Generic button touch start handler which sets the button pressed boolean
flags on each button to keep track of pressed state.

@param p the touch point.
@param buttons the array of buttons for the active menu screen.
@param n the number of buttons on the active screen.
*/
void generic_touch_start(TS_Point p, TFT_eSPI_Button[] buttons, uint8_t n) {
  u_int16_t sx = (u_int16_t)((p.x - 200) / (3600.0 / TFT_HEIGHT));
  u_int16_t sy = (u_int16_t)((p.y - 200) / (3600.0 / TFT_WIDTH));

  for (uint8_t b = 0; b < n; b++) {
    if ((p.z > 0) && buttons[b].contains(sx, sy)) {
      buttons[b].press(true);  // tell the button it is pressed
    } else {
      buttons[b].press(false);  // tell the button it is NOT pressed
    }
  }
}

void generic_touch_run(void (*handler)(uint8_t), TFT_eSPI_Button[] buttons, String[] labels, uint8_t n) {
  for (uint8_t b = 0; b < n; b++) {
    // If button was just pressed, redraw inverted button
    if (buttons[b].justPressed()) {
      handler(b);
      buttons[b].drawButton(true, String(b+1));
    }

    // If button was just released, redraw normal color button
    if (buttons[b].justReleased()) {
      buttons[b].drawButton(false, labels[b]);
    }
  }
}

/*
Touch event handler when the active screen is the home screen.
*/
void home_touch_handler() {
  generic_touch_start(p, btn_home, n_home);
  generic_touch_run(home_button_handler, btn_home, lbl_home, n_home);
}

/*
Touch event handler when the active screen is the biltong dryness selection.
*/
void dryness_touch_handler() {
  generic_touch_start(p, btn_dryness, n_dryness);
  generic_touch_run(dryness_button_handler, btn_dryness, lbl_dryness, n_dryness);
}

/*
Touch event handler when the active screen is the settings selection.
*/
void settings_touch_handler() {
  generic_touch_start(p, btn_settings, n_settings);
  generic_touch_run(settings_button_handler, btn_settings, lbl_settings, n_settings);
}

/*
Touch event handler when the active screen is the temperature min/max screen.
*/
void temperature_touch_handler() {
  generic_touch_start(p, btn_temperature, n_temperature);
  generic_touch_run(temperature_button_handler, btn_temperature, lbl_temperature, n_temperature);
}

/*
Touch event handler when the active screen is the humidity min/max screen.
*/
void humidity_touch_handler() {
  generic_touch_start(p, btn_humidity, n_humidity);
  generic_touch_run(humidity_button_handler, btn_humidity, lbl_humidity, n_humidity);
}

void any_touch_handler() {
  if (ts.tirqTouched() && ts.touched()) {
      last_touch = getLocalEpoch();
      ledcAnalogWrite(LEDC_CHANNEL_0, 255); // On full brightness

      TS_Point p = ts.getPoint();

      switch (active_screen) {
        case HOME:
        home_touch_handler();
          break;
        case DRYNESS:
          dryness_touch_handler();
          break;
        case SETTINGS:
        settings_touch_handler();
          break;
        case TEMPERATURE:
          temperature_touch_handler();
          break;
        case HUMIDITIY:
          humidity_touch_handler();
          break;
      }
  }
}


void setup() {
  Serial.begin(115200);

  // Start the SPI for the touch screen and init the TS library
  mySpi.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS);
  ts.begin(mySpi);
  ts.setRotation(1);

  // Start the tft display and set it to black
  tft.init();

  // Setting up the LEDC and configuring the Back light pin
  // NOTE: this needs to be done after tft.init()
#if ESP_IDF_VERSION_MAJOR == 5
  ledcAttach(LCD_BACK_LIGHT_PIN, LEDC_BASE_FREQ, LEDC_TIMER_12_BIT);
#else
  ledcSetup(LEDC_CHANNEL_0, LEDC_BASE_FREQ, LEDC_TIMER_12_BIT);
  ledcAttachPin(LCD_BACK_LIGHT_PIN, LEDC_CHANNEL_0);
#endif

  tft.setRotation(1); //This is the display in landscape

  // Clear the screen before writing to it
  tft.fillScreen(TFT_BLACK);
  draw_home_buttons();
}

void dim_screen() {
  unsigned long current_time = getLocalEpoch();
  if (current_time - last_touch > 30) {
    ledcAnalogWrite(LEDC_CHANNEL_0, 0); // Off
  }
}

void loop() {
  dim_screen();
  update_temperature();
  any_touch_handler();
  delay(100);
}
