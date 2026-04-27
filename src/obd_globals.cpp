#include "obd_globals.h"

SemaphoreHandle_t obdDataMutex = NULL;

bool isBluetoothConnected = false;
bool bleConnecting = false;
bool bleDisconnectedFlag = false;
bool obdCanConnected = false;
int btTotalDevices = 0;
String btTargetMAC = "";
String btTargetName = "";
uint8_t btTargetType = 0;

float obdSOC = -1;
int obdSOH = -1;
float obdHVBatTemp = -99;
float obdCabinTemp = -99;
float obdExtTemp = -99;
float obdACRpm = -1;
float obdACPressure = -1;
int obdClimateLoopMode = -99;
float obdCellVoltageMax = -1;
float obdCellVoltageMin = -1;
float obdMaxChargePower = -1;
float obdAvailEnergy = -1;
float obdHumidity = -1;
float obdHVBatVoltage = -1;
float obdFanSpeed = -1;
float obdHVBatCurrent = -999;
float obdDCPower = -999;
float obdInsulationRes = -1;
float obdACPhase = -1;
float obd12VBatVoltage = -1;
float obd12VCurrent = -1;
float obdDCDCLoad = -1;

uint32_t activePollFlags = POLL_SOH | POLL_SOC | POLL_BAT_TEMP | POLL_HV_VOLT | POLL_AVAIL_ENERGY | POLL_HVAC | POLL_LBC | POLL_MAX_CHARGE | POLL_AC_COMP;
int currentDashPage = 0;

unsigned long lastOBDPollTime = 0;
unsigned long lastOBDRxTime = 0;
unsigned long lastOBDSentTime = 0;
bool obdZoeMode = false;
int obdCurrentECU = 0;
int obdPollIndex = 0;
unsigned long pollCycleStartTime = 0;

HvacPollState hvacState = HVAC_IDLE;
LbcPollState lbcState = LBC_IDLE;
EvcPollState evcState = EVC_IDLE;
unsigned long hvacCmdSentTime = 0;
unsigned long lbcCmdSentTime = 0;
unsigned long evcCmdSentTime = 0;

const unsigned long HVAC_AT_TIMEOUT = 1000;
const unsigned long HVAC_ISOTP_TIMEOUT = 5000;

CachedDevice btDevices[MAX_BLE_DEVICES];
int btSelectedDeviceIndex = -1;

Preferences preferences;
