// === net.ino — WiFi + Ethernet + UDP + TCP ===
// Единый модуль сети.
// 🔒 Условная компиляция: ESP8266 получает только WiFi (без Ethernet и событий)

// === ОПРЕДЕЛЕНИЕ ВОЗМОЖНОСТЕЙ ===
// Ethernet (ENC28J60) работает только на ESP32
#if defined(ESP32)
    #define HDL_HAS_ETHERNET 1
#else
    #define HDL_HAS_ETHERNET 0
#endif

// === УСЛОВНЫЕ INCLUDE ===
#if defined(ESP8266)
    #include <ESP8266WiFi.h>
#else
    #include <WiFi.h>
#endif
#include <WiFiUdp.h>

#if HDL_HAS_ETHERNET
    #include <esp_log.h>
    #include <esp_event.h>
    #ifndef ETH_ENC28J60_ENABLED
        #define ETH_ENC28J60_ENABLED 1
    #endif
    #if ETH_ENC28J60_ENABLED
        #include <EthernetESP32.h>
        #include <SPI.h>
    #endif
#endif
#if defined(ESP32)
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#endif
// ============================================================
// === ГЛОБАЛЬНОЕ СОСТОЯНИЕ ===
// ============================================================
static int32_t g_wifi_channel = 0;

#if HDL_HAS_ETHERNET && ETH_ENC28J60_ENABLED
    bool ethInstalled = false;
    static uint8_t ethMac[6];
    static ENC28J60Driver* ethDriver = nullptr;
#else
    bool ethInstalled = false;
#endif

// ============================================================
// === АДРЕСА ТЕЛ HDL-ПЕРЕМЕННЫХ В dict_pool ===
// ============================================================
static uint16_t addr_w_start = 0, addr_w_conn = 0, addr_w_disc = 0, addr_w_ip = 0;
static uint16_t addr_e_start = 0, addr_e_conn = 0, addr_e_disc = 0, addr_e_ip = 0;

// ============================================================
// === ХЕЛПЕРЫ ===
// ============================================================
static inline void inc_hdl_var(uint16_t body_addr) {
    if (body_addr == 0) return;
    uint32_t v = decode_uint_le(&dict_pool[body_addr + 1], 4);
    encode_uint_le(&dict_pool[body_addr + 1], v + 1, 4);
}

static uint16_t _find_body(const char* name) {
    uint16_t addr = dict_find(name);
    if (addr == 0xFFFF) return 0;
    uint8_t nlen = dict_pool[addr + 2];
    return addr + 5 + nlen;
}

static uint16_t create_u32(const char* name) {
    char cmd[80];
    snprintf(cmd, sizeof(cmd), "%s = 0u32", name);
    executeLine(cmd);
    return _find_body(name);
}

// === БЕЗОПАСНЫЕ WiFi-РЕЖИМЫ (кросс-платформенные) ===
#if defined(ESP8266)
    #define HDL_WIFI_OFF     WIFI_OFF
    #define HDL_WIFI_STA     WIFI_STA
    #define HDL_WIFI_AP      WIFI_AP
    #define HDL_WIFI_APSTA   WIFI_AP_STA
#else
    #define HDL_WIFI_OFF     WIFI_MODE_NULL
    #define HDL_WIFI_STA     WIFI_STA
    #define HDL_WIFI_AP      WIFI_MODE_AP
    #define HDL_WIFI_APSTA   WIFI_MODE_APSTA
#endif

// Безопасное сканирование — сохраняет текущий режим WiFi
static int safeScanNetworks() {
    uint8_t saved_mode = WiFi.getMode();
    if (saved_mode == HDL_WIFI_OFF) {
        WiFi.mode(HDL_WIFI_STA);
        delay(50);
        int n = WiFi.scanNetworks();
        WiFi.mode(HDL_WIFI_OFF);
        return n;
    }
    if (saved_mode == HDL_WIFI_AP) {
#if defined(ESP8266)
        WiFi.mode(WIFI_AP_STA);
#else
        WiFi.mode(WIFI_MODE_APSTA);
#endif
        delay(50);
        int n = WiFi.scanNetworks();
        WiFi.mode(HDL_WIFI_AP);
        return n;
    }
    return WiFi.scanNetworks();
}

// WiFi STA MAC с двоеточиями
void macFunc() {
    uint8_t mac[6];
    WiFi.macAddress(mac);
    char buf[18];
    snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    pushStringRaw(buf);
}

// WiFi STA MAC без двоеточий
void macRawFunc() {
    uint8_t mac[6];
    WiFi.macAddress(mac);
    char buf[13];
    snprintf(buf, sizeof(buf), "%02X%02X%02X%02X%02X%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    pushStringRaw(buf);
}

// WiFi AP MAC с двоеточиями
void macApFunc() {
    uint8_t mac[6];
    WiFi.softAPmacAddress(mac);
    char buf[18];
    snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    pushStringRaw(buf);
}

// Ethernet MAC
void macEthFunc() {
#if HDL_HAS_ETHERNET && ETH_ENC28J60_ENABLED
    if (!ethInstalled) { pushStringRaw("00:00:00:00:00:00"); return; }
    char buf[18];
    snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
             ethMac[0], ethMac[1], ethMac[2], ethMac[3], ethMac[4], ethMac[5]);
    pushStringRaw(buf);
#else
    pushStringRaw("00:00:00:00:00:00");
#endif
}

// chip.id — уникальный ID чипа
void chipIdFunc() {
#if defined(ESP8266)
    uint32_t id = ESP.getChipId();
    char buf[9];
    snprintf(buf, sizeof(buf), "%08X", id);
#else
    uint64_t id = ESP.getEfuseMac() & 0xFFFFFFFFFFFFULL;
    char buf[13];
    snprintf(buf, sizeof(buf), "%012llX", (unsigned long long)id);
#endif
    pushStringRaw(buf);
}
// === АВТО-ГЕНЕРАЦИЯ MAC ДЛЯ ENC28J60 ===
// Использует уникальный MAC чипа ESP32 с префиксом 0x02 (Locally Administered)
#if ETH_ENC28J60_ENABLED
static void generateEthMac() {
#if defined(ESP32)
    uint64_t chipMac = ESP.getEfuseMac();
    ethMac[0] = 0x02; // Locally Administered Address
    ethMac[1] = (chipMac >> 40) & 0xFF;
    ethMac[2] = (chipMac >> 32) & 0xFF;
    ethMac[3] = (chipMac >> 24) & 0xFF;
    ethMac[4] = (chipMac >> 16) & 0xFF;
    ethMac[5] = (chipMac >> 8)  & 0xFF;
#else
    // ESP8266 fallback — используем фиксированный адрес
    ethMac[0] = 0x02;
    ethMac[1] = 0x00;
    ethMac[2] = 0x00;
    ethMac[3] = 0x12;
    ethMac[4] = 0x34;
    ethMac[5] = 0x56;
#endif
}
#endif // ETH_ENC28J60_ENABLED

// ============================================================
// === DNS: Разрешение имён ===
// ============================================================
void dnsResolveFunc() {
    if (stack_is_empty()) { pushStringRaw("0.0.0.0"); return; }
    uint8_t* top = &stack_mem[stack_ptr];
    if (top[0] != 0x0E && top[0] != 0x0D) { pushStringRaw("0.0.0.0"); return; }
    uint8_t len = top[1];
    if (len == 0 || len > 63) { pushStringRaw("0.0.0.0"); return; }
    char host[65];
    memcpy(host, &top[2], len);
    host[len] = '\0';
    stack_ptr += elem_size(top);
    IPAddress ip;
    if (WiFi.hostByName(host, ip)) {
        pushStringRaw(ip.toString().c_str());
    } else {
        pushStringRaw("0.0.0.0");
    }
}

// ============================================================
// === ОБРАБОТЧИК СОБЫТИЙ WiFi + Ethernet ===
// ============================================================
#if defined(ESP32)
static void onWiFiEvent(arduino_event_id_t event, arduino_event_info_t info) {
    switch (event) {
        case ARDUINO_EVENT_WIFI_STA_START:        inc_hdl_var(addr_w_start); break;
        case ARDUINO_EVENT_WIFI_STA_CONNECTED:    inc_hdl_var(addr_w_conn);  break;
        case ARDUINO_EVENT_WIFI_STA_DISCONNECTED: inc_hdl_var(addr_w_disc);  break;
        case ARDUINO_EVENT_WIFI_STA_GOT_IP:       inc_hdl_var(addr_w_ip);    break;
        case ARDUINO_EVENT_ETH_START:             inc_hdl_var(addr_e_start); break;
        case ARDUINO_EVENT_ETH_CONNECTED:         inc_hdl_var(addr_e_conn);  break;
        case ARDUINO_EVENT_ETH_DISCONNECTED:      inc_hdl_var(addr_e_disc);  break;
        case ARDUINO_EVENT_ETH_GOT_IP:            inc_hdl_var(addr_e_ip);    break;
        default: break;
    }
}
#elif defined(ESP8266)
// ESP8266 использует отдельные callback-и для каждого события
static void onEsp8266StaConnected(const WiFiEventStationModeConnected& evt) {
    inc_hdl_var(addr_w_conn);
}
static void onEsp8266StaDisconnected(const WiFiEventStationModeDisconnected& evt) {
    inc_hdl_var(addr_w_disc);
}
static void onEsp8266StaGotIP(const WiFiEventStationModeGotIP& evt) {
    inc_hdl_var(addr_w_ip);
}
#endif

// ============================================================
// === WiFi: режимы ===
// ============================================================
void modeStaFunc()   { WiFi.mode(HDL_WIFI_STA); }
void modeApFunc()    { WiFi.mode(HDL_WIFI_AP); }
void modeStaApFunc() { WiFi.mode(HDL_WIFI_APSTA); }
void wifiOffFunc() {
    WiFi.mode(HDL_WIFI_OFF);
    g_wifi_channel = 0;
}

// ============================================================
// === channel : ch → ===
// ============================================================
void channelFunc() {
    uint32_t ch = 0;
    if (!popUInt32(ch)) { currentOutput->println("channel: number expected"); return; }
    if (ch > 253) { currentOutput->println("channel: invalid (1-253)"); return; }
    g_wifi_channel = (int32_t)ch;
    currentOutput->printf("channel set to %d\n", g_wifi_channel);
}

// ============================================================
// === channel? : ssid → channel(u16) ===
// ============================================================
void channelQueryFunc() {
    if (stack_is_empty()) { pushUInt16(0); return; }
    uint8_t* top = &stack_mem[stack_ptr];
    if (top[0] != 0x0E && top[0] != 0x0D) { pushUInt16(0); return; }
    uint8_t ssidLen = top[1];
    if (ssidLen == 0 || ssidLen > 63) { pushUInt16(0); return; }
    char ssid[65];
    memcpy(ssid, &top[2], ssidLen); ssid[ssidLen] = '\0';
    stack_ptr += elem_size(top);
    int n = safeScanNetworks();
    int best_ch = 0, best_rssi = -1000;
    for (int i = 0; i < n; i++) {
        if (WiFi.SSID(i) == ssid) {
            int rssi = WiFi.RSSI(i);
            int ch   = WiFi.channel(i);
            if (rssi > best_rssi) { best_rssi = rssi; best_ch = ch; }
        }
    }
    WiFi.scanDelete();
    pushUInt16((uint16_t)best_ch);
}

// === scanPrefix ===
void scanPrefixFunc() {
    if (stack_is_empty()) { pushStringRaw(""); return; }
    uint8_t* top = &stack_mem[stack_ptr];
    if (top[0] != 0x0E && top[0] != 0x0D) { pushStringRaw(""); return; }
    uint8_t plen = top[1];
    if (plen == 0 || plen > 63) { pushStringRaw(""); return; }
    char prefix[65];
    memcpy(prefix, &top[2], plen);
    prefix[plen] = '\0';
    stack_ptr += elem_size(top);
    int n = safeScanNetworks();
    int best_i = -1;
    int best_rssi = -1000;
    char best_ssid[33] = {0};
    for (int i = 0; i < n; i++) {
        String s = WiFi.SSID(i);
        const char* ssid = s.c_str();
        if (strncmp(ssid, prefix, plen) == 0) {
            int rssi = WiFi.RSSI(i);
            if (rssi > best_rssi) {
                best_rssi = rssi;
                best_i = i;
                strncpy(best_ssid, ssid, 32);
                best_ssid[32] = '\0';
            }
        }
    }
    WiFi.scanDelete();
    if (best_i >= 0) pushStringRaw(best_ssid);
    else pushStringRaw("");
}

void gwStaFunc() {
    if (WiFi.status() != WL_CONNECTED) {
        pushStringRaw("0.0.0.0");
        return;
    }
    pushStringRaw(WiFi.gatewayIP().toString().c_str());
}

// ============================================================
// === band : ssid "2.4"|"5" → channel(u16) ===
// ============================================================
void bandFunc() {
    if (stack_is_empty()) { pushUInt16(0); return; }
    uint8_t* top = &stack_mem[stack_ptr];
    if (top[0] != 0x0E && top[0] != 0x0D) { pushUInt16(0); return; }
    char band_str[10];
    uint8_t band_len = top[1];
    if (band_len > 9) band_len = 9;
    memcpy(band_str, &top[2], band_len); band_str[band_len] = '\0';
    stack_ptr += elem_size(top);
    if (stack_is_empty()) { pushUInt16(0); return; }
    top = &stack_mem[stack_ptr];
    if (top[0] != 0x0E && top[0] != 0x0D) { pushUInt16(0); return; }
    char ssid[65];
    uint8_t ssidLen = top[1];
    if (ssidLen == 0 || ssidLen > 63) { pushUInt16(0); return; }
    memcpy(ssid, &top[2], ssidLen); ssid[ssidLen] = '\0';
    stack_ptr += elem_size(top);
    int n = safeScanNetworks();
    int best_ch = 0, best_rssi = -1000;
    bool want_5ghz = (strcmp(band_str, "5") == 0 || strcmp(band_str, "5G") == 0);
    for (int i = 0; i < n; i++) {
        if (WiFi.SSID(i) == ssid) {
            int ch = WiFi.channel(i);
            int rssi = WiFi.RSSI(i);
            bool is_5ghz = (ch >= 32 && ch <= 177);
            bool match = want_5ghz ? is_5ghz : !is_5ghz;
            if (match && rssi > best_rssi) { best_rssi = rssi; best_ch = ch; }
        }
    }
    WiFi.scanDelete();
    pushUInt16((uint16_t)best_ch);
}

void setHostnameFunc() {
    if (stack_is_empty()) {
        currentOutput->println(getMsg("setHostname: stack empty"));
        return;
    }
    uint8_t* top = &stack_mem[stack_ptr];
    if (top[0] != 0x0E && top[0] != 0x0D) {
        currentOutput->println(getMsg("setHostname: expected STRING/NAME"));
        return;
    }
    uint8_t len = top[1];
    if (len == 0 || len > 32) {
        currentOutput->println(getMsg("setHostname: length must be 1..32"));
        return;
    }
    
    // === ВАЛИДАЦИЯ И НОРМАЛИЗАЦИЯ ===
    char hostname[33];
    bool has_invalid = false;
    uint8_t out_len = 0;
    
    for (uint8_t i = 0; i < len; i++) {
        char c = (char)top[2 + i];
        
        // Пробел → заменяем на '_'
        if (c == ' ') {
            hostname[out_len++] = '_';
            continue;
        }
        
        // Допустимые символы: a-z, A-Z, 0-9, '-', '_'
        if ((c >= 'a' && c <= 'z') ||
            (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') ||
            c == '-' || c == '_') {
            hostname[out_len++] = c;
            continue;
        }
        
        // Всё остальное — ошибка
        has_invalid = true;
        break;
    }
    
    if (has_invalid) {
        currentOutput->println(getMsg("setHostname: only English letters, digits, '-' and '_' allowed. Use '_' instead of space."));
        stack_ptr += elem_size(top);
        return;
    }
    
    hostname[out_len] = '\0';
    stack_ptr += elem_size(top);
    
    if (out_len == 0) return;
    
// Замените WiFi.setHostname(hostname); на:
#if defined(ESP8266)
WiFi.hostname(hostname);
#else
WiFi.setHostname(hostname);
#endif
#if ETH_ENC28J60_ENABLED
    if (ethInstalled) {
        Ethernet.setHostname(hostname);
    }
#endif
}

// ============================================================
// === onSta : ssid password → BOOL ===
// ============================================================
void wifiFunc() {
    if (stack_is_empty()) { pushBool(false); return; }
    uint8_t* top = &stack_mem[stack_ptr];
    if (top[0] != 0x0E) { pushBool(false); return; }
    uint8_t ssidLen = top[1];
    if (ssidLen == 0 || ssidLen > 63) { pushBool(false); return; }
    char ssid[65];
    memcpy(ssid, &top[2], ssidLen); ssid[ssidLen] = '\0';
    stack_ptr += elem_size(top);
    if (stack_is_empty()) { pushBool(false); return; }
    top = &stack_mem[stack_ptr];
    if (top[0] != 0x0E) { pushBool(false); return; }
    uint8_t passLen = top[1];
    if (passLen > 63) { pushBool(false); return; }
    char password[65];
    memcpy(password, &top[2], passLen); password[passLen] = '\0';
    stack_ptr += elem_size(top);
    WiFi.mode(HDL_WIFI_STA);
    delay(100);
    if (g_wifi_channel > 0) {
        currentOutput->printf("connecting '%s' ch=%d\n", ssid, g_wifi_channel);
        WiFi.begin(ssid, password, g_wifi_channel);
    } else {
        currentOutput->printf("connecting '%s'\n", ssid);
        WiFi.begin(ssid, password);
    }
    for (int i = 0; i < 100; i++) {
        wl_status_t st = WiFi.status();
        if (st == WL_CONNECTED) {
            currentOutput->printf("OK IP=%s\n", WiFi.localIP().toString().c_str());
            pushBool(true);
            return;
        }
        if (st == WL_CONNECT_FAILED || st == WL_NO_SSID_AVAIL) {
            currentOutput->println("onSta: router unreachable");
            pushBool(false);
            return;
        }
        delay(100);
    }
    currentOutput->printf("onSta: timeout '%s'\n", ssid);
    pushBool(false);
}

// ============================================================
// === Информационные слова WiFi ===
// ============================================================
void dbmFunc() { pushInt32(WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : -1000); }
void ipStaFunc() { pushStringRaw(WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString().c_str() : "0.0.0.0"); }

void scanFunc() {
    int n = safeScanNetworks();
    currentOutput->print("{ \"networks\":[ ");
    for (int i = 0; i < n; i++) {
        if (i) currentOutput->print(", ");
        currentOutput->print("{ \"ssid\": \"");
        String s = WiFi.SSID(i);
        for (char c : s) {
            if (c == '"' || c == '\\') currentOutput->print('\\');
            currentOutput->print(c);
        }
        currentOutput->print("\", \"rssi\": ");
        currentOutput->print(WiFi.RSSI(i));
        currentOutput->print(", \"channel\": ");
        currentOutput->print(WiFi.channel(i));
        currentOutput->print(" }");
    }
    currentOutput->print("]} ");
    WiFi.scanDelete();
}

// ============================================================
// === WiFi AP ===
// ============================================================
void onApFunc() {
    String pass, ssid;
    if (!popString(pass) || !popString(ssid)) { pushBool(false); return; }
    uint8_t mode = WiFi.getMode();
    if (mode != HDL_WIFI_AP && mode != HDL_WIFI_APSTA) WiFi.mode(HDL_WIFI_AP);
    pushBool(WiFi.softAP(ssid.c_str(), pass.length() ? pass.c_str() : nullptr));
}

void setApFunc() {
    uint8_t mode = WiFi.getMode();
    if (mode != HDL_WIFI_AP && mode != HDL_WIFI_APSTA) { pushBool(false); return; }
    bool hidden = false; int32_t ch = 1;
    if (!stack_is_empty()) {
        uint8_t* t = &stack_mem[stack_ptr];
        if (t[0] == 0 || t[0] == 1) { hidden = (t[0] == 1); stack_ptr++; }
    }
    if (!stack_is_empty()) {
        uint8_t* t = &stack_mem[stack_ptr];
        if (t[0] >= 4 && t[0] <= 11) { uint32_t v; if (popUInt32(v)) ch = (int32_t)v; }
    }
    String pass, ssid;
    if (!popString(pass) || !popString(ssid)) { pushBool(false); return; }
    if (ch < 1 || ch > 253) { pushBool(false); return; }
    pushBool(WiFi.softAP(ssid.c_str(), pass.length() ? pass.c_str() : nullptr, (int)ch, hidden));
}

void apConfigFunc() {
    uint8_t mode = WiFi.getMode();
    if (mode != HDL_WIFI_AP && mode != HDL_WIFI_APSTA) { pushBool(false); return; }
    String sn, gw, ip;
    if (!popString(sn) || !popString(gw) || !popString(ip)) { pushBool(false); return; }
    IPAddress L, G, S;
    if (!L.fromString(ip.c_str()) || !G.fromString(gw.c_str()) || !S.fromString(sn.c_str())) { pushBool(false); return; }
    pushBool(WiFi.softAPConfig(L, G, S));
}

void ipApFunc() {
    uint8_t mode = WiFi.getMode();
    pushStringRaw((mode == HDL_WIFI_AP || mode == HDL_WIFI_APSTA) ? WiFi.softAPIP().toString().c_str() : "0.0.0.0");
}

// ============================================================
// === Ethernet (ТОЛЬКО ESP32) ===
// ============================================================
#if HDL_HAS_ETHERNET && ETH_ENC28J60_ENABLED
void ethInitFunc() {
    generateEthMac();
    uint8_t mosi, miso, sck, cs;
    if (!popUInt8(cs) || !popUInt8(sck) || !popUInt8(miso) || !popUInt8(mosi)) { pushBool(false); return; }
    if (ethDriver) delete ethDriver;
    ethDriver = new ENC28J60Driver(cs, 255, 255);
    SPI.begin(sck, miso, mosi, cs);
    ethDriver->setSPI(SPI);
    ethDriver->setSpiFreq(10);
    Ethernet.init(*ethDriver);
    ethInstalled = true;
    pushBool(true);
}

void ethInitExFunc() {
    uint8_t mosi, miso, sck, cs, irq, rst;
    if (!popUInt8(rst) || !popUInt8(irq) || !popUInt8(cs) || !popUInt8(sck) || !popUInt8(miso) || !popUInt8(mosi)) { pushBool(false); return; }
    if (ethDriver) delete ethDriver;
    ethDriver = new ENC28J60Driver(cs, (int8_t)irq, (int8_t)rst);
    SPI.begin(sck, miso, mosi, cs);
    ethDriver->setSPI(SPI);
    ethDriver->setSpiFreq(10);
    if (rst != 255) { pinMode(rst, OUTPUT); digitalWrite(rst, LOW); delay(10); digitalWrite(rst, HIGH); delay(50); }
    if (irq != 255) pinMode(irq, INPUT);
    Ethernet.init(*ethDriver);
    ethInstalled = true;
    pushBool(true);
}

void ethSetMacFunc() {
    uint8_t mac[6];
    for (int i = 5; i >= 0; i--) { if (!popUInt8(mac[i])) { pushBool(false); return; } }
    memcpy(ethMac, mac, 6);
    pushBool(true);
}

void ethBeginFunc() {
    if (!ethInstalled) { pushBool(false); return; }
    int res = Ethernet.begin(ethMac);
    if (!res) {
        for (int i = 0; i < 100; i++) { if (Ethernet.linkStatus() != LinkOFF) break; delay(100); }
        res = Ethernet.begin(ethMac, 10000);
    }
    pushBool(res == 1);
}

void ethStaticFunc() {
    if (!ethInstalled) { pushBool(false); return; }
    String sDns, sSub, sGw, sIp;
    if (!popString(sDns) || !popString(sSub) || !popString(sGw) || !popString(sIp)) { pushBool(false); return; }
    IPAddress ip, gw, sub, dns;
    if (!ip.fromString(sIp.c_str()) || !gw.fromString(sGw.c_str()) || !sub.fromString(sSub.c_str()) || !dns.fromString(sDns.c_str())) { pushBool(false); return; }
    Ethernet.begin(ethMac, ip, dns, gw, sub);
    pushBool(true);
}

void ethIpFunc() {
    if (!ethInstalled) { pushStringRaw("0.0.0.0"); return; }
    pushStringRaw(Ethernet.localIP().toString().c_str());
}

void ethLinkFunc() {
    if (!ethInstalled) { pushBool(false); return; }
    pushBool(Ethernet.linkStatus() == LinkON);
}

void ethDeinitFunc() {
    if (!ethInstalled) { pushBool(false); return; }
    Ethernet.end();
    ethInstalled = false;
    if (ethDriver) { delete ethDriver; ethDriver = nullptr; }
    pushBool(true);
}
#endif // HDL_HAS_ETHERNET

// ============================================================
// === status : → u8 (WiFi + Ethernet) ===
// ============================================================
void networkStatusFunc() {
    uint8_t wifi_st = (WiFi.status() == WL_CONNECTED) ? 1 : 0;
    uint8_t eth_st  = 0;
#if HDL_HAS_ETHERNET && ETH_ENC28J60_ENABLED
    if (ethInstalled) {
        if (Ethernet.linkStatus() == LinkON) {
            IPAddress ip = Ethernet.localIP();
            if (ip != IPAddress(0, 0, 0, 0)) eth_st = 1;
        }
    }
#endif
    pushUInt8((uint8_t)(wifi_st + eth_st * 2));
}

// ============================================================
// === wifiInit ===
// ============================================================
void wifiInit() {
#if defined(ESP32)
    esp_log_level_set("wifi", ESP_LOG_NONE);
    esp_log_level_set("wifi_init", ESP_LOG_NONE);
    esp_log_level_set("esp_netif_handlers", ESP_LOG_NONE);
    esp_log_level_set("eth_enc28j60", ESP_LOG_NONE);
    WiFi.onEvent(onWiFiEvent);
#elif defined(ESP8266)
    WiFi.onStationModeConnected(onEsp8266StaConnected);
    WiFi.onStationModeDisconnected(onEsp8266StaDisconnected);
    WiFi.onStationModeGotIP(onEsp8266StaGotIP);
#endif

    focusTo("network");
    addr_w_start = create_u32("wifi.starts");
    addr_w_conn  = create_u32("wifi.connects");
    addr_w_disc  = create_u32("wifi.disconnects");
    addr_w_ip    = create_u32("wifi.ips");
    addr_e_start = create_u32("eth.starts");
    addr_e_conn  = create_u32("eth.connects");
    addr_e_disc  = create_u32("eth.disconnects");
    addr_e_ip    = create_u32("eth.ips");

    addInternalWord("modeSta",   modeStaFunc);
    addInternalWord("modeAp",    modeApFunc);
    addInternalWord("modeStaAp", modeStaApFunc);
    addInternalWord("onSta",     wifiFunc);
    addInternalWord("channel",   channelFunc);
    addInternalWord("channel?",  channelQueryFunc);
    addInternalWord("band",      bandFunc);
    addInternalWord("status",    networkStatusFunc);
    addInternalWord("dbm",       dbmFunc);
    addInternalWord("ipSta",     ipStaFunc);
    addInternalWord("gwSta",     gwStaFunc);
    addInternalWord("onAp",      onApFunc);
    addInternalWord("setAp",     setApFunc);
    addInternalWord("apConfig",  apConfigFunc);
    addInternalWord("ipAp",      ipApFunc);
    addInternalWord("scan",      scanFunc);
    addInternalWord("scanPrefix", scanPrefixFunc);
    addInternalWord("wifiOff",   wifiOffFunc);
    addInternalWord("dns.resolve", dnsResolveFunc);
    addInternalWord("setHostname", setHostnameFunc);
    addInternalWord("mac",       macFunc);
    addInternalWord("mac.raw",   macRawFunc);
    addInternalWord("mac.ap",    macApFunc);
    addInternalWord("mac.eth",   macEthFunc);
    focusTo("main");
}

// ============================================================
// === ethInit (регистрация слов) ===
// ============================================================
void ethInit() {
#if HDL_HAS_ETHERNET && ETH_ENC28J60_ENABLED
    focusTo("eth");
    addInternalWord("eth.Init",   ethInitFunc);
    addInternalWord("eth.InitEx", ethInitExFunc);
    addInternalWord("eth.Mac",    ethSetMacFunc);
    addInternalWord("eth.Begin",  ethBeginFunc);
    addInternalWord("eth.Static", ethStaticFunc);
    addInternalWord("eth.Ip",     ethIpFunc);
    addInternalWord("eth.Link",   ethLinkFunc);
    addInternalWord("eth.Deinit", ethDeinitFunc);
    focusTo("main");
#endif
    // На ESP8266 — просто выходим, ничего не регистрируя.
}

// ============================================================
// === UDP ===
// ============================================================
struct UdpSocket {
    WiFiUDP udp;
    uint16_t port;
    bool active;
    uint32_t timeout_ms;
    IPAddress multicast_ip;
    bool is_multicast;
    int pending_size;
    bool is_streaming;
    char stream_buf[512];
    uint16_t stream_len;
    IPAddress stream_ip;
    uint16_t stream_port;
};

static UdpSocket* g_udp_sockets[8] = {nullptr};

class UdpStreamPrinter : public Print {
public:
    UdpSocket* sock;
    UdpStreamPrinter(UdpSocket* s) : sock(s) {}
    size_t write(uint8_t c) override {
        if (!sock || !sock->is_streaming) return 0;
        if (sock->stream_len < 511) sock->stream_buf[sock->stream_len++] = (char)c;
        return 1;
    }
    size_t write(const uint8_t* buffer, size_t size) override {
        if (!sock || !sock->is_streaming) return 0;
        size_t to_copy = (size < (512 - sock->stream_len)) ? size : (512 - sock->stream_len);
        if (to_copy > 0) {
            memcpy(&sock->stream_buf[sock->stream_len], buffer, to_copy);
            sock->stream_len += to_copy;
        }
        return to_copy;
    }
};

static UdpStreamPrinter* g_udp_stream_printer = nullptr;

void word_out_udp() {
    uint32_t v = 0;
    if (!popUInt32(v) || v >= 8 || !g_udp_sockets[v] || !g_udp_sockets[v]->active) return;
    String ip_str;
    if (!popString(ip_str)) return;
    uint32_t port32 = 0;
    if (!popUInt32(port32)) return;
    IPAddress ip;
    if (!ip.fromString(ip_str.c_str())) return;
    UdpSocket* s = g_udp_sockets[v];
    s->is_streaming = true;
    s->stream_len = 0;
    s->stream_ip = ip;
    s->stream_port = (uint16_t)port32;
    if (!g_udp_stream_printer) {
        g_udp_stream_printer = new UdpStreamPrinter(s);
    } else {
        g_udp_stream_printer->sock = s;
    }
    currentOutput = g_udp_stream_printer;
}

void udpOpenFunc() {
    uint32_t port32 = 0;
    if (!popUInt32(port32)) { pushUInt8(0xFF); return; }
    int8_t idx = -1;
    for (int i = 0; i < 8; i++) { if (!g_udp_sockets[i] || !g_udp_sockets[i]->active) { idx = i; break; } }
    if (idx == -1) { pushUInt8(0xFF); return; }
    if (!g_udp_sockets[idx]) { g_udp_sockets[idx] = new UdpSocket(); if (!g_udp_sockets[idx]) { pushUInt8(0xFF); return; } }
    UdpSocket* s = g_udp_sockets[idx];
    s->active = true; s->port = (uint16_t)port32; s->timeout_ms = 1000; s->is_multicast = false; s->pending_size = -1;
    s->udp.setTimeout(s->timeout_ms);
    if (s->udp.begin((uint16_t)port32)) pushUInt8((uint8_t)idx);
    else { s->active = false; pushUInt8(0xFF); }
}

void udpMulticastFunc() {
    String ip_str;
    if (!popString(ip_str)) { pushUInt8(0xFF); return; }
    uint32_t port32 = 0;
    if (!popUInt32(port32)) { pushUInt8(0xFF); return; }
    int8_t idx = -1;
    for (int i = 0; i < 8; i++) { if (!g_udp_sockets[i] || !g_udp_sockets[i]->active) { idx = i; break; } }
    if (idx == -1) { pushUInt8(0xFF); return; }
    IPAddress m_ip;
    if (!m_ip.fromString(ip_str.c_str())) { pushUInt8(0xFF); return; }
    if (!g_udp_sockets[idx]) { g_udp_sockets[idx] = new UdpSocket(); if (!g_udp_sockets[idx]) { pushUInt8(0xFF); return; } }
    UdpSocket* s = g_udp_sockets[idx];
    s->active = true; s->port = (uint16_t)port32; s->timeout_ms = 1000; s->is_multicast = true; s->multicast_ip = m_ip; s->pending_size = -1;
    s->udp.setTimeout(s->timeout_ms);
// Замените строку с beginMulticast на:
#if defined(ESP8266)
if (s->udp.beginMulticast(WiFi.localIP(), m_ip, (uint16_t)port32)) pushUInt8((uint8_t)idx);
#else
if (s->udp.beginMulticast(m_ip, (uint16_t)port32)) pushUInt8((uint8_t)idx);
#endif
    else { s->active = false; pushUInt8(0xFF); }
}

void udpCloseFunc() {
    uint32_t v = 0;
    if (!popUInt32(v) || v >= 8 || !g_udp_sockets[v] || !g_udp_sockets[v]->active) return;
    g_udp_sockets[v]->udp.stop();
    g_udp_sockets[v]->active = false;
    g_udp_sockets[v]->pending_size = -1;
    delete g_udp_sockets[v];
    g_udp_sockets[v] = nullptr;
    // Ничего не кладём на стек
}

void udpSendFunc() {
    // === 1. ПРОВЕРКА СОКЕТА ===
    uint32_t v = 0;
    if (!popUInt32(v) || v >= 8 || !g_udp_sockets[v] || !g_udp_sockets[v]->active) return;
    
    // === 2. ПРОВЕРКА IP ===
    String ip_str;
    if (!popString(ip_str)) return;
    IPAddress ip;
    if (!ip.fromString(ip_str.c_str())) return;
    
    // === 3. ПРОВЕРКА ПОРТА ===
    uint32_t port32 = 0;
    if (!popUInt32(port32) || port32 > 65535) return;
    
    // === 4. ПРОВЕРКА ДАННЫХ ===
    if (stack_is_empty()) return;
    
    uint8_t* data_ptr = &stack_mem[stack_ptr];
    uint8_t tag = data_ptr[0];
    uint16_t data_sz = elem_size(data_ptr);
    
    const uint8_t* payload = nullptr;
    uint32_t payload_len = 0;
    
    if (tag == 0x0E || tag == 0x0D) {
        payload = &data_ptr[2];
        payload_len = data_ptr[1];
    }
    else if (tag == 15) {
        uint8_t len = data_ptr[1];
        uint16_t addr = data_ptr[2] | (data_ptr[3] << 8);
        if (addr + len <= DATA_POOL_SIZE) { payload = &data_pool[addr]; payload_len = len; }
    }
    else if (tag == 17 || tag == 20) {
        uint16_t base = data_ptr[1] | (data_ptr[2] << 8);
        uint16_t len  = data_ptr[3] | (data_ptr[4] << 8);
        uint8_t esz   = type_registry[data_ptr[5]].size;
        uint32_t total = (uint32_t)len * esz;
        if (base + total <= DATA_POOL_SIZE) { payload = &data_pool[base]; payload_len = total; }
    }
    else if (tag >= 4 && tag <= 11) {
        payload = &data_ptr[1];
        payload_len = data_sz - 1;
    }
    
    stack_ptr += data_sz;
    
    // === 5. НЕВАЛИДНО — ПРОСТО ВЫХОД, НИЧЕГО НЕ ОТПРАВЛЯЕМ ===
    if (!payload || payload_len == 0) return;
    
    // === 6. ОТПРАВКА ===
    UdpSocket* s = g_udp_sockets[v];
    if (s->udp.beginPacket(ip, (uint16_t)port32)) {
        s->udp.write(payload, payload_len);
        s->udp.endPacket();
    }
    // Ничего не кладём на стек
}
void udpAvailableFunc() {
    uint32_t v = 0;
    if (!popUInt32(v) || v >= 8 || !g_udp_sockets[v] || !g_udp_sockets[v]->active) { pushUInt16(0); return; }
    UdpSocket* s = g_udp_sockets[v];
    int sz = s->udp.parsePacket();
    s->pending_size = sz;
    pushUInt16((sz > 0) ? (uint16_t)sz : 0);
}

void udpRecvFunc() {
    uint32_t v = 0;
    if (!popUInt32(v) || v >= 8 || !g_udp_sockets[v] || !g_udp_sockets[v]->active) { pushUInt16(0); pushStringRaw("0.0.0.0"); pushUInt16(0); return; }
    uint16_t base = 0, max_len = 0;
    if (!popAddrInfo(base, max_len)) { pushUInt16(0); pushStringRaw("0.0.0.0"); pushUInt16(0); return; }
    UdpSocket* s = g_udp_sockets[v];
    int sz = s->pending_size;
    if (sz <= 0) sz = s->udp.parsePacket();
    s->pending_size = -1;
    if (sz <= 0) { pushUInt16(0); pushStringRaw("0.0.0.0"); pushUInt16(0); return; }
    uint16_t rlen = ((uint16_t)sz > max_len) ? max_len : (uint16_t)sz;
    if (base + rlen > DATA_POOL_SIZE) { while (s->udp.available()) s->udp.read(); pushUInt16(0); pushStringRaw("0.0.0.0"); pushUInt16(0); return; }
    s->udp.read(&data_pool[base], rlen);
    uint16_t tail = (uint16_t)sz - rlen;
    while (tail > 0) { uint8_t trash[64]; uint16_t chunk = (tail > 64) ? 64 : tail; s->udp.read(trash, chunk); tail -= chunk; }
    pushUInt16(rlen);
    pushStringRaw(s->udp.remoteIP().toString().c_str());
    pushUInt16(s->udp.remotePort());
}

void udpTimeoutFunc() {
    uint32_t v = 0;
    if (!popUInt32(v) || v >= 8 || !g_udp_sockets[v] || !g_udp_sockets[v]->active) { pushBool(false); return; }
    uint32_t ms = 0;
    if (!popUInt32(ms)) { pushBool(false); return; }
    g_udp_sockets[v]->timeout_ms = ms;
    g_udp_sockets[v]->udp.setTimeout(ms);
    pushBool(true);
}

void udp_finalize_streaming() {
    for (int i = 0; i < 8; i++) {
        if (!g_udp_sockets[i] || !g_udp_sockets[i]->active) continue;
        UdpSocket* s = g_udp_sockets[i];
        if (s->is_streaming && s->stream_len > 0) {
            s->udp.beginPacket(s->stream_ip, s->stream_port);
            s->udp.write((uint8_t*)s->stream_buf, s->stream_len);
            s->udp.endPacket();
            s->is_streaming = false;
            s->stream_len = 0;
        }
    }
}

void udpInit() {
    focusTo("udp");
    addInternalWord("udp.Open",      udpOpenFunc);
    addInternalWord("udp.Multicast", udpMulticastFunc);
    addInternalWord("udp.Close",     udpCloseFunc);
    addInternalWord("udp.Send",      udpSendFunc);
    addInternalWord("udp.Available", udpAvailableFunc);
    addInternalWord("udp.Recv",      udpRecvFunc);
    addInternalWord("udp.Timeout",   udpTimeoutFunc);
    focusTo("streams");
    addInternalWord("out>udp",       word_out_udp);
    focusTo("main");
}

// ============================================================
// === TCP ===
// ============================================================
struct TcpSocket {
    WiFiClient* client;
    int serverFd;
    bool active;
    bool is_server;
    uint32_t timeout_ms;
};

static TcpSocket* g_tcp_sockets[8] = {nullptr};

void word_out_tcp() {
    uint32_t v = 0;
    if (!popUInt32(v) || v >= 8 || !g_tcp_sockets[v] || !g_tcp_sockets[v]->active || !g_tcp_sockets[v]->client) return;
    g_stream.attachTcp(g_tcp_sockets[v]->client);
    currentOutput = &g_stream;
}

static int8_t tcp_find_slot() {
    for (int i = 0; i < 8; i++) { if (!g_tcp_sockets[i] || !g_tcp_sockets[i]->active) return i; }
    return -1;
}

static void tcp_cleanup_idx(int8_t idx) {
    if (idx < 0 || idx >= 8 || !g_tcp_sockets[idx]) return;
    TcpSocket* s = g_tcp_sockets[idx];
    if (s->client) { s->client->stop(); delete s->client; s->client = nullptr; }
    
#if defined(ESP32)
    if (s->serverFd >= 0) { ::close(s->serverFd); s->serverFd = -1; }
#endif
    s->active = false;
    s->is_server = false;
}

void tcpConnectFunc() {
    String ip_str;
    if (!popString(ip_str)) { pushUInt8(0xFF); return; }
    uint32_t port32 = 0;
    if (!popUInt32(port32)) { pushUInt8(0xFF); return; }
    int8_t idx = tcp_find_slot();
    if (idx == -1) { pushUInt8(0xFF); return; }
    IPAddress ip;
    if (!ip.fromString(ip_str.c_str())) { pushUInt8(0xFF); return; }
    if (!g_tcp_sockets[idx]) { g_tcp_sockets[idx] = new TcpSocket(); if (!g_tcp_sockets[idx]) { pushUInt8(0xFF); return; } }
    tcp_cleanup_idx(idx);
    TcpSocket* s = g_tcp_sockets[idx];
    s->client = new WiFiClient();
    s->is_server = false;
    s->timeout_ms = 5000;
    s->client->setTimeout(s->timeout_ms);
    if (s->client->connect(ip, (uint16_t)port32)) { s->active = true; pushUInt8((uint8_t)idx); }
    else { delete s->client; s->client = nullptr; s->active = false; pushUInt8(0xFF); }
}
#if defined(ESP32)
void tcpListenFunc() {
    uint32_t port32 = 0;
    if (!popUInt32(port32)) { pushUInt8(0xFF); return; }
    int8_t idx = tcp_find_slot();
    if (idx == -1) { pushUInt8(0xFF); return; }
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) { pushUInt8(0xFF); return; }
    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons((uint16_t)port32);
    if (::bind(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) { ::close(fd); pushUInt8(0xFF); return; }
    if (::listen(fd, 5) < 0) { ::close(fd); pushUInt8(0xFF); return; }
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    if (!g_tcp_sockets[idx]) { g_tcp_sockets[idx] = new TcpSocket(); if (!g_tcp_sockets[idx]) { ::close(fd); pushUInt8(0xFF); return; } }
    tcp_cleanup_idx(idx);
    TcpSocket* s = g_tcp_sockets[idx];
    s->serverFd = fd;
    s->is_server = true;
    s->active = true;
    s->timeout_ms = 5000;
    pushUInt8((uint8_t)idx);
}

void tcpAcceptFunc() {
    uint32_t v = 0;
    if (!popUInt32(v) || v >= 8 || !g_tcp_sockets[v] || !g_tcp_sockets[v]->active || !g_tcp_sockets[v]->is_server) { pushUInt8(0xFF); return; }
    TcpSocket* srv = g_tcp_sockets[v];
    struct sockaddr_in clientAddr;
    socklen_t addrLen = sizeof(clientAddr);
    int clientFd = ::accept(srv->serverFd, (struct sockaddr*)&clientAddr, &addrLen);
    if (clientFd < 0) { pushUInt8(0xFF); return; }
    int8_t idx = tcp_find_slot();
    if (idx == -1) { ::close(clientFd); pushUInt8(0xFF); return; }
    if (!g_tcp_sockets[idx]) { g_tcp_sockets[idx] = new TcpSocket(); if (!g_tcp_sockets[idx]) { ::close(clientFd); pushUInt8(0xFF); return; } }
    tcp_cleanup_idx(idx);
    TcpSocket* s = g_tcp_sockets[idx];
    s->client = new WiFiClient(clientFd);
    s->is_server = false;
    s->active = true;
    s->timeout_ms = srv->timeout_ms;
    s->client->setTimeout(s->timeout_ms);
    pushUInt8((uint8_t)idx);
}
#endif
void tcpSendFunc() {
    uint32_t v = 0;
    if (!popUInt32(v) || v >= 8 || !g_tcp_sockets[v] || !g_tcp_sockets[v]->active || 
        g_tcp_sockets[v]->is_server || !g_tcp_sockets[v]->client) return;
    
    WiFiClient* c = g_tcp_sockets[v]->client;
    if (!c->connected()) return;
    
    if (stack_is_empty()) return;
    
    uint8_t* data_ptr = &stack_mem[stack_ptr];
    uint8_t tag = data_ptr[0];
    uint16_t data_sz = elem_size(data_ptr);
    
    const uint8_t* payload = nullptr;
    uint32_t payload_len = 0;
    
    if (tag == 0x0E || tag == 0x0D) {
        payload = &data_ptr[2];
        payload_len = data_ptr[1];
    }
    else if (tag == 15) {
        uint8_t len = data_ptr[1];
        uint16_t addr = data_ptr[2] | (data_ptr[3] << 8);
        if (addr + len <= DATA_POOL_SIZE) { payload = &data_pool[addr]; payload_len = len; }
    }
    else if (tag == 17 || tag == 20) {
        uint16_t base = data_ptr[1] | (data_ptr[2] << 8);
        uint16_t len  = data_ptr[3] | (data_ptr[4] << 8);
        uint8_t esz   = type_registry[data_ptr[5]].size;
        uint32_t total = (uint32_t)len * esz;
        if (base + total <= DATA_POOL_SIZE) { payload = &data_pool[base]; payload_len = total; }
    }
    else if (tag >= 4 && tag <= 11) {
        payload = &data_ptr[1];
        payload_len = data_sz - 1;
    }
    
    stack_ptr += data_sz;
    
    if (!payload || payload_len == 0) return;
    
    c->write(payload, payload_len);
    // Ничего не кладём на стек
}
void tcpRecvFunc() {
    uint32_t v = 0;
    if (!popUInt32(v) || v >= 8 || !g_tcp_sockets[v] || !g_tcp_sockets[v]->active || g_tcp_sockets[v]->is_server || !g_tcp_sockets[v]->client) { pushUInt16(0); return; }
    uint16_t base = 0, max_len = 0;
    if (!popAddrInfo(base, max_len)) { pushUInt16(0); return; }
    WiFiClient* c = g_tcp_sockets[v]->client;
    int avail = c->available();
    if (avail <= 0) { pushUInt16(0); return; }
    uint16_t rlen = ((uint16_t)avail > max_len) ? max_len : (uint16_t)avail;
    if (base + rlen > DATA_POOL_SIZE) { pushUInt16(0); return; }
    int bytesRead = c->read(&data_pool[base], rlen);
    if (bytesRead < 0) bytesRead = 0;
    pushUInt16((uint16_t)bytesRead);
}

void tcpAvailableFunc() {
    uint32_t v = 0;
    if (!popUInt32(v) || v >= 8 || !g_tcp_sockets[v] || !g_tcp_sockets[v]->active || g_tcp_sockets[v]->is_server || !g_tcp_sockets[v]->client) { pushUInt16(0); return; }
    int avail = g_tcp_sockets[v]->client->available();
    pushUInt16(avail > 0 ? (uint16_t)avail : 0);
}

void tcpConnectedFunc() {
    uint32_t v = 0;
    if (!popUInt32(v) || v >= 8 || !g_tcp_sockets[v] || !g_tcp_sockets[v]->active || g_tcp_sockets[v]->is_server || !g_tcp_sockets[v]->client) { pushBool(false); return; }
    pushBool(g_tcp_sockets[v]->client->connected());
}

void tcpCloseFunc() {
    uint32_t v = 0;
    if (!popUInt32(v) || v >= 8 || !g_tcp_sockets[v] || !g_tcp_sockets[v]->active) { pushBool(false); return; }
    tcp_cleanup_idx((int8_t)v);
    delete g_tcp_sockets[v];
    g_tcp_sockets[v] = nullptr;
    pushBool(true);
}

void tcpTimeoutFunc() {
    uint32_t v = 0;
    if (!popUInt32(v) || v >= 8 || !g_tcp_sockets[v] || !g_tcp_sockets[v]->active) { pushBool(false); return; }
    uint32_t ms = 0;
    if (!popUInt32(ms)) { pushBool(false); return; }
    g_tcp_sockets[v]->timeout_ms = ms;
    if (g_tcp_sockets[v]->client) g_tcp_sockets[v]->client->setTimeout(ms);
    pushBool(true);
}

void tcpRemoteIpFunc() {
    uint32_t v = 0;
    if (!popUInt32(v) || v >= 8 || !g_tcp_sockets[v] || !g_tcp_sockets[v]->active || g_tcp_sockets[v]->is_server || !g_tcp_sockets[v]->client) { pushStringRaw("0.0.0.0"); return; }
    pushStringRaw(g_tcp_sockets[v]->client->remoteIP().toString().c_str());
}

void tcpRemotePortFunc() {
    uint32_t v = 0;
    if (!popUInt32(v) || v >= 8 || !g_tcp_sockets[v] || !g_tcp_sockets[v]->active || g_tcp_sockets[v]->is_server || !g_tcp_sockets[v]->client) { pushUInt16(0); return; }
    pushUInt16(g_tcp_sockets[v]->client->remotePort());
}

void reqQuestionFunc() {
    uint16_t base = 0, len = 0;
    if (!popAddrInfo(base, len)) { pushBool(false); return; }
    if (len < 4 || base + len > DATA_POOL_SIZE) { pushBool(false); return; }
    const char* methods[] = { "GET ", "POST ", "PUT ", "DELETE ", "HEAD ", "OPTIONS ", "PATCH " };
    const uint8_t method_lens[] = {4, 5, 4, 7, 5, 8, 6};
    for (int i = 0; i < 7; i++) {
        if (len >= method_lens[i] && memcmp(&data_pool[base], methods[i], method_lens[i]) == 0) { pushBool(true); return; }
    }
    pushBool(false);
}

void reqLineFunc() {
    uint16_t base = 0, len = 0;
    if (!popAddrInfo(base, len)) { pushBool(false); pushStringRaw(""); pushStringRaw(""); return; }
    uint16_t line_end = 0;
    while (line_end < len) {
        uint8_t c = data_pool[base + line_end];
        if (c == '\r' || c == '\n') break;
        line_end++;
    }
    if (line_end == 0 || line_end >= len) { pushBool(false); pushStringRaw(""); pushStringRaw(""); return; }
    const uint8_t* data = &data_pool[base];
    struct Method { const char* name; uint8_t len; };
    static const Method methods[] = { {"GET",3},{"POST",4},{"PUT",3},{"DELETE",6},{"HEAD",4},{"OPTIONS",7},{"PATCH",5} };
    int method_idx = -1;
    for (int i = 0; i < 7; i++) {
        uint8_t mlen = methods[i].len;
        if (line_end > mlen && data[mlen] == ' ' && memcmp(data, methods[i].name, mlen) == 0) { method_idx = i; break; }
    }
    if (method_idx < 0) { pushBool(false); pushStringRaw(""); pushStringRaw(""); return; }
    uint8_t mlen = methods[method_idx].len;
    uint16_t p_start = mlen + 1;
    uint16_t p_end = p_start;
    bool has_params = false;
    while (p_end < line_end) {
        uint8_t c = data[p_end];
        if (c == ' ' || c == '\r' || c == '\n') break;
        if (c == '?') { has_params = true; break; }
        p_end++;
    }
    uint16_t path_len = p_end - p_start;
    if (path_len > 255) path_len = 255;
    uint8_t path_buf[257];
    path_buf[0] = 0x0E;
    path_buf[1] = (uint8_t)path_len;
    if (path_len > 0) memcpy(&path_buf[2], &data[p_start], path_len);
    uint8_t method_buf[10];
    method_buf[0] = 0x0E;
    method_buf[1] = mlen;
    memcpy(&method_buf[2], methods[method_idx].name, mlen);
    stack_push(path_buf, 2 + path_len);
    stack_push(method_buf, 2 + mlen);
    pushBool(has_params);
}

void tcpInit() {
    focusTo("tcp");
    addInternalWord("tcp.Connect",    tcpConnectFunc);
#if defined(ESP32)
    addInternalWord("tcp.Listen",     tcpListenFunc);
    addInternalWord("tcp.Accept",     tcpAcceptFunc);
#endif
    addInternalWord("tcp.Send",       tcpSendFunc);
    addInternalWord("tcp.Recv",       tcpRecvFunc);
    addInternalWord("tcp.Available",  tcpAvailableFunc);
    addInternalWord("tcp.Connected",  tcpConnectedFunc);
    addInternalWord("tcp.Close",      tcpCloseFunc);
    addInternalWord("tcp.Timeout",    tcpTimeoutFunc);
    addInternalWord("tcp.RemoteIp",   tcpRemoteIpFunc);
    addInternalWord("tcp.RemotePort", tcpRemotePortFunc);
    addInternalWord("req?",           reqQuestionFunc);
    addInternalWord("req.line",       reqLineFunc);
    focusTo("streams");
    addInternalWord("out>tcp",        word_out_tcp);
    focusTo("main");
}
