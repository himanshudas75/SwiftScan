#include <SoftwareSerial.h>
#include <regex.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>

#include <Arduino.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <AsyncElegantOTA.h>
#include <WebSerial.h>

#include <ESP8266mDNS.h>

#define rx D6
#define tx D5
#define en D7
// #define buzz D2

#define MAX_DEVICES 9
#define WAITING_TIME 5
#define REGEX ".*(OK|ERROR:\\([0-9A-Za-z]+\\))\r\n"

SoftwareSerial BT(rx, tx);
const char* command_list[] = {
  "AT+RESET\r\n",
  "AT+INIT\r\n",
  "AT+ORGL\r\n",
  "AT+ROLE=1\r\n",
  "AT+INQM=0,9,5\r\n",
};

bool initial_setup = true;
bool active = false;

const char* ssid = "<Enter SSID>";
const char* password = "<Enter WIFI Password>";

const char* mqtt_server = "MQTT_BROKER_URL";
const char* mqtt_username = "MQTT_CLIENT_USERNAME";
const char* mqtt_password = "MQTT_CLIENT_PASSWORD";
const int mqtt_port = 8883;

const char *clientId = "NodeMCU_Blue";
const char* recv_topic = "blue_config";
const char* send_topic = "blue_attendance";

const char* hostname = "esp8266";

WiFiClientSecure espClient;
PubSubClient client(espClient);
AsyncWebServer server(80);

void recvMsg(uint8_t *data, size_t len){
  WebSerial.println("Received Data...");
  String d = "";
  for(int i=0; i < len; i++){
    d += char(data[i]);
  }
  WebSerial.println(d);
}

void serialFlush() {
  while (Serial.available() > 0) {
    char t = Serial.read();
  }
}

void BTFlush() {
  while (BT.available() > 0) {
    char t = BT.read();
  }
}

char* stringToHex(char *input) {
    int input_length = strlen(input), i, j;
    char *output = (char*)malloc(2 * input_length + 1);

    for (i = 0, j = 0; i < input_length; i++, j += 2) {
        sprintf((char*)output + j, "%02X", input[i]);
    }
    output[j] = '\0';

    return output;
}

int match_regex(char *str) {
  regex_t regex;
  int ret;

  ret = regcomp(&regex, REGEX, REG_EXTENDED | REG_NEWLINE);
  ret = regexec(&regex, str, 0, NULL, 0);

  regfree(&regex);

  return (ret == 0) ? 1 : 0;
}

char* read_output() {
  char *buffer = NULL;
  int buffer_size = 0;

  int counter=0;
  while (1) {
    if (BT.available()) {
      counter=0;
      char x = BT.read();
      // WebSerial.print(x);
      buffer = (char*)realloc(buffer, buffer_size + 2);
      buffer[buffer_size] = x;
      buffer[buffer_size + 1] = '\0';
      buffer_size += 1;

      if(buffer_size >= 4){
        if(memcmp(buffer + buffer_size - 4, "OK\r\n", 4) == 0){
          WebSerial.print(buffer);
          break;
        }
        else if(memcmp(buffer + buffer_size - 3, ")\r\n", 3) == 0){
          WebSerial.print(buffer);
          initial_setup = true;
          free(buffer);
          buffer = NULL;
          break;
        }
      }

    }
    else {
      counter+=1;
      delay(10);
      if(counter==1000){
        if(buffer != NULL){
          free(buffer);
          buffer = NULL;
        }
        break;
      }
    }
  }

    return buffer;
}

char* send_command(const char *command) {
  delay(1000);
  BT.write(command);
  delay(1000);
  char* ret = read_output();
  delay(1000);
  return ret;
}

void reconnect() {
  while (!client.connected()) {
    WebSerial.print("Attempting MQTT connection... ");
    if (client.connect(clientId, mqtt_username, mqtt_password)) {
      WebSerial.println("connected");
      client.subscribe(recv_topic);

    } else {
      WebSerial.print("failed, rc=");
      WebSerial.print(client.state());
      WebSerial.println(" try again in 5 seconds");
      delay(5000);
    }
  }
}

void publishMessage(char* payload , boolean retained){
  if (!client.connected())
    reconnect();
  WebSerial.print(F("Sending message... "));
  if (client.publish(send_topic, payload, retained))
    WebSerial.println(F("sent"));
  else
    WebSerial.println(F("error"));
}


void send_attendance(char *input) {
  
  if(WiFi.status() == WL_CONNECTED) {
      char *hexified = stringToHex(input);
      publishMessage(hexified, false);
      delay(5000);
      free(hexified);
    }
    else {
      WebSerial.println(F("WiFi Disconnected"));
    }
    
}

void callback(char* topic, byte* payload, unsigned int length) {
  char message[length+1];

  for (int i = 0; i < length; i++)
    message[i]=(char)payload[i];
  message[length] = '\0';

  WebSerial.print("Received message: ");
  WebSerial.println(message);

  if(strcmp(message, "START") == 0){
    WebSerial.println(F("Starting device..."));
    active = true;
  }

  else if(strcmp(message, "STOP") == 0){
    WebSerial.println(F("Terminating Device..."));
    active = false;
  }

  else{
    WebSerial.println(F("Unknown Message Received..."));
  }
}

void setup() {
  pinMode(en, OUTPUT);
  digitalWrite(en, HIGH);

  // pinMode(buzz, OUTPUT);

  BT.begin(38400);
  Serial.begin(9600);

  // Setup Wifi
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.println(F("Connecting"));
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.print(F("Connected to WiFi network with IP Address: "));
  Serial.println(WiFi.localIP());

  if(!MDNS.begin(hostname)){
    Serial.println(F("Error starting mDNS"));
    while (1) { delay(1000); }
  }
  Serial.println(F("mDNS started"));

  WebSerial.begin(&server);
  WebSerial.msgCallback(recvMsg);
  AsyncElegantOTA.begin(&server);
  server.begin();
  Serial.println("HTTP server started");

  MDNS.addService("http", "tcp", 80);

  delay(1000);

  // Set up MQTT
  espClient.setInsecure();
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);

  delay(1000);

}

void loop() {

  MDNS.update();

  if (!client.connected())
    reconnect();
  client.loop();

  if (initial_setup) {
    WebSerial.println(F("Initializing..."));
    serialFlush();
    BTFlush();

    int length = sizeof(command_list) / sizeof(command_list[0]);

    for (int i = 0; i < length; i++)
      send_command(command_list[i]);

    WebSerial.println(F("Done"));
    initial_setup = false;

    // digitalWrite(buzz, HIGH);
    // delay(500);
    // digitalWrite(buzz, LOW);
  }
  
  if(active){
    delay(3000);
    WebSerial.println(F("\nSearching for devices"));

    char* output = send_command("AT+INQ\r\n");

    WebSerial.println(F("Searching done"));

    if(output != NULL){
      send_attendance(output);
      free(output);
    }
 
  }
}