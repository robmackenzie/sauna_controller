#include "M5Dial.h"
#include "temperature_font.h"
#include "logos.h"
#include <WiFiManager.h>
#include <PubSubClient.h>
#include <TaskScheduler.h>
#include <Preferences.h>


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


// Parameter variables
char mqtt_server[40];
int mqtt_port;
char mqtt_username[40];
char mqtt_password[40];
//char mqtt_topic[40] = "sauna";

// Other
long oldPosition;

//TODO: Fix beeping. It should be its own function in loop, do it with a queue or something, with channels.

WiFiManager wifiManager;
WiFiManagerParameter custom_mqtt_server("server", "MQTT Server", "homeassistant.local", 40);
WiFiManagerParameter custom_mqtt_port("port", "MQTT port", "1883", 6);
WiFiManagerParameter custom_mqtt_user("user", "MQTT User","",40);
WiFiManagerParameter custom_mqtt_password("password", "MQTT Password","",40);
//WiFiManagerParameter custom_mqtt_topic("topic", "MQTT Topic");
WiFiClient espClient;
PubSubClient mqttClient(espClient);
Scheduler ts;
Preferences stored_config;

void setup_mqtt() {
  Serial.println("setup_mqtt");

  strcpy(mqtt_server,stored_config.getString("mqtt_server").c_str());
  mqtt_port=stored_config.getInt("mqtt_port", mqtt_port);
  strcpy(mqtt_username,stored_config.getString("mqtt_username").c_str());
  strcpy(mqtt_password,stored_config.getString("mqtt_password").c_str());

  if (strlen(mqtt_server) == 0) {
    Serial.println("No Stored MQTT, using defaults");
    strcpy(mqtt_server, "homeassistant.local");
    mqtt_port = 1883;
    strcpy(mqtt_username, "device");
    strcpy(mqtt_password, "local_device");
    //strcpy(mqtt_topic, "sauna");
  }
  
  Serial.println(mqtt_server);
  Serial.println(mqtt_port);
  Serial.println(mqtt_username);
  Serial.println(mqtt_password);
  Serial.println(wifiConnected);
  Serial.println(mqttClient.connected());

  mqttClient.setServer(mqtt_server, mqtt_port);
  mqttClient.setCallback(mqttCallback);
  if (wifiConnected && !mqttClient.connected()) {
    mqttConnected = mqttClient.connect("sauna_m5dial", mqtt_username, mqtt_password);
    if (mqttConnected) {
      mqttClient.subscribe("sauna/control/#");
    }
  }
}
void saveConfigCallback() {
  Serial.println("saveConfigCallback");

  strcpy(mqtt_server, custom_mqtt_server.getValue());
  mqtt_port = atoi(custom_mqtt_port.getValue());
  strcpy(mqtt_username, custom_mqtt_user.getValue());
  strcpy(mqtt_password, custom_mqtt_password.getValue());

  stored_config.putString("mqtt_server", mqtt_server);
  stored_config.putInt("mqtt_port", mqtt_port);
  stored_config.putString("mqtt_username", mqtt_username);
  stored_config.putString("mqtt_password", mqtt_password);
  //preferences.putString("mqtt_topic", mqtt_topic);
}

void show_boot_screen() {
  Serial.println("show_boot_screen");
  M5Dial.Display.fillScreen(BLACK);
  M5Dial.Display.setTextDatum(middle_center);
  M5Dial.Display.setTextColor(WHITE);
  M5Dial.Display.drawString("Booting...", 120, 120, 4);
}

void connect_wifi() {
  Serial.println("connect_wifi");
  wifiManager.setConfigPortalBlocking(false);
  wifiManager.setConfigPortalTimeout(120);
  wifiManager.setConnectTimeout(5);
  wifiManager.addParameter(&custom_mqtt_server);
  wifiManager.addParameter(&custom_mqtt_port);
  wifiManager.addParameter(&custom_mqtt_user);
  wifiManager.addParameter(&custom_mqtt_password);
  wifiManager.setSaveParamsCallback(saveConfigCallback);
  wifiConnected = wifiManager.autoConnect("Sauna Controller Setup");
  isAPMode = wifiManager.getConfigPortalActive();
}
void render_ui() {
  img.fillSprite(BLACK);
  int background_colour = should_sauna_be_active() ? TFT_MAROON : DEFAULT_BACKGROUND_COLOUR;
  img.fillRect(0, 0, 240, 130, background_colour);

  // Wifi/MQTT status icons
  img.drawBitmap(120 - 30, 15, wifi_logo, 24, 17, isAPMode ? ORANGE : (wifiConnected ? GREEN : RED));
  img.drawBitmap(120 + 30 - 12, 15, mqtt_logo, 17, 17, mqttConnected ? GREEN : RED);

  img.setTextColor(TFT_LIGHTGRAY, background_colour);
  img.drawString(String(get_current_temp()) + char(176), 120 - 65, 80, &FreeSans18pt7b_mod);
  if (in_temperature_setting_mode) img.setTextColor(RED, background_colour);
  img.drawString(String(target_temp) + char(176), 120 + 65, 80, &FreeSans18pt7b_mod);

  img.setTextColor(WHITE, background_colour);
  img.drawString("Current", 120 - 65, 110, 2);
  if (in_temperature_setting_mode) img.setTextColor(RED, background_colour);
  img.drawString("Target", 120 + 65, 110, 2);

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
  target_temp = 90;
  mqttPublisherCallback();
}

void handle_button() {
  if (M5Dial.BtnA.wasPressed()) {
    M5Dial.Speaker.tone(2800, 100);
    //wifiManager.resetSettings();  // Was for testing WIFI
    reset_params();
  }
}

bool should_sauna_be_active() {
  return target_end_time > time(nullptr);
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  Serial.println("mqttCallback");
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
  Serial.println("mqttPublisherCallback");
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
Task keepTryingToConnectToMQTT(40 * 1000,TASK_FOREVER, &setup_mqtt);
void setup() {
  stored_config.begin("sauna-controller", false);
  Serial.begin(115200);
  Serial.println("setup");
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
  updateInternalStatusTask.enable();

  ts.addTask(keepTryingToConnectToMQTT);
  keepTryingToConnectToMQTT.enable();

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

  control_sauna();  // Calling this here for now to have instant updates. Maybe only do on scheduler to reduce overhead on checking and not spam the relays
  // Make things pretty
  render_ui();
}