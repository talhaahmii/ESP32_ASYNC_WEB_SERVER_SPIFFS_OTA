#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <SPIFFS.h>
#include <Update.h>
#include "async_server.h"

// Credits : this is a mashup of code from the following repositories, plus OTA firmware update feature
// https://github.com/smford/esp32-asyncwebserver-fileupload-example
// https://randomnerdtutorials.com/esp32-web-server-spiffs-spi-flash-file-system/

// connect to existing WiFi access point as a station
//#define STATION_WEBSERVER

typedef struct WIFI_CONFIG_ {
  String ssid;               // wifi ssid
  String wifipassword;       // wifi password
  String httpuser;           // username to access web admin
  String httppassword;       // password to access web admin
  int webserverporthttp;     // http port number for web admin
} WIFI_CONFIG;

AsyncWebServer *server = NULL;  

bool IsRebootRequired = false;
         
const char* host = "esp32";

const String default_ssid = "access-point";
const String default_wifipassword = "12345678";
const String default_httpuser = "admin";
const String default_httppassword = "admin";
const int default_webserverporthttp = 80;

static WIFI_CONFIG config;    
static File SpiffsFile;

String Read_rootca;
String Client_cert;
String Client_privatekey;

static String server_directory(bool ishtml = false);
static void server_not_found(AsyncWebServerRequest *request);
static bool server_authenticate(AsyncWebServerRequest * request);
static void server_handle_upload(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final);
static void server_handle_SPIFFS_upload(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final);
static String server_string_processor(const String& var);
static void server_configure();
static String server_ui_size(const size_t bytes);
static void server_handle_OTA_update(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final);
static int spiffs_chunked_read(uint8_t* buffer, int maxLen);


void server_init() {
  //=======================================
  //Root CA File Reading.
  File file2 = SPIFFS.open("/CACert.crt", "r");
  if (!file2) {
    Serial.println("Failed to open file for reading");
    return;
  }
  Serial.println("Root CA File Content:");
  while (file2.available()) {
    Read_rootca = file2.readString();
    Serial.println(Read_rootca);
  }
  //=============================================
  // Cert file reading
  File file4 = SPIFFS.open("/ClientCert.crt", "r");
  if (!file4) {
    Serial.println("Failed to open file for reading");
    return;
  }
  Serial.println("Cert File Content:");
  while (file4.available()) {
    Client_cert = file4.readString();
    Serial.println(Client_cert);
  }
  //=================================================
  //Privatekey file reading
  File file6 = SPIFFS.open("/ClientPrivate.key", "r");
  if (!file6) {
    Serial.println("Failed to open file for reading");
    return;
  }
  Serial.println("privateKey File Content:");
  while (file6.available()) {
    Client_privatekey = file6.readString();
    Serial.println(Client_privatekey);
  }
  //=====================================================

  Serial.print("SPIFFS Free: "); Serial.println(server_ui_size((SPIFFS.totalBytes() - SPIFFS.usedBytes())));
  Serial.print("SPIFFS Used: "); Serial.println(server_ui_size(SPIFFS.usedBytes()));
  Serial.print("SPIFFS Total: "); Serial.println(server_ui_size(SPIFFS.totalBytes()));

  Serial.println(server_directory(false));
  Serial.println("Loading Configuration ...");
  config.httpuser = default_httpuser;
  config.httppassword = default_httppassword;
  config.webserverporthttp = default_webserverporthttp;

#ifdef STATION_WEBSERVER
  // connect to existing WiFi access point as a station
  config.ssid = default_ssid;
  config.wifipassword = default_wifipassword;

  Serial.print("\r\nConnecting to existing Wifi Access Point : ");
  WiFi.begin(config.ssid.c_str(), config.wifipassword.c_str());
  WiFi.setTxPower(WIFI_POWER_MINUS_1dBm);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\r\n\r\nNetwork Configuration:");
  Serial.println("----------------------");
  Serial.print("         SSID: "); Serial.println(WiFi.SSID());
  Serial.print("  Wifi Status: "); Serial.println(WiFi.status());
  Serial.print("Wifi Strength: "); Serial.print(WiFi.RSSI()); Serial.println(" dBm");
  Serial.print("          MAC: "); Serial.println(WiFi.macAddress());
  Serial.print("           IP: "); Serial.println(WiFi.localIP());
  Serial.print("       Subnet: "); Serial.println(WiFi.subnetMask());
  Serial.print("      Gateway: "); Serial.println(WiFi.gatewayIP());
  Serial.print("        DNS 1: "); Serial.println(WiFi.dnsIP(0));
  Serial.print("        DNS 2: "); Serial.println(WiFi.dnsIP(1));
  Serial.print("        DNS 3: "); Serial.println(WiFi.dnsIP(2));
  Serial.println();
  if (!MDNS.begin(host)) { //Use http://esp32.local for web server page
    Serial.println("Error setting up MDNS responder!");
    while (1) {
      delay(1000);
      }
    }
  Serial.println("mDNS responder started");
#else // set up as stand-alone WiFi Access Point
  WiFi.mode(WIFI_AP);
  WiFi.setTxPower(WIFI_POWER_MINUS_1dBm);
  bool result =  WiFi.softAP("Esp32_Access_Point", ""); // "" => no password
  Serial.println(result == true ? "AP setup OK" : "AP setup failed");
  IPAddress myIP = WiFi.softAPIP();  
  Serial.print("Access Point IP address: ");
  Serial.println(myIP);
  
  if (!MDNS.begin(host)) { //Use http://esp32.local for web server page
    Serial.println("Error setting up MDNS responder!");
    while (1) {
      delay(1000);
      }
    }
  Serial.println("mDNS responder started");
#endif

  Serial.println("Configuring Webserver ...");
  server = new AsyncWebServer(config.webserverporthttp);
  server_configure();

  Serial.println("Starting Webserver ...");
  server->begin();
}

// list all of the files, if ishtml=true, return html rather than simple text
static String server_directory(bool ishtml) {
  String returnText = "";
  Serial.println("Listing files stored on SPIFFS");
  File root = SPIFFS.open("/");
  File foundfile = root.openNextFile();
  if (ishtml) {
    returnText += "<table align='center'><tr><th align='left'>Name</th><th align='left'>Size</th><th></th><th></th></tr>";
  }
  while (foundfile) {
    if (ishtml) {
      returnText += "<tr align='left'><td>" + String(foundfile.name()) + "</td><td>" + server_ui_size(foundfile.size()) + "</td>";
      returnText += "<td><button class='directory_buttons' onclick=\"directory_button_handler(\'"+ String("/") + String(foundfile.name()) + "\', \'download\')\">Download</button>";
      returnText += "<td><button class='directory_buttons' onclick=\"directory_button_handler(\'"+ String("/")  + String(foundfile.name()) + "\', \'delete\')\">Delete</button></tr>";
    } else {
      returnText += "File: " + String(foundfile.name()) + " Size: " + server_ui_size(foundfile.size()) + "\n";
    }
    foundfile = root.openNextFile();
  }
  if (ishtml) {
    returnText += "</table>";
  }
  root.close();
  foundfile.close();
  return returnText;
}

// Make size of files human readable
// source: https://github.com/CelliesProjects/minimalUploadAuthESP32
static String server_ui_size(const size_t bytes) {
  if (bytes < 1024) return String(bytes) + " B";
  else if (bytes < (1024 * 1024)) return String(bytes / 1024.0) + " KB";
  else if (bytes < (1024 * 1024 * 1024)) return String(bytes / 1024.0 / 1024.0) + " MB";
  else return String(bytes / 1024.0 / 1024.0 / 1024.0) + " GB";
  }


// replace %SOMETHING%  in webpage with dynamically generated string
static String server_string_processor(const String& var) {
    if (var == "BUILD_TIMESTAMP") {
        return String(__DATE__) + " " + String(__TIME__); 
        }
    else
    if (var == "FREESPIFFS") {
        return server_ui_size((SPIFFS.totalBytes() - SPIFFS.usedBytes()));
        }
    else
    if (var == "USEDSPIFFS") {
        return server_ui_size(SPIFFS.usedBytes());
        }
    else
    if (var == "TOTALSPIFFS") {
        return server_ui_size(SPIFFS.totalBytes());
        }
    else
    if (var == "CLIENTID") {
    return "TALHA";//readEEPROM(CLIENT_ID_ADDR, 50);  // Replace with the actual stored Client ID
      } 
    else 
    if (var == "TOPIC") {
    return "AHMED";//readEEPROM(TOPIC_ADDR, 50);      // Replace with the actual stored Topic
      } 
    else 
    if (var == "SIMAPN") {
    return "HELLO FROM ESP";//readEEPROM(SIM_APN_ADDR, 50);    // Replace with the actual stored SIM APN
      }
    // return "?";
    return String();  // Return an empty string for unknown placeholders
    }


static int spiffs_chunked_read(uint8_t* buffer, int maxLen) {              
  //Serial.printf("MaxLen = %d\n", maxLen);
  if (!SpiffsFile.available()) {
    SpiffsFile.close();
    return 0;
    }
  else {
    int count = 0;
    while (SpiffsFile.available() && (count < maxLen)) {
      buffer[count] = SpiffsFile.read();
      count++;
      }
    return count;
    }
}

void server_configure() {
  // if url isn't found
  server->onNotFound(server_not_found);

  // run handleUpload function when any file is uploaded
  server->onFileUpload(server_handle_upload);

   // route to handle the user inputs for MQTT settings
  server->on("/save-mqtt-settings", HTTP_POST, [](AsyncWebServerRequest *request) {
    String clientID, topic, simAPN;

    // Check if all the required parameters are present
    if (request->hasParam("clientID", true) && request->hasParam("topic", true) && request->hasParam("simAPN", true)) {
      clientID = request->getParam("clientID", true)->value();
      topic = request->getParam("topic", true)->value();
      simAPN = request->getParam("simAPN", true)->value();

      // Debug prints to check the received values
      Serial.println("Received MQTT Client ID: " + clientID);
      Serial.println("Received MQTT Topic: " + topic);
      Serial.println("Received SIM APN: " + simAPN);

      // You can now save or process these variables
      // For example, you can store them in SPIFFS, EEPROM, or use them directly

      // Respond with a success message
      request->send(200, "text/plain", "MQTT Settings Saved Successfully");
    } else {
      // Respond with an error if any parameter is missing
      request->send(400, "text/plain", "Missing parameters");
    }
  });

  // visiting this page will cause you to be logged out
  server->on("/logout", HTTP_GET, [](AsyncWebServerRequest * request) {
    request->requestAuthentication();
    request->send(401);
  });

  // presents a "you are now logged out webpage
  server->on("/logged-out", HTTP_GET, [](AsyncWebServerRequest * request) {
    String logmessage = "Client:" + request->client()->remoteIP().toString() + " " + request->url();
    Serial.println(logmessage);
    request->send(SPIFFS, "/logout.html", String(), false, server_string_processor);
  });

  server->on("/", HTTP_GET, [](AsyncWebServerRequest * request) {
    String logmessage = "Client:" + request->client()->remoteIP().toString() + + " " + request->url();
    if (server_authenticate(request)) {
      logmessage += " Auth: Success";
      Serial.println(logmessage);
      request->send(SPIFFS, "/index.html", String(), false, server_string_processor);
    } else {
      logmessage += " Auth: Failed";
      Serial.println(logmessage);
      return request->requestAuthentication();
    }
    
  });


    // Route to load style.css file
  server->on("/style.css", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(SPIFFS, "/style.css", "text/css");
  });

  server->on("/reboot", HTTP_GET, [](AsyncWebServerRequest * request) {
    String logmessage = "Client:" + request->client()->remoteIP().toString() + " " + request->url();
    request->send(SPIFFS, "/reboot.html", String(), false, server_string_processor);
    logmessage += " Auth: Success";
    Serial.println(logmessage);
    IsRebootRequired = true;
  });

  server->on("/directory", HTTP_GET, [](AsyncWebServerRequest * request)  {
    String logmessage = "Client:" + request->client()->remoteIP().toString() + " " + request->url();
    if (server_authenticate(request)) {
      logmessage += " Auth: Success";
      Serial.println(logmessage);
      request->send(200, "text/plain", server_directory(true));
    } else {
      logmessage += " Auth: Failed";
      Serial.println(logmessage);
      return request->requestAuthentication();
    }
  });


  server->on("/file", HTTP_GET, [](AsyncWebServerRequest * request) {
    String logmessage = "Client:" + request->client()->remoteIP().toString() + " " + request->url();
    if (server_authenticate(request)) {
      logmessage += " Auth: Success";
      Serial.println(logmessage);

      if (request->hasParam("name") && request->hasParam("action")) {
        const char *fileName = request->getParam("name")->value().c_str();
        const char *fileAction = request->getParam("action")->value().c_str();
        Serial.println(fileName);
        logmessage = "Client:" + request->client()->remoteIP().toString() + " " + request->url() + "? name=" + String(fileName) + " & action=" + String(fileAction);

        if (!SPIFFS.exists(fileName)) {
          Serial.println(logmessage + " ERROR: file does not exist");
          request->send(400, "text/plain", "ERROR: file does not exist");
          } 
        else {
          Serial.println(logmessage + " file exists");
          if (strcmp(fileAction, "download") == 0) {
            logmessage += " downloaded";
            SpiffsFile = SPIFFS.open(fileName, "r");
            int sizeBytes = SpiffsFile.size();
            Serial.println("large file, chunked download required");
            AsyncWebServerResponse *response = request->beginResponse("application/octet-stream", sizeBytes, [](uint8_t *buffer, size_t maxLen, size_t index) -> size_t {
              return spiffs_chunked_read(buffer, maxLen);
              });
            char szBuf[80];
            sprintf(szBuf, "attachment; filename=%s", &fileName[1]);// get past the leading '/'
            response->addHeader("Content-Disposition", szBuf);
            response->addHeader("Connection", "close");
            request->send(response);
            } 
          else 
          if (strcmp(fileAction, "delete") == 0) {
            logmessage += " deleted";
            SPIFFS.remove(fileName);
            request->send(200, "text/plain", "Deleted File: " + String(fileName));
            } 
          else {
            logmessage += " ERROR: invalid action param supplied";
            request->send(400, "text/plain", "ERROR: invalid action param supplied");
            }
          Serial.println(logmessage);
          }
      } 
    else {
      request->send(400, "text/plain", "ERROR: name and action params required");
      }
    } 
  else {
    logmessage += " Auth: Failed";
    Serial.println(logmessage);
    return request->requestAuthentication();
    }
  });
}

#if 0
void read_file_chunk(uint8_t* buffer, int maxlen) {
      int bytesRemaining = (int)(FlashLogFreeAddress - flashAddr);
      do {
         int numXmitBytes =  bytesRemaining > 256 ? 256 : bytesRemaining;  
		   spiflash_readBuffer(flashAddr, buffer, numXmitBytes);
         pServer->sendContent_P((const char*)buffer, numXmitBytes);
         flashAddr += numXmitBytes;
         bytesRemaining = (int)(FlashLogFreeAddress - flashAddr);
         delayMs(10);
		   } while (bytesRemaining >= 0);
	   }
   else {
      server_reportFileNotFound("datalog"); 
      }
   }
#endif

static void server_not_found(AsyncWebServerRequest *request) {
  String logmessage = "Client:" + request->client()->remoteIP().toString() + " " + request->url();
  Serial.println(logmessage);
  request->send(404, "text/plain", "Not found");
  }
  
// used by server.on functions to discern whether a user has the correct httpapitoken OR is authenticated by username and password
bool server_authenticate(AsyncWebServerRequest * request) {
  bool isAuthenticated = false;

  if (request->authenticate(config.httpuser.c_str(), config.httppassword.c_str())) {
    Serial.println("is authenticated via username and password");
    isAuthenticated = true;
  }
  return isAuthenticated;
}


static void server_handle_upload(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
    if (filename.endsWith(".bin") ) {
      server_handle_OTA_update(request, filename, index, data, len, final);
      }
    else {
      server_handle_SPIFFS_upload(request, filename, index, data, len, final);
    }
}


// handles non .bin file uploads to the SPIFFS directory
static void server_handle_SPIFFS_upload(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
  // make sure authenticated before allowing upload
  if (server_authenticate(request)) {
    String logmessage = "Client:" + request->client()->remoteIP().toString() + " " + request->url();
    Serial.println(logmessage);

    if (!index) {
      logmessage = "Upload Start: " + String(filename);
      // open the file on first call and store the file handle in the request object
      request->_tempFile = SPIFFS.open("/" + filename, "w");
      Serial.println(logmessage);
    }

    if (len) {
      // stream the incoming chunk to the opened file
      request->_tempFile.write(data, len);
      logmessage = "Writing file: " + String(filename) + " index=" + String(index) + " len=" + String(len);
      Serial.println(logmessage);
    }

    if (final) {
      logmessage = "Upload Complete: " + String(filename) + ",size: " + String(index + len);
      // close the file handle as the upload is now done
      request->_tempFile.close();
      Serial.println(logmessage);
      request->redirect("/");
    }
  } else {
    Serial.println("Auth: Failed");
    return request->requestAuthentication();
  }
}


// handles OTA firmware update
static void server_handle_OTA_update(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
  // make sure authenticated before allowing upload
  if (server_authenticate(request)) {
    String logmessage = "Client:" + request->client()->remoteIP().toString() + " " + request->url();
    Serial.println(logmessage);

    if (!index) {
      logmessage = "OTA Update Start: " + String(filename);
      Serial.println(logmessage);
      if (!Update.begin(UPDATE_SIZE_UNKNOWN)) { //start with max available size
        Update.printError(Serial);
        }
    }

    if (len) {
     // flashing firmware to ESP
     if (Update.write(data, len) != len) {
        Update.printError(Serial);
        }      
      logmessage = "Writing file: " + String(filename) + " index=" + String(index) + " len=" + String(len);
      Serial.println(logmessage);
    }

    if (final) {
     if (Update.end(true)) { //true to set the size to the current progress
         logmessage = "OTA Complete: " + String(filename) + ",size: " + String(index + len);
         Serial.println(logmessage);
          } 
     else {
          Update.printError(Serial);
          }
      request->redirect("/");
      }
  } else {
    Serial.println("Auth: Failed");
    return request->requestAuthentication();
  }
}
