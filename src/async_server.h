#ifndef ASYNC_SERVER_H_
#define ASYNC_SERVER_H_

#include <ESPAsyncWebServer.h>

extern AsyncWebServer *server;
extern bool IsRebootRequired;

extern String Read_rootca;
extern String Client_cert;
extern String Client_privatekey;

void server_init();

#endif
