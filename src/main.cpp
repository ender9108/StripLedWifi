#include <Arduino.h>
#include <SPIFFS.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>

void logger(String message, bool endLine = true);

struct Config {
  char wifiSsid[32] = "";
  char wifiPassword[64] = "";
  char mqttHost[128] = "";
  int  mqttPort = 1883;
  char mqttUsername[32] = "";
  char mqttPassword[64] = "";
  char mqttPublishChannel[128] = "device/to/marvin";
  char mqttSubscribeChannel[128] = "marvin/to/device";
  char uuid[64] = "";
};

WiFiClient wifiClient;
PubSubClient mqttClient;
Config config;
AsyncWebServer server(80);

/* ***** pin component ***** */
const int ledStatusPin = 4;
const int ledStripRedPin = 19;
const int ledStripGreenPin = 18;
const int ledStripBluePin = 5;
const int restartBtnPin = 16;
const int resetBtnPin = 17;

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
int ledStatusState = LOW;
String errorMessage = "";
unsigned long restartRequested = 0;
unsigned long resetRequested = 0;
unsigned long resetBtnPressed = 0;
unsigned long previousBlinkLed = 0;

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
        logger(F("getConfig"));
        serializeJson(json, Serial);
        logger(F(""));
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

    logger(F("wifiSsid : "), false);
    logger(String(config.wifiSsid));
    logger(F("wifiPassword : "), false);
    logger(String(config.wifiPassword));
    logger(F("mqttHost : "), false);
    logger(String(config.mqttHost));
    logger(F("mqttPort : "), false);
    logger(String(config.mqttPort));
    logger(F("mqttUsername : "), false);
    logger(String(config.mqttUsername));
    logger(F("mqttPassword : "), false);
    logger(String(config.mqttPassword));
    logger(F("mqttPublishChannel : "), false);
    logger(String(config.mqttPublishChannel));
    logger(F("mqttSubscribeChannel : "), false);
    logger(String(config.mqttSubscribeChannel));
    logger(F("uuid : "), false);
    logger(String(config.uuid));

    return true;
}

bool setConfig(Config newConfig) {
    StaticJsonDocument<512> json;

    json["wifiSsid"] = String(newConfig.wifiSsid);
    json["wifiPassword"] = String(newConfig.wifiPassword);
    json["mqttHost"] = String(newConfig.mqttHost);
    json["mqttPort"] = newConfig.mqttPort;
    json["mqttUsername"] = String(newConfig.mqttUsername);
    json["mqttPassword"] = String(newConfig.mqttPassword);
    json["mqttPublishChannel"] = String(newConfig.mqttPublishChannel);
    json["mqttSubscribeChannel"] = String(newConfig.mqttSubscribeChannel);

    if (strlen(newConfig.uuid) == 0) {
        uint32_t tmpUuid = esp_random();
        String(tmpUuid).toCharArray(config.uuid, 64);
    }

    json["uuid"] = String(config.uuid);

    File configFile = SPIFFS.open(configFilePath, FILE_WRITE);

    if (!configFile) {
        logger(F("Failed to open config file for writing"));
        return false;
    }

    serializeJson(json, configFile);

    configFile.close();

    return true;
}

bool wifiConnect() {
    unsigned int count = 0;
    WiFi.begin(config.wifiSsid, config.wifiPassword);
    Serial.print(F("Try to connect to "));
    logger(config.wifiSsid);

    while (count < 20) {
        if (WiFi.status() == WL_CONNECTED) {
            logger("");
            Serial.print(F("WiFi connected (IP : "));  
            Serial.print(WiFi.localIP());
            logger(F(")"));

            return true;
        } else {
            delay(500);
            Serial.print(F("."));  
        }

        count++;
    }

    Serial.print(F("Error connection to "));
    logger(String(config.wifiSsid));
    errorMessage = "Wifi connection error to " + String(config.wifiSsid);
    return false;
}

bool checkWifiConfigValues() {
    logger(F("config.wifiSsid length : "), false);
    logger(String(strlen(config.wifiSsid)));

    logger(F("config.wifiPassword length : "), false);
    logger(String(strlen(config.wifiPassword)));

    if ( strlen(config.wifiSsid) > 1 && strlen(config.wifiPassword) > 1 ) {
        return true;
    }

    logger(F("Ssid and passw not present in SPIFFS"));
    return false;
}

bool mqttConnect() {
    int count = 0;

    while (!mqttClient.connected()) {
        logger("Attempting MQTT connection (host: " + String(config.mqttHost) + ")...");
        // Attempt to connect
        if (mqttClient.connect(mqttName, config.mqttUsername, config.mqttPassword)) {
            logger(F("connected !"));
            mqttClient.subscribe(config.mqttSubscribeChannel);
            return true;
        } else {
            logger(F("failed, rc="), false);
            logger(String(mqttClient.state()));
            logger(F("try again in 5 seconds"));
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
        setConfig(config);

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
    
    char response[1280];
    
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
        else if (json["action"] == "configure") {
            String message  = "{\"code\":\"200\",\"actionCalled\":\"\",\"payload\":{\"ip\":\"\",\"Mac address\":\"\",\"protocol\":\"mqtt\",\"port\":\"\",\"actions\":[{\"action\":\"ping\",\"payload\":null,\"response\":{\"code\":{\"type\":\"integer\",\"value\":\"[200, 500]\",\"definition\":{\"200\":\"ok\",\"500\":\"error\"}},\"actionCalled\":{\"type\":\"string\"},\"payload\":{\"type\":\"string\"}}},{\"action\":\"status\",\"payload\":null,\"response\":{\"code\":{\"type\":\"integer\",\"value\":\"[200, 500]\",\"definition\":{\"200\":\"ok\",\"500\":\"error\"}},\"actionCalled\":{\"type\":\"string\"},\"payload\":{\"type\":\"string\"}}},{\"action\":\"lightOn\",\"payload\":null,\"response\":{\"code\":{\"type\":\"integer\",\"value\":\"[200,500]\",\"definition\":{\"200\":\"ok\",\"500\":\"error\"}},\"actionCalled\":{\"type\":\"string\"},\"payload\":{\"type\":\"string\"}}},{\"action\":\"lightOff\",\"payload\":null,\"response\":{\"code\":{\"type\":\"integer\",\"value\":\"[200,500]\",\"definition\":{\"200\":\"ok\",\"500\":\"error\"}},\"actionCalled\":{\"type\":\"string\"},\"payload\":{\"type\":\"string\"}}},{\"action\":\"changeColor\",\"payload\":{\"red\":{\"type\":\"integer\",\"value\":\"[0,255]\"},\"green\":{\"type\":\"integer\",\"value\":\"[0,255]\"},\"blue\":{\"type\":\"integer\",\"value\":\"[0,255]\"}},\"response\":{\"code\":{\"type\":\"integer\",\"value\":\"[200,500]\",\"definition\":{\"200\":\"ok\",\"500\":\"error\"}},\"actionCalled\":{\"type\":\"string\"},\"payload\":{\"type\":\"string\"}}}]}}";
            message.toCharArray(response, 1280);
        }
        else if (json["action"] == "restart") {
            sprintf(response, "{\"code\": \"200\", \"actionCalled\": \"%s\", \"payload\": \"Restart in progress\"}", action.as<char *>());
            restartRequested = millis();
        }
        else if (json["action"] == "reset") {
            sprintf(response, "{\"code\": \"200\", \"actionCalled\": \"%s\", \"payload\": \"Reset in progress\"}", action.as<char *>());
            resetRequested = millis();
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
            sprintf(response, "{\"code\": \"200\", \"actionCalled\": \"%s\", \"payload\": \"Change color to %d,%d,%d\"}", action.as<char *>(), red.as<unsigned int>(), green.as<unsigned int>(), blue.as<unsigned int>());
        }
        else {
            sprintf(response, "{\"code\": \"404\", \"payload\": \"Action %s not found !\"}", action.as<char *>());
        }

        mqttClient.publish(config.mqttPublishChannel, response);
    }

    memset(response, 0, sizeof(response));
}

void resetConfig() {
    logger(F("Reset ESP"));
    Config resetConfig;
    setConfig(resetConfig);
    logger(F("Restart ESP"));
    resetRequested = 0;
    restartRequested = 0;
    blinkLed();
    restart();
}

void blinkLed() {
    digitalWrite(ledStatusPin, HIGH);
    delay(300);
    digitalWrite(ledStatusPin, LOW);
    delay(300);
    digitalWrite(ledStatusPin, HIGH);
}

void blinkLedNoDelay() {
    unsigned long currentBlinkLed = millis();

    if (currentBlinkLed - previousBlinkLed >= interval) {
        previousBlinkLed = currentBlinkLed;

        // if the LED is off turn it on and vice-versa:
        if (ledStatusState == LOW) {
            ledStatusState = HIGH;
        } else {
            ledStatusState = LOW;
        }

        // set the LED with the ledState of the variable:
        digitalWrite(ledStatusPin, ledState);
    }
}

void setup() {
    Serial.begin(115200);
    logger(F("Start program !"));

    if (!SPIFFS.begin(true)) {
        logger(F("An Error has occurred while mounting SPIFFS"));
        return;
    }

    logger(F("SPIFFS mounted"));

    pinMode(ledStatusPin, OUTPUT);
    pinMode(restartBtnPin, OUTPUT);
    pinMode(resetBtnPin, OUTPUT);

    digitalWrite(ledStatusPin, LOW);

    // Get wifi SSID and PASSW from SPIFFS
    if (true == getConfig()) {
        if (true == checkWifiConfigValues()) {
            wifiConnected = wifiConnect();

            if (true == wifiConnected) {
                mqttClient.setClient(wifiClient);
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
        logger(F("WiFi AP is ready (IP : "), false);  
        logger(WiFi.softAPIP().toString(), false);
        logger(F(")"));
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

        digitalWrite(ledStatusPin, HIGH);
        logger(F("App started !"));
    }
}

void loop() {
    if (true == startApp) {
        if (digitalRead(restartBtnPin) == HIGH) {
            restartRequested = millis();
        }

        if (digitalRead(resetBtnPin) == HIGH) {
            if (resetBtnPressed != 0) {
                if (millis() - resetBtnPressed >= 5000) {
                    resetConfig();
                }
            } else {
                resetBtnPressed = millis();
            }
        }

        if (restartRequested != 0) {
            if (millis() - restartRequested >= 5000 ) {
                logger(F("Restart ESP"));
                restartRequested = 0;
                restart();
            }
        }

        if (resetRequested != 0) {
            if (millis() - resetRequested >= 5000) {
                resetConfig();
            }
        }

        mqttClient.loop();
    } else {
        blinkLedNoDelay();
    }
}