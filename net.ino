#ifdef ESP8266
#include <ESP8266WiFi.h>
#else
#include <WiFi.h>
#endif

void wifiInit() {
  executeLine("network cont");
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

// === udp.ino — UDP интерфейс для HDL ===
// Максимум 8 сокетов. Каждое слово имеет фиксированное число аргументов.
#include <WiFiUdp.h>

// === Структура сокета ===
struct UdpSocket {
    WiFiUDP udp;
    uint16_t port;
    bool active;
    uint32_t timeout_ms;
    bool broadcast_enabled;
    IPAddress multicast_ip;
    bool is_multicast;
    IPAddress last_remote_ip;
    uint16_t last_remote_port;
    int pending_size; // кэш parsePacket(), -1 = нет пакета
};

static UdpSocket g_udp_sockets[8];

// === Локальные вспомогательные функции стека ===
static inline void udp_pushUInt8(uint8_t val) {
    uint8_t buf[2] = {4, val};
    stack_push(buf, 2);
}

static inline void udp_pushUInt16(uint16_t val) {
    uint8_t buf[3] = {6, (uint8_t)(val & 0xFF), (uint8_t)(val >> 8)};
    stack_push(buf, 3);
}

static inline void udp_pushBool(bool v) {
    uint8_t b[1] = {(uint8_t)v};
    stack_push(b, 1);
}

static inline void udp_pushString(const char* s) {
    uint8_t len = strlen(s);
    if (len > 253) len = 253;
    uint8_t buf[256];
    buf[0] = 0x0E;
    buf[1] = len;
    if (len > 0) memcpy(&buf[2], s, len);
    stack_push(buf, 2 + len);
}

static inline bool udp_popUInt32(uint32_t& out) {
    if (stack_is_empty()) return false;
    uint8_t* top = &stack_mem[stack_ptr];
    if (top[0] < 4 || top[0] > 11) return false;
    uint16_t sz = elem_size(top);
    if (sz < 2) return false;
    out = 0;
    for (uint16_t i = 0; i < sz - 1 && i < 4; i++) {
        out |= (uint32_t)top[1 + i] << (i * 8);
    }
    stack_ptr += sz;
    return true;
}

static inline bool udp_popString(String& out) {
    if (stack_is_empty()) return false;
    uint8_t* top = &stack_mem[stack_ptr];
    if (top[0] != 0x0D && top[0] != 0x0E) return false;
    uint8_t len = top[1];
    out = String((char*)&top[2], len);
    stack_ptr += elem_size(top);
    return true;
}

// Вспомогательная: снять массив со стека (при ошибке, чтобы не оставлять мусор)
static inline void udp_discardArray() {
    if (stack_is_empty()) return;
    uint8_t* arr = &stack_mem[stack_ptr];
    if (arr[0] == 17 || arr[0] == 20) {
        stack_ptr += elem_size(arr);
    }
}

// ============================================================
// 1. udp.Open : port → socket(u8)
//    R2L: 1900 → udp.Open
// ============================================================
void udpOpenFunc() {
    uint32_t port32 = 0;
    if (!udp_popUInt32(port32)) { udp_pushUInt8(0xFF); return; }

    int8_t idx = -1;
    for (int i = 0; i < 8; i++) {
        if (!g_udp_sockets[i].active) { idx = i; break; }
    }
    if (idx == -1) { udp_pushUInt8(0xFF); return; }

    UdpSocket& s = g_udp_sockets[idx];
    s.active = true;
    s.port = (uint16_t)port32;
    s.broadcast_enabled = false;
    s.timeout_ms = 1000;
    s.is_multicast = false;
    s.last_remote_ip = IPAddress(0, 0, 0, 0);
    s.last_remote_port = 0;
    s.pending_size = -1;
    s.udp.setTimeout(s.timeout_ms);

    if (s.udp.begin((uint16_t)port32)) {
        udp_pushUInt8((uint8_t)idx);
    } else {
        s.active = false;
        udp_pushUInt8(0xFF);
    }
}

// ============================================================
// 2. udp.Multicast : port ip_string → socket(u8)
//    R2L: "239.255.255.250" → 1900 → udp.Multicast
// ============================================================
void udpMulticastFunc() {
    uint32_t port32 = 0;
    if (!udp_popUInt32(port32)) { udp_pushUInt8(0xFF); return; }

    String ip_str;
    if (!udp_popString(ip_str)) { udp_pushUInt8(0xFF); return; }

    int8_t idx = -1;
    for (int i = 0; i < 8; i++) {
        if (!g_udp_sockets[i].active) { idx = i; break; }
    }
    if (idx == -1) { udp_pushUInt8(0xFF); return; }

    IPAddress m_ip;
    if (!m_ip.fromString(ip_str.c_str())) { udp_pushUInt8(0xFF); return; }

    UdpSocket& s = g_udp_sockets[idx];
    s.active = true;
    s.port = (uint16_t)port32;
    s.broadcast_enabled = false;
    s.timeout_ms = 1000;
    s.is_multicast = true;
    s.multicast_ip = m_ip;
    s.last_remote_ip = IPAddress(0, 0, 0, 0);
    s.last_remote_port = 0;
    s.pending_size = -1;
    s.udp.setTimeout(s.timeout_ms);

    if (s.udp.beginMulticast(m_ip, (uint16_t)port32)) {
        udp_pushUInt8((uint8_t)idx);
    } else {
        s.active = false;
        udp_pushUInt8(0xFF);
    }
}

// ============================================================
// 3. udp.Close : socket → bool
//    R2L: sock → udp.Close
// ============================================================
void udpCloseFunc() {
    uint32_t v = 0;
    if (!udp_popUInt32(v) || v >= 8 || !g_udp_sockets[v].active) {
        udp_pushBool(false); return;
    }
    g_udp_sockets[v].udp.stop();
    g_udp_sockets[v].active = false;
    g_udp_sockets[v].pending_size = -1;
    udp_pushBool(true);
}

// ============================================================
// 4. udp.Send : socket data_string → bool
//    R2L: "Hello" → sock → udp.Send
// ============================================================
void udpSendFunc() {
    uint32_t v = 0;
    if (!udp_popUInt32(v) || v >= 8 || !g_udp_sockets[v].active) {
        udp_pushBool(false); return;
    }
    String data;
    if (!udp_popString(data)) { udp_pushBool(false); return; }

    UdpSocket& s = g_udp_sockets[v];
    IPAddress target = s.last_remote_ip;
    uint16_t tport = s.last_remote_port;

    if (target == IPAddress(0, 0, 0, 0)) {
        if (s.broadcast_enabled) {
            target = IPAddress(255, 255, 255, 255);
            tport = s.port;
        } else {
            udp_pushBool(false); return;
        }
    }

    s.udp.beginPacket(target, tport);
    s.udp.write((const uint8_t*)data.c_str(), data.length());
    udp_pushBool(s.udp.endPacket() > 0);
}

// ============================================================
// 5. udp.SendTo : socket ip port data_string → bool
//    R2L: "Data" → 8080 → "192.168.1.50" → sock → udp.SendTo
// ============================================================
void udpSendToFunc() {
    uint32_t v = 0;
    if (!udp_popUInt32(v) || v >= 8 || !g_udp_sockets[v].active) {
        udp_pushBool(false); return;
    }
    String ip_str;
    if (!udp_popString(ip_str)) { udp_pushBool(false); return; }
    uint32_t port32 = 0;
    if (!udp_popUInt32(port32)) { udp_pushBool(false); return; }
    String data;
    if (!udp_popString(data)) { udp_pushBool(false); return; }

    IPAddress ip;
    if (!ip.fromString(ip_str.c_str())) { udp_pushBool(false); return; }

    UdpSocket& s = g_udp_sockets[v];
    s.last_remote_ip = ip;
    s.last_remote_port = (uint16_t)port32;

    s.udp.beginPacket(ip, (uint16_t)port32);
    s.udp.write((const uint8_t*)data.c_str(), data.length());
    udp_pushBool(s.udp.endPacket() > 0);
}

// ============================================================
// 6. udp.Available : socket → u16
//    R2L: sock → udp.Available
//    Вызывает parsePacket() и кэширует результат!
// ============================================================
void udpAvailableFunc() {
    uint32_t v = 0;
    if (!udp_popUInt32(v) || v >= 8 || !g_udp_sockets[v].active) {
        udp_pushUInt16(0); return;
    }
    UdpSocket& s = g_udp_sockets[v];
    int sz = s.udp.parsePacket();
    s.pending_size = sz;
    if (sz > 0) {
        s.last_remote_ip = s.udp.remoteIP();
        s.last_remote_port = s.udp.remotePort();
        udp_pushUInt16((uint16_t)sz);
    } else {
        udp_pushUInt16(0);
    }
}

// ============================================================
// 7. udp.Recv : socket → string
//    R2L: sock → udp.Recv
//    Использует кэш из udp.Available если есть
// ============================================================
void udpRecvFunc() {
    uint32_t v = 0;
    if (!udp_popUInt32(v) || v >= 8 || !g_udp_sockets[v].active) {
        udp_pushString(""); return;
    }
    UdpSocket& s = g_udp_sockets[v];

    // Используем кэш, если Available уже вызывался
    int sz = s.pending_size;
    if (sz <= 0) {
        sz = s.udp.parsePacket();
    }
    s.pending_size = -1; // пакет потреблён

    if (sz > 0) {
        s.last_remote_ip = s.udp.remoteIP();
        s.last_remote_port = s.udp.remotePort();
        char buf[1500];
        int len = s.udp.read(buf, (sz > 1499) ? 1499 : sz);
        buf[len] = '\0';
        udp_pushString(buf);
    } else {
        udp_pushString("");
    }
}

// ============================================================
// 8. udp.RecvFrom : socket → string ip_string port(u16)
//    R2L: sock → udp.RecvFrom
//    Возвращает: data (верх), ip (середина), port (низ)
// ============================================================
void udpRecvFromFunc() {
    uint32_t v = 0;
    if (!udp_popUInt32(v) || v >= 8 || !g_udp_sockets[v].active) {
        udp_pushUInt16(0);
        udp_pushString("0.0.0.0");
        udp_pushString("");
        return;
    }
    UdpSocket& s = g_udp_sockets[v];

    int sz = s.pending_size;
    if (sz <= 0) {
        sz = s.udp.parsePacket();
    }
    s.pending_size = -1;

    if (sz > 0) {
        s.last_remote_ip = s.udp.remoteIP();
        s.last_remote_port = s.udp.remotePort();
        char buf[1500];
        int len = s.udp.read(buf, (sz > 1499) ? 1499 : sz);
        buf[len] = '\0';

        udp_pushUInt16(s.last_remote_port);
        udp_pushString(s.last_remote_ip.toString().c_str());
        udp_pushString(buf);
    } else {
        udp_pushUInt16(0);
        udp_pushString("0.0.0.0");
        udp_pushString("");
    }
}

// ============================================================
// 9. udp.Broadcast : socket bool → bool
//    R2L: true → sock → udp.Broadcast
// ============================================================
void udpBroadcastFunc() {
    uint32_t v = 0;
    if (!udp_popUInt32(v) || v >= 8 || !g_udp_sockets[v].active) {
        udp_pushBool(false); return;
    }
    uint32_t flag = 0;
    if (!udp_popUInt32(flag)) { udp_pushBool(false); return; }
    g_udp_sockets[v].broadcast_enabled = (flag != 0);
    udp_pushBool(true);
}

// ============================================================
// 10. udp.Timeout : socket timeout_ms → bool
//     R2L: 1000 → sock → udp.Timeout
// ============================================================
void udpTimeoutFunc() {
    uint32_t v = 0;
    if (!udp_popUInt32(v) || v >= 8 || !g_udp_sockets[v].active) {
        udp_pushBool(false); return;
    }
    uint32_t ms = 0;
    if (!udp_popUInt32(ms)) { udp_pushBool(false); return; }
    g_udp_sockets[v].timeout_ms = ms;
    g_udp_sockets[v].udp.setTimeout(ms);
    udp_pushBool(true);
}

// ============================================================
// 11. udp.ReadArray : socket array_ref → u16 (bytes_read)
//     R2L: myArray → sock → udp.ReadArray
//     Читает сырые байты прямо в data_pool
// ============================================================
void udpReadArrayFunc() {
    uint32_t v = 0;
    if (!udp_popUInt32(v) || v >= 8 || !g_udp_sockets[v].active) {
        udp_discardArray();
        udp_pushUInt16(0); return;
    }

    if (stack_is_empty()) { udp_pushUInt16(0); return; }
    uint8_t* arr = &stack_mem[stack_ptr];
    if (arr[0] != 17 && arr[0] != 20) {
        udp_discardArray();
        udp_pushUInt16(0); return;
    }
    uint16_t arr_sz = elem_size(arr);
    uint16_t base = arr[1] | (arr[2] << 8);
    uint16_t max_len = arr[3] | (arr[4] << 8);
    stack_ptr += arr_sz;

    UdpSocket& s = g_udp_sockets[v];

    int sz = s.pending_size;
    if (sz <= 0) {
        sz = s.udp.parsePacket();
    }
    s.pending_size = -1;

    if (sz > 0) {
        s.last_remote_ip = s.udp.remoteIP();
        s.last_remote_port = s.udp.remotePort();
        uint16_t rlen = ((uint16_t)sz > max_len) ? max_len : (uint16_t)sz;
        s.udp.read(&data_pool[base], rlen);
        udp_pushUInt16(rlen);
    } else {
        udp_pushUInt16(0);
    }
}

// ============================================================
// 12. udp.SendArray : socket array_ref → bool
//     R2L: myArray → sock → udp.SendArray
//     Отправляет сырые байты из data_pool
// ============================================================
void udpSendArrayFunc() {
    uint32_t v = 0;
    if (!udp_popUInt32(v) || v >= 8 || !g_udp_sockets[v].active) {
        udp_discardArray();
        udp_pushBool(false); return;
    }

    if (stack_is_empty()) { udp_pushBool(false); return; }
    uint8_t* arr = &stack_mem[stack_ptr];
    if (arr[0] != 17 && arr[0] != 20) {
        udp_discardArray();
        udp_pushBool(false); return;
    }
    uint16_t arr_sz = elem_size(arr);
    uint16_t base = arr[1] | (arr[2] << 8);
    uint16_t arr_len = arr[3] | (arr[4] << 8);
    uint8_t tp = arr[5];
    stack_ptr += arr_sz;

    uint8_t esz = type_registry[tp].size;
    uint32_t total = (uint32_t)arr_len * esz;
    if (total == 0 || base + total > DATA_POOL_SIZE) {
        udp_pushBool(false); return;
    }

    UdpSocket& s = g_udp_sockets[v];
    IPAddress target = s.last_remote_ip;
    uint16_t tport = s.last_remote_port;

    if (target == IPAddress(0, 0, 0, 0)) {
        if (s.broadcast_enabled) {
            target = IPAddress(255, 255, 255, 255);
            tport = s.port;
        } else {
            udp_pushBool(false); return;
        }
    }

    s.udp.beginPacket(target, tport);
    s.udp.write(&data_pool[base], total);
    udp_pushBool(s.udp.endPacket() > 0);
}

// ============================================================
// 13. udp.SendArrayTo : socket ip port array_ref → bool
//     R2L: myArray → 8080 → "192.168.1.50" → sock → udp.SendArrayTo
// ============================================================
void udpSendArrayToFunc() {
    uint32_t v = 0;
    if (!udp_popUInt32(v) || v >= 8 || !g_udp_sockets[v].active) {
        udp_discardArray();
        udp_pushBool(false); return;
    }

    String ip_str;
    if (!udp_popString(ip_str)) { udp_pushBool(false); return; }

    uint32_t port32 = 0;
    if (!udp_popUInt32(port32)) { udp_pushBool(false); return; }

    if (stack_is_empty()) { udp_pushBool(false); return; }
    uint8_t* arr = &stack_mem[stack_ptr];
    if (arr[0] != 17 && arr[0] != 20) {
        udp_discardArray();
        udp_pushBool(false); return;
    }
    uint16_t arr_sz = elem_size(arr);
    uint16_t base = arr[1] | (arr[2] << 8);
    uint16_t arr_len = arr[3] | (arr[4] << 8);
    uint8_t tp = arr[5];
    stack_ptr += arr_sz;

    IPAddress ip;
    if (!ip.fromString(ip_str.c_str())) { udp_pushBool(false); return; }

    uint8_t esz = type_registry[tp].size;
    uint32_t total = (uint32_t)arr_len * esz;
    if (total == 0 || base + total > DATA_POOL_SIZE) {
        udp_pushBool(false); return;
    }

    UdpSocket& s = g_udp_sockets[v];
    s.last_remote_ip = ip;
    s.last_remote_port = (uint16_t)port32;

    s.udp.beginPacket(ip, (uint16_t)port32);
    s.udp.write(&data_pool[base], total);
    udp_pushBool(s.udp.endPacket() > 0);
}

// ============================================================
// Регистрация слов в контексте udp
// ============================================================
void udpInit() {
    executeLine("udp cont");
    addInternalWord("udp.Open",        udpOpenFunc);
    addInternalWord("udp.Multicast",   udpMulticastFunc);
    addInternalWord("udp.Close",       udpCloseFunc);
    addInternalWord("udp.Send",        udpSendFunc);
    addInternalWord("udp.SendTo",      udpSendToFunc);
    addInternalWord("udp.Available",   udpAvailableFunc);
    addInternalWord("udp.Recv",        udpRecvFunc);
    addInternalWord("udp.RecvFrom",    udpRecvFromFunc);
    addInternalWord("udp.Broadcast",   udpBroadcastFunc);
    addInternalWord("udp.Timeout",     udpTimeoutFunc);
    addInternalWord("udp.ReadArray",   udpReadArrayFunc);
    addInternalWord("udp.SendArray",   udpSendArrayFunc);
    addInternalWord("udp.SendArrayTo", udpSendArrayToFunc);
    executeLine("main");
}
