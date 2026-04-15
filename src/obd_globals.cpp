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
float obdHumidity = -1;

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
