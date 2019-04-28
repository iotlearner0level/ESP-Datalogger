
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <ArduinoOTA.h>
#include <FS.h>
#include <Hash.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <SPIFFSEditor.h>
#include <FS.h>
#define ONE_HOUR 3600000UL
#define TEN_MINUTES 600
#include <WiFiUdp.h>
#include <Ticker.h>
#include <TimeLib.h>
//#include "ArduinoJson-v5.13.5.h"

bool firebasebatchsync=false, livedata=false;
int firebasesynctimeout=20000,lastfirebasesync=millis();
ADC_MODE(ADC_VCC);
Ticker timer;
#include <FirebaseArduino.h>

#include "credentials.h"

  FSInfo fs_info;
String dirname="/csv/";
String fileName="currentdata.csv";
String oldFileName="data";
String pathname=dirname+fileName;
#define NUM_READINGS 4
float dataLog[NUM_READINGS];
String dataLogTitle[]={"Time","Vcc","heap","heap-frag"};
int dataLogTimeout=5000; int prevDataLogTime=0;
int csvFileSize=1024;
int maxcsvFileSize=100*1024;


const char * hostName = "esp-datalogger";
const char* http_username = "admin";
const char* http_password = "admin";

// SKETCH BEGIN
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");
AsyncEventSource events("/events");

void onWsEvent(AsyncWebSocket * server, AsyncWebSocketClient * client, AwsEventType type, void * arg, uint8_t *data, size_t len){
  if(type == WS_EVT_CONNECT){
    Serial.printf("ws[%s][%u] connect\n", server->url(), client->id());
//    client->printf("Hello Client %u :)", client->id());
    client->ping();
  } else if(type == WS_EVT_DISCONNECT){
    Serial.printf("ws[%s][%u] disconnect: %u\n", server->url(), client->id());
  } else if(type == WS_EVT_ERROR){
    Serial.printf("ws[%s][%u] error(%u): %s\n", server->url(), client->id(), *((uint16_t*)arg), (char*)data);
  } else if(type == WS_EVT_PONG){
    Serial.printf("ws[%s][%u] pong[%u]: %s\n", server->url(), client->id(), len, (len)?(char*)data:"");
  } else if(type == WS_EVT_DATA){
    AwsFrameInfo * info = (AwsFrameInfo*)arg;
    String msg = "";
    if(info->final && info->index == 0 && info->len == len){
      //the whole message is in a single frame and we got all of it's data
      Serial.printf("ws[%s][%u] %s-message[%llu]: ", server->url(), client->id(), (info->opcode == WS_TEXT)?"text":"binary", info->len);
      Serial.println("We are in line 38");String floatrcd="";
      if(info->opcode == WS_TEXT){
      for(size_t i=0; i < info->len; i++) {
          floatrcd += (char) data[i];
          msg += (char) data[i];
        }
      Serial.println("Line 44");char buf[floatrcd.length()]; floatrcd.toCharArray(buf,floatrcd.length());float dataRate = 10.0*(float) atof(buf);timer.detach();timer.attach_ms(dataRate, getData);Serial.print("Data rate:");Serial.println(dataRate);Serial.print("Data Received:");Serial.println(floatrcd);
 
      } else {
        char buff[3];
        for(size_t i=0; i < info->len; i++) {
          Serial.println("Line 46");
          sprintf(buff, "%02x ", (uint8_t) data[i]);
          msg += buff ;
        }
      }
      Serial.printf("%s\n",msg.c_str());

      if(info->opcode == WS_TEXT)
        client->text("I got your text message");
      else
        client->binary("I got your binary message");
    } else {
      //message is comprised of multiple frames or the frame is split into multiple packets
      if(info->index == 0){
        if(info->num == 0)
          Serial.printf("ws[%s][%u] %s-message start\n", server->url(), client->id(), (info->message_opcode == WS_TEXT)?"text":"binary");
        Serial.printf("ws[%s][%u] frame[%u] start[%llu]\n", server->url(), client->id(), info->num, info->len);
      }

      Serial.printf("ws[%s][%u] frame[%u] %s[%llu - %llu]: ", server->url(), client->id(), info->num, (info->message_opcode == WS_TEXT)?"text":"binary", info->index, info->index + len);

      if(info->opcode == WS_TEXT){
        for(size_t i=0; i < info->len; i++) {
          msg += (char) data[i];Serial.print("LIne 69:msg=");Serial.println(msg);
          float dataRate = (float) atof((const char *) &data);
          timer.detach();
          timer.attach(dataRate, getData);
 
        }
      } else {
        char buff[3];
        for(size_t i=0; i < info->len; i++) {
          sprintf(buff, "%02x ", (uint8_t) data[i]);
          msg += buff ;
        }
      }
      Serial.printf("%s\n",msg.c_str());

      if((info->index + len) == info->len){
        Serial.printf("ws[%s][%u] frame[%u] end[%llu]\n", server->url(), client->id(), info->num, info->len);
        if(info->final){
          Serial.printf("ws[%s][%u] %s-message end\n", server->url(), client->id(), (info->message_opcode == WS_TEXT)?"text":"binary");
          if(info->message_opcode == WS_TEXT)
            client->text("I got your text message");
          else
            client->binary("I got your binary message");
        }
      }
    }
  }
}

// Serving a web page (from flash memory)
// formatted as a string literal!
/*
char webpage[] PROGMEM = R"=====(
<html>
<!-- Adding a data chart using Chart.js -->
<head>
  <script src='https://cdnjs.cloudflare.com/ajax/libs/Chart.js/2.5.0/Chart.min.js'></script>
</head>
<body onload="javascript:init()">
<!-- Adding a slider for controlling data rate -->
<div>
  <input type="range" min="1" max="1000" value="50" id="dataRateSlider" oninput="sendDataRate()" />
  <label for="dataRateSlider" id="dataRateLabel">Rate: 0.2Hz</label>
</div>
<hr />
<div>
  <canvas id="line-chart" width="800" height="450"></canvas>
</div>
<!-- Adding a websocket to the client (webpage) -->
<script>
  var webSocket, dataPlot;
  var maxDataPoints = 20;
  function removeData(){
    dataPlot.data.labels.shift();
    dataPlot.data.datasets[0].data.shift();
  }
  function addData(label, data) {
    if(dataPlot.data.labels.length > maxDataPoints) removeData();
    dataPlot.data.labels.push(label);
    dataPlot.data.datasets[0].data.push(data);
    dataPlot.update();
  }
  var incomingdata;
  function init() {
    webSocket = new WebSocket('ws://' + window.location.hostname + ':80/ws');
    dataPlot = new Chart(document.getElementById("line-chart"), {
      type: 'line',
      data: {
        labels: [],
        datasets: [{
          data: [],
          label: "Temperature (C)",
          borderColor: "#3e95cd",
          fill: false
        }]
      }
    });
    webSocket.onmessage = function(event) {
     console.log("Received data:");console.log(event.data);
      var data = JSON.parse(event.data);
      incomingdata=event.data;
      var today = new Date();
      var t = today.getHours() + ":" + today.getMinutes() + ":" + today.getSeconds();
      addData(t, data.value);
      console.log("Parsed data:");console.log(data.value);
    }
  }
  function sendDataRate(){
    var dataRate = document.getElementById("dataRateSlider").value;
    webSocket.send(dataRate);
    //dataRate = dataRate;
    dataRate = 1000/dataRate;
 //   document.getElementById("dataRateLabel").innerHTML = "Rate: " + dataRate + "Hz";
    document.getElementById("dataRateLabel").innerHTML = "Rate: " + dataRate.toFixed(2) + "Hz";

  }
</script>
</body>
</html>
)=====";
*/
WiFiUDP UDP;                   // Create an instance of the WiFiUDP class to send and receive UDP messages

IPAddress timeServerIP;        // The time.nist.gov NTP server's IP address
const char* ntpServerName = "time.nist.gov";

const int NTP_PACKET_SIZE = 48;          // NTP time stamp is in the first 48 bytes of the message

byte packetBuffer[NTP_PACKET_SIZE];      // A buffer to hold incoming and outgoing packets
const unsigned long intervalNTP = ONE_HOUR; // Update the time every hour
unsigned long prevNTP = 0;
unsigned long lastNTPResponse = millis();

const unsigned long intervalTemp = 1000;   // Do a temperature measurement every minute
unsigned long prevTemp = 0;
bool tmpRequested = false;
const unsigned long DS_delay = 750;         // Reading the temperature from the DS18x20 can take up to 750ms

uint32_t timeUNIX = 0, epoch=1556367049;uint32_t actualTime=0;  
#define TIMEZONE 5.5

void setup(){
  Serial.begin(115200);
  Serial.setDebugOutput(true);
  WiFi.hostname(hostName);
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(hostName);
  WiFi.begin(ssid, password);
  if (WiFi.waitForConnectResult() != WL_CONNECTED) {
    Serial.printf("STA: Failed!\n");
    WiFi.disconnect(false);
    delay(1000);
    WiFi.begin(ssid, password);
  }

  //Send OTA events to the browser
  ArduinoOTA.onStart([]() { events.send("Update Start", "ota"); });
  ArduinoOTA.onEnd([]() { events.send("Update End", "ota"); });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    char p[32];
    sprintf(p, "Progress: %u%%\n", (progress/(total/100)));
    events.send(p, "ota");
  });
  ArduinoOTA.onError([](ota_error_t error) {
    if(error == OTA_AUTH_ERROR) events.send("Auth Failed", "ota");
    else if(error == OTA_BEGIN_ERROR) events.send("Begin Failed", "ota");
    else if(error == OTA_CONNECT_ERROR) events.send("Connect Failed", "ota");
    else if(error == OTA_RECEIVE_ERROR) events.send("Recieve Failed", "ota");
    else if(error == OTA_END_ERROR) events.send("End Failed", "ota");
  });
  ArduinoOTA.setHostname(hostName);
  ArduinoOTA.begin();

  MDNS.addService("http","tcp",80);

  SPIFFS.begin();

  ws.onEvent(onWsEvent);
  server.addHandler(&ws);

  events.onConnect([](AsyncEventSourceClient *client){
    client->send("hello!",NULL,millis(),1000);
  });
  server.addHandler(&events);

  server.addHandler(new SPIFFSEditor(http_username,http_password));

  server.on("/heap", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(200, "text/plain", String(ESP.getFreeHeap()));
  });
  server.on("/scan", HTTP_GET, [](AsyncWebServerRequest *request){
  String json = "[";
  int n = WiFi.scanComplete();
  if(n == -2){
    WiFi.scanNetworks(true);
  } else if(n){
    for (int i = 0; i < n; ++i){
      if(i) json += ",";
      json += "{";
      json += "\"rssi\":"+String(WiFi.RSSI(i));
      json += ",\"ssid\":\""+WiFi.SSID(i)+"\"";
      json += ",\"bssid\":\""+WiFi.BSSIDstr(i)+"\"";
      json += ",\"channel\":"+String(WiFi.channel(i));
      json += ",\"secure\":"+String(WiFi.encryptionType(i));
      json += ",\"hidden\":"+String(WiFi.isHidden(i)?"true":"false");
      json += "}";
    }
    WiFi.scanDelete();
    if(WiFi.scanComplete() == -2){
      WiFi.scanNetworks(true);
    }
  }
  json += "]";
  request->send(200, "application/json", json);
  json = String();
});
  server.on("/time", HTTP_GET, [](AsyncWebServerRequest *request){
    String response="";
    response=String(day())+"/";
    response +=String(month())+"/";
    response +=String(year())+"-";
    response +=String(hour())+":";
    response +=String(minute())+":";
    response +=String(second());
    Serial.print("Time requested:");Serial.print(day());Serial.print("/");Serial.print(month());Serial.print("/");Serial.print(year());
    Serial.println(response);
    request->send(200, "text/plain", response);
    response=String();

  });
 /*server.on("/webpage", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(200, "text/html", webpage);
  });
  */
 server.on("/delete", HTTP_GET, [](AsyncWebServerRequest *request){
  Serial.print("Delete requested:");
  String response="<meta http-equiv='refresh' content='10;url=/csv.html' />";
  if(request->hasParam("fileName")){
    Serial.println("Parameter filename found");
  AsyncWebParameter* p = request->getParam("fileName");
  if(SPIFFS.exists(p->value())){
    Serial.print("File found");Serial.print(p->value());
    if(    SPIFFS.remove(p->value())){
    Serial.print("\t File delete Successful");
    response+="<h1>File Delete Successful</h1>";
    response +="Successfully deleted file ";
    response +=p->value();
    response  +="\nNow refresh browser";
    }
    else {
      Serial.println("Delete unsuccessfule");
      response+="File delete unsuccessful for file ";
      response +=p->value();
      
    }
    response+="\ Operation Successful";
    
  }
  else {
    Serial.print("File not found:");Serial.println(p->value());
    response +="<h1> Not found</h1>";
    response += "File not found";
    response +=p->value();
  }


  } 
  else {
    response +="<h1> Parameter Not found</h1>";
    response += "Parameter fileName not found";
//    response += p->value();
//    Serial.print("Parameter not found\t");Serial.println(p->value());
    
  }
    Serial.println(response);

    request->send(200, "text/html", response);

  });

  server.on("/firebase", HTTP_GET, [](AsyncWebServerRequest *request){
  Serial.print("Firebase settings requested:");
  String response="";//="<meta http-equiv='refresh' content='10;url=/csv.html' />";
  if(request->hasParam("batchsync")){
    Serial.println("batch sync Parameter filename found");
  AsyncWebParameter* p = request->getParam("batchsync");
  if(p->value()=="true")
          firebasebatchsync=true;
  else
          firebasebatchsync=false;
  }
  if(request->hasParam("firebasesyncsec")){
  AsyncWebParameter* p = request->getParam("firebasesyncsec");
      firebasesynctimeout=p->value().toInt();
  }    
    request->send(200, "text/html", response);

  });

  server.on("/list", HTTP_GET, [](AsyncWebServerRequest *request){
/*
String str = "";
Dir dir = SPIFFS.openDir("/");
    str+="<table><tr><th>Directory</th><th>Size</th></tr>";
while (dir.next()) {
    str +=  "<tr><td><a href='csv.html?fileName=";
    str += dir.fileName();
    str += "'>";
    str +=dir.fileName();
    str +="</a></td><td><a href='";
    str += dir.fileName();
    str += "' download>" ;   
    str += dir.fileSize();
    str += "</a></td><td><a href='delete?fileName=";
    str += dir.fileName();
    str +="'>";
    str +="Delete";
    str += "</a>";
    str +="</td></tr>";
}
  str += "</table>";
  SPIFFS.remove("/dirlist.html");

  File f=SPIFFS.open("/dirlist.html","w");
  f.print(str);
  f.close();
  Serial.print(str);
//    request->send(200, "text/html", str.c_str());
//    str="";
  request->send(SPIFFS, "/dirlist.html");
 */

 String str = "";
Dir dir = SPIFFS.openDir("/");
while (dir.next()) {
    str += dir.fileName();
    str += ",";
    str += dir.fileSize();
    str += "\r\n";
}
  Serial.print(str);
//    request->send(200, "text/plain", str);
  SPIFFS.remove("/dirlist.html");

  File f=SPIFFS.open("/dirlist.txt","w");
  f.print(str.c_str());
  f.close();
  
  request->send(SPIFFS, "/dirlist.txt");
str=String ();

 });

  server.on("/livedataon", HTTP_GET, [](AsyncWebServerRequest *request){
    livedata=true;
    String webpage="Hello, we will send livedata soon through websockets";
    request->send(200, "text/html", webpage);
  });
  server.on("/livedataoff", HTTP_GET, [](AsyncWebServerRequest *request){
    livedata=false;
    String webpage="Livedata stream end";
    request->send(200, "text/html", webpage);
  });
  

 server.serveStatic("/", SPIFFS, "/").setDefaultFile("csv.html");

  server.onNotFound([](AsyncWebServerRequest *request){
    Serial.printf("NOT_FOUND: ");
    if(request->method() == HTTP_GET)
      Serial.printf("GET");
    else if(request->method() == HTTP_POST)
      Serial.printf("POST");
    else if(request->method() == HTTP_DELETE)
      Serial.printf("DELETE");
    else if(request->method() == HTTP_PUT)
      Serial.printf("PUT");
    else if(request->method() == HTTP_PATCH)
      Serial.printf("PATCH");
    else if(request->method() == HTTP_HEAD)
      Serial.printf("HEAD");
    else if(request->method() == HTTP_OPTIONS)
      Serial.printf("OPTIONS");
    else
      Serial.printf("UNKNOWN");
    Serial.printf(" http://%s%s\n", request->host().c_str(), request->url().c_str());

    if(request->contentLength()){
      Serial.printf("_CONTENT_TYPE: %s\n", request->contentType().c_str());
      Serial.printf("_CONTENT_LENGTH: %u\n", request->contentLength());
    }

    int headers = request->headers();
    int i;
    for(i=0;i<headers;i++){
      AsyncWebHeader* h = request->getHeader(i);
      Serial.printf("_HEADER[%s]: %s\n", h->name().c_str(), h->value().c_str());
    }

    int params = request->params();
    for(i=0;i<params;i++){
      AsyncWebParameter* p = request->getParam(i);
      if(p->isFile()){
        Serial.printf("_FILE[%s]: %s, size: %u\n", p->name().c_str(), p->value().c_str(), p->size());
      } else if(p->isPost()){
        Serial.printf("_POST[%s]: %s\n", p->name().c_str(), p->value().c_str());
      } else {
        Serial.printf("_GET[%s]: %s\n", p->name().c_str(), p->value().c_str());
      }
    }

    request->send(404);
  });
  server.onFileUpload([](AsyncWebServerRequest *request, const String& fileName, size_t index, uint8_t *data, size_t len, bool final){
    if(!index)
      Serial.printf("UploadStart: %s\n", fileName.c_str());
    Serial.printf("%s", (const char*)data);
    if(final)
      Serial.printf("UploadEnd: %s (%u)\n", fileName.c_str(), index+len);
  });
  server.onRequestBody([](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total){
    if(!index)
      Serial.printf("BodyStart: %u\n", total);
    Serial.printf("%s", (const char*)data);
    if(index + len == total)
      Serial.printf("BodyEnd: %u\n", total);
  });
  server.begin();


  timer.attach(5, getData);
  startUDP();  
  WiFi.hostByName(ntpServerName, timeServerIP); // Get the IP address of the NTP server
  Serial.print("Time server IP:\t");
  Serial.println(timeServerIP);

  sendNTPpacket(timeServerIP);
  delay(500);
  Firebase.begin(FIREBASE_HOST, FIREBASE_AUTH);
  setSyncProvider(getNtpTime);
  setSyncInterval(300);

  File f=SPIFFS.open(".ntptime","r");
  if(f){
    uint32_t newtime=f.parseInt();
    setTime(newtime);
    epoch=newtime;
    f.close();
  }
}
int n = 0;
void loop(){
  ArduinoOTA.handle();
  
   unsigned long currentMillis = millis();

  if (currentMillis - prevNTP > intervalNTP) { // Request the time from the time server every hour
    prevNTP = currentMillis;
    sendNTPpacket(timeServerIP);
  }

  uint32_t time = getNtpTime();                   // Check if the time server has responded, if so, get the UNIX time
  if (time) {
    timeUNIX = time;
    Serial.print("getNTP() returned:\t");
    Serial.println(timeUNIX);
    lastNTPResponse = millis();
  } else if ((millis() - lastNTPResponse) > 24UL * ONE_HOUR) {
    Serial.println("More than 24 hours since last NTP response. Rebooting.");
    Serial.flush();
    ESP.reset();
  }
  if (timeUNIX != 0) {
    Serial.print("timeUNIX=");Serial.println(timeUNIX);
    /*
    if (currentMillis - prevTemp > intervalTemp) {  // Every minute, request the temperature
//      tempSensors.requestTemperatures(); // Request the temperature from the sensor (it takes some time to read it)
      tmpRequested = true;
      prevTemp = currentMillis;
      Serial.println("Temperature requested");
    }
    if (currentMillis - prevTemp > DS_delay && tmpRequested) { // 750 ms after requesting the temperature
      actualTime = timeUNIX + (currentMillis - lastNTPResponse) / 1000;
      // The actual time is the last NTP time plus the time that has elapsed since the last NTP response
      tmpRequested = false;
      float temp = ESP.getVcc();
      //tempSensors.getTempCByIndex(0); // Get the temperature from the sensor
      temp = round(temp * 100.0) / 100.0; // round temperature to 2 digits

      Serial.printf("Appending temperature to file: %lu,", actualTime);
      Serial.println(temp);
      File tempLog = SPIFFS.open("/temp.csv", "a"); // Write the time and the temperature to the csv file
      if(tempLog){
      tempLog.print(actualTime);
      tempLog.print(',');
      tempLog.print(ESP.getVcc());
      tempLog.print(',');
      tempLog.print(ESP.getFreeHeap());
      tempLog.print(',');
      tempLog.println(ESP.getFreeSketchSpace());
      tempLog.close();
      }
      else
        Serial.println("Error opening templog");
    }*/
  } 
  else {                                    // If we didn't receive an NTP response yet, send another request
  }
  yield();
  // put your main code here, to run repeatedly:
      dataLog[0]=timeUNIX;
      dataLog[1]= dataLog[1]==0?(float)ESP.getVcc()/1000.0 : (dataLog[1]+(float)ESP.getVcc()/1000.0)/2;
      dataLog[2]=dataLog[2]==0?ESP.getFreeHeap():(dataLog[2]+ESP.getFreeHeap())/2;
      dataLog[3]=dataLog[3]==0?ESP.getHeapFragmentation():(dataLog[3]+ESP.getHeapFragmentation())/2;
      
      if(millis()-prevDataLogTime>dataLogTimeout){
      logReadings();
      dataLog[1]=0;
      dataLog[2]=0;
      dataLog[3]=0;
      prevDataLogTime=millis();
      }
  yield();
  if(millis()-lastfirebasesync>firebasesynctimeout){
    lastfirebasesync=millis();
    handleFirebase();
  }
  delay(1000);
  
}


void startUDP() {
  Serial.println("Starting UDP");
  UDP.begin(123);                          // Start listening for UDP messages to port 123
  Serial.print("Local port:\t");
  Serial.println(UDP.localPort());
}
void getData() {
  int vcc = ESP.getVcc();Serial.println(vcc);
//  Serial.println(bmp.readTemperature());
  String json = "{\"Vcc\":";
  json+=vcc;
     json += ",\"heap\":",
  json+=ESP.getFreeHeap();
       json += ",\"getBootVersion\":";
  json+=ESP.getBootVersion();
 
     json += ",\"ChipId\":";
  json+=ESP.getChipId();
     json += ",\"Mhz\":";
  json+=ESP.getCpuFreqMHz();
     json += ",\"CycleCount\":";
  json+=ESP.getCycleCount();
     json += ",\"sdkVer\":";
  json+="\""+(String)ESP.getSdkVersion()+"\"";
     json += ",\"FlashSize\":";
  json+=ESP.getFlashChipSize();
     json += ",\"FlashChipRealSize\":";
  json+=ESP.getFlashChipRealSize();
     json += ",\"FlashChipVendorId\":";
  json+=ESP.getFlashChipVendorId();
     json += ",\"ChipSpeed\":";
  json+=ESP.getFlashChipSpeed();
     json += ",\"getFlashChipSizeByChipId\":";
  json+=ESP.getFlashChipSizeByChipId();
     json += ",\"SketchSize\":";
  json+=ESP.getSketchSize();
     json += ",\"FreeSketchSpace\":";
  json+=ESP.getFreeSketchSpace();
     json += ",\"ResetReason\":";
  json+="\""+ESP.getResetReason()+"\"";
     json += ",\"ResetInfo\":";
  json+="\""+ESP.getResetInfo()+"\"";



  json += "}";
//  Serial.println("Json string is:"+json);
//  Serial.println("Converted Cstyle string is:");Serial.println(json.c_str());
//  Serial.println("Length:");json.length();
    ws.textAll((char*)json.c_str());
    json =String ();
    json= "{\"";
    json +=dataLogTitle[0];
    json +="\":";
    json +=dataLog[0];
    for (int i=1;i<1;i++){
      json +=",\"";
      json +=dataLogTitle[i];
      json += "\"";
      json +=":";
      json +=dataLog[i];
    }
    json += "}";
    ws.textAll((char*)json.c_str());

//  webSocket.broadcastTXT(json.c_str(), json.length());
}


void sendNTPpacket(IPAddress& address) {
  Serial.println("Sending NTP request");
  memset(packetBuffer, 0, NTP_PACKET_SIZE);  // set all bytes in the buffer to 0
  // Initialize values needed to form NTP request
  packetBuffer[0] = 0b11100011;   // LI, Version, Mode

  // send a packet requesting a timestamp:
  UDP.beginPacket(address, 123); // NTP requests are to port 123
  UDP.write(packetBuffer, NTP_PACKET_SIZE);
  UDP.endPacket();
}
long getNtpTime() { // Check if the time server has responded, if so, get the UNIX time, otherwise, return 0
  if (UDP.parsePacket() == 0) { // If there's no response (yet)
    Serial.print("No response from NTP");
    Serial.println("\nNo response from NTP server; resend NTP packet again");
    sendNTPpacket(timeServerIP);
    delay(500);

    return epoch;
  }
  UDP.read(packetBuffer, NTP_PACKET_SIZE); // read the packet into the buffer
  // Combine the 4 timestamp bytes into one 32-bit number
  uint32_t NTPTime = (packetBuffer[40] << 24) | (packetBuffer[41] << 16) | (packetBuffer[42] << 8) | packetBuffer[43];
  // Convert NTP time to a UNIX timestamp:
  // Unix time starts on Jan 1 1970. That's 2208988800 seconds in NTP time:
  const uint32_t seventyYears = 2208988800L;
  // subtract seventy years:
  epoch=NTPTime - seventyYears+(long)(TIMEZONE*3600 ) ; //ADD 5.5 Hours (For GMT+5:30)

  uint32_t UNIXTime = NTPTime - seventyYears;
  Serial.print("\nUNIXTime=");Serial.println(epoch);
  SPIFFS.remove(".ntptime");
  File f=SPIFFS.open(".ntptime","w");
  if(f){
    Serial.print("\nWriting new time to .ntptime:");Serial.println(epoch);
    f.print(epoch);
    f.close();
  }
  return epoch;
}

/*__________________________________________________________HELPER_FUNCTIONS__________________________________________________________*/

String formatBytes(size_t bytes) { // convert sizes in bytes to KB and MB
  if (bytes < 1024) {
    return String(bytes) + "B";
  } else if (bytes < (1024 * 1024)) {
    return String(bytes / 1024.0) + "KB";
  } else if (bytes < (1024 * 1024 * 1024)) {
    return String(bytes / 1024.0 / 1024.0) + "MB";
  }
}

String getContentType(String fileName) { // determine the filetype of a given fileName, based on the extension
  
  if (fileName.endsWith(".html")) return "text/html";
  else if (fileName.endsWith(".css")) return "text/css";
  else if (fileName.endsWith(".js")) return "application/javascript";
  else if (fileName.endsWith(".ico")) return "image/x-icon";
  else if (fileName.endsWith(".gz")) return "application/x-gzip";
  return "text/plain";
}

void handleFirebase(){
  yield();
  String timeString=String(year())+"/"+String(month())+"/"+String(day())+"/"+String(hour())+":"+String(minute())+":"+String(second());

    // set value
//  String lastsync=String(year())+"/"+String(month())+"/"+String(day())+"-"+String(hour())+":"+String(minute())+":"+String(second());

  Firebase.setString("lastsync", timeString);
//  lastsync=String ();
// handle error
  if (Firebase.failed()) {
      Serial.print("setting /lastsync failed:");
      Serial.println(Firebase.error());  
//      return;
  }
  delay(1000);
  

/*
  // get value 
  Serial.print("number: ");
  Serial.println(Firebase.getString("lastsync"));
  delay(1000);
*/
  // set string value

  
  // set bool value
  Firebase.setBool("batchsync", firebasebatchsync);
 
  delay(1000);
  Firebase.setInt ("syncInterval",firebasesynctimeout);
  delay(1000);
  // handle error
  if (Firebase.failed()) {
      Serial.print("setting /truth failed:");
      Serial.println(Firebase.error());  
      return;
  }

  // append a new value to /logs
  String jsonText="{\"";
  jsonText+=dataLogTitle[0];
  jsonText+="\":";
  jsonText+=dataLog[0];
  for (int i=1;i<NUM_READINGS;i++)
     jsonText+= ",\""+dataLogTitle[i]+"\":"+dataLog[i];
     jsonText +="}";
  Serial.print("Pushing json text:");Serial.println(jsonText);
  // handle error
  StaticJsonBuffer<200> jsonBuffer;
    JsonObject& root = jsonBuffer.createObject();
    char json[200];
    root["time"] = timeString;
    for (int i=0;i<NUM_READINGS;i++){
      root[dataLogTitle[i]]=dataLog[i];
    }
    root.printTo(json, 200);
    Firebase.set("/data/"+timeString,root);

  //Firebase.pushString("/data/",jsonText);
  if (Firebase.failed()) {
      Serial.print("pushing /logs failed:");
      Serial.println(Firebase.error());  
      return;
  }
  Serial.print("pushed: /logs/");

}

void readFile(){
  
}
void logReadings(){
  
SPIFFS.info(fs_info);

  if(!SPIFFS.exists(pathname)){  //File doesn't exist, create a new file and write titles to it
    //File did not exist. Creating in w mode. We write title values
      File f=SPIFFS.open(pathname,"w");
      if (!f) {
        Serial.println("file open failed");
        }
      else{

      Serial.print("File did not exist, creating new file:");
      Serial.println(pathname);

      f.print(dataLogTitle[0]);
      for(int i=1;i<NUM_READINGS;i++){
        f.print(",");f.print(dataLogTitle[i]);
      }
      f.println();
      }
 
    
  }
  else{//File exists open in append mode to write readings
      Serial.print("File already exists, open in append mode");
      Serial.println(pathname);
      File f=SPIFFS.open(pathname,"a");
        

      // Now writing readingss 
      
      if (!f) {
        Serial.println("file open failed");
        }
      else{
      Serial.println("Now writing readings");
      f.print(dataLog[0]);
      for(int i=1;i<NUM_READINGS;i++){
        f.print(",");f.print(dataLog[i]);
      }
        
      f.println();
      if(f.size()>csvFileSize){
      // print the hour, minute and second:

  
        String newpathname=dirname+oldFileName+"/"+String(year())+"/"+String(month())+"/"+String(day())+"-"+hour()+"-"+minute()+"-"+second()+".csv";
        int i=0;
        while(SPIFFS.exists(newpathname)){//New pathname doesn't exist
          
          
          newpathname=newpathname+"-"+"duplicate"+i++;
        }
         SPIFFS.rename(pathname, newpathname);
         Serial.print("File exceeds 20kb, renaming file:");
         Serial.println(newpathname);      
      f.close();
      }



      }
        

  }  
 
 Serial.println("Wrote readings");
  
}
