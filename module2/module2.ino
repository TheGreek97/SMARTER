#include "credentials.h"
#include "pins.h"
#include <SPI.h>
#include <SD.h>
#include <MFRC522.h>
#include <ESP8266WiFi.h>
#include <string.h>
#include <PubSubClient.h>
#include <Arduino.h>
#include <ArduinoJson.h>
//#include "AudioFileSourceHTTPStream.h"
//#include "AudioFileSourceBuffer.h"
//#include "AudioGeneratorMP3.h"
#include "AudioFileSourcePROGMEM.h"
#include "AudioGeneratorWAV.h"
#include "AudioOutputI2SNoDAC.h"

#include "success.h"
#include "fail.h"
#include "pop.h"

//GLOBALS
// MQTT Broker
const char *topic = "smarter/letturaRFID";
const char *topic_out = "smarter/output";
const int mqtt_port = 1883;


// LED RGB
const uint8_t RED_PIN = D0_PIN;
const uint8_t GREEN_PIN = D2_PIN;
const uint8_t BLUE_PIN = D4_PIN;


boolean isPlay = false;

WiFiClient espClient;
PubSubClient client(espClient);
/*
AudioGeneratorMP3 *mp3;
AudioFileSourceBuffer *buff;
AudioFileSourceHTTPStream *file_http;
AudioOutputI2SNoDAC *out;

const int preallocateBufferSize = 2048;
void *preallocateBuffer = NULL;
*/

AudioGeneratorWAV *audio_gen;
AudioFileSourcePROGMEM *file;
AudioOutputI2SNoDAC *out;


void setup() {
    Serial.begin(9600);
    Serial.println("INIZIO");

    pinMode(GREEN_PIN, OUTPUT);
    pinMode(BLUE_PIN, OUTPUT);
    pinMode(RED_PIN, OUTPUT);
    
    // connecting to a WiFi network
    WiFi.begin(ssid, password);
    int tryCount = 0;
    while (WiFi.status() != WL_CONNECTED) {
        tryCount++;
        delay(1000);
        Serial.println(String(tryCount) + ") Connecting to WiFi..");
    }
    Serial.println("Connected to the WiFi network");
    Serial.print("ESP8266 has the IP address: ");
    Serial.println(WiFi.localIP());
    WiFi.setAutoReconnect(true);
    WiFi.persistent(true);

    //connecting to a mqtt broker
    client.setServer(mqtt_broker, mqtt_port);
    client.setCallback(callback);
    while (!client.connected()) {
        client.setKeepAlive(90); // needs to be made before connecting
        String client_id = "esp8266-client-";
        client_id += String(WiFi.macAddress());
        Serial.printf("The client %s connects to the mqtt broker\n", client_id.c_str());
        if (client.connect(client_id.c_str(), mqtt_username, mqtt_password)) {
            Serial.println("Public emqx mqtt broker connected");
        } else {
            Serial.println("failed with state ");
            Serial.println(client.state());
            delay(2000);
        }
    }
    // publish and subscribe
    client.publish(topic, "Collegamento con MQTT");
    client.subscribe(topic_out);

    SPI.begin();

    audioLogger = &Serial;

    // blue color set initially
    analogWrite(RED_PIN, 255);
    analogWrite(GREEN_PIN, 255);
    analogWrite(BLUE_PIN, 0);
}

void loop() {
    client.loop();
    
    if(isPlay == true) {
        if (audio_gen && !audio_gen->loop()) {
          stopPlaying();
        }
    }
}

// In this function all music streams are interrupted
void stopPlaying() {
    Serial.println("Interrupted! 1");
    
    if (audio_gen) {
    Serial.println("Interrupted! 2");
    audio_gen->stop();
    /*
    if (file_http) {
      Serial.println("Interrupted! 3");
      file_http->close();
    }

    if (buff) {
      Serial.println("Interrupted! 4");
      buff->close();
    }*/
    
    isPlay = false;
  }
}

// Function to handle MQTT's messages
void callback(char *topic, byte *payload, unsigned int mlength) {
    Serial.print("Message arrived in topic: ");
    Serial.println(topic);
    
    char message[256];
    if (mlength > 0) {
      memset(message, '\0' , sizeof(message));
      memcpy(message, payload, mlength);
    }
  
    Serial.print("Message:");
    Serial.println(message);

    // if the message "stop" arrives, the effect stops
    if (strcmp(message,"stop") == 0) {
        stopPlaying();
        delay(1000);
        return;
    }

    // if the message "reset" arrives, the board restarts
    if (strcmp(message,"reset") == 0) {
        WiFi.disconnect(); // Drop current connection
        delay(1000);
        ESP.restart();
    }

    // Reading of JSON
    const size_t capacity = JSON_OBJECT_SIZE(2) + 200;
    //DynamicJsonBuffer jsonBuffer(capacity);
    DynamicJsonDocument doc(capacity);
    //JsonObject& root = jsonBuffer.parseObject(message);
    DeserializationError err = deserializeJson(doc, message);
  
    if (err) {
      Serial.print ("Deserialization error: ");
      Serial.println(err.c_str());
    }
    
    stopPlaying();
    delay(500);
    
    /*file_http = new AudioFileSourceHTTPStream();
    // sound effect started based on URL arrived in JSON

    if (file_http->open(doc["URL"])) { 
      Serial.println("Opening audio at:");
        buff = new AudioFileSourceBuffer(file_http, sizeof(file_http));
        //buff = new AudioFileSourceBuffer(file_http, sizeof(file_http));
        out = new AudioOutputI2SNoDAC();
        mp3 = new AudioGeneratorMP3();
        mp3->begin(buff, out);
        isPlay = true;
    } else {
        Serial.print("Error opening the resource at url");
        Serial.println((String) (doc["URL"]));
        stopPlaying();
    }*/
    String url = (String) doc["URL"];
    if (url == "vittoria") {
      file = new AudioFileSourcePROGMEM( success, sizeof(success) );
    } else if (url == "sconfitta") {
      file = new AudioFileSourcePROGMEM( fail, sizeof(fail) );
    } else if (url == "pop") {
      file = new AudioFileSourcePROGMEM( pop, sizeof(pop) );
    } else {
      Serial.println("No file!");
    }
    if (file) {
      out = new AudioOutputI2SNoDAC();
      audio_gen = new AudioGeneratorWAV();
      audio_gen->begin(file, out);
      isPlay = true;
    } else {
      Serial.println("Error in opening the file");
      stopPlaying();
    }
    
    // color set according to the arrived JSON
    if (getValue(doc["LED"], ',', 0).toInt() < 256) //256 in the first position is the value for "Do nothing"
    {
      analogWrite(RED_PIN, 255-getValue(doc["LED"], ',', 0).toInt());
      analogWrite(GREEN_PIN, 255-getValue(doc["LED"], ',', 1).toInt());
      analogWrite(BLUE_PIN, 255-getValue(doc["LED"], ',', 2).toInt());
    }

    Serial.println();
    Serial.println("-----------------------");
}

// Function capable of splitting a string with a separator character 
// passed as input and retrieving only the piece with the chosen index
String getValue(String data, char separator, int index) {
  int found = 0;
  int strIndex[] = {0, -1};
  int maxIndex = data.length()-1;

  for(int i=0; i<=maxIndex && found<=index; i++){
    if(data.charAt(i)==separator || i==maxIndex){
        found++;
        strIndex[0] = strIndex[1]+1;
        strIndex[1] = (i == maxIndex) ? i+1 : i;
    }
  }

  return found>index ? data.substring(strIndex[0], strIndex[1]) : "";
}
