  #include "credentials.h"
#include "pins.h"
#include <SPI.h>
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
const uint8_t SSPINS[] = {D4_PIN, D2_PIN, D1_PIN, D8_PIN, D0_PIN};
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
boolean isMandatoEmpty = false;
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
    client.setCallback(callback);
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
    delay (100);
    client.subscribe(topic);
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

//this is the block number we will write into and then read. Do not write into 'sector trailer' block, since this can make the block unusable.
int block = 2;

//all zeros. This can be used to delete a block.
//byte blockcontent[16] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
//This array is used for reading out a block. The MIFARE_Read method requires a buffer that is at least 18 bytes to hold the 16 bytes of a block.
byte* payloadMqtt;

void loop(){
  if (isMqtt) {
    client.loop();  
   }

  mfrc522[0].PCD_Init();
  mfrc522[1].PCD_Init();
  
  //establishing contact with a tag/card
  if (mfrc522[0].PICC_IsNewCardPresent() && mfrc522[0].PICC_ReadCardSerial()) {
    //writing a block on the card on reader 1
    //the blockcontent array is written into the card block
    if (payloadMqtt) {
      writeBlock(block, payloadMqtt);
    }
  }
  if (mfrc522[1].PICC_IsNewCardPresent() && mfrc522[1].PICC_ReadCardSerial()) {
    Serial.println("Card found on reader #2:");
    //reading a card on reader 2    
    //read the block
    Serial.print("Read block: ");
    readBlock(block, readbackblock, 1);
    
    for (int j = 0 ; j < 16 ; j++) {
      Serial.write (readbackblock[j]);
    }
    Serial.println("");
  }
  delay (500);
}


// Function to handle MQTT's messages
void callback(char *topic, byte *payload, unsigned int mlength) {
    char message[256];
    if (mlength > 0) {
        memset(message, '\0' , sizeof(message));
        memcpy(message, payload, mlength);
    }
  
    Serial.print("Message arrived in topic: ");
    Serial.println(topic);
    Serial.print("Message:");
    Serial.println(message);

    payloadMqtt = payload;
    
    Serial.println();
    Serial.println("-----------------------");
}

// Function to handle the RFID tag writing
int writeBlock(int blockNumber, byte arrayAddress[]) {
  //this makes sure that we only write into data blocks. Every 4th block is a trailer block for the access/security info.
  int largestModulo4Number = blockNumber / 4 * 4;
  int trailerBlock = largestModulo4Number + 3;
  if (blockNumber > 2 && (blockNumber + 1) % 4 == 0) {
    Serial.print(blockNumber);
    Serial.println(" is a trailer block:");
    return 2;
  }
  Serial.print(blockNumber);
  Serial.println(" is a data block:");

  //authentication of the desired block for access
  byte status = mfrc522[0].PCD_Authenticate(MFRC522::PICC_CMD_MF_AUTH_KEY_A, trailerBlock, &key, &(mfrc522[0].uid));
  if (status != MFRC522::STATUS_OK) {
    Serial.print("PCD_Authenticate() failed: ");
    Serial.println(mfrc522[0].GetStatusCodeName((MFRC522::StatusCode)status));
    return 3;//return "3" as error message
  }

  //writing the block
  status = mfrc522[0].MIFARE_Write(blockNumber, arrayAddress, 16);
  if (status != MFRC522::STATUS_OK) {
    Serial.print("MIFARE_Write() failed: ");
    Serial.println(mfrc522[0].GetStatusCodeName((MFRC522::StatusCode)status));
    return 4; // error
  }
  Serial.println("block was written");

  return 0;
}

/*
// Function to handle the RFID tag reading
int readBlock(int blockNumber, byte arrayAddress[]) {
  int largestModulo4Number = blockNumber / 4 * 4;
  int trailerBlock = largestModulo4Number + 3; //determine trailer block for the sector

  //authentication of the desired block for access
  byte status = mfrc522[0].PCD_Authenticate(MFRC522::PICC_CMD_MF_AUTH_KEY_A, trailerBlock, &key, &(mfrc522[0].uid));
  
  if (status != MFRC522::STATUS_OK) {
    Serial.print("PCD_Authenticate() failed (read): ");
    Serial.println(mfrc522[0].GetStatusCodeName((MFRC522::StatusCode)status));
    return 3;// error
  }

  //reading a block
  byte buffersize = 18;
  status = mfrc522[0].MIFARE_Read(blockNumber, arrayAddress, &buffersize);
  if (status != MFRC522::STATUS_OK) {
    return 4;// error
  }
  Serial.println("block was read");

  return 0;
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
  Serial.println(status);
  arrayAddress += '\0';
  if (status != MFRC522::STATUS_OK) {
    return 4; //error
  }
  
  return 0;
}
