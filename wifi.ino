#if defined(ESP8266)
#include <ESP8266WiFi.h>
#else
#include <WiFi.h>
#endif
#include <WiFiUdp.h>
WiFiUDP udp_obj[5]; // <-- Теперь массив из 5 объектов

void webInit() {
   String tmp = "cont network";
   executeLine(tmp);
  addInternalWord("modeStaAp", modeStaApFunc);
  addInternalWord("modeSta", modeStaFunc);
  addInternalWord("modeAp", modeApFunc);
  addInternalWord("onSta", wifiFunc);
  addInternalWord("dbm", dbmFunc);
  addInternalWord("ipSta", ipStaFunc);
  addInternalWord("onAp", onApFunc);
  addInternalWord("setAp", setApFunc);
  addInternalWord("apConfig", apConfigFunc);
  addInternalWord("ipAp", ipApFunc);
  addInternalWord("scan", scanFunc);
  addInternalWord("wifiOff", wifiOffFunc);
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
    outputStream->println("⚠️ onAp: SSID (string) expected");
    pushBool(false);
    return;
  }
  if (!popStringFromStack(password)) {
    outputStream->println("⚠️ onAp: password (string) expected");
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
    outputStream->println("⚠️ sta: SSID (string) expected");
    pushBool(false);
    return;
  }
  String ssid = String((char*)ssidData, ssidLen);
  dropTop(0);

  // 2. Читаем пароль
  uint8_t passType, passLen;
  const uint8_t* passData;
  if (!peekStackTop(&passType, &passLen, &passData) || passType != TYPE_STRING) {
    outputStream->println("⚠️ sta: password (string) expected");
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
    outputStream->printf("⚠️ sta: network '%s' not found\n", ssid.c_str());
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
    outputStream->println("setAp: Wi-Fi not in AP or STA+AP mode. Use modeAp or modeStaAp first.");
    pushBool(false);
    return;
  }

  if (stackTop < 2) { outputStream->println("⚠️ setAp: hidden flag expected"); pushBool(false); return; }
  bool hidden = popBool();

  if (stackTop < 2) { outputStream->println("⚠️ setAp: channel expected"); pushBool(false); return; }
  int32_t channel = popInt();

  String password, ssid;
  if (!popStringFromStack(password)) {
    outputStream->println("⚠️ setAp: password (string) expected");
    pushBool(false);
    return;
  }
  if (!popStringFromStack(ssid)) {
    outputStream->println("⚠️ setAp: SSID (string) expected");
    pushBool(false);
    return;
  }

  if (channel < 1 || channel > 13) {
    outputStream->println("⚠️ setAp: channel must be 1–13");
    pushBool(false);
    return;
  }

  bool ok = WiFi.softAP(ssid.c_str(), password.length() > 0 ? password.c_str() : nullptr, (int)channel, hidden);
  pushBool(ok);
}

void apConfigFunc(uint16_t addr) {
  wifi_mode_t mode = WiFi.getMode();
  if (mode != WIFI_MODE_AP && mode != WIFI_MODE_APSTA) {
    outputStream->println("apConfig: Wi-Fi not in AP or STA+AP mode. Use modeAp or modeStaAp first.");
    pushBool(false);
    return;
  }

  String subnetStr, gatewayStr, localIpStr;
  if (!popStringFromStack(subnetStr)) {
    outputStream->println("⚠️ apConfig: subnet mask (string) expected");
    pushBool(false);
    return;
  }
  if (!popStringFromStack(gatewayStr)) {
    outputStream->println("⚠️ apConfig: gateway IP (string) expected");
    pushBool(false);
    return;
  }
  if (!popStringFromStack(localIpStr)) {
    outputStream->println("⚠️ apConfig: local IP (string) expected");
    pushBool(false);
    return;
  }

  IPAddress local_ip, gateway, subnet;
  if (!local_ip.fromString(localIpStr) ||
      !gateway.fromString(gatewayStr) ||
      !subnet.fromString(subnetStr)) {
    outputStream->println("⚠️ apConfig: invalid IP address format");
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
  outputStream->print("{\"networks\":[");
  int n = WiFi.scanNetworks();
  for (int i = 0; i < n; i++) {
    if (i > 0) outputStream->print(",");
    outputStream->print("{\"ssid\":\"");
    String ssid = WiFi.SSID(i);
    for (char c : ssid) {
      if (c == '"' || c == '\\') outputStream->print('\\');
      outputStream->print(c);
    }
    outputStream->print("\",\"pass\":\"");
#if defined(ESP8266)
    outputStream->print(WiFi.encryptionType(i) == ENC_TYPE_NONE ? "" : "*");
#else
    outputStream->print(WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "" : "*");
#endif
    outputStream->print("\",\"dbm\":");
    outputStream->print(WiFi.RSSI(i));
    outputStream->print("}");
  }
  outputStream->print("]}");
  WiFi.scanDelete();
}


void udpBeginWord(uint16_t addr) {
  outputStream->println("--- udpBeginWord (NEW) ---");

  // 1. Прочитать индекс UDP-объекта (верхний элемент стека)
  // Используем popAsUInt8, так как индекс должен быть u8
  uint8_t index;
  if (!popAsUInt8(&index)) {
    outputStream->println("⚠️ udpBegin: invalid UDP index (must be u8 compatible)");
    pushBool(false);
    outputStream->println("--- udpBeginWord END (invalid index) ---");
    return;
  }

  if (index >= 5) {
    outputStream->println("⚠️ udpBegin: UDP index out of range (0-4)");
    pushBool(false);
    outputStream->println("--- udpBeginWord END (index > 4) ---");
    return;
  }
  outputStream->printf("DEBUG: Read index as %u\n", index);

  // 2. Прочитать номер порта (теперь он на вершине стека)
  // Используем popInt32FromAny, чтобы поддержать разные числовые типы
  int32_t portValue;
  if (!popInt32FromAny(&portValue)) {
    outputStream->println("⚠️ udpBegin: invalid port number");
    pushBool(false);
    outputStream->println("--- udpBeginWord END (invalid port) ---");
    return;
  }
  outputStream->printf("DEBUG: Read portValue as %d (0x%X)\n", portValue, (uint16_t)portValue);

  if (portValue < 1 || portValue > 65535) {
    outputStream->printf("⚠️ udpBegin: port out of range (1-65535). Got %d\n", portValue);
    pushBool(false);
    outputStream->println("--- udpBeginWord END (port out of range) ---");
    return;
  }
  uint16_t port = (uint16_t)portValue;

  // 3. Проверка активности WiFi
  wifi_mode_t currentMode = WiFi.getMode();
  if (currentMode == WIFI_MODE_NULL) {
      outputStream->println("⚠️ udpBegin: WiFi is turned off (WIFI_MODE_NULL). Cannot begin UDP.");
      pushBool(false);
      outputStream->println("--- udpBeginWord END (WiFi off) ---");
      return;
  }

  // 4. Попытаться открыть UDP-порт на выбранном объекте
  bool success = udp_obj[index].begin(port);

  // 5. Положить результат (true/false) на стек
  pushBool(success);

  if (success) {
    outputStream->printf("UDP[%d] listening on port %u\n", index, port);
  } else {
    outputStream->printf("Failed to begin UDP[%d] on port %u\n", index, port);
  }
  outputStream->println("--- udpBeginWord END (success/fail) ---");
}

void udpAvailableWord(uint16_t addr) {
  // 1. Прочитать индекс UDP-объекта
  if (stackTop < 2) {
    outputStream->println("⚠️ udpAvailable: UDP index expected");
    pushUInt16(0); // Если ошибка - возвращаем 0
    return;
  }
  uint8_t index;
  if (!popAsUInt8(&index)) {
    outputStream->println("⚠️ udpAvailable: invalid UDP index (0-4)");
    pushUInt16(0);
    return;
  }
  if (index >= 5) {
    outputStream->println("⚠️ udpAvailable: UDP index out of range (0-4)");
    pushUInt16(0);
    return;
  }

  // 2. Проверить размер пакета
  int packetSize = udp_obj[index].parsePacket();

  // 3. Положить результат (количество байт) на стек
  pushUInt16((uint16_t)(packetSize > 0 ? packetSize : 0)); // Всегда положительное значение или 0
}


void udpBeginMulticastWord(uint16_t addr) {
    outputStream->println("--- udpBeginMulticastWord (FINAL ORDER) ---");
        // 3. Прочитать индекс UDP-объекта (следующий элемент после строки - TYPE_UINT8)
    uint8_t udpIndex;
    if (!popAsUInt8(&udpIndex) || udpIndex >= 5) {
        outputStream->println("⚠️ udpBeginMulticast: invalid UDP index (0-4)");
        pushBool(false);
        outputStream->println("--- udpBeginMulticastWord END (invalid index) ---");
        return;
    }

    // 1. Прочитать порт (верхний элемент стека - TYPE_UINT16 или TYPE_INT)
    int32_t portValue;
    if (!popInt32FromAny(&portValue) || portValue < 1 || portValue > 65535) {
        outputStream->println("⚠️ udpBeginMulticast: invalid port number (1-65535)");
        pushBool(false);
        outputStream->println("--- udpBeginMulticastWord END (invalid port) ---");
        return;
    }
    uint16_t port = (uint16_t)portValue;

    // 2. Прочитать IP-адрес мультикаста (следующий элемент после порта - TYPE_STRING)
    String multicastIpStr;
    if (!popStringFromStack(multicastIpStr)) {
        outputStream->println("⚠️ udpBeginMulticast: expected multicast IP as string (e.g., \"239.255.255.250\")");
        pushBool(false);
        outputStream->println("--- udpBeginMulticastWord END (not a string) ---");
        return;
    }



    // 4. Преобразовать строку IP в IPAddress
    IPAddress multicastIP;
    if (!multicastIP.fromString(multicastIpStr)) {
        outputStream->println("⚠️ udpBeginMulticast: invalid multicast IP address format");
        pushBool(false);
        outputStream->println("--- udpBeginMulticastWord END (invalid IP) ---");
        return;
    }

    // 5. Проверить, подключен ли WiFi
    if (WiFi.status() != WL_CONNECTED) {
         outputStream->println("⚠️ udpBeginMulticast: WiFi is not connected.");
         pushBool(false);
         outputStream->println("--- udpBeginMulticastWord END (WiFi not connected) ---");
         return;
    }

    outputStream->printf("DEBUG: Attempting to beginMulticast on UDP[%d] for MulticastIP=%s, Port=%u\n", udpIndex, multicastIP.toString().c_str(), port);

    // 6. Вызвать beginMulticast
    bool success = udp_obj[udpIndex].beginMulticast(multicastIP, port);

    // 7. Положить результат (true/false) на стек
    pushBool(success);

    if (success) {
        outputStream->printf("UDP[%d] listening for multicast on %s:%u\n", udpIndex, multicastIP.toString().c_str(), port);
    } else {
        outputStream->printf("Failed to beginMulticast on UDP[%d] for %s:%u\n", udpIndex, multicastIP.toString().c_str(), port);
    }
    outputStream->println("--- udpBeginMulticastWord END (success/fail) ---");
}

void udpReadArrayWord(uint16_t addr) {
    // 1. Первый аргумент: udpsoc (u8)
    uint8_t udpIndex;
    if (!popAsUInt8(&udpIndex) || udpIndex >= 5) {
        pushBool(false);
        return;
    }

    // 2. Второй аргумент: udpBuffer → должен быть TYPE_ADDRINFO (5 байт)
    uint8_t type, len;
    const uint8_t* data;
    if (!peekStackTop(&type, &len, &data) || type != TYPE_ADDRINFO || len != 5) {
        dropTop(0); // убираем некорректный элемент
        pushBool(false);
        return;
    }
    dropTop(0); // убираем addrInfo

    uint16_t address = data[0] | (data[1] << 8);      // адрес начала данных
    uint16_t dataLen = data[2] | (data[3] << 8);      // длина данных
    uint8_t elemType = data[4];                       // тип элемента

    // Поддерживаем только байтовые массивы для UDP
    if (elemType != TYPE_UINT8 && elemType != TYPE_INT8) {
        pushBool(false);
        return;
    }

    // 3. Третий аргумент: packet_size (число)
    int32_t packetSize;
    if (!popInt32FromAny(&packetSize) || packetSize <= 0) {
        pushBool(false);
        return;
    }

    // Проверка: address — это смещение в dataPool, должно быть >= 4
    if (address < 4 || address >= DATA_POOL_SIZE) {
        pushBool(false);
        return;
    }

    // Проверка общей границы
    if (address + dataLen > DATA_POOL_SIZE) {
        pushBool(false);
        return;
    }

    // Ограничиваем чтение размером буфера
    int bytesToRead = (int)packetSize;
    if (bytesToRead > (int)dataLen) {
        bytesToRead = (int)dataLen;
    }

    // Читаем напрямую в dataPool по адресу `address`
    int bytesRead = udp_obj[udpIndex].read(&dataPool[address], bytesToRead);

    // Возвращаем успех
    pushBool(bytesRead >= 0);
}
