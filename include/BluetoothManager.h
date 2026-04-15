#pragma once
#include <Arduino.h>
#include "obd_globals.h"

class BluetoothManager {
public:
    static void runBLEScan();
    static bool connectByMAC(String mac, uint8_t addrType = 0);
    static void startReconnectTask(String mac, uint8_t addrType = 0);
    static void disconnect();
    static int getConnectedRSSI();
};
