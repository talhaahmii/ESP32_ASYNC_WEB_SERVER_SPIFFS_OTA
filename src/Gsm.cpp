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
//        **************Certificates***************
//                  1. CA Certificate
//                  2. Client certificate
//                  3. Client private Key
// **************************************************************************************

// CA Certificate
const char *CA_certificate =
    "-----BEGIN CERTIFICATE-----\n"
    "MIIDQTCCAimgAwIBAgITBmyfz5m/jAo54vB4ikPmljZbyjANBgkqhkiG9w0BAQsF\n"
    "ADA5MQswCQYDVQQGEwJVUzEPMA0GA1UEChMGQW1hem9uMRkwFwYDVQQDExBBbWF6\n"
    "b24gUm9vdCBDQSAxMB4XDTE1MDUyNjAwMDAwMFoXDTM4MDExNzAwMDAwMFowOTEL\n"
    "MAkGA1UEBhMCVVMxDzANBgNVBAoTBkFtYXpvbjEZMBcGA1UEAxMQQW1hem9uIFJv\n"
    "b3QgQ0EgMTCCASIwDQYJKoZIhvcNAQEBBQADggEPADCCAQoCggEBALJ4gHHKeNXj\n"
    "ca9HgFB0fW7Y14h29Jlo91ghYPl0hAEvrAIthtOgQ3pOsqTQNroBvo3bSMgHFzZM\n"
    "9O6II8c+6zf1tRn4SWiw3te5djgdYZ6k/oI2peVKVuRF4fn9tBb6dNqcmzU5L/qw\n"
    "IFAGbHrQgLKm+a/sRxmPUDgH3KKHOVj4utWp+UhnMJbulHheb4mjUcAwhmahRWa6\n"
    "VOujw5H5SNz/0egwLX0tdHA114gk957EWW67c4cX8jJGKLhD+rcdqsq08p8kDi1L\n"
    "93FcXmn/6pUCyziKrlA4b9v7LWIbxcceVOF34GfID5yHI9Y/QCB/IIDEgEw+OyQm\n"
    "jgSubJrIqg0CAwEAAaNCMEAwDwYDVR0TAQH/BAUwAwEB/zAOBgNVHQ8BAf8EBAMC\n"
    "AYYwHQYDVR0OBBYEFIQYzIU07LwMlJQuCFmcx7IQTgoIMA0GCSqGSIb3DQEBCwUA\n"
    "A4IBAQCY8jdaQZChGsV2USggNiMOruYou6r4lK5IpDB/G/wkjUu0yKGX9rbxenDI\n"
    "U5PMCCjjmCXPI6T53iHTfIUJrU6adTrCC2qJeHZERxhlbI1Bjjt/msv0tadQ1wUs\n"
    "N+gDS63pYaACbvXy8MWy7Vu33PqUXHeeE6V/Uq2V8viTO96LXFvKWlJbYK8U90vv\n"
    "o/ufQJVtMVT8QtPHRh8jrdkPSHCa2XV4cdFyQzR1bldZwgJcJmApzyMZFo6IQ6XU\n"
    "5MsI+yMRQ+hDKXJioaldXgjUkK642M4UwtBV8ob2xJNDd2ZhwLnoQdeXeGADbkpy\n"
    "rqXRfboQnoZsG4q5WTP468SQvvG5\n"
    "-----END CERTIFICATE-----\n";

// Client Certificate
const char *Cliet_certificate =
    "-----BEGIN CERTIFICATE-----\n"
    "MIIDWjCCAkKgAwIBAgIVAJ4aWhELV2HZRzvq7ZBDWXcY2AvPMA0GCSqGSIb3DQEB\n"
    "CwUAME0xSzBJBgNVBAsMQkFtYXpvbiBXZWIgU2VydmljZXMgTz1BbWF6b24uY29t\n"
    "IEluYy4gTD1TZWF0dGxlIFNUPVdhc2hpbmd0b24gQz1VUzAeFw0yNDA4MTAxMzM5\n"
    "MzBaFw00OTEyMzEyMzU5NTlaMB4xHDAaBgNVBAMME0FXUyBJb1QgQ2VydGlmaWNh\n"
    "dGUwggEiMA0GCSqGSIb3DQEBAQUAA4IBDwAwggEKAoIBAQDZ8AQIHBpJaqlp8UNK\n"
    "725e4e+aRdbgUYBTzorjqIGMcykyCM/9PsK7nWjDAp2EHEsALu4jUEuQUGE0vCEK\n"
    "HuCVdScXasIeTwUx0pbIVBNL0Ot7YgLlaAT7L9R6GCR1EPl/Jz1Vg9xPFcpH1jyJ\n"
    "J9mGpLerbhx4n6jkKAjOLRXLuMAZTdnOp+YRoFAUmMWM5fu/v90XbANoa0nBHRO3\n"
    "tvGev9q/+gehPvw1w8+HLVZBZhPvDI2cuLyksPtSzhaN7CJgqIopBCpY3XGTQ3VN\n"
    "UCibX0L7mj9MueAOMnGCglMYWEqRQ/1ECcK2nSqM+TQ4WvslDXnldplYFqyeH4UN\n"
    "hhFNAgMBAAGjYDBeMB8GA1UdIwQYMBaAFH6XX7FECTT3SpERWDreltdMDoL5MB0G\n"
    "A1UdDgQWBBTzhymxpokvjyXXqVidQhF8ejTF5TAMBgNVHRMBAf8EAjAAMA4GA1Ud\n"
    "DwEB/wQEAwIHgDANBgkqhkiG9w0BAQsFAAOCAQEAaGMiU0MB6qPIBTZer2nVHp9J\n"
    "HeoT8FteJErOrx0uUmm0ZKszVx9nEfhYvqVD9nkobchmU3dF4Ovr2jnuCHAq6ksn\n"
    "FLpeftXwMl4prEVqYPB5vKogDu4D2nbtJh3qyur1tvHHTFgsWLJpoVKo5cWvzLEI\n"
    "zzf4ciM5w43J4JVgn1nV/28xEKN9XYVi85pfmAXkZEfv7jy6PXyyuoyZORX9fCes\n"
    "xRhuqZcfpbv9Bm5S7Bnytu3XFtCR8lxi+tju0ML0QVBfvCs6pp7QepR9MxLQksfX\n"
    "+MF9+mUg9kj+wRVX760hn7TwgBAmyMRCexHPV4L2xYKoNI2zv0InQKRDUyp4aw==\n"
    "-----END CERTIFICATE-----\n";

// Client KEY
const char *Cliet_key =
    "-----BEGIN RSA PRIVATE KEY-----\n"
    "MIIEowIBAAKCAQEA2fAECBwaSWqpafFDSu9uXuHvmkXW4FGAU86K46iBjHMpMgjP\n"
    "/T7Cu51owwKdhBxLAC7uI1BLkFBhNLwhCh7glXUnF2rCHk8FMdKWyFQTS9Dre2IC\n"
    "5WgE+y/UehgkdRD5fyc9VYPcTxXKR9Y8iSfZhqS3q24ceJ+o5CgIzi0Vy7jAGU3Z\n"
    "zqfmEaBQFJjFjOX7v7/dF2wDaGtJwR0Tt7bxnr/av/oHoT78NcPPhy1WQWYT7wyN\n"
    "nLi8pLD7Us4WjewiYKiKKQQqWN1xk0N1TVAom19C+5o/TLngDjJxgoJTGFhKkUP9\n"
    "RAnCtp0qjPk0OFr7JQ155XaZWBasnh+FDYYRTQIDAQABAoIBAB1yT6kk2uxmjANz\n"
    "hMsgNMJ/NpeariDbAkLQmnWONArdGIjZJfkqvLcK2rfWp5/NDtk0fhqpY7xZD/lH\n"
    "HhO2/lNTY/fHBfmAZcxIjvT8XysUTGz8XjXO6zVhTg09K9fhdkSW8bOXQHIzGITC\n"
    "TqWdi8ekg+iW1SP7Np+1RRNOhi5jRRB8NW+U/UahxWAqM5/zlybec8PtCy7Iagxo\n"
    "TEq5n0zVI4hHXXYc1xuvRTA4xua8CT0cCH/G6t/rch1ZK+eWtrfWSbVbxgyYrdE/\n"
    "5VkWDlYW24zLYh8VgyRzwpyqXYW5D+r1dBMjnLnkPCZzR8/L3knqIzy2XLWX2B5S\n"
    "JixNKwECgYEA/+ZyxdDSNxBotb//GV1n5O/EU9blA+RO8FVF9UpClJ4ZA+xhF4Ce\n"
    "hP0lYF6UlkFTd+tlIEV6ebQEK8qqrji+S/dQ76Mv9IX0QD8a1xdz13OwjFVdmHvQ\n"
    "8LqFtOHNzG+zi+zaODnLtbziHu0wH/g/LAzg4nPH3YP265ucIAXZI8ECgYEA2gXG\n"
    "30PuAE+tnTktPIeiMeq8aCOQl9lZhzhevtIfVm1BNY2jPM/8zG7uJLNWe/WyG56p\n"
    "qh2VjS+wp5zb0YtAj//Kcrk/DPaJQbGpoIYTJcGufhxsgB7kL+JoRovNwlsW5IOW\n"
    "3mCMZroof5AwvlseufejSuTmv+CY5WcrwOTFYI0CgYAhApMvnV5gqAc52siHdxsd\n"
    "1ygWQJROSjc8nWNm3utzzGkhrm5f38GTGiymH80/DLI9t+nVneDMrkITfBNEYiF6\n"
    "Hy8bmotnGZiGaR2HPYk987iEgcaPEvnC8+ynhrFLe+VHWYhU1G/Iw9LPdn1MwnMz\n"
    "tX2U+KaBlrJVdj9PijGWgQKBgQC6K1ioaX8f8OnVaW+BUmhjq4f6fPQJVmWmm7H7\n"
    "y71KtbyLGEkdspSxlL/xwtnEvAa8ov1J8D019FUqqzzhb8FPtSKQWDLIxPRrjmPE\n"
    "WPicswhnU6oqtTYw1WopY1Pt9I5Vzy/S8CqzxZ6zXtLgmTphnl5no5KOoiCtMy4f\n"
    "ZrpMOQKBgGkcTE0FecGPTzUZnWvfSbiwapXcvRvwtyhESboFT1divIq3xKk1EFRg\n"
    "RpvnkgwHbUjvhFIfr1Uh/0XeZhlGj0mFKuOZKu6Rnv5CxHBuCDWXrjtMAZWOGE8c\n"
    "cw3QG/25zmEeOc6Eyf8ErsKApIxAc9RHlneC76E4wYuA1AkOSqdy\n"
    "-----END RSA PRIVATE KEY-----\n";

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
    vTaskDelay(pdMS_TO_TICKS(100));
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
        vTaskDelay(pdMS_TO_TICKS(100));
        readGSMResponse();
        Serial.print("..");
        esp_task_wdt_reset();
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
        vTaskDelay(pdMS_TO_TICKS(100));
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
        vTaskDelay(pdMS_TO_TICKS(70));
    } while ((ret == false) && (retries < 50));
    return ret;
}

static bool checkGPRS()
{
    GSM.print(F("AT+CGATT?\r"));
    vTaskDelay(pdMS_TO_TICKS(500));
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
                vTaskDelay(pdMS_TO_TICKS(100));
            }
            else
            {
                printf("MQTT not found in the response.\n");
                ret = false;
                mqttAlreadyOpen = false;
                vTaskDelay(pdMS_TO_TICKS(100));
            }
        }
        vTaskDelay(pdMS_TO_TICKS(100));
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
                vTaskDelay(pdMS_TO_TICKS(500));
            }
            else
            {
                printf("QMTCONN not found in the response.\n");
                ret = false;
                vTaskDelay(pdMS_TO_TICKS(500));
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
        vTaskDelay(pdMS_TO_TICKS(100));
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
    vTaskDelay(pdMS_TO_TICKS(1000));
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
    GSM.print(F("AT+QSECWRITE=\"RAM:cacert.pem\",1187,200\r\n"));
    if (waitForResponse("CONNECT", 10000))
    {
        GSM.print(CA_certificate);
        readGSMResponse();
    }
    else
    {
        Serial.println("No CONNECT response received.");
    }

    GSM.print(F("AT+QSECWRITE=\"RAM:client.pem\",1224,100\r\n"));
    if (waitForResponse("CONNECT", 10000))
    {
        GSM.print(Cliet_certificate);
        readGSMResponse();
    }
    else
    {
        Serial.println("No CONNECT response received.");
    }

    GSM.print(F("AT+QSECWRITE=\"RAM:user_key.pem\",1679,100\r\n"));
    if (waitForResponse("CONNECT", 10000))
    {
        GSM.print(Cliet_key);
        readGSMResponse();
    }
    else
    {
        Serial.println("No CONNECT response received.");
    }

    GSM.print(F("AT+QSECREAD=\"RAM:cacert.pem\"\r\n"));
    vTaskDelay(pdMS_TO_TICKS(200));
    readGSMResponse();

    GSM.print(F("AT+QSECREAD=\"RAM:client.pem\"\r\n"));
    vTaskDelay(pdMS_TO_TICKS(200));
    readGSMResponse();

    GSM.print(F("AT+QSECREAD=\"RAM:user_key.pem\"\r\n"));
    vTaskDelay(pdMS_TO_TICKS(200));
    readGSMResponse();

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

    vTaskDelay(pdMS_TO_TICKS(1000));
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
    unsigned char waterDistance= 12;
    createJSON(waterDistance);
    do
    {
        esp_task_wdt_reset();
        if (mqtt == true || mqttAlreadyOpen == true)
        {
            // send data first time
            GSM.print(mqttPubStr);
            readGSMResponse();
            GSM.print(F(jsonBuffer));
            GSM.write(0X1A);
            GSM.write(0X1A);
            vTaskDelay(pdMS_TO_TICKS(700));
            // send data second time
            GSM.print(mqttPubStr);
            readGSMResponse();
            GSM.print(F(jsonBuffer));
            GSM.write(0X1A);
            GSM.write(0X1A);
            vTaskDelay(pdMS_TO_TICKS(700));
            // send data third time
            GSM.print(mqttPubStr);
            readGSMResponse();
            GSM.print(F(jsonBuffer));
            GSM.write(0X1A);
            GSM.write(0X1A);
            vTaskDelay(pdMS_TO_TICKS(700));
        }
        readGSMResponse();
        isDataPublished(rx_buf);
        esp_task_wdt_reset();
        retries++;
        vTaskDelay(pdMS_TO_TICKS(100));
    } while ((retries < 3) && (dataPublished == false));

    if (dataPublished == true)
    {
        GSM.print(F("AT+QMTDISC=0\r\n")); // disconnect MQTT before entering sleep, so we open again after wakeup
        readGSMResponse();
        errorretry = 0;
        enterDeepSleep();
    }
    else
    {
        gsmStateRun = errorState;
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
        else
        {
            gsmError = true;
            takeSensorReadings = false;
            vTaskDelay(pdMS_TO_TICKS(100));
            // Serial.println("Error state");
            // If the error state just started, record the start time
            if (errorStateStartTime == 0)
            {
                errorStateStartTime = millis();
            }
            // Check if 5 minutes have passed
            if (millis() - errorStateStartTime >= errorStateDuration)
            {
                vTaskDelay(pdMS_TO_TICKS(100));
                enterDeepSleep();
            }
        }
        errorretry++;
        break;

    default:
        gsmStateRun = errorState; // code is not supposed to come here, if it comes put it in error state
        break;
    }
}