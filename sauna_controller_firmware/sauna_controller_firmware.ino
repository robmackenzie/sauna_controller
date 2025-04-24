#include "M5Dial.h"
#include "temperature_font.h"
#define DEFAULT_BACKGROUND_COLOUR 0x0A2D
#define SAUNA_GPIO_PIN 1

M5Canvas img(&M5Dial.Display);
time_t target_end_time = 0;
bool in_temperature_setting_mode = false;
long oldPosition;
int set_temp = 90;
bool beeped = false;
bool beeping = false;
unsigned long last_beep_time = 0;
int beep_count = 0;
bool beep_on = false;

void setup() {
  auto cfg = M5.config();
  M5Dial.begin(cfg, true, false);  // config, enableEncoder, enableRFID
  M5Dial.Rtc.setDateTime({ { 2023, 10, 25 }, { 15, 56, 56 } });
  M5Dial.Display.setBrightness(24);
  img.createSprite(240, 240);
  img.setTextDatum(middle_center);
  oldPosition = M5Dial.Encoder.read();
  pinMode(SAUNA_GPIO_PIN, OUTPUT);
  digitalWrite(SAUNA_GPIO_PIN, LOW);
  delay(200);
}

bool is_sauna_running() {
  time_t now = time(nullptr);
  return target_end_time > now;
}

void render_ui() {
  img.fillSprite(BLACK);  // Black Background
  int background_colour;
  if (is_sauna_running()) background_colour = TFT_MAROON;
  else background_colour = DEFAULT_BACKGROUND_COLOUR;
  img.fillRect(0, 0, 240, 130, background_colour);

  // Temps
  img.setTextColor(TFT_LIGHTGRAY, background_colour);
  img.drawString("--", 120 - 45, 80, &FreeSans18pt7b_mod);
  if (in_temperature_setting_mode) img.setTextColor(RED, background_colour);
  img.drawString(String(set_temp) + char(176), 120 + 45, 80, &FreeSans18pt7b_mod);

  // Labels for temps
  img.setTextColor(WHITE, background_colour);
  img.drawString("Current", 120 - 45, 110, 2);
  if (in_temperature_setting_mode) img.setTextColor(RED, background_colour);
  img.drawString("Target", 120 + 45, 110, 2);

  // Main time display
  img.setTextColor(WHITE, BLACK);
  time_t now = time(nullptr);
  unsigned long delta_seconds = (target_end_time > now) ? (target_end_time - now) : 0;
  uint8_t seconds = delta_seconds % 60;
  unsigned long mins = (delta_seconds - seconds) / 60;
  char time_string[7];  // "MM:SS\0" = 6 chars
  sprintf(time_string, "%02lu:%02d", mins, seconds);
  img.drawString(time_string, 120, 170, 7);

  //Label the bottom override button
  img.setTextColor(ORANGE, BLACK);
  img.drawString("OFF", 120, 232, 2);

  img.pushSprite(0, 0);
}

void reset() {
  target_end_time = 0;
  in_temperature_setting_mode = false;
  digitalWrite(SAUNA_GPIO_PIN, LOW);
  beeped = false;
  beeping = false;
  beep_count = 0;
}

void adjust_time(bool up) {
  time_t now = time(nullptr);
  if (!is_sauna_running()) {
    target_end_time = now;
  }
  if (up) {
    target_end_time += 60 * 5;
  } else {
    if (target_end_time < now + 60 * 5) {
      target_end_time = now;
    } else {
      target_end_time -= 60 * 5;
    }
  }
}

void control_sauna() {
  if (is_sauna_running()) {
    digitalWrite(SAUNA_GPIO_PIN, HIGH);
    beeped = false;
    beeping = false;
    beep_count = 0;
  } else {
    digitalWrite(SAUNA_GPIO_PIN, LOW);
    if (!beeped && target_end_time != 0) {
      if (!beeping) {
        beeping = true;
        last_beep_time = millis();
        beep_count = 0;
        beep_on = false;
      }

      if (beeping) {
        unsigned long now = millis();
        if (!beep_on && now - last_beep_time >= 100) {
          M5Dial.Speaker.tone(2200);
          beep_on = true;
          last_beep_time = now;
        } else if (beep_on && now - last_beep_time >= 200) {
          M5Dial.Speaker.end();
          beep_on = false;
          last_beep_time = now;
          beep_count++;
        }

        if (beep_count >= 5) {
          beeping = false;
          beeped = true;
        }
      }
    }
  }
}

static m5::touch_state_t touch_prev_state;

void loop() {
  M5Dial.update();
  if (M5Dial.BtnA.isPressed()) {
    M5Dial.Speaker.end();
    M5Dial.Speaker.tone(2800, 100);
    reset();
    delay(200);
  }

  const auto touch_sense = M5Dial.Touch.getDetail();
  if (touch_prev_state != touch_sense.state) {
    touch_prev_state = touch_sense.state;
    if (touch_sense.wasPressed()) {
      in_temperature_setting_mode = !in_temperature_setting_mode;
      M5Dial.Speaker.tone(3000, 100);
    }
  }

  long newPosition = M5Dial.Encoder.read();
  if (newPosition != oldPosition && newPosition % 4 == 0) {
    M5Dial.Speaker.tone(3600, 30);
    if (in_temperature_setting_mode) {
      if (newPosition > oldPosition) set_temp++;
      else set_temp--;
    } else {
      adjust_time(newPosition > oldPosition);
    }
    oldPosition = newPosition;
  }
  control_sauna();
  render_ui();
}
