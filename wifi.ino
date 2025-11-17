#if defined(ESP8266)
#include <ESP8266WiFi.h>
#else
#include <WiFi.h>
#endif
#include <WiFiUdp.h>
WiFiUDP udp_obj[5]; // <-- Теперь массив из 5 объектов

void wifiInit() {
   String tmp = "cont network";
   executeLine(tmp);
  addInternalWord("modeStaAp", modeStaApFunc,currentContext);
  addInternalWord("modeSta", modeStaFunc,currentContext);
  addInternalWord("modeAp", modeApFunc,currentContext);
  addInternalWord("onSta", wifiFunc,currentContext);
  addInternalWord("dbm", dbmFunc,currentContext);
  addInternalWord("ipSta", ipStaFunc,currentContext);
  addInternalWord("onAp", onApFunc,currentContext);
  addInternalWord("setAp", setApFunc,currentContext);
  addInternalWord("apConfig", apConfigFunc,currentContext);
  addInternalWord("ipAp", ipApFunc,currentContext);
  addInternalWord("scan", scanFunc,currentContext);
  addInternalWord("wifiOff", wifiOffFunc, currentContext);
  addInternalWord("udpBegin", udpBeginWord);
  addInternalWord("udpBeginMulticast", udpBeginMulticastWord);
  addInternalWord("udpAvailable", udpAvailableWord);
  addInternalWord("udpReadArray", udpReadArrayWord); 
  tmp = "main";
  executeLine(tmp);
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


void udpBeginWord(uint16_t addr) {
  Serial.println("--- udpBeginWord (NEW) ---");

  // 1. Прочитать индекс UDP-объекта (верхний элемент стека)
  // Используем popAsUInt8, так как индекс должен быть u8
  uint8_t index;
  if (!popAsUInt8(&index)) {
    Serial.println("⚠️ udpBegin: invalid UDP index (must be u8 compatible)");
    pushBool(false);
    Serial.println("--- udpBeginWord END (invalid index) ---");
    return;
  }

  if (index >= 5) {
    Serial.println("⚠️ udpBegin: UDP index out of range (0-4)");
    pushBool(false);
    Serial.println("--- udpBeginWord END (index > 4) ---");
    return;
  }
  Serial.printf("DEBUG: Read index as %u\n", index);

  // 2. Прочитать номер порта (теперь он на вершине стека)
  // Используем popInt32FromAny, чтобы поддержать разные числовые типы
  int32_t portValue;
  if (!popInt32FromAny(&portValue)) {
    Serial.println("⚠️ udpBegin: invalid port number");
    pushBool(false);
    Serial.println("--- udpBeginWord END (invalid port) ---");
    return;
  }
  Serial.printf("DEBUG: Read portValue as %d (0x%X)\n", portValue, (uint16_t)portValue);

  if (portValue < 1 || portValue > 65535) {
    Serial.printf("⚠️ udpBegin: port out of range (1-65535). Got %d\n", portValue);
    pushBool(false);
    Serial.println("--- udpBeginWord END (port out of range) ---");
    return;
  }
  uint16_t port = (uint16_t)portValue;

  // 3. Проверка активности WiFi
  wifi_mode_t currentMode = WiFi.getMode();
  if (currentMode == WIFI_MODE_NULL) {
      Serial.println("⚠️ udpBegin: WiFi is turned off (WIFI_MODE_NULL). Cannot begin UDP.");
      pushBool(false);
      Serial.println("--- udpBeginWord END (WiFi off) ---");
      return;
  }

  // 4. Попытаться открыть UDP-порт на выбранном объекте
  bool success = udp_obj[index].begin(port);

  // 5. Положить результат (true/false) на стек
  pushBool(success);

  if (success) {
    Serial.printf("UDP[%d] listening on port %u\n", index, port);
  } else {
    Serial.printf("Failed to begin UDP[%d] on port %u\n", index, port);
  }
  Serial.println("--- udpBeginWord END (success/fail) ---");
}

void udpAvailableWord(uint16_t addr) {
  // 1. Прочитать индекс UDP-объекта
  if (stackTop < 2) {
    Serial.println("⚠️ udpAvailable: UDP index expected");
    pushUInt16(0); // Если ошибка - возвращаем 0
    return;
  }
  uint8_t index;
  if (!popAsUInt8(&index)) {
    Serial.println("⚠️ udpAvailable: invalid UDP index (0-4)");
    pushUInt16(0);
    return;
  }
  if (index >= 5) {
    Serial.println("⚠️ udpAvailable: UDP index out of range (0-4)");
    pushUInt16(0);
    return;
  }

  // 2. Проверить размер пакета
  int packetSize = udp_obj[index].parsePacket();

  // 3. Положить результат (количество байт) на стек
  pushUInt16((uint16_t)(packetSize > 0 ? packetSize : 0)); // Всегда положительное значение или 0
}

void udpReadArrayWord(uint16_t addr) {
    Serial.println("--- udpReadArrayWord (CORRECT ORDER: index, poolRef, size) ---");

    // --- ШАГ 1: Получить udp_index с вершины стека ---
    uint8_t udpIndex;
    if (!popAsUInt8(&udpIndex)) {
        Serial.println("⚠️ udpReadArray: [Step 1] invalid UDP index (must be u8 compatible)");
        pushBool(false);
        Serial.println("--- udpReadArrayWord END (invalid UDP index) ---");
        return;
    }
    if (udpIndex >= 5) {
        Serial.println("⚠️ udpReadArray: [Step 1] UDP index out of range (0-4)");
        pushBool(false);
        Serial.println("--- udpReadArrayWord END (UDP index out of range) ---");
        return;
    }
    Serial.printf("DEBUG: [Step 1] Received UDP index: %u\n", udpIndex);

    // --- ШАГ 2: Получить poolRef ---
    uint8_t type, len;
    const uint8_t* data;
    if (!peekStackTop(&type, &len, &data)) {
         Serial.println("⚠️ udpReadArray: [Step 2] expected poolRef as TYPE_ARRAY(len=4)");
         pushBool(false);
         Serial.println("--- udpReadArrayWord END (no TYPE_ARRAY) ---");
         return;
    }
    if (type != TYPE_ARRAY || len != 4) {
        Serial.printf("⚠️ udpReadArray: [Step 2] expected TYPE_ARRAY(len=4), got TYPE_%d(len=%d)\n", type, len);
        pushBool(false);
        Serial.println("--- udpReadArrayWord END (wrong TYPE_ARRAY len) ---");
        return;
    }

    uint32_t poolRef;
    memcpy(&poolRef, data, 4);

    dropTop(0); // Убираем TYPE_ARRAY(len=4)

    Serial.printf("DEBUG: [Step 2] Received poolRef: 0x%X\n", poolRef);

    // --- ШАГ 3: Получить packet_size ---
    int32_t packetSize;
    if (!popInt32FromAny(&packetSize)) {
        Serial.println("⚠️ udpReadArray: [Step 3] expected packet size as integer (e.g., u16, i32)");
        pushBool(false);
        Serial.println("--- udpReadArrayWord END (no packet size) ---");
        return;
    }

    if (packetSize <= 0) {
        Serial.printf("DEBUG: [Step 3] Packet size is %d, nothing to read.\n", packetSize);
        pushBool(false);
        Serial.println("--- udpReadArrayWord END (packet size <= 0) ---");
        return;
    }
    Serial.printf("DEBUG: [Step 3] Received packet size: %d\n", packetSize);

    // --- ПРОВЕРЯЕМ ГРАНИЦЫ poolRef ---
    if (poolRef == 0 || poolRef == 0xFFFFFFFF) {
        Serial.println("⚠️ udpReadArray: [Step 2] poolRef is null/invalid");
        pushBool(false);
        Serial.println("--- udpReadArrayWord END (null/invalid poolRef) ---");
        return;
    }

    if (poolRef >= DATA_POOL_SIZE) {
        Serial.printf("⚠️ udpReadArray: [Step 2] poolRef (0x%X) is out of DATA_POOL_SIZE bounds\n", poolRef);
        pushBool(false);
        Serial.println("--- udpReadArrayWord END (poolRef out of bounds) ---");
        return;
    }

    // --- ЧИТАЕМ БЛОК ДАННЫХ ИЗ dataPool ПО poolRef ---
    if (dataPool[poolRef] != TYPE_ARRAY) {
        Serial.printf("⚠️ udpReadArray: [Step 2] poolRef (0x%X) does not point to an array header\n", poolRef);
        pushBool(false);
        Serial.println("--- udpReadArrayWord END (not an array header) ---");
        return;
    }

    uint8_t blockDataLen = dataPool[poolRef + 1];
    if (blockDataLen < 5) {
        Serial.printf("⚠️ udpReadArray: [Step 2] poolRef (0x%X) points to a corrupted array header (len < 5)\n", poolRef);
        pushBool(false);
        Serial.println("--- udpReadArrayWord END (corrupted header) ---");
        return;
    }

    uint8_t elemType = dataPool[poolRef + 2];
    uint16_t elemCount = dataPool[poolRef + 3] | (dataPool[poolRef + 4] << 8);

    uint8_t elemSize = 1;
    if (elemType == TYPE_UINT16 || elemType == TYPE_INT16) elemSize = 2;
    else if (elemType == TYPE_INT) elemSize = 4;

    uint16_t arrayCapacityBytes = elemCount * elemSize;

    uint8_t* targetBufferPtr = &dataPool[poolRef + 5];

    Serial.printf("DEBUG: Calculated target buffer ptr: %p from poolRef 0x%X\n", targetBufferPtr, poolRef);
    Serial.printf("DEBUG: Array capacity: %d bytes\n", arrayCapacityBytes);

    // --- СРАВНИТЬ РАЗМЕР ПАКЕТА И ЁМКОСТЬ БУФЕРА ---
    int bytesToRead = (int)packetSize;
    if (bytesToRead > arrayCapacityBytes) {
        Serial.printf("⚠️ udpReadArray: packet size (%d) exceeds array capacity (%d). Truncating.\n", bytesToRead, arrayCapacityBytes);
        bytesToRead = arrayCapacityBytes;
    }

    // --- ПРОЧИТАТЬ ДАННЫЕ НАПРЯМУЮ В targetBufferPtr ---
    int bytesRead = udp_obj[udpIndex].read(targetBufferPtr, bytesToRead);
    if (bytesRead < 0) {
        Serial.printf("⚠️ udpReadArray: Error reading UDP[%d]\n", udpIndex);
        pushBool(false);
        Serial.println("--- udpReadArrayWord END (read error) ---");
        return;
    }
    if (bytesRead != bytesToRead) {
        Serial.printf("⚠️ udpReadArray: Expected to read %d bytes, but read %d. Buffer might be larger than packet.\n", bytesToRead, bytesRead);
    }
    Serial.printf("DEBUG: Successfully read %d bytes directly into Words array memory at %p\n", bytesRead, targetBufferPtr);

    // --- ПОЛОЖИТЬ РЕЗУЛЬТАТ (true) НА СТЕК ---
    pushBool(true);

    Serial.printf("SUCCESS: Read %d bytes into Words array (poolRef 0x%X) memory via UDP[%d]\n", bytesRead, poolRef, udpIndex);
    Serial.println("--- udpReadArrayWord END (success) ---");
}

void udpBeginMulticastWord(uint16_t addr) {
    Serial.println("--- udpBeginMulticastWord (FINAL ORDER) ---");
        // 3. Прочитать индекс UDP-объекта (следующий элемент после строки - TYPE_UINT8)
    uint8_t udpIndex;
    if (!popAsUInt8(&udpIndex) || udpIndex >= 5) {
        Serial.println("⚠️ udpBeginMulticast: invalid UDP index (0-4)");
        pushBool(false);
        Serial.println("--- udpBeginMulticastWord END (invalid index) ---");
        return;
    }

    // 1. Прочитать порт (верхний элемент стека - TYPE_UINT16 или TYPE_INT)
    int32_t portValue;
    if (!popInt32FromAny(&portValue) || portValue < 1 || portValue > 65535) {
        Serial.println("⚠️ udpBeginMulticast: invalid port number (1-65535)");
        pushBool(false);
        Serial.println("--- udpBeginMulticastWord END (invalid port) ---");
        return;
    }
    uint16_t port = (uint16_t)portValue;

    // 2. Прочитать IP-адрес мультикаста (следующий элемент после порта - TYPE_STRING)
    String multicastIpStr;
    if (!popStringFromStack(multicastIpStr)) {
        Serial.println("⚠️ udpBeginMulticast: expected multicast IP as string (e.g., \"239.255.255.250\")");
        pushBool(false);
        Serial.println("--- udpBeginMulticastWord END (not a string) ---");
        return;
    }



    // 4. Преобразовать строку IP в IPAddress
    IPAddress multicastIP;
    if (!multicastIP.fromString(multicastIpStr)) {
        Serial.println("⚠️ udpBeginMulticast: invalid multicast IP address format");
        pushBool(false);
        Serial.println("--- udpBeginMulticastWord END (invalid IP) ---");
        return;
    }

    // 5. Проверить, подключен ли WiFi
    if (WiFi.status() != WL_CONNECTED) {
         Serial.println("⚠️ udpBeginMulticast: WiFi is not connected.");
         pushBool(false);
         Serial.println("--- udpBeginMulticastWord END (WiFi not connected) ---");
         return;
    }

    Serial.printf("DEBUG: Attempting to beginMulticast on UDP[%d] for MulticastIP=%s, Port=%u\n", udpIndex, multicastIP.toString().c_str(), port);

    // 6. Вызвать beginMulticast
    bool success = udp_obj[udpIndex].beginMulticast(multicastIP, port);

    // 7. Положить результат (true/false) на стек
    pushBool(success);

    if (success) {
        Serial.printf("UDP[%d] listening for multicast on %s:%u\n", udpIndex, multicastIP.toString().c_str(), port);
    } else {
        Serial.printf("Failed to beginMulticast on UDP[%d] for %s:%u\n", udpIndex, multicastIP.toString().c_str(), port);
    }
    Serial.println("--- udpBeginMulticastWord END (success/fail) ---");
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
