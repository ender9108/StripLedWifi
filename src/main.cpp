#include <SPIFFS.h>
//#include <WiFi.h>
//#include <WiFiClient.h>
//#include <PubSubClient.h>
#include <ArduinoJson.h>
//#include <WebServer.h>
//#include <ESPAsyncWebServer.h>

void logger(String message, bool endLine = true);

struct Config {
  char wifiSsid[32];
  char wifiPassword[64];
  char mqttHost[128];
  int  mqttPort;
  char mqttUsername[32];
  char mqttPassword[64];
  char mqttPublishChannel[128];
  char uuid[64];
};

Config config;

const char *configFilePath = "/config.json";
const bool debug = true;

void logger(String message, bool endLine) {
  if (true == debug) {
    if (true == endLine) {
      Serial.println(message);
    } else {
      Serial.print(message);
    }
  }
}

bool getConfig() {
  File configFile = SPIFFS.open(configFilePath, FILE_READ);

  if (!configFile) {
    logger("Failed to open config file \"" + String(configFilePath) + "\".");
    return false;
  }

  size_t size = configFile.size();

  if (size == 0) {
    logger("Config file is empty !");
    return false;
  }

  if (size > 1024) {
    logger("Config file size is too large");
    return false;
  }

  StaticJsonDocument<512> json;
  deserializeJson(json, configFile);

  // Copy values from the JsonObject to the Config
  strlcpy(config.wifiSsid, json["wifiSsid"], sizeof(config.wifiSsid));
  strlcpy(config.wifiPassword, json["wifiPassword"], sizeof(config.wifiPassword));
  strlcpy(config.mqttHost, json["mqttHost"], sizeof(config.mqttHost));
  config.mqttPort = json["mqttPort"] | 1883;
  strlcpy(config.mqttUsername, json["mqttUsername"], sizeof(config.mqttUsername));
  strlcpy(config.mqttPassword, json["mqttPassword"], sizeof(config.mqttPassword));
  strlcpy(config.mqttPublishChannel, json["mqttPublishChannel"], sizeof(config.mqttPublishChannel));
  strlcpy(config.uuid, json["uuid"], sizeof(config.uuid));

  configFile.close();

  logger("wifiSsid : ", false);
  logger(String(config.wifiSsid));
  logger("wifiPassword : ", false);
  logger(String(config.wifiPassword));
  logger("mqttHost : ", false);
  logger(String(config.mqttHost));
  logger("mqttPort : ", false);
  logger(String(config.mqttPort));
  logger("mqttUsername : ", false);
  logger(String(config.mqttUsername));
  logger("mqttPassword : ", false);
  logger(String(config.mqttPassword));
  logger("mqttPublishChannel : ", false);
  logger(String(config.mqttPublishChannel));
  logger("uuid : ", false);
  logger(String(config.uuid));

  return true;
}

bool setConfig() {
  StaticJsonDocument<512> json;
  
  json["wifiSsid"] = String(config.wifiSsid);
  json["wifiPassword"] = String(config.wifiPassword);
  json["mqttHost"] = String(config.mqttHost);
  json["mqttPort"] = config.mqttPort;
  json["mqttUsername"] = String(config.mqttUsername);
  json["mqttPassword"] = String(config.mqttPassword);
  json["mqttPublishChannel"] = String(config.mqttPublishChannel);

  if (strlen(config.uuid) == 0) {
    uint32_t tmpUuid = esp_random();
    String(tmpUuid).toCharArray(config.uuid, 64);
    json["uuid"] = String(config.uuid);
  }

  /*if (SPIFFS.exists(configFilePath)) {
    SPIFFS.remove(configFilePath);
  }*/

  File configFile = SPIFFS.open(configFilePath, FILE_WRITE);
  
  if (!configFile) {
    logger("Failed to open config file for writing");
    return false;
  }

  //logger(json["wifiSsid"]);
  serializeJson(json, configFile);
  serializeJson(json, Serial);

  configFile.close();

  delay(100);
  getConfig();

  return true;
}

void setup() {
  Serial.begin(115200);
  logger("Start program !");

  if (!SPIFFS.begin(true)) {
    logger("An Error has occurred while mounting SPIFFS");
    return;
  }

  logger("SPIFFS mounted");

  getConfig();

  String("test.com").toCharArray(config.wifiSsid, 32);

  setConfig();
}

void loop() {
  
}