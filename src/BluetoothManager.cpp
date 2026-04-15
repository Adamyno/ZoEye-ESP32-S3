#include "BluetoothManager.h"
#include "ObdManager.h"
#include <NimBLEDevice.h>
#include <WiFi.h>
#include <algorithm>

extern TaskHandle_t btReconnectTaskHandle;

NimBLEClient *pClient = nullptr;
NimBLERemoteCharacteristic *pTxChar = nullptr;
NimBLERemoteCharacteristic *pRxChar = nullptr;
TaskHandle_t btReconnectTaskHandle = nullptr;

class MyClientCallbacks : public NimBLEClientCallbacks {
  void onConnect(NimBLEClient *client) {
    Serial.println("[BLE/NimBLE] Connected to server");
  }
    void onDisconnect(NimBLEClient *client, int reason) {
        Serial.printf("[BLE/NimBLE] Disconnected (reason=%d)\n", reason);
        // Clear characteristic pointers
        pTxChar = nullptr;
        pRxChar = nullptr;
        // Mark connection as lost; do not delete or nullify pClient here (handled elsewhere)
        if (xSemaphoreTake(obdDataMutex, pdMS_TO_TICKS(200))) {
            isBluetoothConnected = false;
            obdCanConnected = false;
            bleDisconnectedFlag = true;
            // Reset state machines
            hvacState = HVAC_IDLE;
            lbcState = LBC_IDLE;
            xSemaphoreGive(obdDataMutex);
        }
        // Note: pClient is left unchanged; cleanup will be performed by disconnect() or connectByMAC
    }
};

void BluetoothManager::runBLEScan() {
  Serial.println("[BLE] Preparing to scan...");

  if (WiFi.getMode() != WIFI_OFF) {
    WiFi.scanDelete();
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    delay(300);
  }

  NimBLEDevice::deinit(true);
  delay(300);
  NimBLEDevice::init("ZoEyee-Scanner");
  NimBLEDevice::setOwnAddrType(BLE_OWN_ADDR_PUBLIC); // Fixated MAC to prevent Konnwei rejection

  NimBLEScan* pBLEScan = NimBLEDevice::getScan();
  pBLEScan->setActiveScan(true);
  pBLEScan->setDuplicateFilter(false);
  pBLEScan->setInterval(100);
  pBLEScan->setWindow(60);
  pBLEScan->clearResults();

  int scanTime = 5;
  Serial.printf("[BLE] Starting scan (scanTime=%d s)...\n", scanTime);
  
  if (pBLEScan->start(scanTime * 1000, false, true)) {
    unsigned long startMillis = millis();
    while (pBLEScan->isScanning()) {
      delay(100);
      if (millis() - startMillis > (scanTime * 1000 + 4000)) break;
    }
    Serial.printf("[BLE] Scan complete after %lu ms.\n", millis() - startMillis);
  } else {
    Serial.println("[BLE] ERROR: pBLEScan->start() failed!");
  }

  NimBLEScanResults foundDevices = pBLEScan->getResults();
  
  if (xSemaphoreTake(obdDataMutex, portMAX_DELAY)) {
    btTotalDevices = foundDevices.getCount();
    if (btTotalDevices > MAX_BLE_DEVICES) btTotalDevices = MAX_BLE_DEVICES;

    for (int i = 0; i < btTotalDevices; i++) {
        const NimBLEAdvertisedDevice *device = foundDevices.getDevice(i);
        if (device) {
            btDevices[i].name = device->haveName() ? String(device->getName().c_str()) : "(unknown)";
            btDevices[i].address = String(device->getAddress().toString().c_str());
            btDevices[i].rssi = device->getRSSI();
        }
    }

    // Sort by RSSI descending
    std::sort(btDevices, btDevices + btTotalDevices, [](const CachedDevice &a, const CachedDevice &b) {
        return a.rssi > b.rssi;
    });

    for (int i = 0; i < btTotalDevices; i++) {
        Serial.printf("[BLE]   #%d: %s [%s] RSSI=%d\n", i,
            btDevices[i].name.c_str(), btDevices[i].address.c_str(), btDevices[i].rssi);
    }
    xSemaphoreGive(obdDataMutex);
  }
  
  pBLEScan->clearResults();
}

bool BluetoothManager::connectByMAC(String mac) {
  if (bleConnecting) return false;
  
  if (xSemaphoreTake(obdDataMutex, portMAX_DELAY)) {
      bleConnecting = true;
      btTargetMAC = mac;
      xSemaphoreGive(obdDataMutex);
  }
  
  Serial.printf("[BLE] Auto-reconnect to [%s]\n", mac.c_str());

  if (pClient != nullptr) {
    if (pClient->isConnected())
      pClient->disconnect();
    NimBLEDevice::deleteClient(pClient);
    pClient = nullptr;
    vTaskDelay(pdMS_TO_TICKS(200));
  }

  pClient = NimBLEDevice::createClient();
  pClient->setClientCallbacks(new MyClientCallbacks());

  // CLONE-COMPATIBLE PARAMETERS
  pClient->setConnectionParams(32, 80, 0, 500);
  pClient->setConnectTimeout(5000);

  NimBLEAddress targetAddr(std::string(mac.c_str()), 0); // Assuming BLE_ADDR_PUBLIC
  
  if (!pClient->connect(targetAddr)) {
    Serial.println("[BLE] Connection failed!");
    NimBLEDevice::deleteClient(pClient);
    pClient = nullptr;
    if (xSemaphoreTake(obdDataMutex, portMAX_DELAY)) {
        bleConnecting = false;
        xSemaphoreGive(obdDataMutex);
    }
    return false;
  }

  Serial.println("[BLE] Physical connection success. Waiting for GATT...");
  vTaskDelay(pdMS_TO_TICKS(1500));

  // Check if still connected after GATT wait (device may have disconnected)
  if (pClient == nullptr || !pClient->isConnected()) {
    Serial.println("[BLE] Connection lost during GATT wait.");
    if (pClient != nullptr) {
      NimBLEDevice::deleteClient(pClient);
      pClient = nullptr;
    }
    if (xSemaphoreTake(obdDataMutex, pdMS_TO_TICKS(200))) {
        bleConnecting = false;
        xSemaphoreGive(obdDataMutex);
    }
    return false;
  }

  auto pServices = pClient->getServices(true);
  if (pServices.empty()) {
    pClient->disconnect();
    if (xSemaphoreTake(obdDataMutex, portMAX_DELAY)) {
        bleConnecting = false;
        xSemaphoreGive(obdDataMutex);
    }
    return false;
  }

  pTxChar = nullptr;
  pRxChar = nullptr;
  NimBLERemoteService *pObdSvc = nullptr;

  for (auto pSvc : pServices) {
    String svcUUID = pSvc->getUUID().toString().c_str();
    svcUUID.toLowerCase();
    if (svcUUID.indexOf("fff0") >= 0 || svcUUID.indexOf("ffe0") >= 0 || svcUUID.indexOf("ae30") >= 0) {
      pObdSvc = pSvc;
      break;
    }
  }

  if (pObdSvc != nullptr) {
    auto pChars = pObdSvc->getCharacteristics(true);
    for (auto pChr : pChars) {
      if ((pChr->canNotify() || pChr->canIndicate()) && pRxChar == nullptr)
        pRxChar = pChr;
      else if ((pChr->canWrite() || pChr->canWriteNoResponse()) && pTxChar == nullptr)
        pTxChar = pChr;
    }
  } else {
    for (auto pSvc : pServices) {
      String svcUUID = pSvc->getUUID().toString().c_str();
      svcUUID.toLowerCase();
      if (svcUUID.indexOf("1800") >= 0 || svcUUID.indexOf("1801") >= 0) continue;
      auto pChars = pSvc->getCharacteristics(true);
      for (auto pChr : pChars) {
        if ((pChr->canWrite() || pChr->canWriteNoResponse()) && pTxChar == nullptr)
          pTxChar = pChr;
        else if (pChr->canNotify() && pRxChar == nullptr)
          pRxChar = pChr;
      }
      if (pTxChar && pRxChar) break;
    }
  }

  if (pTxChar == nullptr || pRxChar == nullptr) {
    disconnect();
    return false;
  }

  if (pRxChar->canNotify()) {
    pRxChar->subscribe(true, ObdManager::onBLENotify);
    vTaskDelay(pdMS_TO_TICKS(200));
    NimBLERemoteDescriptor *p2902 = pRxChar->getDescriptor(NimBLEUUID((uint16_t)0x2902));
    if (p2902 != nullptr) {
      uint8_t notifyOn[] = {0x01, 0x00};
      p2902->writeValue(notifyOn, 2, true);
    }
  }

  pRxChar->subscribe(true, ObdManager::onBLENotify);
  
  if (xSemaphoreTake(obdDataMutex, portMAX_DELAY)) {
      isBluetoothConnected = true;
      bleConnecting = false;
      xSemaphoreGive(obdDataMutex);
  }

  vTaskDelay(pdMS_TO_TICKS(500));
  
  // CAN/OBD init is now handled separately by obdTask
  return true;
}

void BluetoothManager::disconnect() {
  Serial.println("[BLE] Disconnect initiated...");
  if (pClient != nullptr && pClient->isConnected()) {
    if (pRxChar != nullptr) {
      pRxChar->unsubscribe();
      vTaskDelay(pdMS_TO_TICKS(100));
    }
    pClient->disconnect();
    vTaskDelay(pdMS_TO_TICKS(500));
  }
  if (xSemaphoreTake(obdDataMutex, portMAX_DELAY)) {
      isBluetoothConnected = false;
      xSemaphoreGive(obdDataMutex);
  }
  pTxChar = nullptr;
  pRxChar = nullptr;
  Serial.println("[BLE] Successfully disconnected.");
}

static String _reconnectMAC;

static void btReconnectTaskFunc(void *pvParameters) {
  String mac = _reconnectMAC;
  Serial.printf("[BLE] Reconnect task started for [%s]\n", mac.c_str());
  bool result = BluetoothManager::connectByMAC(mac);
  Serial.printf("[BLE] Reconnect task finished, result=%d\n", result);
  btReconnectTaskHandle = nullptr;
  vTaskDelete(NULL);
}

void BluetoothManager::startReconnectTask(String mac) {
  if (btReconnectTaskHandle != nullptr || bleConnecting) {
    return;
  }
  _reconnectMAC = mac;
  xTaskCreatePinnedToCore(
    btReconnectTaskFunc,
    "bt_reconn",
    8192,
    NULL,
    1,
    &btReconnectTaskHandle,
    0 // Pin to Core 0
  );
  Serial.println("[BLE] Reconnect task created on Core 0");
}
