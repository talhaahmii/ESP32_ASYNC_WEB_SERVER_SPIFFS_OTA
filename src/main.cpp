#include <Arduino.h>
#include <SPIFFS.h>
#include "async_server.h"
#include "gsm.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
//#include <esp_task_wdt.h>

// **************************************************************************************
//
//                      Definitions of Macros
//
//
// **************************************************************************************
// 3 seconds WDT
#define WDT_TIMEOUT 4
#define ledPin 2
#define GSM_SLEEP_PIN 32
#define uS_TO_S_FACTOR 1000000ULL /* Conversion factor for micro seconds to seconds */     
uint64_t TIME_TO_SLEEP = 14400ULL * uS_TO_S_FACTOR; /* Time ESP32 will go to sleep (in seconds) */

// **************************************************************************************
//
//      Local Functions declaration
//
//
// **************************************************************************************

static void Led(uint8_t ledState, uint32_t ledDelay);
void ledTask(void *pvParameters);
void setup();
void loop();

// **************************************************************************************
//
//                      Definition of Local Functions
//
//
// **************************************************************************************
static void Led(uint8_t ledState, uint32_t ledDelay)
{
    digitalWrite(ledPin, ledState);
    vTaskDelay(ledDelay / portTICK_PERIOD_MS); // Use FreeRTOS delay instead of delay()
}

// **************************************************************************************
//
//                      Definition of Main Functions
//
//
// **************************************************************************************

// LED Task function
void ledTask(void *pvParameters)
{
    while (1)
    {
        if (gsmError == false)
        {
            Led(HIGH, 200); // LED ON for 1 second
            Led(LOW, 200);  // LED OFF for 1 second
        }
        else
        {
            // Error indication
            Led(HIGH, 2000); // LED ON for 3 seconds
            Led(LOW, 700);   // LED OFF for 1 second
        }
    }
}

void setup() {
	Serial.begin(115200);
    GSM.begin(9600);
    // Serial.println("Configuring WDT...");
    // esp_task_wdt_init(WDT_TIMEOUT, true); // enable panic so ESP32 restarts
    // esp_task_wdt_add(NULL);               // add current thread to WDT watch
    while (!GSM)
    {
        ;
    }
    pinMode(ledPin, OUTPUT);
    pinMode(GSM_SLEEP_PIN, OUTPUT);
    Led(HIGH, 0);
    digitalWrite(32, HIGH);
    Serial.println("LETS Start");

    // Create the LED task
    xTaskCreate(
        ledTask,    // Task function
        "LED Task", // Name of the task (for debugging)
        1000,       // Stack size (in words, not bytes)
        NULL,       // Task input parameter
        1,          // Priority of the task
        NULL);      // Task handle


	Serial.printf("\r\n\r\nBinary compiled on %s at %s\r\n", __DATE__, __TIME__);
	Serial.println("Mounting SPIFFS ...");
	if (!SPIFFS.begin(true)) {
		// SPIFFS will be configured on reboot
		Serial.println("ERROR: Cannot mount SPIFFS, Rebooting");
		delay(1000);
		ESP.restart();
		}
 
	server_init();
	// your application initialization code ...
	}


void loop() {
	if (IsRebootRequired) {
		Serial.println("Rebooting ESP32: "); 
		delay(1000); // give time for reboot page to load
		ESP.restart();
		}
	// your application loop ...

    vTaskDelay(pdMS_TO_TICKS(10));
    gsmStateMachine();

	}