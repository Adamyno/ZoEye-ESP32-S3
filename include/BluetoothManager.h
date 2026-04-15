#pragma once
#include <Arduino.h>
#include "obd_globals.h"

class BluetoothManager {
public:
    static void runBLEScan();
    static bool connectByMAC(String mac);
    static void startReconnectTask(String mac); // Non-blocking reconnect via FreeRTOS task
    static void disconnect();
    static int getConnectedRSSI();
};
