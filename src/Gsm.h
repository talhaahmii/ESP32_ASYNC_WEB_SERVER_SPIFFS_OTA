// **************************************************************************************
//    This header file handles gsm.c file data
//    Author: Talha Ahmed
//    Date: 7 August 2024
// **************************************************************************************
#ifndef GSM_STATES_H
#define GSM_STATES_H

#include <Arduino.h>
#include <SoftwareSerial.h>

// **************************************************************************************
//
//              Extern variabels which we use in other C files
//
//
// **************************************************************************************
extern SoftwareSerial GSM;
extern bool gsmError;
extern bool takeSensorReadings;
const byte MAX_RX_CAHRS = 254;
// **************************************************************************************
//
//                      Global functions definition
//
//
// **************************************************************************************
void gsmStateMachine();

#endif // GSM_STATES_H