// **************************************************************************************
//   This file handles all GSm based functionality for sending data to Thingsty over
//                                  MQTTS (AWS)
//    Author: Talha Ahmed
//  ` Date: 7 August 2024
// **************************************************************************************

#include <Arduino.h>
#include <stdio.h>
#include "gsm.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <esp_task_wdt.h>
#include "async_server.h"

// Define the broker and port as macros
#define BROKER "iot.thingsty.com"
#define PORT 8883
#define CLIENT_ID "625fdecb95fe84dd1ac9"
#define MQTT_TOPIC "625fdecb95fe8/pub/l/4dd1ac9"
#define SIM_APN "wap.mobilinkworld.com"

SoftwareSerial GSM(19, 18);

// **************************************************************************************
//
//      Variables
//
//
// **************************************************************************************

String _buffer;
static char rx_buf[MAX_RX_CAHRS];
static bool mqttAlreadyOpen = false;
static bool mqtt = false;
static bool simInserted = false;
static bool dataPublished = false;
static char jsonBuffer[30]; // Ensure the buffer is large enough to hold the JSON string
static unsigned char errorretry = 0;
// bool needToOpenMqttAgain = false;
bool takeSensorReadings = false;
bool gsmError = false;

unsigned long errorStateStartTime = 0;
const unsigned long errorStateDuration = 1 * 60 * 1000; // 5 minutes in milliseconds

// **************************************************************************************
//
//      Data structures
//
//
// **************************************************************************************
enum gsmState
{
    checkGsmResponse = 1,
    checkSimPresense,
    storeCertAndConfigSSL,
    registerNetwork,
    openGPRS,
    openMqttConn,
    publishDataOnMqtt,
    errorState
};

enum gsmState gsmStateRun = checkGsmResponse;

// **************************************************************************************
//
//      Local Functions declaration
//
//
// **************************************************************************************

static void readGSMResponse();
static bool waitForResponse(const char *response, unsigned long timeout);
static bool checkModuleResponse();
static void processResponseCCID(const char *response);
static bool checkGSMRegistration();
static bool checkGPRS();
static bool openMqtt();
static bool openMqttConnection();
static void isDataPublished(const char *response);
static void checkResponse();
static void checkSim();
static void networkReg();
static void configSSL();
static void gprsOpen();
static void mqttOpen();
static void createJSON(uint16_t readInMM);
static void publishData();
static void enterDeepSleep();
static void restartGSM();

// **************************************************************************************
//
//                      Definition of Local Functions
//
//
// **************************************************************************************

static void readGSMResponse()
{
    static byte ndx = 0;
    char rc;
    memset(rx_buf, 0, sizeof(rx_buf));
    vTaskDelay(pdMS_TO_TICKS(600));
    while (GSM.available() > 0)
    {
        esp_task_wdt_reset();
        rc = GSM.read();

        if (GSM.available() > 0)
        {
            rx_buf[ndx] = rc;
            ndx++;
            if (ndx >= MAX_RX_CAHRS)
            {
                ndx = MAX_RX_CAHRS - 1;
            }
        }
        else
        {
            rx_buf[ndx] = '\0'; // terminate the string
            ndx = 0;
        }
    }
    Serial.println(rx_buf);
}

static bool waitForResponse(const char *response, unsigned long timeout)
{
    unsigned long startTime = millis();
    uint8_t *searchString = 0;
    bool ret = 0;

    while (millis() - startTime < timeout)
    {
        vTaskDelay(pdMS_TO_TICKS(10));
        readGSMResponse();
        esp_task_wdt_reset();
        Serial.println(millis() - startTime);
        searchString = (uint8_t *)strstr(rx_buf, response);
        if (searchString != NULL)
        {
            ret = true;
            break;
        }
        searchString = (uint8_t *)strstr(rx_buf, "ERROR");
        if (searchString != NULL)
        {
            ret = false;
            break;
        }
        searchString = (uint8_t *)strstr(rx_buf, "+CME ERROR:");
        if (searchString != NULL)
        {
            ret = false;
            break;
        }
    }
    return ret; // Timed out waiting for the response
}

uint8_t ProcessCopsCommand(const char *response)
{
    uint8_t ret = 0;
    if (strstr(response, "Telenor"))
    {
        ret = 1; // carrier telenor
        Serial.println("Network is Telenor");
    }
    else if (strstr(response, "Mobilink"))
    {
        ret = 2; // carrier Mobilink Jazz
        Serial.println("Network is JAZZ");
    }
    return ret;
}

static bool checkModuleResponse()
{
    bool ret = false;
    char retries = 0;
    uint8_t *responseString = 0;
    do
    {
        esp_task_wdt_reset();
        GSM.print(F("AT\r\n"));
        readGSMResponse();
        responseString = (uint8_t *)strstr(rx_buf, "OK");
        if (responseString != 0)
        {
            printf("ATString: %s\n", responseString);
            ret = true; // modem detected and responsed
        }
        else
        {
            printf("AT not found, Module not responded\n");
            ret = false; // modem not detected
            retries++;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    } while ((ret == false) && (retries < 15));
    return ret;
}

static void processResponseCCID(const char *response)
{
    uint8_t *CCIDString = 0;
    CCIDString = (uint8_t *)strstr(response, "+CCID");
    if (CCIDString != 0)
    {
        // Increment the pointer to skip "+COPS:"
        CCIDString += 6;
        // Print the resulting copsString
        printf("CCIDString: %s\n", CCIDString);
        simInserted = true;
    }
    else
    {
        printf("CCID not found in the response.\n");
    }

    // Check if the response contains "+CREG: 0,1" or "+CREG: 0,5"
    if (strstr(response, "+CME ERROR: SIM not inserted"))
    {
        printf("Sim not present in module.\n");
        simInserted = false;
    }
}

// static bool processResponseLocalIP(const char *response)
// {
//     uint8_t *IPString = 0;
//     bool ret = false;
//     IPString = (uint8_t *)strstr(response, "ERROR");
//     if (IPString != 0)
//     {
//         printf("Error Returned, No Local IP address\n");
//         ret = false;
//     }
//     else
//     {
//         // Print the resulting copsString
//         printf("IPString: %s\n", IPString);
//         ret = true;
//     }
//     return ret;
// }

static bool checkGSMRegistration()
{
    bool ret = false;
    char retries = 0;
    do
    {
        esp_task_wdt_reset();
        GSM.print(F("AT+CREG?\r\n"));
        readGSMResponse();

        if (strstr(rx_buf, "+CREG: 0,1") || strstr(rx_buf, "+CREG: 0,5"))
        {
            printf("Registered to the network\n");
            ret = true;
        }
        retries++;
        vTaskDelay(pdMS_TO_TICKS(10));
    } while ((ret == false) && (retries < 50));
    return ret;
}

static bool checkGPRS()
{
    GSM.print(F("AT+CGATT?\r"));
    vTaskDelay(pdMS_TO_TICKS(100));
    readGSMResponse();
    bool ret = false;

    if (strstr(rx_buf, "+CGATT: 1"))
    {
        ret = true; // GPRS active
    }
    else
    {
        ret = false;
    }
    return ret;
}

static bool openMqtt()
{
    uint8_t *mqttString = 0;
    bool ret;
    unsigned char retries = 0;
    // Buffer to hold the constructed string
    char mqttStr[100]; // Make sure the buffer is large enough to hold the entire string

    // Construct the full mqttStr string
    sprintf(mqttStr, "AT+QMTOPEN=0,\"%s\",%d\r\n", BROKER, PORT);
    // Print the resulting string (for demonstration)
    printf("%s", mqttStr);

    do
    {
        esp_task_wdt_reset();
        GSM.print(mqttStr);
        ret = waitForResponse("+QMTOPEN:", 75000);
        if (ret == true)
        {
            mqttString = (uint8_t *)strstr(rx_buf, "+QMTOPEN:");
            if (mqttString != 0)
            {
                mqttString += 9;

                // Print the resulting String
                printf("MQTTString: %s\n", mqttString);
                // Parse the result manually
                int num1 = -1, num2 = -1;

                char *token = strtok((char *)mqttString, ",");

                if (token != NULL)
                {
                    num1 = atoi(token); // Convert the first number
                    token = strtok(NULL, ",");
                    if (token != NULL)
                    {
                        num2 = atoi(token); // Convert the second number
                    }
                }
                Serial.println(num1);
                Serial.println(num2);
                // Check the values
                if (num1 == 0 && num2 == 0)
                {
                    Serial.println("MQTT OPEN");
                    ret = true;
                    mqttAlreadyOpen = true;
                }
                else if (num1 == 0 && num2 == 2)
                {
                    Serial.println("MQTT Already OPEN");
                    ret = false;
                    mqttAlreadyOpen = true;
                }
                else
                {
                    Serial.println("MQTT not Open");
                    ret = false;
                    mqttAlreadyOpen = false;
                }
                vTaskDelay(pdMS_TO_TICKS(10));
            }
            else
            {
                printf("MQTT not found in the response.\n");
                ret = false;
                mqttAlreadyOpen = false;
                vTaskDelay(pdMS_TO_TICKS(10));
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10));
        retries++;
    } while (mqttAlreadyOpen == false && retries < 7);

    if ((ret == false) && (mqttAlreadyOpen != true))
    {
        Serial.println("retries exceed for open mqtt");
    }
    return ret;
}

static bool openMqttConnection()
{
    uint8_t *mqttConnString = 0;
    bool ret = false;
    unsigned char retries = 0;

    // Buffer to hold the constructed string
    char mqttConnStr[100]; // Make sure the buffer is large enough to hold the entire string
    // Construct the full mqttConnStr string
    sprintf(mqttConnStr, "AT+QMTCONN=0,\"%s\"\r\n", CLIENT_ID);
    // Print the resulting string (for demonstration)
    printf("%s", mqttConnStr);

    do
    {
        esp_task_wdt_reset();
        GSM.print(mqttConnStr);
        ret = waitForResponse("+QMTCONN:", 7000);
        if (ret == true)
        {
            mqttConnString = (uint8_t *)strstr(rx_buf, "+QMTCONN:");
            if (mqttConnString != NULL)
            {
                // Increment the pointer to skip "+COPS:"
                mqttConnString += 9;

                // Print the resulting copsString
                printf("MQTTString: %s\n", mqttConnString);
                // Parse the result manually
                int num1 = -1, num2 = -1, num3 = -1;

                char *token = strtok((char *)mqttConnString, ",");

                if (token != NULL)
                {
                    num1 = atoi(token); // Convert the first number
                    token = strtok(NULL, ",");
                    if (token != NULL)
                    {
                        num2 = atoi(token); // Convert the second number
                        token = strtok(NULL, ",");
                        if (token != NULL)
                        {
                            num3 = atoi(token); // Convert the second number
                        }
                    }
                }
                Serial.println(num1);
                Serial.println(num2);
                Serial.println(num3);
                // Check the values
                if ((num1 == 0 && num2 == 0 && num3 == 0))
                {
                    // Perform some stuff
                    Serial.println("MQTT connection OPEN");
                    ret = true;
                    break;
                }
                else
                {
                    Serial.println("MQTT connection not Open");
                    ret = false;
                }
                vTaskDelay(pdMS_TO_TICKS(100));
            }
            else
            {
                printf("QMTCONN not found in the response.\n");
                ret = false;
                vTaskDelay(pdMS_TO_TICKS(100));
                // needToOpenMqttAgain = true;
            }

            if (strstr(rx_buf, "+QMTSTAT: 0,1"))
            {
                Serial.println("+QMTSTAT: 0,1 received so mqtt need to open again");
                // needToOpenMqttAgain = true;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(100));
        retries++;
    } while (ret == false && retries < 7);
    if (ret == false)
    {
        Serial.println("timeout mqtt connection\n");
    }
    return ret;
}

static void isDataPublished(const char *response)
{
    uint8_t *pubString = 0;
    pubString = (uint8_t *)strstr(response, "+QMTPUB:");
    if (pubString != 0)
    {
        // Increment the pointer to skip "+COPS:"
        pubString += 8;
        // Print the resulting pubString
        printf("MQTTPUBtring: %s\n", pubString);

        if (strstr((char *)pubString, "0,0,0"))
        {
            Serial.println("Data Published Succesfully");
            dataPublished = true;
        }
        else
        {
            Serial.println("Data not Published");
        }
    }
    else
    {
        dataPublished = false;
        printf("QMTPUB not found in the response.\n");
    }
}

static void checkResponse()
{
    bool modemDetected = false;
    vTaskDelay(pdMS_TO_TICKS(200));
    GSM.print(F("AT+IPR=9600\r\n"));
    readGSMResponse();
    GSM.print(F("AT+IPR=9600\r\n"));
    readGSMResponse();
    modemDetected = checkModuleResponse();
    errorStateStartTime = 0;
    if (modemDetected == true)
    {
        GSM.print(F("AT+CFUN=1\r\n"));
        readGSMResponse();

        // GSM.print(F("AT&W\r\n")); // save settings
        // readGSMResponse();
        GSM.print(F("AT+IPR?\r\n"));
        readGSMResponse();

        GSM.print(F("ATI\r\n"));
        readGSMResponse();

        GSM.print(F("AT+GSN\r\n"));
        readGSMResponse();

        // GSM.print(F("AT+QGMR\r\n")); // firmware version of module
        // readGSMResponse();

        GSM.print(F("ATE0\r\n")); // turn echo off
        readGSMResponse();

        GSM.println("AT+QSCLK=1\r\n"); // Configuring sleep mode
        readGSMResponse();

        gsmStateRun = checkSimPresense;
    }
    else
    {
        gsmStateRun = errorState;
    }

    // GSM.print(F("ATZ\r")); //reset module
    // readGSMResponse();

    // GSM.print(F("AT+COPS=?\r")); //search network
    // readGSMResponse();
}

static void checkSim()
{
    char retries = 0;
    do
    {
        esp_task_wdt_reset();
        GSM.print(F("AT+CCID\r\n")); // Read SIM information to confirm whether the SIM is plugged
        readGSMResponse();
        processResponseCCID(rx_buf);
        retries++;
        vTaskDelay(pdMS_TO_TICKS(10));
    } while ((simInserted == false) && (retries < 30));

    GSM.print(F("AT+CSQ\r\n")); // Signal quality test, value range is 0-31 , 31 is the best
    readGSMResponse();

    if (simInserted == true)
    {
        gsmStateRun = registerNetwork;
    }
    else
    {
        Serial.println("Sim not present, no need to furthur proceed for gsm commands");
        gsmStateRun = errorState;
    }
}

static void networkReg()
{
    bool reg = false;
    vTaskDelay(pdMS_TO_TICKS(100));
    reg = checkGSMRegistration();
    if (reg == true)
    {
        gsmStateRun = storeCertAndConfigSSL;
    }
    else
    {
        Serial.println("reg failed so no need to open MQTT");
        gsmStateRun = errorState;
    }
}

static void configSSL()
{
    // GSM.print(F("AT+QSECDEL=\"RAM:cacert.pem\"\r\n"));
    // readGSMResponse();
    // GSM.print(F("AT+QSECDEL=\"RAM:client.pem\"\r\n"));
    // readGSMResponse();
    // GSM.print(F("AT+QSECDEL=\"RAM:user_key.pem\"\r\n"));
    // readGSMResponse();

    GSM.print(F("AT+QMTCFG=\"SSL\",0,1,2\r\n"));
    readGSMResponse();
    GSM.print(F("AT+QSECWRITE=\"RAM:cacert.pem\",1187,100\r\n"));
    if (waitForResponse("CONNECT", 10000))
    {
        GSM.print(Read_rootca);
        readGSMResponse();
    }
    else
    {
        Serial.println("No CONNECT response received for CA Cert");
    }

    GSM.print(F("AT+QSECWRITE=\"RAM:client.pem\",1224,100\r\n"));
    if (waitForResponse("CONNECT", 10000))
    {
        GSM.print(Client_cert);
        readGSMResponse();
    }
    else
    {
        Serial.println("No CONNECT response received for Client Cert");
    }

    GSM.print(F("AT+QSECWRITE=\"RAM:user_key.pem\",1679,100\r\n"));
    if (waitForResponse("CONNECT", 10000))
    {
        GSM.print(Client_privatekey);
        readGSMResponse();
    }
    else
    {
        Serial.println("No CONNECT response received for Client PVT key");
    }

    vTaskDelay(pdMS_TO_TICKS(200));
    GSM.print(F("AT+QSECREAD=\"RAM:cacert.pem\"\r\n"));
    readGSMResponse();

    GSM.print(F("AT+QSECREAD=\"RAM:client.pem\"\r\n"));
    readGSMResponse();
    vTaskDelay(pdMS_TO_TICKS(200));

    GSM.print(F("AT+QSECREAD=\"RAM:user_key.pem\"\r\n"));
    readGSMResponse();
    vTaskDelay(pdMS_TO_TICKS(200));

    GSM.print(F("AT+QSSLCFG=\"cacert\",2,\"RAM:cacert.pem\"\r\n"));
    readGSMResponse();
    GSM.print(F("AT+QSSLCFG=\"clientcert\",2,\"RAM:client.pem\"\r\n"));
    readGSMResponse();
    GSM.print(F("AT+QSSLCFG=\"clientkey\",2,\"RAM:user_key.pem\"\r\n"));
    readGSMResponse();

    GSM.print(F("AT+QSSLCFG=\"seclevel\",2,2\r\n"));
    readGSMResponse();
    GSM.print(F("AT+QSSLCFG=\"sslversion\",2,4\r\n"));
    readGSMResponse();
    GSM.print(F("AT+QSSLCFG=\"ciphersuite\",2,\"0xFFFF\"\r\n"));
    readGSMResponse();
    GSM.print(F("AT+QSSLCFG=\"ignorertctime\",1\r\n"));
    readGSMResponse();

    gsmStateRun = openGPRS;
}

static void gprsOpen()
{
    uint8_t carrierDetect = 0;

    vTaskDelay(pdMS_TO_TICKS(100));
    GSM.print(F("AT+COPS?\r\n"));
    readGSMResponse();
    carrierDetect = ProcessCopsCommand(rx_buf);

    // BUild APN respectively by carrier
    char APNStr[50]; // Make sure the buffer is large enough to hold the entire string
    char APN[50];
    if (carrierDetect == 1)
    {
        strcpy(APN, "internet"); // telenor
    }
    else if (carrierDetect == 2)
    {
        strcpy(APN, "wap.mobilinkworld.com"); // mobilink jazz
    }
    else
    {
        // do nothing, added suport for only 2 carriers yet
    }
    // Construct the full mqttConnStr string
    sprintf(APNStr, "AT+QICSGP=1,\"%s\"\r\n", APN);
    // Print the resulting string (for demonstration)
    printf("%s", APNStr);

    GSM.print(F("AT+CPIN?\r\n"));
    readGSMResponse();

    (void)checkGPRS();

    GSM.print(F("AT+QIMODE=0\r\n"));
    readGSMResponse();
    GSM.print(APNStr); // set APN
    readGSMResponse();
    GSM.print(F("AT+QIREGAPP\r\n"));
    readGSMResponse();
    GSM.print(F("AT+QICSGP?\r\n"));
    readGSMResponse();
    GSM.print(F("AT+QIACT\r\n"));
    readGSMResponse();
    GSM.print(F("AT+QILOCIP\r\n"));
    readGSMResponse();

    gsmStateRun = openMqttConn;
}

static void mqttOpen()
{
    mqtt = false;
    mqtt = openMqtt();
    //    if (mqtt == true)
    //    {
    (void)openMqttConnection();
    // }
    takeSensorReadings = true;
    gsmStateRun = publishDataOnMqtt;
}

static void createJSON(uint16_t distanceInMM)
{
    sprintf(jsonBuffer, "{\"distanceMeasure\": %u}", distanceInMM);

    Serial.println(jsonBuffer); // Output: {"distance_measure": 123.45}
}

static void publishData()
{
    // Buffer to hold the constructed string
    char mqttPubStr[100]; // Make sure the buffer is large enough to hold the entire string
    // Construct the full mqttConnStr string
    sprintf(mqttPubStr, "AT+QMTPUB=0,0,0,0,\"%s\"\r\n", MQTT_TOPIC);
    // Print the resulting string (for demonstration)
    printf("%s", mqttPubStr);

    unsigned char retries = 0;
    Serial.flush();
    createJSON(14);
    do
    {
        esp_task_wdt_reset();
        // if (mqtt == true || mqttAlreadyOpen == true)
        // {
            // send data first time
            GSM.print(mqttPubStr);
            readGSMResponse();
            GSM.print(F(jsonBuffer));
            GSM.write(0X1A);
            GSM.write(0X1A);
            readGSMResponse();
            isDataPublished(rx_buf);
            vTaskDelay(pdMS_TO_TICKS(2000));
            // send data second time
            GSM.print(mqttPubStr);
            readGSMResponse();
            GSM.print(F(jsonBuffer));
            GSM.write(0X1A);
            GSM.write(0X1A);
            readGSMResponse();
            isDataPublished(rx_buf);
            vTaskDelay(pdMS_TO_TICKS(2000));
        // }
       // esp_task_wdt_reset();
        retries++;
    } while ((retries < 3) && (dataPublished == false));

    if (dataPublished == true)
    {
       // GSM.print(F("AT+QMTDISC=0\r\n")); // disconnect MQTT before entering sleep, so we open again after wakeup
       // readGSMResponse();
        errorretry = 0;
       // enterDeepSleep();
    }
    else
    {
        gsmStateRun = errorState;
    }

    for (uint8_t i=0; i<60; i++)
    {
        vTaskDelay(pdMS_TO_TICKS(1000));
        Serial.println(i);
    }
}

static void restartGSM()
{
    GSM.print(F("AT+CFUN=0\r\n")); // set module to minimum functioanlity
    readGSMResponse();
    vTaskDelay(pdMS_TO_TICKS(2000));
    GSM.print(F("AT+CFUN=1,1\r\n")); // reset and set module to full functioanlity
    readGSMResponse();
}
// **************************************************************************************
//
//           This function put ESP32 and Quecetel M95 GSm module into sleep
//
//
// **************************************************************************************

static void enterDeepSleep()
{
    digitalWrite(32, LOW); // put GSm to Sleep
    vTaskDelay(pdMS_TO_TICKS(1000));
    Serial.println("Entering sleep Mode");
    Serial.flush();
    esp_deep_sleep_start(); // put ESP32 to Sleep
}

// **************************************************************************************
//
//             **********Global Function*********
//                      GSM STATE MACHINE
//
// **************************************************************************************
void gsmStateMachine()
{
    switch (gsmStateRun)
    {
    case checkGsmResponse:
        checkResponse();
        break;

    case checkSimPresense:
        checkSim();
        break;

    case registerNetwork:
        networkReg();
        break;

    case storeCertAndConfigSSL:
        configSSL();
        break;

    case openGPRS:
        gprsOpen();
        break;

    case openMqttConn:
        mqttOpen();
        break;

    case publishDataOnMqtt:
        publishData();
        break;

    case errorState:
        if (errorretry < 5)
        {
            restartGSM();
            gsmStateRun = checkGsmResponse;
            takeSensorReadings = false;
        }
        // else
        // {
        //     gsmError = true;
        //     takeSensorReadings = false;
        //     vTaskDelay(pdMS_TO_TICKS(100));
        //     // Serial.println("Error state");
        //     // If the error state just started, record the start time
        //     if (errorStateStartTime == 0)
        //     {
        //         errorStateStartTime = millis();
        //     }
        //     // Check if 5 minutes have passed
        //     if (millis() - errorStateStartTime >= errorStateDuration)
        //     {
        //         vTaskDelay(pdMS_TO_TICKS(100));
        //         enterDeepSleep();
        //     }
        // }
        errorretry++;
        break;

    default:
        gsmStateRun = errorState; // code is not supposed to come here, if it comes put it in error state
        break;
    }
}