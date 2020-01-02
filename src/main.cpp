#include <WiFi.h>
#include <WiFiClient.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <WebServer.h>
#include <SPIFFS.h>
#include <ESPAsyncWebServer.h>

/*struct Config {
  char uuid[64] = "a1a63e0b-3271-4377-988b-a6381a94b3f0";
  String deviceName = "RgbLedStrip-" + String(uuid);
  char wifiSsid[32] = "Livebox-D1BD";
  char wifiPassword[32] = "962607AA57F5051A7F86F9639A";
  IPAddress mqttHost = IPAddress(192,168,1,17);
  int  mqttPort = 1883;
  char mqttUsername[12] = "marvin";
  char mqttPassword[12] = "q1pl+PL-";
  char mqttChannel[64] = "/marvin/devices";
  String mqttChannelCallback = "/marvin/devices/" + String(uuid);
};*/

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


WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);
Config config;
AsyncWebServer server(80);

const char *configFilePath = "/config.json";
const char *mqttName = "StripLedWifi";
const char *wifiApSsid = "strip-led-wifi-ssid";
const char *wifiApPassw = "strip-led-wifi-passw";
//, wifiPassword

bool wifiConnected = false;
bool mqttConnected = false;
bool startApp = false;
bool lightOn = false;
bool debug = true;

void logger(String message, bool endLine) {
  if (true == debug) {
    if (true == endLine) {
      logger(message);
    } else {
      Serial.print(message);
    }
  }
}

bool getConfig() {
  File configFile = SPIFFS.open(configFilePath, "r");

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

  // Allocate a buffer to store contents of the file.
  std::unique_ptr<char[]> buf(new char[size]);

  // We don't use String here because ArduinoJson library requires the input
  // buffer to be mutable. If you don't use ArduinoJson, you may as well
  // use configFile.readString instead.
  configFile.readBytes(buf.get(), size);

  StaticJsonDocument<512> json;
  DeserializationError error = deserializeMsgPack(json, buf.get());

  // Test if parsing succeeds.
  if (error) {
    logger(F("deserializeMsgPack() failed: "), false);
    logger(error.c_str());
    return false;
  }

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
  
  json["wifiSsid"] = config.wifiSsid;
  json["wifiPassword"] = config.wifiPassword;
  json["mqttHost"] = config.mqttHost;
  json["mqttPort"] = config.mqttPort;
  json["mqttUsername"] = config.mqttUsername;
  json["mqttPassword"] = config.mqttPassword;
  json["mqttPublishChannel"] = config.mqttPublishChannel;

  if (strlen(config.uuid) == 0) {
    uint32_t tmpUuid = esp_random();
    String(tmpUuid).toCharArray(config.uuid, 64);
    json["uuid"] = config.uuid;
  }

  if (SPIFFS.exists(configFilePath)) {
    SPIFFS.remove(configFilePath);
  }

  File configFile = SPIFFS.open(configFilePath, "w");
  
  if (!configFile) {
    logger("Failed to open config file for writing");
    return false;
  }

  serializeJson(json, configFile);

  delay(100);
  getConfig();

  return true;
}


bool checkWifiConfigValues() {
  logger("config.wifiSsid length : ", false);
  logger(String(strlen(config.wifiSsid)));

  logger("config.wifiPassword length : ", false);
  logger(String(strlen(config.wifiPassword)));
  
  if ( strlen(config.wifiSsid) > 1 && strlen(config.wifiPassword) > 1 ) {
    return true;
  }

  logger("Ssid and passw not present in SPIFFS");
  return false;
}

bool wifiConnect() {
  unsigned int count = 0;
  WiFi.begin(config.wifiSsid, config.wifiPassword);
  Serial.print("Try to connect to ");
  logger(config.wifiSsid);

  while (count < 20) {
    if (WiFi.status() == WL_CONNECTED) {
      logger("");
      Serial.print("WiFi connected (IP : ");  
      Serial.print(WiFi.localIP());
      logger(")");
  
      return true;
    } else {
      delay(500);
      Serial.print(".");  
    }

    count++;
  }

  Serial.print("Error connection to ");
  logger(config.wifiSsid);
  return false;
}

bool mqttConnect() {
    mqttClient.setServer(config.mqttHost, config.mqttPort);
    int count = 0;

    while (!mqttClient.connected()) {
        Serial.print("Attempting MQTT connection (host: " + String(config.mqttHost) + ")...");
        // Attempt to connect
        if (mqttClient.connect(mqttName, config.mqttUsername, config.mqttPassword)) {
            logger("connected");
            return true;
        } else {
            Serial.print("failed, rc=");
            Serial.print(mqttClient.state());
            logger(" try again in 5 seconds");
            // Wait 5 seconds before retrying
            delay(5000);

            if (count == 10) {
              return false;
            }
        }

        count++;
    }

    return false;
}

void serverConfig() {
  /*
  server.on("/", httpParametersPage);
  server.on("/save", HTTP_POST, httpSaveParameters);
  server.on("/restart", httpRestartEsp);
  server.begin();
  */
  server.begin();
  logger("HTTP server started");
}

void setup() {
  Serial.begin(9600);
  SPIFFS.begin();

  // Get wifi SSID and PASSW from SPIFFS
  if (true == getConfig()) {
    if (true == checkWifiConfigValues()) {
      wifiConnected = wifiConnect();
  
      if (true == wifiConnected) {
        mqttConnected = mqttConnect();
      }
    }
  } // endif true == getConfig()

  if (false == wifiConnected || false == mqttConnected) {
    startApp = false;
  } else {
    startApp = true;
  }

  if (false == startApp) {
    WiFi.mode(WIFI_AP);
    WiFi.softAP(wifiApSsid, wifiApPassw);
    logger("WiFi AP is ready (IP : ", false);  
    logger(WiFi.softAPIP().toString(), false);
    logger(")");

    serverConfig();
  } else {

  }
}

void loop() {
  if (true == startApp) {
    mqttClient.loop();
  }
}

/*void callback(char* topic, byte* payload, unsigned int length) {
  String payloadBuffer = "";

  for (int i=0;i<length;i++) {
    payloadBuffer += (char)payload[i];
  }

  logger(payloadBuffer);
  StaticJsonDocument<512> doc;
  DeserializationError error = deserializeMsgPack(doc, payloadBuffer);

  // Test if parsing succeeds.
  if (error) {
    Serial.print(F("deserializeMsgPack() failed: "));
    logger(error.c_str());
    return;
  }
}

void handlePing() {
  server.send(200, "applicaton/json", "{\"status\": \"ok\", \"message\": \"pong\"}");
}*/