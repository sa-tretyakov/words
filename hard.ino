void wifiFunc(WordEntry* self, WordEntry** body, int* ip) {
  if (dataStack.size() < 2) {
    Serial.println("wifi: expected 2 arguments (ssid password)");
    return;
  }

  WordEntry* ssidWord = dataStack.back(); dataStack.pop_back();
  WordEntry* passWord = dataStack.back(); dataStack.pop_back();

  if (ssidWord->type != STRING || passWord->type != STRING) {
    Serial.println("wifi: ssid and password must be strings");
    return;
  }

  const char* ssid = (char*)ssidWord->value;
  const char* password = (char*)passWord->value;

  Serial.print("Connecting to Wi-Fi: ");
  Serial.println(ssid);

  // Умная установка режима
  wifi_mode_t currentMode = WiFi.getMode();
  if (!(currentMode & WIFI_MODE_STA)) {
    if (currentMode == WIFI_MODE_NULL) {
      WiFi.mode(WIFI_STA);
    } else {
      WiFi.mode(WIFI_AP_STA); // сохраняем AP, если был
    }
  }

  WiFi.begin(ssid, password);

  // Ждём подключения
  int count = 0;
  while (WiFi.status() != WL_CONNECTED && count < 50) {
    delay(200);
    Serial.print(".");
    count++;
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("Wi-Fi connected. IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("Wi-Fi connection failed!");
  }
}

// Переключиться в STA-режим (клиент)
void modeStaFunc(WordEntry* self, WordEntry** body, int* ip) {
  wifi_mode_t current = WiFi.getMode();
  
  if (current == WIFI_MODE_STA) {
    Serial.println("Wi-Fi already in STA mode");
    return;
  }

  Serial.println("Switching Wi-Fi to STA mode...");
  WiFi.mode(WIFI_STA);
  Serial.println("STA mode activated");
}

void modeApFunc(WordEntry* self, WordEntry** body, int* ip) {
  wifi_mode_t current = WiFi.getMode();
  if (current == WIFI_MODE_AP) {
    Serial.println("Wi-Fi already in AP mode");
    return;
  }
  Serial.println("Setting Wi-Fi mode: AP");
  WiFi.mode(WIFI_AP);
}
void modeStaApFunc(WordEntry* self, WordEntry** body, int* ip) {
  wifi_mode_t current = WiFi.getMode();
  if (current == WIFI_MODE_APSTA) {
    Serial.println("Wi-Fi already in STA+AP mode");
    return;
  }
  Serial.println("Setting Wi-Fi mode: STA+AP");
  WiFi.mode(WIFI_AP_STA);
}

void setApFunc(WordEntry* self, WordEntry** body, int* ip) {
  if (dataStack.size() < 2) {
    Serial.println("setAp: expected 2 arguments (ssid password)");
    return;
  }

  // Порядок: сначала SSID (левый аргумент), потом пароль (правый)
  WordEntry* ssidWord = dataStack.back(); dataStack.pop_back(); // "MySSID"
  WordEntry* passWord = dataStack.back(); dataStack.pop_back(); // "MyPass"

  if (ssidWord->type != STRING || passWord->type != STRING) {
    Serial.println("setAp: ssid and password must be strings");
    return;
  }

  const char* ssid = (char*)ssidWord->value;
  const char* password = (char*)passWord->value;

  if (strlen(password) > 0 && strlen(password) < 8) {
    Serial.println("setAp: password must be at least 8 characters (or empty for open AP)");
    return;
  }

  wifi_mode_t mode = WiFi.getMode();
  if (mode != WIFI_MODE_AP && mode != WIFI_MODE_APSTA) {
    Serial.println("setAp: Wi-Fi not in AP or STA+AP mode. Use modeAp or modeStaAp first.");
    return;
  }

  Serial.print("Configuring AP: SSID='");
  Serial.print(ssid);
  Serial.print("', password='");
  Serial.print(strlen(password) > 0 ? "********" : "<open>");
  Serial.println("'");

  bool result;
  if (strlen(password) == 0) {
    result = WiFi.softAP(ssid);
  } else {
    result = WiFi.softAP(ssid, password);
  }

  if (result) {
    Serial.print("AP started. IP: ");
    Serial.println(WiFi.softAPIP());
  } else {
    Serial.println("setAp: failed to start AP");
  }
}
void apConfigFunc(WordEntry* self, WordEntry** body, int* ip) {
  // Проверяем, что Wi-Fi в режиме AP или STA+AP
  wifi_mode_t mode = WiFi.getMode();
  if (mode != WIFI_MODE_AP && mode != WIFI_MODE_APSTA) {
    Serial.println("apConfig: Wi-Fi not in AP or STA+AP mode. Use modeAp or modeStaAp first.");
    return;
  }

  // Ищем переменные в текущем контексте
  auto findVar = [](const String& name) -> WordEntry* {
    for (const auto& pair : dictionary) {
      if (pair.second.contextId == currentContextId && pair.first == name) {
        return const_cast<WordEntry*>(&pair.second);
      }
    }
    return nullptr;
  };

  // Получаем ssidAp
  WordEntry* ssidApVar = findVar("ssidAp");
  if (!ssidApVar || ssidApVar->type != STRING) {
    Serial.println("apConfig: missing or invalid 'ssidAp' (string expected)");
    return;
  }
  const char* ssid = (char*)ssidApVar->value;

  // Получаем ssidApPass
  WordEntry* passVar = findVar("ssidApPass");
  const char* password = "";
  if (passVar && passVar->type == STRING) {
    password = (char*)passVar->value;
    if (strlen(password) > 0 && strlen(password) < 8) {
      Serial.println("apConfig: password must be at least 8 characters");
      return;
    }
  }

  // Получаем IP-настройки
  IPAddress local_ip, gateway_ip, subnet_mask;
  WordEntry* ipVar = findVar("ip");
  WordEntry* gwVar = findVar("gateway");
  WordEntry* snVar = findVar("subnet");

  if (ipVar && ipVar->type == STRING) {
    local_ip.fromString((char*)ipVar->value);
  } else {
    local_ip = IPAddress(192, 168, 4, 1);
  }

  if (gwVar && gwVar->type == STRING) {
    gateway_ip.fromString((char*)gwVar->value);
  } else {
    gateway_ip = local_ip;
  }

  if (snVar && snVar->type == STRING) {
    subnet_mask.fromString((char*)snVar->value);
  } else {
    subnet_mask = IPAddress(255, 255, 255, 0);
  }

  // Получаем channel
  int channel = 1;
  WordEntry* chVar = findVar("channel");
  if (chVar && chVar->type == INT) {
    channel = *(int32_t*)chVar->value;
    if (channel < 1) channel = 1;
    if (channel > 13) channel = 13;
  }

  // Получаем hidden
  bool hidden = false;
  WordEntry* hidVar = findVar("hidden");
  if (hidVar && hidVar->type == INT) {
    hidden = (*(int32_t*)hidVar->value != 0);
  }

  // Получаем maxClients
  int maxClients = 4;
  WordEntry* mcVar = findVar("maxClients");
  if (mcVar && mcVar->type == INT) {
    maxClients = *(int32_t*)mcVar->value;
    if (maxClients < 1) maxClients = 1;
    if (maxClients > 8) maxClients = 8; // ESP32 максимум ~8
  }

  // Настройка AP
  Serial.print("Starting AP: SSID='");
  Serial.print(ssid);
  Serial.print("', IP=");
  Serial.print(local_ip);
  Serial.println("");

  bool result;
  if (strlen(password) == 0) {
    result = WiFi.softAP(ssid, nullptr, channel, hidden, maxClients);
  } else {
    result = WiFi.softAP(ssid, password, channel, hidden, maxClients);
  }

  if (result) {
    WiFi.softAPConfig(local_ip, gateway_ip, subnet_mask);
    Serial.print("AP started. IP: ");
    Serial.println(WiFi.softAPIP());
  } else {
    Serial.println("apConfig: failed to start AP");
  }
}
void ipStaFunc(WordEntry* self, WordEntry** body, int* ip) {
  // Получаем IP в режиме STA
  IPAddress staIP = WiFi.localIP();

  // Преобразуем в строку
  String ipStr = staIP.toString();

  // Создаём литерал-строку с этим IP
  String token = "\"" + ipStr + "\"";
  WordEntry* lit = getOrCreateLiteral(token);

  if (lit) {
    dataStack.push_back(lit);
  } else {
    // На случай ошибки — кладём "0.0.0.0"
    WordEntry* fallback = getOrCreateLiteral("\"0.0.0.0\"");
    if (fallback) dataStack.push_back(fallback);
  }
}
void dbmFunc(WordEntry* self, WordEntry** body, int* ip) {
  int32_t rssi = WiFi.RSSI(); // Возвращает int, обычно от -100 до -1

  // Создаём литерал-число
  String token = String(rssi);
  WordEntry* lit = getOrCreateLiteral(token);

  if (lit) {
    dataStack.push_back(lit);
  } else {
    // На случай ошибки — кладём 0
    WordEntry* fallback = getOrCreateLiteral("0");
    if (fallback) dataStack.push_back(fallback);
  }
}
