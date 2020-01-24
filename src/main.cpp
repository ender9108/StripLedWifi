#include <Arduino.h>
#include <SPIFFS.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>

void logger(String message, bool endLine = true);

struct Config {
  char wifiSsid[32];
  char wifiPassword[64];
  char mqttHost[128];
  int  mqttPort = 1883;
  char mqttUsername[32];
  char mqttPassword[64];
  char mqttPublishChannel[128];
  char mqttSubscribeChannel[128];
  char uuid[64];
};

WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);
Config config;
AsyncWebServer server(80);

const int restartButtonPin = 23;
const int ledStripRedPin = 19;
const int ledStripGreenPin = 18;
const int ledStripBluePin = 5;

const char *configFilePath = "/config.json";
const bool debug = true;
const char *mqttName = "StripLedWifi";
const char *wifiApSsid = "strip-led-wifi-ssid";
const char *wifiApPassw = "strip-led-wifi-passw";
const char *appName = "Marvin led strip wifi";

bool wifiConnected = false;
bool mqttConnected = false;
bool startApp = false;
bool lightOn = false;
String errorMessage = "";
unsigned long restartRequested = 0;

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
    logger(F("Config file is empty !"));
    return false;
  }

  if (size > 1024) {
    logger(F("Config file size is too large"));
    return false;
  }

  StaticJsonDocument<512> json;
  DeserializationError err = deserializeJson(json, configFile);

  switch (err.code()) {
    case DeserializationError::Ok:
        logger(F("Deserialization succeeded"));
        break;
    case DeserializationError::InvalidInput:
        logger(F("Invalid input!"));
        return false;
        break;
    case DeserializationError::NoMemory:
        logger(F("Not enough memory"));
        return false;
        break;
    default:
        logger(F("Deserialization failed"));
        return false;
        break;
  }

  // Copy values from the JsonObject to the Config
  if (
    !json.containsKey("wifiSsid") ||
    !json.containsKey("wifiPassword") ||
    !json.containsKey("mqttHost") ||
    !json.containsKey("mqttPort") ||
    !json.containsKey("mqttUsername") ||
    !json.containsKey("mqttPassword") ||
    !json.containsKey("mqttPublishChannel") ||
    !json.containsKey("mqttSubscribeChannel") ||
    !json.containsKey("uuid")
  ) {
    logger("getConfig");
    serializeJson(json, Serial);
    logger("");
    logger(F("Key not found in json fille"));
    return false;
  }

  strlcpy(config.wifiSsid, json["wifiSsid"], sizeof(config.wifiSsid));
  strlcpy(config.wifiPassword, json["wifiPassword"], sizeof(config.wifiPassword));
  strlcpy(config.mqttHost, json["mqttHost"], sizeof(config.mqttHost));
  config.mqttPort = json["mqttPort"] | 1883;
  strlcpy(config.mqttUsername, json["mqttUsername"], sizeof(config.mqttUsername));
  strlcpy(config.mqttPassword, json["mqttPassword"], sizeof(config.mqttPassword));
  strlcpy(config.mqttPublishChannel, json["mqttPublishChannel"], sizeof(config.mqttPublishChannel));
  strlcpy(config.mqttSubscribeChannel, json["mqttSubscribeChannel"], sizeof(config.mqttSubscribeChannel));
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
  logger("mqttSubscribeChannel : ", false);
  logger(String(config.mqttSubscribeChannel));
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
  json["mqttSubscribeChannel"] = String(config.mqttSubscribeChannel);

  if (strlen(config.uuid) == 0) {
    uint32_t tmpUuid = esp_random();
    String(tmpUuid).toCharArray(config.uuid, 64);
  }

  json["uuid"] = String(config.uuid);

  File configFile = SPIFFS.open(configFilePath, FILE_WRITE);
  
  if (!configFile) {
    logger("Failed to open config file for writing");
    return false;
  }

  serializeJson(json, configFile);

  configFile.close();

  delay(100);
  getConfig();

  return true;
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
  logger(String(config.wifiSsid));
  errorMessage = "Wifi connection error to " + String(config.wifiSsid);
  return false;
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

bool mqttConnect() {
    int count = 0;

    while (!mqttClient.connected()) {
        logger("Attempting MQTT connection (host: " + String(config.mqttHost) + ")...");
        // Attempt to connect
        if (mqttClient.connect(mqttName, config.mqttUsername, config.mqttPassword)) {
            logger("connected !");
            mqttClient.subscribe(config.mqttSubscribeChannel);
            return true;
        } else {
            logger("failed, rc=", false);
            logger(String(mqttClient.state()));
            logger(" try again in 5 seconds");
            // Wait 5 seconds before retrying
            delay(5000);

            if (count == 10) {
              errorMessage = "Mqtt connection error to " + String(config.mqttHost);
              return false;
            }
        }

        count++;
    }

    errorMessage = "Mqtt connection error to " + String(config.mqttHost);
    return false;
}

String processor(const String& var){
  Serial.println(var);

  if (var == "TITLE" || var == "MODULE_NAME"){
    return String(appName);
  } else if (var == "WIFI_SSID") {
    return String(config.wifiSsid);
  } else if (var == "WIFI_PASSWD") {
    return String(config.wifiPassword);
  } else if (var == "MQTT_HOST") {
    return String(config.mqttHost);
  } else if (var == "MQTT_PORT") {
    return String(config.mqttPort);
  } else if (var == "MQTT_USERNAME") {
    return String(config.mqttUsername);
  } else if (var == "MQTT_PASSWD") {
    return String(config.mqttPassword);
  } else if (var == "MQTT_PUB_CHAN") {
    return String(config.mqttPublishChannel);
  } else if (var == "MQTT_SUB_CHAN") {
    return String(config.mqttSubscribeChannel);
  } else if (var == "ERROR_MESSAGE") {
    return errorMessage;
  } else if (var == "ERROR_HIDDEN") {
    if (errorMessage.length() == 0) {
      return String("d-none");
    }
  }

  return String();
}

void restart() {
  ESP.restart();
}

void serverConfig() {
  server.on("/", HTTP_GET, [] (AsyncWebServerRequest *request) {
      request->send(SPIFFS, "/index.html", "text/html", false, processor);
  });
  server.on("/bootstrap.min.css", HTTP_GET, [] (AsyncWebServerRequest *request) {
      request->send(SPIFFS, "/bootstrap.min.css", "text/css");
  });
  server.on("/save", HTTP_POST, [] (AsyncWebServerRequest *request) {
      int params = request->params();

      for (int i = 0 ; i < params ; i++) {
        AsyncWebParameter* p = request->getParam(i);

        Serial.printf("POST[%s]: %s\n", p->name().c_str(), p->value().c_str());

        if (p->name() == "wifiSsid") {
          strlcpy(config.wifiSsid, p->value().c_str(), sizeof(config.wifiSsid));
        } else if (p->name() == "wifiPasswd") {
          strlcpy(config.wifiPassword, p->value().c_str(), sizeof(config.wifiPassword));
        } else if (p->name() == "mqttHost") {
          strlcpy(config.mqttHost, p->value().c_str(), sizeof(config.mqttHost));
        } else if (p->name() == "mqttPort") {
          config.mqttPort = p->value().toInt();
        } else if (p->name() == "mqttUsername") {
          strlcpy(config.mqttUsername, p->value().c_str(), sizeof(config.mqttUsername));
        } else if (p->name() == "mqttPasswd") {
          strlcpy(config.mqttPassword, p->value().c_str(), sizeof(config.mqttPassword));
        } else if (p->name() == "mqttPublishChannel") {
          strlcpy(config.mqttPublishChannel, p->value().c_str(), sizeof(config.mqttPublishChannel));
        } else if (p->name() == "mqttSubscribeChannel") {
          strlcpy(config.mqttSubscribeChannel, p->value().c_str(), sizeof(config.mqttSubscribeChannel));
        }
      }
      // save config
      setConfig();

      request->send(SPIFFS, "/restart.html", "text/html", false, processor);
  });
  server.on("/restart", HTTP_GET, [] (AsyncWebServerRequest *request) {
      restart();
  });
  server.onNotFound([](AsyncWebServerRequest *request){
      request->send(SPIFFS, "/404.html", "text/html", false, processor);
  });

  server.begin();
  logger("HTTP server started");
}

void setLightColor(unsigned int red, unsigned int green, unsigned int blue) {
  ledcWrite(1, red);
  ledcWrite(2, green);
  ledcWrite(3, blue);

  if(
    red == 0 &&
    green == 0 &&
    blue == 0
  ) {
    lightOn = false;
  } else {
    lightOn = true;
  }
}

void callback(char* topic, byte* payload, unsigned int length) {
  StaticJsonDocument<256> json;
  deserializeJson(json, payload, length);
  
  //serializeJson(json, Serial);
  char response[256];
  
  if (json.containsKey("action")) {
    JsonVariant action = json["action"];

    if (json["action"] == "ping") {
      sprintf(response, "{\"code\": \"200\", \"actionCalled\": \"%s\" \"payload\": \"pong\"}", action.as<char *>());
    } 
    else if (json["action"] == "status") {
      int status = 0;

      if(lightOn == true) {
        status = 1;
      }

      if (restartRequested != 0) {
        sprintf(response, "{\"code\": \"200\", \"actionCalled\": \"%s\", \"payload\": \"Restart in progress\"}", action.as<char *>());
      } else {
        sprintf(response, "{\"code\": \"200\", \"actionCalled\": \"%s\", \"payload\": \"%d\"}", action.as<char *>(), status);
      }
    } 
    else if (json["action"] == "configure") {
        String message  = "{";
        message        += "    \"code\": \"200\",";
        message        += "    \"actionCalled\": \"" + json["action"] + "\",";
        message        += "    \"payload\": {";
        message        += "        \"ip\": \"" + WiFi.localIP().toString() + "\",";
        message        += "        \"Mac address\": \"" + WiFi.macAddress() + "\",";
        message        += "        \"protocol\": \"mqtt\",";
        message        += "        \"port\": \"" + String(config.mqttPort) + "\",";
        message        += "        \"actions\": [";
        message        += "            {";
        message        += "                \"action\": \"ping\",";
        message        += "                \"payload\": null,";
        message        += "                \"response\": {";
        message        += "                    \"code\": {";
        message        += "                        \"type\": \"integer\",";
        message        += "                        \"value\": \"[200, 500]\",";
        message        += "                        \"definition\": {";
        message        += "                            \"200\": \"ok\",";
        message        += "                            \"500\": \"error\"";
        message        += "                        }";
        message        += "                    },";
        message        += "                    \"actionCalled\": {";
        message        += "                        \"type\": \"string\"";
        message        += "                    },";
        message        += "                    \"payload\": {";
        message        += "                        \"type\": \"string\"";
        message        += "                    }";
        message        += "                }";
        message        += "            },{";
        message        += "                \"action\": \"status\",";
        message        += "                \"payload\": null,";
        message        += "                \"response\": {";
        message        += "                    \"code\": {";
        message        += "                        \"type\": \"integer\",";
        message        += "                        \"value\": \"[200, 500]\",";
        message        += "                        \"definition\": {";
        message        += "                            \"200\": \"ok\",";
        message        += "                            \"500\": \"error\"";
        message        += "                        }";
        message        += "                    },";
        message        += "                    \"actionCalled\": {";
        message        += "                        \"type\": \"string\"";
        message        += "                    },";
        message        += "                    \"payload\": {";
        message        += "                        \"type\": \"string\"";
        message        += "                    }";
        message        += "                }";
        message        += "            }, {";
        message        += "                \"action\": \"lightOn\",";
        message        += "                \"payload\": null,";
        message        += "                \"response\": {";
        message        += "                    \"code\": {";
        message        += "                        \"type\": \"integer\",";
        message        += "                        \"value\": \"[200, 500]\",";
        message        += "                        \"definition\": {";
        message        += "                            \"200\": \"ok\",";
        message        += "                            \"500\": \"error\"";
        message        += "                        }";
        message        += "                    },";
        message        += "                    \"actionCalled\": {";
        message        += "                        \"type\": \"string\"";
        message        += "                    },";
        message        += "                    \"payload\": {";
        message        += "                        \"type\": \"string\"";
        message        += "                    }";
        message        += "                }";
        message        += "            }, {";
        message        += "                \"action\": \"lightOff\",";
        message        += "                \"payload\": null,";
        message        += "                \"response\": {";
        message        += "                    \"code\": {";
        message        += "                        \"type\": \"integer\",";
        message        += "                        \"value\": \"[200, 500]\",";
        message        += "                        \"definition\": {";
        message        += "                            \"200\": \"ok\",";
        message        += "                            \"500\": \"error\"";
        message        += "                        }";
        message        += "                    },";
        message        += "                    \"actionCalled\": {";
        message        += "                        \"type\": \"string\"";
        message        += "                    },";
        message        += "                    \"payload\": {";
        message        += "                        \"type\": \"string\"";
        message        += "                    }";
        message        += "                }";
        message        += "            }, {";
        message        += "                \"action\": \"changeColor\",";
        message        += "                \"payload\": {";
        message        += "                    \"red\": {";
        message        += "                        \"type\": \"integer\",";
        message        += "                        \"value\": \"[0,255]\"";
        message        += "                    },";
        message        += "                    \"green\": {";
        message        += "                        \"type\": \"integer\",";
        message        += "                        \"value\": \"[0,255]\"";
        message        += "                    },";
        message        += "                    \"blue\": {";
        message        += "                        \"type\": \"integer\",";
        message        += "                        \"value\": \"[0,255]\"";
        message        += "                    }";
        message        += "                },";
        message        += "                \"response\": {";
        message        += "                    \"code\": {";
        message        += "                        \"type\": \"integer\",";
        message        += "                        \"value\": \"[200, 500]\",";
        message        += "                        \"definition\": {";
        message        += "                            \"200\": \"ok\",";
        message        += "                            \"500\": \"error\"";
        message        += "                        }";
        message        += "                    },";
        message        += "                    \"actionCalled\": {";
        message        += "                        \"type\": \"string\"";
        message        += "                    },";
        message        += "                    \"payload\": {";
        message        += "                        \"type\": \"string\"";
        message        += "                    }";
        message        += "                }";
        message        += "            }";
        message        += "        ]";
        message        += "    }";
        message        += "}";

        response = message.toCharArray();
    }
    else if (json["action"] == "restart") {
      sprintf(response, "{\"code\": \"200\", \"actionCalled\": \"%s\", \"payload\": \"Restart in progress\"}", action.as<char *>());
      restartRequested = millis();
    }
    else if (json["action"] == "lightOn") {
      setLightColor(255, 255, 255);
      sprintf(response, "{\"code\": \"200\", \"actionCalled\": \"%s\", \"payload\": \"Light on\"}", action.as<char *>());
    }
    else if (json["action"] == "lightOff") {
      setLightColor(0, 0, 0);
      sprintf(response, "{\"code\": \"200\", \"actionCalled\": \"%s\", \"payload\": \"Light off\"}", action.as<char *>());
    }
    else if (json["action"] == "changeColor") {
      // @todo check payload contain key red, green, blue and is interger value
      JsonVariant red = json["payload"]["red"];
      JsonVariant green = json["payload"]["green"];
      JsonVariant blue = json["payload"]["blue"];

      setLightColor(red.as<unsigned int>(), green.as<unsigned int>(), blue.as<unsigned int>());
      sprintf(response, "{\"code\": \"200\", \"actionCalled\": \"%s\", \"payload\": \"Change color to %d-%d-%d\"}", action.as<char *>(), red.as<unsigned int>(), green.as<unsigned int>(), blue.as<unsigned int>());
    }
    else {
      sprintf(response, "{\"code\": \"404\", \"payload\": \"Action %s not found !\"}", action.as<char *>());
    }

    mqttClient.publish(config.mqttPublishChannel, response);
  }

  memset(response, 0, sizeof(response));
}

void setup() {
  Serial.begin(115200);
  logger("Start program !");

  if (!SPIFFS.begin(true)) {
    logger("An Error has occurred while mounting SPIFFS");
    return;
  }

  logger("SPIFFS mounted");

  pinMode(restartButtonPin, INPUT);

  // Get wifi SSID and PASSW from SPIFFS
  if (true == getConfig()) {
    if (true == checkWifiConfigValues()) {
      wifiConnected = wifiConnect();
  
      if (true == wifiConnected) {
        mqttClient.setServer(config.mqttHost, config.mqttPort);
        mqttClient.setCallback(callback);
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
    ledcAttachPin(ledStripRedPin, 1); // assign RGB led pins to channels
    ledcAttachPin(ledStripGreenPin, 2);
    ledcAttachPin(ledStripBluePin, 3);
    
    // Initialize channels 
    // channels 0-15, resolution 1-16 bits, freq limits depend on resolution
    // ledcSetup(uint8_t channel, uint32_t freq, uint8_t resolution_bits);
    ledcSetup(1, 12000, 8); // 12 kHz PWM, 8-bit resolution
    ledcSetup(2, 12000, 8);
    ledcSetup(3, 12000, 8);

    logger("App started !");
  }
}

void loop() {
  if (true == startApp) {
    // Subscribe channel
    mqttClient.loop();

    if (restartRequested != 0) {
      if (millis() - restartRequested >= 5000 ) {
        logger("Restart ESP");
        restartRequested = 0;
        restart();
      }
    }

    if (digitalRead(restartButtonPin) == LOW) {
      restartRequested = millis();
    }
  }
}