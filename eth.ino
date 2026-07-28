// === eth.ino — Бесшовный Ethernet ENC28J60 через esp_netif ===
// Библиотека: https://github.com/Networking-for-Arduino/EthernetESP32
//
// 🔧 УПРАВЛЕНИЕ КОМПИЛЯЦИЕЙ:
//   #define ETH_ENC28J60_ENABLED 0  — выключить (экономит Flash/RAM)

#ifndef ETH_ENC28J60_ENABLED
#define ETH_ENC28J60_ENABLED 1
#endif

#if ETH_ENC28J60_ENABLED

#include <EthernetESP32.h>
#include <SPI.h>

static bool ethInstalled = false;
static uint8_t ethMac[6] = {0x02, 0x00, 0x00, 0x12, 0x34, 0x56};
static ENC28J60Driver* ethDriver = nullptr;

// === eth.Init : mosi miso sck cs → BOOL ===
// R2L: cs → sck → miso → mosi (фиксированно 4 аргумента)
void ethInitFunc() {
    uint8_t mosi, miso, sck, cs;
    
    // Используем НАТИВНЫЙ хелпер ядра popUInt8
    if (!popUInt8(cs) || !popUInt8(sck) || !popUInt8(miso) || !popUInt8(mosi)) {
        pushBool(false); return;
    }

    if (ethDriver) delete ethDriver;
    ethDriver = new ENC28J60Driver(cs, 255, 255); // irq=255, rst=255 (не используются)

    SPI.begin(sck, miso, mosi, cs);
    ethDriver->setSPI(SPI);
    ethDriver->setSpiFreq(10);  // 10 МГц для стабильности ENC28J60

    Ethernet.init(*ethDriver);
    ethInstalled = true;
    pushBool(true);
}

// === eth.InitEx : mosi miso sck cs irq rst → BOOL ===
// R2L: rst → irq → cs → sck → miso → mosi (фиксированно 6 аргументов)
void ethInitExFunc() {
    uint8_t mosi, miso, sck, cs, irq, rst;
    
    // Используем НАТИВНЫЙ хелпер ядра popUInt8
    if (!popUInt8(rst) || !popUInt8(irq) || !popUInt8(cs) || 
        !popUInt8(sck) || !popUInt8(miso) || !popUInt8(mosi)) {
        pushBool(false); return;
    }

    if (ethDriver) delete ethDriver;
    ethDriver = new ENC28J60Driver(cs, (int8_t)irq, (int8_t)rst);

    SPI.begin(sck, miso, mosi, cs);
    ethDriver->setSPI(SPI);
    ethDriver->setSpiFreq(10);

    // Аппаратный сброс (если пин задан и не равен 255)
    if (rst != 255) {
        pinMode(rst, OUTPUT);
        digitalWrite(rst, LOW);
        delay(10);
        digitalWrite(rst, HIGH);
        delay(50);
    }

    // IRQ как вход (если пин задан и не равен 255)
    if (irq != 255) {
        pinMode(irq, INPUT);
    }

    Ethernet.init(*ethDriver);
    ethInstalled = true;
    pushBool(true);
}

// === eth.Mac : b1 b2 b3 b4 b5 b6 → BOOL ===
// R2L: b6 → b5 → b4 → b3 → b2 → b1 (фиксированно 6 байт)
void ethSetMacFunc() {
    uint8_t mac[6];
    // Используем НАТИВНЫЙ хелпер ядра popUInt8
    for (int i = 5; i >= 0; i--) {
        if (!popUInt8(mac[i])) { pushBool(false); return; }
    }
    memcpy(ethMac, mac, 6);
    pushBool(true);
}

// === eth.Begin : → BOOL (DHCP, ожидание до 10 сек) ===
void ethBeginFunc() {
    if (!ethInstalled) { pushBool(false); return; }
    
    int res = Ethernet.begin(ethMac);
    if (!res) {
        for (int i = 0; i < 100; i++) {
            if (Ethernet.linkStatus() != LinkOFF) break;
            delay(100);
        }
        res = Ethernet.begin(ethMac, 10000);
    }
    pushBool(res == 1);
}

// === eth.Static : ip gateway subnet dns → BOOL ===
// R2L: dns → subnet → gateway → ip (фиксированно 4 строки)
void ethStaticFunc() {
    if (!ethInstalled) { pushBool(false); return; }
    String sDns, sSub, sGw, sIp;
    
    // Используем НАТИВНЫЙ хелпер ядра popString
    if (!popString(sDns) || !popString(sSub) || !popString(sGw) || !popString(sIp)) {
        pushBool(false); return;
    }
    
    IPAddress ip, gw, sub, dns;
    if (!ip.fromString(sIp.c_str())   || !gw.fromString(sGw.c_str()) ||
        !sub.fromString(sSub.c_str()) || !dns.fromString(sDns.c_str())) {
        currentOutput->println("eth.Static: неверный формат IP");
        pushBool(false); return;
    }
    
    Ethernet.begin(ethMac, ip, dns, gw, sub);
    pushBool(true);
}

// === eth.Ip : → STRING ===
void ethIpFunc() {
    if (!ethInstalled) { pushStringRaw("0.0.0.0"); return; }
    pushStringRaw(Ethernet.localIP().toString().c_str());
}

// === eth.Link : → BOOL ===
void ethLinkFunc() {
    if (!ethInstalled) { pushBool(false); return; }
    pushBool(Ethernet.linkStatus() == LinkON);
}

// === eth.Deinit : → BOOL ===
void ethDeinitFunc() {
    if (!ethInstalled) { pushBool(false); return; }
    Ethernet.end();
    ethInstalled = false;
    if (ethDriver) {
        delete ethDriver;
        ethDriver = nullptr;
    }
    pushBool(true);
}

// === РЕГИСТРАЦИЯ СЛОВ КОНТЕКСТА eth ===
void ethInit() {
    executeLine("eth cont");
    addInternalWord("eth.Init",    ethInitFunc);     // Инициализация ENC28J60 с явными пинами SPI (mosi miso sck cs).
    addInternalWord("eth.InitEx",  ethInitExFunc);   // Расширенная инициализация с пинами прерывания и сброса (mosi miso sck cs irq rst).
    addInternalWord("eth.Mac",     ethSetMacFunc);   // Установка MAC-адреса интерфейса (6 байт, R2L: b6 b5 b4 b3 b2 b1).
    addInternalWord("eth.Begin",   ethBeginFunc);    // Подключение к сети и получение IP-адреса по DHCP (ожидание до 10 сек).
    addInternalWord("eth.Static",  ethStaticFunc);   // Настройка статического IP-адреса (R2L: dns subnet gateway ip).
    addInternalWord("eth.Ip",      ethIpFunc);       // Возврат текущего IP-адреса интерфейса в виде строки ("192.168.1.42").
    addInternalWord("eth.Link",    ethLinkFunc);     // Проверка физического состояния линка (наличие кабеля и согласование).
    addInternalWord("eth.Deinit",  ethDeinitFunc);   // Полное освобождение ресурсов: остановка esp_netif и удаление драйвера.
    executeLine("main");
}

#else
// ============================================================
// 🔧 ЗАГЛУШКА: модуль отключён на этапе компиляции
// ethInit() остаётся валидной функцией, но ничего не делает.
// ============================================================
void ethInit() {
    // пусто — блок скомпилирован с ETH_ENC28J60_ENABLED = 0
}
#endif
