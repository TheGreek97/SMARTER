#include "credentials.h"
#include "pins.h"
#include <MFRC522.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <Arduino.h>


//GLOBALS
// MQTT Broker
const char *topic = "smarter/letturaRFID";
const int mqtt_port = 1883;

WiFiClient espClient;
PubSubClient client(espClient);

const uint8_t RESETPIN = D3_PIN;
//const uint8_t SSPINS[] = {D4_PIN, D2_PIN, D1_PIN, D8_PIN, D0_PIN};
const uint8_t SSPINS[] = {D0_PIN, D8_PIN, D1_PIN, D2_PIN, D4_PIN};
const uint8_t NUMREADERS = 5;

// Debug value
const boolean isMqtt = true;

MFRC522 *mfrc522 = new MFRC522[5];
//create a MIFARE_Key struct named 'key', which will hold the card information

MFRC522::MIFARE_Key key; 

//const String correctIDs[];
String currentIDs[NUMREADERS];
//This array is used for reading out a block. The MIFARE_Read method requires a buffer that is at least 18 bytes to hold the 16 bytes of a block.
byte readbackblock[18];
boolean emptyReader[]  = {true, true, true, true, true};
String realIDs[NUMREADERS];
// Variable for resending data in MQTT
int sendMessCont = 0;

byte nuidPICC[4];

void setup() {
  Serial.begin(9600);
  Serial.println("BOOTING...");

  if (isMqtt) {
    // connecting to a WiFi network
    WiFi.begin(ssid, password);
    int tryCount = 0;
    while (WiFi.status() != WL_CONNECTED) {
      tryCount++;
      delay(1000);
      Serial.println(String(tryCount) + ") Connecting to WiFi..");
    }
    Serial.println(F("Connected to the WiFi network"));
    Serial.print(F("ESP8266 has the IP address: "));
    Serial.println(WiFi.localIP());
    WiFi.setAutoReconnect(true);
    WiFi.persistent(true);

    //connecting to a mqtt broker
    client.setServer(mqtt_broker, mqtt_port);
    //client.setCallback(callback);
    while (!client.connected()) {
      client.setKeepAlive(90); // needs be made before connecting
      String client_id = "esp8266-client-";
      client_id += String(WiFi.macAddress());
      Serial.printf("The client %s connects to the mqtt broker\n", client_id.c_str());
      if (client.connect(client_id.c_str(), mqtt_username, mqtt_password)) {
        Serial.println(F("Public emqx mqtt broker connected"));
      } else {
        Serial.println(F("failed with state "));
        Serial.println(client.state());
        delay(2000);
      }
    }
    // publish and subscribe
    client.publish(topic, "Collegamento con MQTT");
    //client.subscribe(topic);
  }

  SPI.begin();
  
  for (uint8_t i = 0; i < NUMREADERS; i++) {
    mfrc522[i] = MFRC522(SSPINS[i], RESETPIN);
    mfrc522[i].PCD_Init();
  }
  
  //pinMode(LED_BUILTIN, OUTPUT);

  // Prepare the security key for the read and write functions - all six key bytes are set to 0xFF at chip delivery from the factory.
  // Since the cards in the kit are new and the keys were never defined, they are 0xFF
  // if we had a card that was programmed by someone else, we would need to know the key to be able to access it. 
  // This key would then need to be stored in 'key' instead.

  for (byte i = 0; i < 6; i++) {
    //keyByte is defined in the "MIFARE_Key" 'struct' definition in the .h file of the library
    key.keyByte[i] = 0xFF;
  }

  Serial.println(F("SETUP FINISHED"));
}

void loop() {
  if (isMqtt) {
    client.loop();
  }
  //readRFIDSensor(mfrc522[0], 1);
 
  boolean changedValue = false;
  for (uint8_t i = 0; i < NUMREADERS; i++) {
    mfrc522[i].PCD_Init();
    String readRFID = "";

    //Serial.print (mfrc522[i].PICC_ReadCardSerial());
    // If some reader reads a tag
    if (mfrc522[i].PICC_IsNewCardPresent() && mfrc522[i].PICC_ReadCardSerial()) {
      //Serial.println ("Reading: ");
      readBlock(2, readbackblock, i);

      // Convert readbackblock in String
      char str[(sizeof readbackblock) + 1];
      memcpy(str, readbackblock, sizeof readbackblock);
      str[sizeof readbackblock] = 0; // Null termination.
      Serial.println (str);
      printf("%s\n", str);
      readRFID = str;
      readRFID = readRFID.substring(0, readRFID.length()-2);
      realIDs[i] = str;
     
      MFRC522::PICC_Type piccType = mfrc522[i].PICC_GetType(mfrc522[i].uid.sak);
      // Check is the PICC of Classic MIFARE type
      if (piccType != MFRC522::PICC_TYPE_MIFARE_MINI &&  
        piccType != MFRC522::PICC_TYPE_MIFARE_1K &&
        piccType != MFRC522::PICC_TYPE_MIFARE_4K) {
        Serial.println(F("Your tag is not of type MIFARE Classic."));
        return;
      }
      byte * buffer = mfrc522[i].uid.uidByte;
      for (byte i = 0; i < mfrc522[i].uid.size; i++) {
        Serial.print(buffer[i] < 0x10 ? " 0" : " ");
        Serial.print(buffer[i], DEC);
      }
      emptyReader[i] = false;
    } else {
      //Serial.print(F("No card found on RFID n° "));
      //Serial.println(i);
      if (emptyReader[i] == false) { // If the reader was not empty, but now it is, its value has changed
        changedValue = true;
        currentIDs[i] = "";
      }
      emptyReader[i] = true;
    }

    // If there is a new RFID and this value is not empty
    if (readRFID != currentIDs[i] && readRFID != "") {
      changedValue = true;
      currentIDs[i] = readRFID;
      
    } else if (readRFID == currentIDs[i]) {
      sendMessCont++;
    }

    mfrc522[i].PICC_HaltA();
    mfrc522[i].PCD_StopCrypto1();
  }

  String mqttString;
  String jsonReaders[NUMREADERS];

  // Send if there is a new value
  // Resend if a minute has passed
  if (changedValue) 
    Serial.println("Changed value!");

  if (changedValue || sendMessCont == 1000) {
    sendMessCont = 0;

    for (uint8_t i = 0; i < NUMREADERS; i++) {
      Serial.println();
      Serial.print(F("Reader #"));
      Serial.print(String(i));
      Serial.print(F(" on Pin #"));
      Serial.print(String((SSPINS[i])));
      Serial.print(F(" detected tag: #"));
      Serial.println(currentIDs[i]);

      // Creation of JSON
      //jsonReaders[i] = "\"Reader" + String(i) + "\":{\"pin\": \"" + String(SSPINS[i]) +  "\", \"tag\": \"" + String(currentIDs[i]) + "\"}";
      jsonReaders[i] = "\"Reader" + String(i) + "\":{\"tag\": \"" + String(currentIDs[i]) + "\"}";
      if (i != NUMREADERS - 1) jsonReaders[i] += ",";
    }
    Serial.println(F("---------"));

    if (isMqtt) {
      sendMessCont = 0;
      mqttString += "{";
      for (uint8_t i = 0; i < NUMREADERS; i++) {
        mqttString += jsonReaders[i];
      }
      mqttString += "}";

      Serial.println(mqttString);
      // Conversion String to Array of Char
      char charBuf[mqttString.length() + 1];
      mqttString.toCharArray(charBuf, mqttString.length() + 1);
      client.publish(topic, (char*) mqttString.c_str());
      client.subscribe(topic);
    }
  }
  delay(100);
}

/*
// Function to check if on the reader there aren't tag
boolean isEmptyReaders() {
    String emptyArray[NUMREADERS];
    for (uint8_t i = 0; i < NUMREADERS; i++) {
        emptyArray[i] = "";
    }
    return emptyArray == realIDs; 
}

// Function to handle MQTT's messages
void callback(char *topic, byte *payload, unsigned int mlength) {
  char message[256];
  if (mlength > 0) {
    memset(message, '\0' , sizeof(message));
    memcpy(message, payload, mlength);
  }

  Serial.print(F("Message arrived in topic: "));
  Serial.println(topic);
  Serial.print(F("Message:"));
  Serial.println(message);

  if (strcmp(message, "reset") == 0) {
    WiFi.disconnect(); // Drop current connection
    delay(1000);
    ESP.restart();
  }

  Serial.println();
  Serial.println(F("-----------------------"));
}*/

// This method is used to authenticate a certain block for reading
int readBlock(int blockNumber, byte arrayAddress[], int index) {
  int largestModulo4Number = blockNumber / 4 * 4;
  int trailerBlock = largestModulo4Number + 3; //determine trailer block for the sector

  //authentication of the desired block for access
  //blockAddr is the number of the block from 0 to 15.
  //MIFARE_Key *key is a pointer to the MIFARE_Key struct defined above, this struct needs to be defined for each block. New cards have all A/B= FF FF FF FF FF FF
  //Uid *uid is a pointer to the UID struct that contains the user ID of the card.
  byte status = mfrc522[index].PCD_Authenticate(MFRC522::PICC_CMD_MF_AUTH_KEY_A, trailerBlock, &key, &(mfrc522[index].uid));

  if (status != MFRC522::STATUS_OK) {
    Serial.print(F("PCD_Authenticate() failed (read): "));
    Serial.println(mfrc522[index].GetStatusCodeName((MFRC522::StatusCode)status));
    return 3; //error
  }

  //reading a block
  //we need to define a variable with the read buffer size, since the MIFARE_Read method below needs a pointer to the variable that contains the size
  byte buffersize = 18;
  //&buffersize is a pointer to the buffersize variable; MIFARE_Read requires a pointer instead of just a number
  status = mfrc522[index].MIFARE_Read(blockNumber, arrayAddress, &buffersize); 
  
  Serial.print("READ sensor n° ");
  Serial.println(index);
  arrayAddress += '\0';
  if (status != MFRC522::STATUS_OK) {
    return 4; //error
  }
  
  return 0;
}
