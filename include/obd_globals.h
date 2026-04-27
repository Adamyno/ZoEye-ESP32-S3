#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#define ZOEYEE_VERSION "v1.2.2-S3"

// ─── Poll Flags (bitmask) ────────────────────────────────
#define POLL_SOH           (1 << 0)
#define POLL_SOC           (1 << 1)
#define POLL_BAT_TEMP      (1 << 2)
#define POLL_HV_VOLT       (1 << 3)
#define POLL_AVAIL_ENERGY  (1 << 4)
#define POLL_HV_CURRENT    (1 << 5)
#define POLL_FAN_SPEED     (1 << 6)
#define POLL_AC_PHASE      (1 << 7)
#define POLL_INSULATION    (1 << 8)
#define POLL_DCDC_LOAD     (1 << 9)
#define POLL_12V_CURRENT   (1 << 10)
#define POLL_AT_RV         (1 << 11)
#define POLL_HVAC          (1 << 12)
#define POLL_LBC           (1 << 13)
#define POLL_MAX_CHARGE    (1 << 14)
#define POLL_AC_COMP       (1 << 15)

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

// EVC Polling State Machine (non-blocking) - dynamic per-page polling
enum EvcPollState {
  EVC_IDLE = 0,
  EVC_SWITCH_SH,
  EVC_SWITCH_CRA,
  EVC_SWITCH_FCSH,
  EVC_SESSION,
  EVC_QUERY_SOH,           // 223206 - Battery State of Health
  EVC_QUERY_SOC,           // 222002
  EVC_QUERY_BAT_TEMP,      // 222001
  EVC_QUERY_HV_VOLT,       // 223203
  EVC_QUERY_AVAIL_ENERGY,  // 22320C
  EVC_QUERY_HV_CURRENT,    // 223204 - HV Battery Current
  EVC_QUERY_FAN_SPEED,     // 223471 - Engine Fan Speed
  EVC_QUERY_AC_PHASE,      // 2233BA - AC Charge Phase
  EVC_QUERY_INSULATION,    // 2233EE - Insulation Resistance
  EVC_QUERY_DCDC_LOAD,     // 223028 - DCDC Converter Load
  EVC_QUERY_12V_CURRENT,   // 223025 - 12V DCDC Current
  EVC_QUERY_AT_RV,         // AT RV  - 12V Battery Voltage
  EVC_DONE
};

// ─── BLE MACROSS ───────────────────────────────────────
#define MAX_BLE_DEVICES 10

struct CachedDevice {
  String name;
  String address;
  int rssi;
  uint8_t addrType;  // 0=public, 1=random
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
extern float obdAvailEnergy;
extern float obdHumidity;
extern float obdHVBatVoltage;
extern float obdFanSpeed;
extern float obdHVBatCurrent;
extern float obdDCPower;
extern float obdInsulationRes;
extern float obdACPhase;
extern float obd12VBatVoltage;
extern float obd12VCurrent;
extern float obdDCDCLoad;

// Polling system
extern uint32_t activePollFlags;
extern int currentDashPage;

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
extern EvcPollState evcState;
extern unsigned long hvacCmdSentTime;
extern unsigned long lbcCmdSentTime;
extern unsigned long evcCmdSentTime;

extern const unsigned long HVAC_AT_TIMEOUT;
extern const unsigned long HVAC_ISOTP_TIMEOUT;

// BLE device cache for scan results
extern CachedDevice btDevices[MAX_BLE_DEVICES];
extern int btSelectedDeviceIndex;

// NVS Preferences
#include <Preferences.h>
extern Preferences preferences;
