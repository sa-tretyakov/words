#include <WiFi.h>

void wifiInit() {
  addInternalWord("modeStaAp", modeStaApFunc,254);
  addInternalWord("modeSta", modeStaFunc,254);
  addInternalWord("modeAp", modeApFunc,254);
  addInternalWord("onSta", wifiFunc,254);
  addInternalWord("dbm", dbmFunc,254);
  addInternalWord("ipSta", ipStaFunc,254);
  addInternalWord("onAp", onApFunc,254);
  addInternalWord("setAp", setApFunc,254);
  addInternalWord("apConfig", apConfigFunc,254);
  addInternalWord("ipAp", ipApFunc,254);
  addInternalWord("scan", scanFunc,254);
  addInternalWord("wifiOff", wifiOffFunc, 254);
}

void modeStaApFunc(uint16_t addr) {
  WiFi.mode(WIFI_MODE_APSTA);
  // Опционально: можно положить результат (например, true) на стек
  // Но по аналогии с GPIO-функциями — просто выполняем действие
}
void modeStaFunc(uint16_t addr) {
  WiFi.mode(WIFI_MODE_STA);
}
void modeApFunc(uint16_t addr) {
  WiFi.mode(WIFI_MODE_AP);
}


void ipStaFunc(uint16_t addr) {
  if (WiFi.status() != WL_CONNECTED) {
    // Если не подключены — возвращаем пустую строку или "0.0.0.0"
    pushString("0.0.0.0");
    return;
  }
  IPAddress ip = WiFi.localIP();
  String ipStr = ip.toString();
  pushString(ipStr.c_str());
}

void dbmFunc(uint16_t addr) {
  if (WiFi.status() != WL_CONNECTED) {
    pushInt(0); // или -1000 — "нет сигнала"
    return;
  }
  int32_t rssi = WiFi.RSSI(); // возвращает int, но кладём как int32
  pushInt(rssi);
}


void onApFunc(uint16_t addr) {
  String ssid, password;
  if (!popStringFromStack(ssid)) {
    Serial.println("⚠️ onAp: SSID (string) expected");
    pushBool(false);
    return;
  }
  if (!popStringFromStack(password)) {
    Serial.println("⚠️ onAp: password (string) expected");
    pushBool(false);
    return;
  }
  // Включаем AP
  wifi_mode_t mode = WiFi.getMode();
  if (mode != WIFI_MODE_AP && mode != WIFI_MODE_APSTA) {
    WiFi.mode(WIFI_MODE_AP);
  }


  bool ok = WiFi.softAP(ssid.c_str(), password.length() > 0 ? password.c_str() : nullptr);
  pushBool(ok);
}
void wifiFunc(uint16_t addr) {
  // 1. Читаем SSID (должен быть на верхушке стека)
  uint8_t ssidType, ssidLen;
  const uint8_t* ssidData;
  if (!peekStackTop(&ssidType, &ssidLen, &ssidData) || ssidType != TYPE_STRING) {
    Serial.println("⚠️ sta: SSID (string) expected");
    pushBool(false);
    return;
  }
  String ssid = String((char*)ssidData, ssidLen);
  dropTop(0);

  // 2. Читаем пароль
  uint8_t passType, passLen;
  const uint8_t* passData;
  if (!peekStackTop(&passType, &passLen, &passData) || passType != TYPE_STRING) {
    Serial.println("⚠️ sta: password (string) expected");
    pushBool(false);
    return;
  }
  String password = String((char*)passData, passLen);
  dropTop(0);

  // 3. Убедимся, что режим включает STA
  wifi_mode_t mode = WiFi.getMode();
  if ((mode & WIFI_MODE_STA) == 0) {
    WiFi.mode((wifi_mode_t)(mode | WIFI_MODE_STA));
  }

  // 4. Сканируем и ищем сеть
  bool found = false;
  int n = WiFi.scanNetworks();
  for (int i = 0; i < n; i++) {
    if (WiFi.SSID(i) == ssid) {
      found = true;
      break;
    }
  }

  if (!found) {
    Serial.printf("⚠️ sta: network '%s' not found\n", ssid.c_str());
    pushBool(false);
    return;
  }

  // 5. Подключаемся
  WiFi.begin(ssid.c_str(), password.c_str());

  // Ждём до 10 секунд
  for (int i = 0; i < 100; i++) {
    if (WiFi.status() == WL_CONNECTED) {
      pushBool(true);
      return;
    }
    delay(100);
  }

  pushBool(false);
}

void setApFunc(uint16_t addr) {
  wifi_mode_t mode = WiFi.getMode();
  if (mode != WIFI_MODE_AP && mode != WIFI_MODE_APSTA) {
    Serial.println("setAp: Wi-Fi not in AP or STA+AP mode. Use modeAp or modeStaAp first.");
    pushBool(false);
    return;
  }

  if (stackTop < 2) { Serial.println("⚠️ setAp: hidden flag expected"); pushBool(false); return; }
  bool hidden = popBool();

  if (stackTop < 2) { Serial.println("⚠️ setAp: channel expected"); pushBool(false); return; }
  int32_t channel = popInt();

  String password, ssid;
  if (!popStringFromStack(password)) {
    Serial.println("⚠️ setAp: password (string) expected");
    pushBool(false);
    return;
  }
  if (!popStringFromStack(ssid)) {
    Serial.println("⚠️ setAp: SSID (string) expected");
    pushBool(false);
    return;
  }

  if (channel < 1 || channel > 13) {
    Serial.println("⚠️ setAp: channel must be 1–13");
    pushBool(false);
    return;
  }

  bool ok = WiFi.softAP(ssid.c_str(), password.length() > 0 ? password.c_str() : nullptr, (int)channel, hidden);
  pushBool(ok);
}

void apConfigFunc(uint16_t addr) {
  wifi_mode_t mode = WiFi.getMode();
  if (mode != WIFI_MODE_AP && mode != WIFI_MODE_APSTA) {
    Serial.println("apConfig: Wi-Fi not in AP or STA+AP mode. Use modeAp or modeStaAp first.");
    pushBool(false);
    return;
  }

  String subnetStr, gatewayStr, localIpStr;
  if (!popStringFromStack(subnetStr)) {
    Serial.println("⚠️ apConfig: subnet mask (string) expected");
    pushBool(false);
    return;
  }
  if (!popStringFromStack(gatewayStr)) {
    Serial.println("⚠️ apConfig: gateway IP (string) expected");
    pushBool(false);
    return;
  }
  if (!popStringFromStack(localIpStr)) {
    Serial.println("⚠️ apConfig: local IP (string) expected");
    pushBool(false);
    return;
  }

  IPAddress local_ip, gateway, subnet;
  if (!local_ip.fromString(localIpStr) ||
      !gateway.fromString(gatewayStr) ||
      !subnet.fromString(subnetStr)) {
    Serial.println("⚠️ apConfig: invalid IP address format");
    pushBool(false);
    return;
  }

  bool ok = WiFi.softAPConfig(local_ip, gateway, subnet);
  pushBool(ok);
}

void ipApFunc(uint16_t addr) {
  wifi_mode_t mode = WiFi.getMode();
  if (mode != WIFI_MODE_AP && mode != WIFI_MODE_APSTA) {
    // AP не активен — возвращаем заглушку
    pushString("0.0.0.0");
    return;
  }

  IPAddress ip = WiFi.softAPIP();
  String ipStr = ip.toString();
  pushString(ipStr.c_str());
}

void wifiOffFunc(uint16_t addr) {
  bool ok = WiFi.mode(WIFI_MODE_NULL);
  pushBool(ok);
}

void scanFunc(uint16_t addr) {
  jsonOutput->print("{\"networks\":[");
  int n = WiFi.scanNetworks();
  for (int i = 0; i < n; i++) {
    if (i > 0) jsonOutput->print(",");
    jsonOutput->print("{\"ssid\":\"");
    String ssid = WiFi.SSID(i);
    for (char c : ssid) {
      if (c == '"' || c == '\\') jsonOutput->print('\\');
      jsonOutput->print(c);
    }
    jsonOutput->print("\",\"pass\":\"");
#if defined(ESP8266)
    jsonOutput->print(WiFi.encryptionType(i) == ENC_TYPE_NONE ? "" : "*");
#else
    jsonOutput->print(WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "" : "*");
#endif
    jsonOutput->print("\",\"dbm\":");
    jsonOutput->print(WiFi.RSSI(i));
    jsonOutput->print("}");
  }
  jsonOutput->print("]}");
  WiFi.scanDelete();
}
/*
void scanFunc(uint16_t addr) {
  bool async = false;

  // Проверяем, есть ли bool на стеке
  if (stackTop >= 2 && stack[stackTop - 1] == TYPE_BOOL && stack[stackTop - 2] == 1) {
    async = (stack[stackTop - 3] != 0);
    dropTop(0);
  }

  if (!async) {
    // === БЛОКИРУЮЩИЙ РЕЖИМ ===
    int n = WiFi.scanNetworks();
    jsonOutput->print("{\"networks\":[");
    for (int i = 0; i < n; i++) {
      if (i > 0) jsonOutput->print(",");
      jsonOutput->print("{\"ssid\":\"");

      String ssid = WiFi.SSID(i);
      for (int j = 0; j < ssid.length(); j++) {
        char c = ssid[j];
        if (c == '"' || c == '\\') jsonOutput->print('\\');
        jsonOutput->print(c);
      }

      jsonOutput->print("\",\"pass\":\"");
#if defined(ESP8266)
      jsonOutput->print(WiFi.encryptionType(i) == ENC_TYPE_NONE ? "" : "*");
#else
      jsonOutput->print(WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "" : "*");
#endif
      jsonOutput->print("\",\"dbm\":");
      jsonOutput->print(WiFi.RSSI(i));
      jsonOutput->print("}");
    }
    jsonOutput->print("]}");
    WiFi.scanDelete();
    cachedScanResult = ""; // сбрасываем кэш, так как режим разовый
  } else {
    // === АСИНХРОННЫЙ РЕЖИМ ===
    int n = WiFi.scanComplete();

    if (n == -2) {
      // Сканирование не запущено — запускаем
      WiFi.scanNetworks(true, true); // async, show_hidden
      // Возвращаем кэш, если есть
      if (cachedScanResult.length() > 0) {
        jsonOutput->print(cachedScanResult);
      } else {
        jsonOutput->print("{\"networks\":[]}");
      }
    } else if (n == -1) {
      // В процессе — возвращаем кэш
      if (cachedScanResult.length() > 0) {
        jsonOutput->print(cachedScanResult);
      } else {
        jsonOutput->print("{\"networks\":[]}");
      }
    } else if (n >= 0) {
      // Завершено — формируем новый JSON
      String json = "{\"networks\":[";
      for (int i = 0; i < n; i++) {
        if (i > 0) json += ",";
        json += "{\"ssid\":\"";

        String ssid = WiFi.SSID(i);
        for (int j = 0; j < ssid.length(); j++) {
          char c = ssid[j];
          if (c == '"' || c == '\\') json += '\\';
          json += c;
        }

        json += "\",\"pass\":\"";
#if defined(ESP8266)
        json += (WiFi.encryptionType(i) == ENC_TYPE_NONE) ? "" : "*";
#else
        json += (WiFi.encryptionType(i) == WIFI_AUTH_OPEN) ? "" : "*";
#endif
        json += "\",\"dbm\":" + String(WiFi.RSSI(i)) + "}";
      }
      json += "]}";

      cachedScanResult = json;
      jsonOutput->print(json);
      WiFi.scanDelete();
    } else {
      // Ошибка
      if (cachedScanResult.length() > 0) {
        jsonOutput->print(cachedScanResult);
      } else {
        jsonOutput->print("{\"networks\":[]}");
      }
    }
  }
}
*/
