#pragma once
#include <Arduino.h>
#include <NimBLEDevice.h>
#include "obd_globals.h"

class ObdManager {
public:
    static void onBLENotify(NimBLERemoteCharacteristic *pChar, uint8_t *pData, size_t length, bool isNotify);
    static void sendCommand(const char *cmd);
    static bool initOBD();
    static void processHvacStep();
    static void processLbcStep();
    
    // Raw parsing helpers
    static int parseUDSHex(const String &resp, const char *expectedPrefix, int byteCount);
    static int parseUDSBits(const String &resp, const char *expectedPrefix, int startBit, int endBit);
    static String parseIsoTpResponse(const String &resp);
};
