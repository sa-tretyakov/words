#ifdef ESP8266
#include <ESP8266WiFi.h>
#else
#include <WiFi.h>
#endif

void wifiInit() {
  executeLine("cont network");
  addInternalWord("modeSta", modeStaFunc);
  addInternalWord("modeAp", modeApFunc);
  addInternalWord("modeStaAp", modeStaApFunc);
  addInternalWord("onSta", wifiFunc);
  addInternalWord("dbm", dbmFunc);
  addInternalWord("ipSta", ipStaFunc);
  addInternalWord("onAp", onApFunc);
  addInternalWord("setAp", setApFunc);
  addInternalWord("apConfig", apConfigFunc);
  addInternalWord("ipAp", ipApFunc);
  addInternalWord("scan", scanFunc);
  addInternalWord("wifiOff", wifiOffFunc);
  executeLine("main"); // Вернуться в корневой контекст
}


void wifiFunc() { // onSta: ssid password → BOOL
  // 1. Читаем SSID (должен быть на верхушке стека)
  if (stack_is_empty()) {
    currentOutput->println("⚠️ onSta: SSID (string) expected");
    pushBool(false);
    return;
  }
  
  uint8_t* top = &stack_mem[stack_ptr];
  if (top[0] != 0x0E) { // Проверяем тег STRING
    currentOutput->println("⚠️ onSta: SSID (string) expected");
    pushBool(false);
    return;
  }
  
  uint8_t ssidLen = top[1];
  if (ssidLen == 0 || ssidLen > 63) {
    currentOutput->println("⚠️ onSta: invalid SSID length");
    pushBool(false);
    return;
  }
  
  char ssid[65];
  memcpy(ssid, &top[2], ssidLen);
  ssid[ssidLen] = '\0'; // Добавляем null-terminator
  
  uint16_t ssidSize = elem_size(top);
  stack_ptr += ssidSize; // Снимаем SSID со стека

  // 2. Читаем пароль
  if (stack_is_empty()) {
    currentOutput->println("⚠️ onSta: password (string) expected");
    pushBool(false);
    return;
  }
  
  top = &stack_mem[stack_ptr];
  if (top[0] != 0x0E) {
    currentOutput->println("⚠️ onSta: password (string) expected");
    pushBool(false);
    return;
  }
  
  uint8_t passLen = top[1];
  if (passLen > 63) {
    currentOutput->println("⚠️ onSta: password too long");
    pushBool(false);
    return;
  }
  
  char password[65];
  memcpy(password, &top[2], passLen);
  password[passLen] = '\0';
  
  uint16_t passSize = elem_size(top);
  stack_ptr += passSize; // Снимаем пароль со стека

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
    currentOutput->printf("⚠️ onSta: network '%s' not found\n", ssid);
    pushBool(false);
    return;
  }

  // 5. Подключаемся
  WiFi.begin(ssid, password);

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
void dbmFunc() {
  pushInt32(WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : -1000);
}
void ipStaFunc() {
  pushStringRaw(WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString().c_str() : "0.0.0.0");
}

void onApFunc() {
  String pass, ssid;
  if (!popString(pass) || !popString(ssid)) {
    pushBool(false);
    return;
  }
  wifi_mode_t mode = WiFi.getMode();
  if (mode != WIFI_MODE_AP && mode != WIFI_MODE_APSTA) WiFi.mode(WIFI_MODE_AP);
  pushBool(WiFi.softAP(ssid.c_str(), pass.length() ? pass.c_str() : nullptr));
}

void setApFunc() {
  wifi_mode_t mode = WiFi.getMode();
  if (mode != WIFI_MODE_AP && mode != WIFI_MODE_APSTA) {
    currentOutput->println("setAp: modeAp/modeStaAp first");
    pushBool(false);
    return;
  }
  bool hidden = false; int32_t ch = 1;
  if (!stack_is_empty()) {
    uint8_t* t = &stack_mem[stack_ptr];
    if (t[0] == 0 || t[0] == 1) {
      hidden = (t[0] == 1);
      stack_ptr++;
    }
  }
  if (!stack_is_empty()) {
    uint8_t* t = &stack_mem[stack_ptr];
    if (t[0] >= 4 && t[0] <= 11) {
      uint32_t v = 0;
      uint16_t d = elem_size(t) - 1;
      for (uint16_t k = 0; k < d && k < 4; k++)v |= (uint32_t)t[1 + k] << (k * 8);
      ch = v;
      stack_ptr += elem_size(t);
    }
  }
  String pass, ssid;
  if (!popString(pass) || !popString(ssid)) {
    pushBool(false);
    return;
  }
  if (ch < 1 || ch > 14) {
    currentOutput->println("⚠️ setAp: channel 1–14");
    pushBool(false);
    return;
  }
  pushBool(WiFi.softAP(ssid.c_str(), pass.length() ? pass.c_str() : nullptr, (int)ch, hidden));
}

void apConfigFunc() {
  wifi_mode_t mode = WiFi.getMode();
  if (mode != WIFI_MODE_AP && mode != WIFI_MODE_APSTA) {
    currentOutput->println("apConfig: modeAp/modeStaAp first");
    pushBool(false);
    return;
  }
  String sn, gw, ip;
  if (!popString(sn) || !popString(gw) || !popString(ip)) {
    pushBool(false);
    return;
  }
  IPAddress L, G, S;
  if (!L.fromString(ip) || !G.fromString(gw) || !S.fromString(sn)) {
    currentOutput->println("⚠️ apConfig: invalid IP");
    pushBool(false);
    return;
  }
  pushBool(WiFi.softAPConfig(L, G, S));
}

void ipApFunc() {
  wifi_mode_t mode = WiFi.getMode();
  pushStringRaw((mode == WIFI_MODE_AP || mode == WIFI_MODE_APSTA) ? WiFi.softAPIP().toString().c_str() : "0.0.0.0");
}
void wifiOffFunc() {
  pushBool(WiFi.mode(WIFI_MODE_NULL));
}

void scanFunc() {
  currentOutput->print("{\"networks\":[");
  int n = WiFi.scanNetworks();
  for (int i = 0; i < n; i++) {
    if (i) currentOutput->print(",");
    currentOutput->print("{\"ssid\":\"");
    String s = WiFi.SSID(i);
    for (char c : s) {
      if (c == '"' || c == '\\') currentOutput->print('\\');
      currentOutput->print(c);
    }
    currentOutput->print("\",\"rssi\":"); currentOutput->print(WiFi.RSSI(i)); currentOutput->print("}");
  }
  currentOutput->print("]}"); WiFi.scanDelete();
}

void modeStaFunc() {
  WiFi.mode(WIFI_STA);
  // uint8_t res[1] = {1}; stack_push(res, 1); // push BOOL true
}

void modeApFunc() {
  WiFi.mode(WIFI_AP);
  // uint8_t res[1] = {1}; stack_push(res, 1);
}

void modeStaApFunc() {
#if defined(ESP32)
  WiFi.mode(WIFI_MODE_APSTA);
#else
  WiFi.mode(WIFI_AP_STA);
#endif
  // uint8_t res[1] = {1}; stack_push(res, 1);
}
