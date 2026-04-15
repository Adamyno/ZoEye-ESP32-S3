#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#define ZOEYEE_VERSION "v1.1.0-S3"

// HVAC Polling State Machine (non-blocking)
enum HvacPollState {
  HVAC_IDLE = 0,
  HVAC_SWITCH_SH,
  HVAC_SWITCH_CRA,
  HVAC_SWITCH_FCSH,
  HVAC_SET_ATS0,
  HVAC_SET_ATCAF0,
  HVAC_SET_ATAL,
  HVAC_SET_FCSD,
  HVAC_SET_FCSM,
  HVAC_SET_STFF,
  HVAC_SESSION,
  HVAC_QUERY_2121,
  HVAC_QUERY_2143,
  HVAC_QUERY_2144,
  HVAC_QUERY_2167,
  HVAC_RESTORE_ATS1,
  HVAC_RESTORE_ATCAF1,
  HVAC_RESTORE_ATST32,
  HVAC_BACK_SH,
  HVAC_BACK_CRA,
  HVAC_BACK_FCSH,
  HVAC_DONE
};

// LBC Polling State Machine (non-blocking)
enum LbcPollState {
  LBC_IDLE = 0,
  LBC_SWITCH_SH,
  LBC_SWITCH_CRA,
  LBC_SWITCH_FCSH,
  LBC_SESSION,
  LBC_SET_ATS0,
  LBC_SET_ATCAF0,
  LBC_SET_ATAL,
  LBC_QUERY_2101,
  LBC_QUERY_2103, // 2103 for Cell Voltages (Max & Min) - ZOE Q210 specific
  LBC_RESTORE_ATS1,
  LBC_RESTORE_ATCAF1,
  LBC_RESTORE_ATST32,
  LBC_DONE
};

// ─── BLE MACROSS ───────────────────────────────────────
#define MAX_BLE_DEVICES 10

struct CachedDevice {
  String name;
  String address;
  int rssi;
  void* bleAddress; // Using void* to avoid NimBLE dependency leaking here if possible
};

// ─── THREAD-SAFE OBD GLOBALS ──────────────────────────────
extern SemaphoreHandle_t obdDataMutex;

// Status & State
extern bool isBluetoothConnected;
extern bool bleConnecting;
extern bool bleDisconnectedFlag;
extern bool obdCanConnected;
extern int btTotalDevices;
extern String btTargetMAC;
extern String btTargetName;
extern uint8_t btTargetType;

// Vehicle Data
extern float obdSOC;
extern int obdSOH;
extern float obdHVBatTemp;
extern float obdCabinTemp;
extern float obdExtTemp;
extern float obdACRpm;
extern float obdACPressure;
extern int obdClimateLoopMode;
extern float obdCellVoltageMax;
extern float obdCellVoltageMin;
extern float obdMaxChargePower;
extern float obdHumidity;

// Polling configuration & tracking
extern unsigned long lastOBDPollTime;
extern unsigned long lastOBDRxTime;
extern unsigned long lastOBDSentTime;
extern bool obdZoeMode;
extern int obdCurrentECU;
extern int obdPollIndex;
extern unsigned long pollCycleStartTime;

// State machines
extern HvacPollState hvacState;
extern LbcPollState lbcState;
extern unsigned long hvacCmdSentTime;
extern unsigned long lbcCmdSentTime;

extern const unsigned long HVAC_AT_TIMEOUT;
extern const unsigned long HVAC_ISOTP_TIMEOUT;

// BLE device cache for scan results
extern CachedDevice btDevices[MAX_BLE_DEVICES];
extern int btSelectedDeviceIndex;

// NVS Preferences
#include <Preferences.h>
extern Preferences preferences;
