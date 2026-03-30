#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <ESPmDNS.h>
#include <ArduinoJson.h> 


// ── Pin Definitions ─────────────────────────────────────────────
#define BUZ      15
#define VIB      27
#define CUR      34
#define TEM      35
#define LED_GR    2
#define LED_CUR   4
#define LED_TEM  16
#define LED_VIB  17

//Global State
float currentTresh   = 0;
float vibrationTresh = 0;
float tempTresh      = 0;
bool treshReceived = false;

//Wifi Credentials 
const char* ssid = "Vishnu-GBk";
const char* password = "2aCD1gAn";

unsigned long lastSend = 0;         //used to delay updates to server
const unsigned long INTERVAL = 2000;

AsyncWebServer server(80);
AsyncEventSource events("/events");

void clearAlarm() {         //Resets warning lights and turns off buzzer
  digitalWrite(LED_CUR, LOW);
  digitalWrite(LED_VIB, LOW);
  digitalWrite(LED_TEM, LOW);
  digitalWrite(LED_GR, HIGH);
  digitalWrite(BUZ,    LOW);
}
//--------------------------------------------------
//             Sensor reading functions
//--------------------------------------------------
float getCurrent(){

    return 20.0 + random(-20, 20) / 10.0;    
}

float getVibration(){
    
    return 20.0 + random(-20, 20) / 10.0;
}

float getTemp(){
    
    return 20.0 + random(-20, 20) / 10.0;
}

//-----------------------------------------------------------
//              Setup WebServer
//-----------------------------------------------------------
void connectWifi(){  //connecting to wifi
    WiFi.begin(ssid,password);
    while (WiFi.status()!=WL_CONNECTED) 
    {
        delay(100);
        Serial.print(".");
    }
    Serial.println("\nConnected! IP: " + WiFi.localIP().toString());

}
void mntFs(){    //mount file system
    if (!LittleFS.begin(true)) {
        Serial.println("LittleFS mount failed!");
        Serial.println("Filesystem mounted.");
        return;
        }
}

void serverInit(){    //initializes the server
      // ── Serve static files ──────────────────────────────────
    server.on("/", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->send(LittleFS, "/index.html", "text/html");
    });
    server.serveStatic("/", LittleFS, "/");

    //404 handler
    server.onNotFound([](AsyncWebServerRequest* request) {
        request->send(404, "text/plain", "404: Not Found");
    });
}

void POST(){     //Receive threshold values from webpage
    server.on("/treshold", HTTP_POST,
    [](AsyncWebServerRequest* request) {},
    NULL,
    [](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
        StaticJsonDocument<256> doc;
        DeserializationError err = deserializeJson(doc, data, len);

        if (err) {
        request->send(400, "application/json", "{\"status\":\"bad json\"}");
        return;
    }

    // Store setpoints
      currentTresh = doc["currentTresh"].as<float>();
      vibrationTresh = doc["vibrationTresh"].as<float>();
      tempTresh = doc["tempTresh"].as<float>();

      Serial.printf("treshold received →\n  Current : %.2f\n  Vibration : %.2f\n  Temp : %.2f\n",currentTresh,vibrationTresh,tempTresh);

      treshReceived = true;   // ← unlock main loop

      request->send(200, "application/json", "{\"status\":\"ok\"}");
    }
);
}

void SSE(){ //send data to webserver
    events.onConnect([](AsyncEventSourceClient* client) {
        Serial.println("SSE client connected");
    client->send("connected", NULL, millis(), 1000);
});
    server.addHandler(&events);
}

//------------------------------------------------
//          check fault
//------------------------------------------------

void checkFault(float current, float vibration, float temp) {
  bool fault1 = current > currentTresh;
  bool fault2 = vibration > vibrationTresh;
  bool fault3 = temp > tempTresh;

  // No fault in any reading
  if (!fault1 && !fault2 && !fault3) {
    clearAlarm();
    return;
  }

  // At least one fault — turn off green LED and turn on buzzer
  digitalWrite(LED_GR, LOW);
  digitalWrite(BUZ,    HIGH);

  // Turn on only the LEDs whose sensor exceeded threshold
  digitalWrite(LED_CUR, fault1 ? HIGH : LOW);
  digitalWrite(LED_VIB, fault2 ? HIGH : LOW);
  digitalWrite(LED_TEM, fault3 ? HIGH : LOW);

  // Log which sensors are in fault
  if (fault1) Serial.printf("  ⚠ Current FAULT: %.2f > threshold %.2f\n", current, currentTresh);
  if (fault2) Serial.printf("  ⚠ Vibration FAULT: %.2f > threshold %.2f\n", vibration, vibrationTresh);
  if (fault3) Serial.printf("  ⚠ temprature FAULT: %.2f > threshold %.2f\n", temp, tempTresh);
}

void setup(){
    digitalWrite(BUZ, LOW);// Stops buzzer glitch 
    
    Serial.begin(115200);
    delay(2000);

    pinMode(LED_GR,  OUTPUT);
    pinMode(LED_CUR, OUTPUT);
    pinMode(LED_VIB, OUTPUT);
    pinMode(LED_TEM, OUTPUT);
    pinMode(BUZ,     OUTPUT);
    pinMode(VIB,     INPUT);
    
    clearAlarm();
    
    Serial.println("Hardware initiated...");
    
    //Mounting filesystem
    mntFs();

    //Connecting to wifi
    connectWifi();

    serverInit();


    
    POST();//get data from website
    SSE(); //send data to website
    server.begin();
    Serial.println("Server started!");
}

void loop(){
    // Serial.println("Waiting for Tresholds");
    if(!treshReceived)return;//Block execution till treshold values are received 

      // ── Send sensor data on interval ────────────────────────
    if (millis() - lastSend >= INTERVAL) {
        lastSend = millis();

        float current=getCurrent();
        float vibration=getVibration();
        float temp=getTemp();

        printf("Current : %.2f  Vibration : %.2f  Temprature : %.2f\n",current,vibration,temp);

        checkFault(current,vibration,temp);

        String json = "{";
        json += "\"current\":"  + String(current, 2) + ",";
        json += "\"vibration\":"  + String(vibration, 2) + ",";
        json += "\"temp\":" + String(temp, 2);
        json += "}";

        events.send(json.c_str(), "sensor", millis());
        Serial.println("Sent: " + json);
    }

}