#include "M5Dial.h"
#include "temperature_font.h"
#include "logos.h"
#include <WiFiManager.h>
#include <PubSubClient.h>
#include <TaskScheduler.h>

#define DEFAULT_BACKGROUND_COLOUR 0x0A2D
#define SAUNA_GPIO_PIN 1

M5Canvas img(&M5Dial.Display);

//Internal Statuses
time_t target_end_time = 0;
int target_temp = 90;
bool in_temperature_setting_mode = false;
bool mqttConnected = false;
bool wifiConnected = false;
bool isAPMode = false;
bool heating_element_on = false;

long oldPosition;


//TODO: Fix beeping. It should be its own function in loop, do it with a queue or something, with channels.
//TODO: Better wifi/mqtt config. Currently a mix of config'ed wifi, but hardcoded mqtt. Some stuff commented out. Might need to force config mode on button hold or something to configure it.
//  Might also need to figure out way to store data. Wanted to just use EEPROM, but that might not be easy anymore.
//TODO: Need to get it to try to reconnect after coming back from wifi. Right now it fails and doesn't try til next boot after creds are put in.

WiFiManager wifiManager;
// WiFiManagerParameter custom_mqtt_server("server", "mqtt server");
// WiFiManagerParameter custom_mqtt_user("server", "mqtt server");
// WiFiManagerParameter custom_mqtt_password("server", "mqtt server");
WiFiClient espClient;
PubSubClient mqttClient(espClient);
Scheduler ts;

void setup_mqtt() {
  // const char* mqtt_server = custom_mqtt_server.getValue();
  // if (strlen(mqtt_server) == 0) mqtt_server = "homeassistant.local";
  // const char* mqtt_user = custom_mqtt_user.getValue();
  // if (strlen(mqtt_user) == 0) mqtt_user = "device";
  // const char* mqtt_password = custom_mqtt_password.getValue();
  // if (strlen(mqtt_password) == 0) mqtt_password = "local_device";
  mqttClient.setServer("homeassistant.local", 1883);
  mqttClient.setCallback(mqttCallback);
  if (wifiConnected) {
    mqttConnected = mqttClient.connect("sauna_m5dial", "device", "local_device");
    if (mqttConnected) {
      mqttClient.subscribe("sauna/control/#");
    }
  }
}

void show_boot_screen() {
  M5Dial.Display.fillScreen(BLACK);
  M5Dial.Display.setTextDatum(middle_center);
  M5Dial.Display.setTextColor(WHITE);
  M5Dial.Display.drawString("Booting...", 120, 120, 4);
}

void connect_wifi() {
  wifiManager.setConfigPortalBlocking(false);
  wifiManager.setConfigPortalTimeout(120);
  wifiManager.setConnectTimeout(15);
  // wifiManager.addParameter(&custom_mqtt_server);
  // wifiManager.addParameter(&custom_mqtt_user);
  // wifiManager.addParameter(&custom_mqtt_password);
  // wifiManager.setSaveParamsCallback(wifiManagerSaveParamsCallback);
  wifiConnected = wifiManager.autoConnect("Sauna Controller Setup");
  isAPMode = wifiManager.getConfigPortalActive();
}

void render_ui() {
  img.fillSprite(BLACK);
  int background_colour = should_sauna_be_active() ? TFT_MAROON : DEFAULT_BACKGROUND_COLOUR;
  img.fillRect(0, 0, 240, 130, background_colour);

  // Wifi/MQTT status icons
  img.drawBitmap(120 - 50, 20, wifi_logo, 24, 17, isAPMode ? ORANGE : (wifiConnected ? GREEN : RED));
  img.drawBitmap(120 + 50 - 12, 20, mqtt_logo, 17, 17, mqttConnected ? GREEN : RED);

  img.setTextColor(TFT_LIGHTGRAY, background_colour);
  img.drawString(String(get_current_temp()) + char(176), 120 - 45, 80, &FreeSans18pt7b_mod);
  if (in_temperature_setting_mode) img.setTextColor(RED, background_colour);
  img.drawString(String(target_temp) + char(176), 120 + 45, 80, &FreeSans18pt7b_mod);

  img.setTextColor(WHITE, background_colour);
  img.drawString("Current", 120 - 45, 110, 2);
  if (in_temperature_setting_mode) img.setTextColor(RED, background_colour);
  img.drawString("Target", 120 + 45, 110, 2);

  img.setTextColor(WHITE, BLACK);
  time_t now = time(nullptr);
  unsigned long delta_seconds = (target_end_time > now) ? (target_end_time - now) : 0;
  uint8_t seconds = delta_seconds % 60;
  unsigned long mins = (delta_seconds - seconds) / 60;

  char time_string[7];
  sprintf(time_string, "%02lu:%02d", mins, seconds);
  img.drawString(time_string, 120, 170, 7);

  img.setTextColor(ORANGE, BLACK);
  img.drawString("RESET", 120, 232, 2);

  img.pushSprite(0, 0);
}

int get_current_temp() {
  // Stub - replace with real sensor value later
  return 20;
}

void reset_params() {
  target_end_time = 0;
  in_temperature_setting_mode = false;
  target_temp = 0;
  mqttPublisherCallback();
}

void handle_button() {
  if (M5Dial.BtnA.wasPressed()) {
    M5Dial.Speaker.tone(2800, 100);
    //wifiManager.resetSettings(); // Was for testing WIFI
    reset_params();
  }
}

bool should_sauna_be_active() {
  return target_end_time > time(nullptr);
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String message;
  for (unsigned int i = 0; i < length; i++) message += (char)payload[i];
  if (String(topic) == "sauna/control/target_temp") target_temp = message.toInt();
  else if (String(topic) == "sauna/control/start") {
    target_end_time = time(nullptr) + message.toInt();
  } else if (String(topic) == "sauna/control/stop") {
    reset_params();
  }
}
static m5::touch_state_t touch_prev_state;
void handle_touch() {
  const auto touch_sense = M5Dial.Touch.getDetail();
  if (touch_prev_state != touch_sense.state) {
    touch_prev_state = touch_sense.state;
    if (touch_sense.wasPressed()) {
      in_temperature_setting_mode = !in_temperature_setting_mode;
      M5Dial.Speaker.tone(3000, 100);
    }
  }
}

void handle_dial() {
  long newPosition = M5Dial.Encoder.read();
  if (newPosition != oldPosition && newPosition % 4 == 0) {
    bool spun_dial_up = newPosition > oldPosition;
    M5Dial.Speaker.tone(3600, 30);
    if (in_temperature_setting_mode) {
      if (spun_dial_up) target_temp++;
      else target_temp--;
    } else {
      time_t now = time(nullptr);
      if (!should_sauna_be_active()) target_end_time = now;
      if (spun_dial_up) target_end_time += 60 * 10;
      else target_end_time = (target_end_time < now + 60 * 5) ? now : target_end_time - 60 * 10;
    }
    mqttPublisherCallback();
  }
  oldPosition = newPosition;
}

void mqttPublisherCallback() {
  if (!mqttClient.connected()) return;

  mqttClient.publish("sauna/status/target_temp", String(target_temp).c_str());
  mqttClient.publish("sauna/status/current_temp", String(get_current_temp()).c_str());

  time_t now = time(nullptr);
  int remaining = (target_end_time > now) ? (target_end_time - now) : 0;
  mqttClient.publish("sauna/status/remaining_time", String(remaining).c_str());
  mqttClient.publish("sauna/status/is_on", should_sauna_be_active() ? "true" : "false");
  mqttClient.publish("sauna/status/is_heating", heating_element_on ? "true" : "false");
}
void updateInternalStatusCallback() {
  //Internal Statuses
  mqttConnected = mqttClient.connected();
  wifiConnected = WiFi.status() == WL_CONNECTED;
  isAPMode = wifiManager.getConfigPortalActive();
}
void control_sauna() {
  if (should_sauna_be_active() && target_temp > get_current_temp()) {
    digitalWrite(SAUNA_GPIO_PIN, HIGH);
    heating_element_on = true;
  } else {
    digitalWrite(SAUNA_GPIO_PIN, LOW);
    heating_element_on = false;
  }
}

Task mqttPublisherTaskFast(3 * 1000, TASK_FOREVER, &mqttPublisherCallback);
Task mqttPublisherTaskSlow(60 * 1000, TASK_FOREVER, &mqttPublisherCallback);
Task saunaControlTask(20 * 1000, TASK_FOREVER, &control_sauna);
Task updateInternalStatusTask(200, TASK_FOREVER, &updateInternalStatusCallback);
void setup() {
  Serial.begin(115200);
  Serial.println("m5");
  auto cfg = M5.config();
  M5Dial.begin(cfg, true, false);
  Serial.println("ShotBoow");
  show_boot_screen();
  Serial.println("connectWifi");
  connect_wifi();
  Serial.println("SetupMQTT");
  setup_mqtt();
  Serial.println("ts.init");
  ts.init();
  ts.addTask(mqttPublisherTaskFast);
  ts.addTask(mqttPublisherTaskSlow);
  mqttPublisherTaskSlow.enable();

  ts.addTask(saunaControlTask);
  saunaControlTask.enable();

  ts.addTask(updateInternalStatusTask);
  saunaControlTask.enable();

  M5Dial.Display.setBrightness(24);
  img.createSprite(240, 240);
  img.setTextDatum(middle_center);
  oldPosition = M5Dial.Encoder.read();
  pinMode(SAUNA_GPIO_PIN, OUTPUT);
  digitalWrite(SAUNA_GPIO_PIN, LOW);
  delay(500);
}

// LOOP TIME
void loop() {
  M5Dial.update();
  mqttClient.loop();
  wifiManager.process();
  if (should_sauna_be_active()) {
    mqttPublisherTaskFast.enableIfNot();
  } else {
    mqttPublisherTaskFast.disable();
  }
  ts.execute();  // Run any tasks are scheduled.
    // Sauna control
    // MQTT publush

  // Handle User intactions with physical button
  handle_button();
  handle_touch();
  handle_dial();

  // Make things pretty
  render_ui();
}