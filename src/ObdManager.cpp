#include "ObdManager.h"
#include "BluetoothManager.h"

// Local buffers for AT response parsing
static String lastOBDValue = "";
static char obdBuffer[256];
static int obdBufIndex = 0;

extern NimBLERemoteCharacteristic *pTxChar;

int ObdManager::parseUDSHex(const String &resp, const char *expectedPrefix, int byteCount) {
  String r = resp;
  r.trim();
  r.replace(" ", "");
  String prefix = String(expectedPrefix);
  prefix.replace(" ", "");
  int idx = r.indexOf(prefix);
  if (idx < 0) return -1;

  int startPos = idx + prefix.length();
  if (r.length() < (unsigned int)(startPos + byteCount * 2)) return -1;

  char buf[33];
  int len = byteCount * 2;
  if (len > 32) len = 32;
  memcpy(buf, r.c_str() + startPos, len);
  buf[len] = '\0';

  return (int)strtol(buf, NULL, 16);
}

int ObdManager::parseUDSBits(const String &resp, const char *expectedPrefix, int startBit, int endBit) {
  String r = resp;
  r.trim();
  r.replace(" ", "");
  String prefix = String(expectedPrefix);
  prefix.replace(" ", "");
  int idx = r.indexOf(prefix);
  if (idx < 0) return -1;

  String fullHex = r.substring(idx);
  int startByte = startBit / 8;
  int endByte = endBit / 8;

  if (fullHex.length() < (unsigned int)(endByte + 1) * 2) return -1;

  uint64_t val = 0;
  char byteBuf[3] = {0};
  const char *s = fullHex.c_str();
  for (int i = startByte; i <= endByte; i++) {
    byteBuf[0] = s[i * 2];
    byteBuf[1] = s[i * 2 + 1];
    val = (val << 8) | strtol(byteBuf, NULL, 16);
  }

  int bitsToExtract = endBit - startBit + 1;
  int shiftRight = (8 - ((endBit + 1) % 8)) % 8;
  val = val >> shiftRight;
  uint64_t mask = (1ULL << bitsToExtract) - 1;
  return (int)(val & mask);
}

String ObdManager::parseIsoTpResponse(const String &resp) {
  String r = resp;
  r.trim();
  r.replace(" ", "");
  r.toUpperCase();
  if (r.length() < 2) return "";

  char frameType = r.charAt(0);
  if (frameType == '0') {
    int len = 0;
    if (r.charAt(1) >= '0' && r.charAt(1) <= '9') len = r.charAt(1) - '0';
    else if (r.charAt(1) >= 'A' && r.charAt(1) <= 'F') len = r.charAt(1) - 'A' + 10;
    String hexData = r.substring(2);
    if ((int)hexData.length() > len * 2) hexData = hexData.substring(0, len * 2);
    return hexData;
  }
  if (frameType == '1') {
    if (r.length() < 16) return "";
    String lenHex = r.substring(1, 4);
    int totalLen = (int)strtol(lenHex.c_str(), NULL, 16);
    String hexData = r.substring(4, 16);

    int pos = 16;
    while (pos + 16 <= (int)r.length()) {
      String frame = r.substring(pos, pos + 16);
      if (frame.charAt(0) == '2') {
        hexData += frame.substring(2);
      }
      pos += 16;
    }
    if ((int)hexData.length() > totalLen * 2) hexData = hexData.substring(0, totalLen * 2);
    return hexData;
  }
  return "";
}

void ObdManager::onBLENotify(NimBLERemoteCharacteristic *pChar, uint8_t *pData, size_t length, bool isNotify) {
  for (size_t i = 0; i < length; i++) {
    char c = (char)pData[i];
    if (c == '>') {
      if (obdBufIndex < (int)sizeof(obdBuffer)) obdBuffer[obdBufIndex] = '\0';
      else obdBuffer[sizeof(obdBuffer) - 1] = '\0';

      String fullResponse = "";
      char *ptr = obdBuffer;
      while (*ptr) {
        char *lineStart = ptr;
        while (*ptr && *ptr != '\r') ptr++;
        if (*ptr == '\r') { *ptr = '\0'; ptr++; }

        char *s = lineStart;
        while (*s == ' ') s++;
        int l = strlen(s);
        while (l > 0 && (s[l - 1] == ' ' || s[l - 1] == '\n')) s[--l] = '\0';

        if (strlen(s) == 0) continue;
        String line = String(s);
        line.trim();
        if (line.length() == 0) continue;

        String lineUpper = line;
        lineUpper.toUpperCase();

        if (lineUpper.startsWith("AT") && lineUpper.indexOf(' ') < 0 && lineUpper.length() <= 6) continue;
        if (lineUpper.length() <= 3 && lineUpper.indexOf(' ') < 0 && lineUpper != "OK") continue;

        if (lineUpper.length() >= 2 && lineUpper.charAt(1) == ':') {
          lineUpper = lineUpper.substring(2);
          lineUpper.trim();
        } else if (lineUpper.length() >= 3 && lineUpper.charAt(2) == ':') {
          lineUpper = lineUpper.substring(3);
          lineUpper.trim();
        }

        if (fullResponse.length() > 0) fullResponse += " ";
        fullResponse += lineUpper;
      }

      obdBufIndex = 0;
      obdBuffer[0] = '\0';
      if (fullResponse.length() == 0) continue;

      if (xSemaphoreTake(obdDataMutex, portMAX_DELAY)) {
          lastOBDValue = fullResponse;
          xSemaphoreGive(obdDataMutex);
      }
      Serial.printf("[OBD] Response: '%s'\n", fullResponse.c_str());
    } else if (c != '\n') {
      if (obdBufIndex < (int)sizeof(obdBuffer) - 1) {
        obdBuffer[obdBufIndex++] = c;
      }
    }
  }
}

void ObdManager::sendCommand(const char *cmd) {
  if (cmd == nullptr) return;
  if (bleConnecting) return;  // Don't send during reconnect
  if (pTxChar == nullptr) return;
  
  // Take a local copy of the pointer to avoid race
  NimBLERemoteCharacteristic *txChar = pTxChar;
  if (txChar == nullptr) return;
  
  char fullCmd[64];
  if (strlen(cmd) + 2 > sizeof(fullCmd)) {
    Serial.println("[OBD] Error: Command too long, dropping.");
    return;
  }
  snprintf(fullCmd, sizeof(fullCmd), "%s\r", cmd);
  try {
    txChar->writeValue((uint8_t *)fullCmd, strlen(fullCmd));
    Serial.printf("[OBD] Sent: %s\n", cmd);
  } catch (...) {
    Serial.println("[OBD] Write failed (disconnected?)");
  }
}

static bool sendATAndWait(const char *cmd, unsigned long timeoutMs = 800) {
  if (xSemaphoreTake(obdDataMutex, portMAX_DELAY)) {
      lastOBDValue = "";
      xSemaphoreGive(obdDataMutex);
  }
  ObdManager::sendCommand(cmd);
  unsigned long t0 = millis();
  while (millis() - t0 < timeoutMs) {
    vTaskDelay(pdMS_TO_TICKS(30));
    String r = "";
    if (xSemaphoreTake(obdDataMutex, portMAX_DELAY)) {
        if (lastOBDValue.length() > 0) {
            r = lastOBDValue;
            lastOBDValue = "";
        }
        xSemaphoreGive(obdDataMutex);
    }
    if (r.length() > 0) {
      r.toUpperCase();
      if (r.indexOf("OK") >= 0) return true;
    }
  }
  Serial.printf("[OBD] AT cmd '%s' no OK within %lu ms\n", cmd, timeoutMs);
  return false;
}

static String sendAndWaitResponse(const char *cmd, unsigned long timeoutMs = 3000) {
  if (xSemaphoreTake(obdDataMutex, portMAX_DELAY)) {
      lastOBDValue = "";
      xSemaphoreGive(obdDataMutex);
  }
  ObdManager::sendCommand(cmd);
  unsigned long t0 = millis();
  while (millis() - t0 < timeoutMs) {
    vTaskDelay(pdMS_TO_TICKS(30));
    String r = "";
    if (xSemaphoreTake(obdDataMutex, portMAX_DELAY)) {
        if (lastOBDValue.length() > 0) {
            r = lastOBDValue;
            lastOBDValue = "";
        }
        xSemaphoreGive(obdDataMutex);
    }
    if (r.length() > 0) return r;
  }
  Serial.printf("[OBD] Data cmd '%s' no response within %lu ms\n", cmd, timeoutMs);
  return "";
}

static bool switchToECU(const char *txId, const char *rxId) {
  char cmd[16];
  snprintf(cmd, sizeof(cmd), "ATSH%s", txId);   if (!sendATAndWait(cmd)) return false;
  snprintf(cmd, sizeof(cmd), "ATCRA%s", rxId);  if (!sendATAndWait(cmd)) return false;
  snprintf(cmd, sizeof(cmd), "ATFCSH%s", txId); if (!sendATAndWait(cmd)) return false;
  return true;
}

bool ObdManager::initOBD() {
  bool isELM = false;
  const char *atzCommands[] = {"ATZ", "ATZ\n"};

  for (int cmdIdx = 0; cmdIdx < 2 && !isELM; cmdIdx++) {
    sendCommand(atzCommands[cmdIdx]);
    unsigned long verifyStart = millis();
    while (millis() - verifyStart < 2500) {
      vTaskDelay(pdMS_TO_TICKS(50));
      String r = "";
      if (xSemaphoreTake(obdDataMutex, portMAX_DELAY)) {
          if (lastOBDValue.length() > 0) {
              r = lastOBDValue;
              lastOBDValue = "";
          }
          xSemaphoreGive(obdDataMutex);
      }
      if (r.length() > 0) {
        Serial.printf("[OBD] ATZ Válasz: '%s'\n", r.c_str());
        r.toUpperCase();
        if (r.indexOf("ELM") >= 0 || r.indexOf("KONNWEI") >= 0 || r.indexOf("OBD") >= 0 || r.indexOf("KW") >= 0 || r.indexOf("V1.5") >= 0 || r.indexOf("OK") >= 0) {
          isELM = true;
          break;
        }
      }
    }
  }

  if (!isELM) {
    Serial.println("[BLE] Nem válaszolt mint ELM327.");
    return false;
  }

  sendATAndWait("ATE0");
  Serial.println("[OBD] ZOE CAN protokoll beállítás...");
  sendATAndWait("ATSP6");
  sendATAndWait("ATFCSD300000");
  sendATAndWait("ATFCSM1");
  switchToECU("7E4", "7EC");
  sendAndWaitResponse("10C0", 500);

  if (xSemaphoreTake(obdDataMutex, portMAX_DELAY)) {
      obdZoeMode = true;
      obdPollIndex = 0;
      obdSOC = -1;
      obdSOH = -1;
      obdHVBatTemp = -99;
      lastOBDSentTime = 0;
      lastOBDPollTime = 0;
      obdCurrentECU = 0; // 0 = EVC
      xSemaphoreGive(obdDataMutex);
  }
  Serial.println("[BLE] OBD adapter felállt, ZOE mód aktív!");
  return true;
}

void ObdManager::processHvacStep() {
  if (hvacState == HVAC_IDLE) {
    hvacState = HVAC_SWITCH_SH;
    if (xSemaphoreTake(obdDataMutex, portMAX_DELAY)) { lastOBDValue = ""; xSemaphoreGive(obdDataMutex); }
  }

  if (hvacState == HVAC_DONE) {
    obdCurrentECU = 0;
    hvacState = HVAC_IDLE;
    lastOBDRxTime = millis();
    return;
  }

  unsigned long now = millis();
  unsigned long timeout = (hvacState >= HVAC_QUERY_2121 && hvacState <= HVAC_QUERY_2167) ? HVAC_ISOTP_TIMEOUT : HVAC_AT_TIMEOUT;
  if (hvacState == HVAC_SESSION) timeout = 2000;

  if (hvacCmdSentTime > 0) {
    String r = "";
    if (xSemaphoreTake(obdDataMutex, portMAX_DELAY)) {
        if (lastOBDValue.length() > 0) { r = lastOBDValue; lastOBDValue = ""; }
        xSemaphoreGive(obdDataMutex);
    }

    if (r.length() > 0) {
      hvacCmdSentTime = 0;
      switch (hvacState) {
        case HVAC_QUERY_2121: {
          String isotp = parseIsoTpResponse(r);
          if (isotp.indexOf("6121") >= 0) {
            int rawCabin = parseUDSBits(isotp, "6121", 26, 35);
            int rawHum = parseUDSBits(isotp, "6121", 36, 43);
            if (xSemaphoreTake(obdDataMutex, portMAX_DELAY)) {
                if (rawCabin >= 0) obdCabinTemp = (rawCabin * 0.1f) - 40.0f;
                if (rawHum >= 0) obdHumidity = rawHum * 0.5f;
                xSemaphoreGive(obdDataMutex);
            }
          }
          break;
        }
        case HVAC_QUERY_2143: {
          String isotp = parseIsoTpResponse(r);
          if (isotp.indexOf("6143") >= 0) {
            int rawExt = parseUDSBits(isotp, "6143", 110, 117);
            int rawPress = parseUDSBits(isotp, "6143", 134, 142);
            if (xSemaphoreTake(obdDataMutex, portMAX_DELAY)) {
                if (rawExt >= 0) obdExtTemp = rawExt - 40.0f;
                if (rawPress >= 0) obdACPressure = rawPress * 0.1f;
                xSemaphoreGive(obdDataMutex);
            }
          }
          break;
        }
        case HVAC_QUERY_2144: {
          String isotp = parseIsoTpResponse(r);
          if (isotp.indexOf("6144") >= 0) {
            int raw = parseUDSBits(isotp, "6144", 107, 116);
            if (raw >= 0 && xSemaphoreTake(obdDataMutex, portMAX_DELAY)) {
                obdACRpm = raw * 10;
                xSemaphoreGive(obdDataMutex);
            }
          }
          break;
        }
        case HVAC_QUERY_2167: {
          String isotp = parseIsoTpResponse(r);
          if (isotp.indexOf("6167") >= 0) {
            int raw = parseUDSBits(isotp, "6167", 21, 23);
            if (raw >= 0 && xSemaphoreTake(obdDataMutex, portMAX_DELAY)) {
                obdClimateLoopMode = raw;
                xSemaphoreGive(obdDataMutex);
            }
          }
          break;
        }
        default: break;
      }
      hvacState = (HvacPollState)(hvacState + 1);
      return;
    } else if (now - hvacCmdSentTime >= timeout) {
      Serial.printf("[OBD] HVAC state %d timeout, advancing\n", hvacState);
      hvacCmdSentTime = 0;
      hvacState = (HvacPollState)(hvacState + 1);
      return;
    }
    return;
  }

  if (xSemaphoreTake(obdDataMutex, portMAX_DELAY)) { lastOBDValue = ""; xSemaphoreGive(obdDataMutex); }
  hvacCmdSentTime = now;

  switch (hvacState) {
    case HVAC_SWITCH_SH:   sendCommand("ATSH744"); obdCurrentECU = 1; break;
    case HVAC_SWITCH_CRA:  sendCommand("ATCRA764");     break;
    case HVAC_SWITCH_FCSH: sendCommand("ATFCSH744");    break;
    case HVAC_SET_ATS0:    sendCommand("ATS0");         break;
    case HVAC_SET_ATCAF0:  sendCommand("ATCAF0");       break;
    case HVAC_SET_ATAL:    sendCommand("ATAL");          break;
    case HVAC_SET_FCSD:    sendCommand("ATFCSD300000"); break;
    case HVAC_SET_FCSM:    sendCommand("ATFCSM1");      break;
    case HVAC_SET_STFF:    sendCommand("ATSTFF");        break;
    case HVAC_SESSION:     sendCommand("0210C0");       break;
    case HVAC_QUERY_2121:  sendCommand("022121");       break;
    case HVAC_QUERY_2143:  sendCommand("022143");       break;
    case HVAC_QUERY_2144:  sendCommand("022144");       break;
    case HVAC_QUERY_2167:  sendCommand("022167");       break;
    case HVAC_RESTORE_ATS1:   sendCommand("ATS1");      break;
    case HVAC_RESTORE_ATCAF1: sendCommand("ATCAF1");    break;
    case HVAC_RESTORE_ATST32: sendCommand("ATST32");    break;
    case HVAC_BACK_SH:   sendCommand("ATSH7E4");      break;
    case HVAC_BACK_CRA:  sendCommand("ATCRA7EC");     break;
    case HVAC_BACK_FCSH: sendCommand("ATFCSH7E4");    break;
    default:
      hvacState = HVAC_DONE;
      hvacCmdSentTime = 0;
      break;
  }
}

void ObdManager::processLbcStep() {
  if (lbcState == LBC_IDLE) {
    lbcState = LBC_SWITCH_SH;
    if (xSemaphoreTake(obdDataMutex, portMAX_DELAY)) { lastOBDValue = ""; xSemaphoreGive(obdDataMutex); }
  }

  if (lbcState == LBC_DONE) {
    obdCurrentECU = -1;
    lbcState = LBC_IDLE;
    lastOBDRxTime = millis();
    return;
  }

  unsigned long now = millis();
  unsigned long timeout = (lbcState == LBC_QUERY_2103 || lbcState == LBC_QUERY_2101) ? HVAC_ISOTP_TIMEOUT : HVAC_AT_TIMEOUT;
  if (lbcState == LBC_SESSION) timeout = 2000;

  if (lbcCmdSentTime > 0) {
    String r = "";
    if (xSemaphoreTake(obdDataMutex, portMAX_DELAY)) {
        if (lastOBDValue.length() > 0) { r = lastOBDValue; lastOBDValue = ""; }
        xSemaphoreGive(obdDataMutex);
    }

    if (r.length() > 0) {
      lbcCmdSentTime = 0;
      if (lbcState == LBC_QUERY_2101) {
        String isotp = parseIsoTpResponse(r);
        if (isotp.indexOf("6101") >= 0) {
          int rawMaxChg = parseUDSBits(isotp, "6101", 336, 351);
          if (rawMaxChg >= 0 && xSemaphoreTake(obdDataMutex, portMAX_DELAY)) {
              obdMaxChargePower = rawMaxChg * 0.01f;
              lastOBDRxTime = millis();
              lastOBDPollTime = millis();
              xSemaphoreGive(obdDataMutex);
          }
        }
      } else if (lbcState == LBC_QUERY_2103) {
        String isotp = parseIsoTpResponse(r);
        if (isotp.indexOf("6103") >= 0) {
          int rawMax = parseUDSBits(isotp, "6103", 96, 111);
          int rawMin = parseUDSBits(isotp, "6103", 112, 127);
          if (xSemaphoreTake(obdDataMutex, portMAX_DELAY)) {
              if (rawMax >= 0) obdCellVoltageMax = rawMax * 0.01f;
              if (rawMin >= 0) obdCellVoltageMin = rawMin * 0.01f;
              lastOBDRxTime = millis();
              lastOBDPollTime = millis();
              xSemaphoreGive(obdDataMutex);
          }
        }
      }
      lbcState = (LbcPollState)(lbcState + 1);
      return;
    } else if (now - lbcCmdSentTime >= timeout) {
      Serial.printf("[OBD] LBC state %d timeout, advancing\n", lbcState);
      lbcCmdSentTime = 0;
      lbcState = (LbcPollState)(lbcState + 1);
      return;
    }
    return;
  }

  if (xSemaphoreTake(obdDataMutex, portMAX_DELAY)) { lastOBDValue = ""; xSemaphoreGive(obdDataMutex); }
  lbcCmdSentTime = now;

  switch (lbcState) {
    case LBC_SWITCH_SH:      sendCommand("ATSH79B");     obdCurrentECU = 2; break;
    case LBC_SWITCH_CRA:     sendCommand("ATCRA7BB");    break;
    case LBC_SWITCH_FCSH:    sendCommand("ATFCSH79B");   break;
    case LBC_SESSION:        sendCommand("0210C0");      break;
    case LBC_SET_ATS0:       sendCommand("ATS0");        break;
    case LBC_SET_ATCAF0:     sendCommand("ATCAF0");      break;
    case LBC_SET_ATAL:       sendCommand("ATAL");         break;
    case LBC_QUERY_2101:     sendCommand("022101");      break;
    case LBC_QUERY_2103:     sendCommand("022103");      break;
    case LBC_RESTORE_ATS1:   sendCommand("ATS1");        break;
    case LBC_RESTORE_ATCAF1: sendCommand("ATCAF1");      break;
    case LBC_RESTORE_ATST32: sendCommand("ATST32");      break;
    default:
      lbcState = LBC_DONE;
      lbcCmdSentTime = 0;
      break;
  }
}

// ═══════════════════════════════════════════════════════════
//  EVC (Electric Vehicle Controller) - SOC, HV Voltage, Battery Temp
//  Non-blocking state machine, same pattern as HVAC/LBC
// ═══════════════════════════════════════════════════════════

void ObdManager::processEvcStep() {
  if (evcState == EVC_IDLE) {
    evcState = EVC_SWITCH_SH;
    if (xSemaphoreTake(obdDataMutex, portMAX_DELAY)) { lastOBDValue = ""; xSemaphoreGive(obdDataMutex); }
  }

  if (evcState == EVC_DONE) {
    obdCurrentECU = 0;
    evcState = EVC_IDLE;
    lastOBDRxTime = millis();
    return;
  }

  unsigned long now = millis();
  unsigned long timeout = (evcState >= EVC_QUERY_SOC && evcState <= EVC_QUERY_HV_VOLT) ? HVAC_ISOTP_TIMEOUT : HVAC_AT_TIMEOUT;
  if (evcState == EVC_SESSION) timeout = 2000;

  if (evcCmdSentTime > 0) {
    String r = "";
    if (xSemaphoreTake(obdDataMutex, portMAX_DELAY)) {
        if (lastOBDValue.length() > 0) { r = lastOBDValue; lastOBDValue = ""; }
        xSemaphoreGive(obdDataMutex);
    }

    if (r.length() > 0) {
      evcCmdSentTime = 0;
      switch (evcState) {
        case EVC_QUERY_SOC: {
          // Response: 62 20 02 XX XX -> raw * 0.02 = SOC%
          if (r.indexOf("622002") >= 0 || r.indexOf("62 20 02") >= 0) {
            int raw = parseUDSHex(r, "622002", 2);
            if (raw >= 0 && xSemaphoreTake(obdDataMutex, portMAX_DELAY)) {
                obdSOC = raw * 0.02f;
                Serial.printf("[ZOE] SOC = %.1f%%\n", obdSOC);
                lastOBDRxTime = millis();
                xSemaphoreGive(obdDataMutex);
            }
          }
          break;
        }
        case EVC_QUERY_BAT_TEMP: {
          // Response: 62 20 01 XX -> raw - 40 = temp °C
          if (r.indexOf("622001") >= 0 || r.indexOf("62 20 01") >= 0) {
            int raw = parseUDSHex(r, "622001", 1);
            if (raw >= 0 && xSemaphoreTake(obdDataMutex, portMAX_DELAY)) {
                obdHVBatTemp = raw - 40;
                Serial.printf("[ZOE] Bat Temp = %.0f°C\n", obdHVBatTemp);
                lastOBDRxTime = millis();
                xSemaphoreGive(obdDataMutex);
            }
          }
          break;
        }
        case EVC_QUERY_HV_VOLT: {
          // Response: 62 32 03 XX XX -> raw * 0.5 = V
          // Not stored as standalone var on S3 yet, but cell voltage card uses obdCellVoltageMax * 96
          // We could add obdHVBatVoltage here if needed
          break;
        }
        default: break;
      }
      evcState = (EvcPollState)(evcState + 1);
      return;
    } else if (now - evcCmdSentTime >= timeout) {
      Serial.printf("[OBD] EVC state %d timeout, advancing\n", evcState);
      evcCmdSentTime = 0;
      evcState = (EvcPollState)(evcState + 1);
      return;
    }
    return;
  }

  if (xSemaphoreTake(obdDataMutex, portMAX_DELAY)) { lastOBDValue = ""; xSemaphoreGive(obdDataMutex); }
  evcCmdSentTime = now;

  switch (evcState) {
    case EVC_SWITCH_SH:      sendCommand("ATSH7E4");      obdCurrentECU = 0; break;
    case EVC_SWITCH_CRA:     sendCommand("ATCRA7EC");     break;
    case EVC_SWITCH_FCSH:    sendCommand("ATFCSH7E4");    break;
    case EVC_SESSION:        sendCommand("10C0");         break;
    case EVC_QUERY_SOC:      sendCommand("222002");       break;
    case EVC_QUERY_BAT_TEMP: sendCommand("222001");       break;
    case EVC_QUERY_HV_VOLT:  sendCommand("223203");       break;
    default:
      evcState = EVC_DONE;
      evcCmdSentTime = 0;
      break;
  }
}
