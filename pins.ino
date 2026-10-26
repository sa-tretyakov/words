// === pins.ino — GPIO, звук, LEDC, I2C (ESP32 Core 3.x, все семейства) ===
//
// Поддерживаемые чипы: ESP32, ESP32-S2, ESP32-S3, ESP32-C3, ESP32-C5, ESP32-C6, ESP32-H2
// Требуется: ESP_ARDUINO_VERSION_MAJOR >= 3 (Arduino Core 3.x)
//
// 🔒 УСЛОВНАЯ КОМПИЛЯЦИЯ через #ifdef:
// По умолчанию ВСЕ блоки ВКЛЮЧЕНЫ (строки раскомментированы).
// Чтобы ОТКЛЮЧИТЬ ненужный блок — закомментируйте соответствующую строку:

//#define HDL_ENABLE_ADC       // ADC API (оптимизированный для классического ESP32)
//#define HDL_ENABLE_OW        // 1-Wire (ow.*)
#define HDL_ENABLE_GPIO      // Базовые GPIO (pinMode, digitalRead, analogRead, ...)
#define HDL_ENABLE_SOUND     // Звук (tone, beep, noTone)
//#define HDL_ENABLE_LEDC      // Аппаратный PWM (ledcSetup, ledcAttach, ledcWrite)
#define HDL_ENABLE_I2C       // I2C (Wire)

#include <Arduino.h>

// ============================================================
// === БЛОК 1: ADC API ДЛЯ ESP32 CORE 3.x ===
// ============================================================
#ifdef HDL_ENABLE_ADC

#if defined(CONFIG_IDF_TARGET_ESP32)
// Классический ESP32 — используем adc_oneshot API (быстрее, точнее)
#include "esp_adc/adc_oneshot.h"

static adc_oneshot_unit_handle_t g_adc1_handle = nullptr;
static adc_oneshot_unit_handle_t g_adc2_handle = nullptr;

static bool adc_read_oneshot(uint8_t pin, int &out_raw) {
    adc_unit_t unit;
    adc_channel_t channel;
    switch (pin) {
        case 36: unit = ADC_UNIT_1; channel = ADC_CHANNEL_0; break;
        case 37: unit = ADC_UNIT_1; channel = ADC_CHANNEL_1; break;
        case 38: unit = ADC_UNIT_1; channel = ADC_CHANNEL_2; break;
        case 39: unit = ADC_UNIT_1; channel = ADC_CHANNEL_3; break;
        case 32: unit = ADC_UNIT_1; channel = ADC_CHANNEL_4; break;
        case 33: unit = ADC_UNIT_1; channel = ADC_CHANNEL_5; break;
        case 34: unit = ADC_UNIT_1; channel = ADC_CHANNEL_6; break;
        case 35: unit = ADC_UNIT_1; channel = ADC_CHANNEL_7; break;
        case 4:  unit = ADC_UNIT_2; channel = ADC_CHANNEL_0; break;
        case 0:  unit = ADC_UNIT_2; channel = ADC_CHANNEL_1; break;
        case 2:  unit = ADC_UNIT_2; channel = ADC_CHANNEL_2; break;
        case 15: unit = ADC_UNIT_2; channel = ADC_CHANNEL_3; break;
        case 13: unit = ADC_UNIT_2; channel = ADC_CHANNEL_4; break;
        case 12: unit = ADC_UNIT_2; channel = ADC_CHANNEL_5; break;
        case 14: unit = ADC_UNIT_2; channel = ADC_CHANNEL_6; break;
        case 27: unit = ADC_UNIT_2; channel = ADC_CHANNEL_7; break;
        case 25: unit = ADC_UNIT_2; channel = ADC_CHANNEL_8; break;
        case 26: unit = ADC_UNIT_2; channel = ADC_CHANNEL_9; break;
        default: return false;
    }
    adc_oneshot_unit_handle_t *handle_ptr = (unit == ADC_UNIT_1) ? &g_adc1_handle : &g_adc2_handle;
    if (*handle_ptr == nullptr) {
        adc_oneshot_unit_init_cfg_t init_cfg = { .unit_id = unit, .ulp_mode = ADC_ULP_MODE_DISABLE };
        if (adc_oneshot_new_unit(&init_cfg, handle_ptr) != ESP_OK) return false;
    }
    adc_oneshot_chan_cfg_t chan_cfg = { .atten = ADC_ATTEN_DB_12, .bitwidth = ADC_BITWIDTH_DEFAULT };
    if (adc_oneshot_config_channel(*handle_ptr, channel, &chan_cfg) != ESP_OK) return false;
    return adc_oneshot_read(*handle_ptr, channel, &out_raw) == ESP_OK;
}
#define HDL_ADC_ONESHOT 1
#else
// Все остальные чипы (S2/S3/C3/C5/C6/H2) — используем универсальный analogRead()
#define HDL_ADC_ONESHOT 0
#endif

#endif // HDL_ENABLE_ADC

// ============================================================
// === БЛОК 2: 1-Wire (ow.*) ===
// ============================================================
#ifdef HDL_ENABLE_OW
#include <OneWire.h>
static OneWire* g_ow = nullptr;

static bool popByteArray(const uint8_t*& out_data, uint16_t& out_len) {
    if (stack_is_empty()) return false;
    uint8_t* top = &stack_mem[stack_ptr];
    const uint8_t* data; uint16_t len, sz;
    if (!extractPayload(top, data, len, sz)) { stack_ptr += sz; return false; }
    if (top[0] != 17 && top[0] != 20) { stack_ptr += sz; return false; }
    uint8_t type_id = top[5];
    if (type_registry[type_id].size != 1) { stack_ptr += sz; return false; }
    out_data = data; out_len = len; stack_ptr += sz;
    return true;
}

void owInitFunc() {
    uint8_t pin;
    if (!popUInt8(pin)) { pushBool(false); return; }
    if (g_ow) { delete g_ow; g_ow = nullptr; }
    g_ow = new OneWire(pin);
    pushBool(g_ow != nullptr);
}
void owResetFunc()    { if (!g_ow) { pushBool(false); return; } pushBool(g_ow->reset() != 0); }
void owWriteFunc()    { uint8_t b; if (!popUInt8(b) || !g_ow) return; g_ow->write(b, 0); }
void owReadFunc()     { if (!g_ow) { pushUInt8(0xFF); return; } pushUInt8((uint8_t)g_ow->read()); }
void owWriteBitFunc() { uint8_t b; if (!popUInt8(b) || !g_ow) return; g_ow->write_bit(b & 1); }
void owReadBitFunc()  { if (!g_ow) { pushUInt8(0); return; } pushUInt8((uint8_t)g_ow->read_bit()); }
void owSelectFunc() {
    if (!g_ow) { pushBool(false); return; }
    const uint8_t* data; uint16_t len;
    if (!popByteArray(data, len) || len < 8) { pushBool(false); return; }
    g_ow->select(data); pushBool(true);
}
void owSkipFunc()   { if (!g_ow) { pushBool(false); return; } g_ow->skip(); pushBool(true); }
void owSearchFunc() {
    if (!g_ow) { pushUInt8(0); return; }
    if (stack_is_empty()) { pushUInt8(0); return; }
    uint8_t* top = &stack_mem[stack_ptr];
    uint16_t sz = elem_size(top);
    if (top[0] != 17 && top[0] != 20) { stack_ptr += sz; pushUInt8(0); return; }
    uint16_t base  = top[1] | (top[2] << 8);
    uint16_t count = top[3] | (top[4] << 8);
    stack_ptr += sz;
    if (type_registry[top[5]].size != 1) { pushUInt8(0); return; }
    if (base + count > DATA_POOL_SIZE)   { pushUInt8(0); return; }
    uint8_t max_devs = count / 8, found = 0;
    uint8_t rom[8];
    g_ow->reset_search();
    while (found < max_devs && g_ow->search(rom)) {
        memcpy(&data_pool[base + found * 8], rom, 8);
        found++;
    }
    pushUInt8(found);
}

void owModuleInit() {
    focusTo("ow");
    addInternalWord("ow.Init",     owInitFunc);
    addInternalWord("ow.Reset",    owResetFunc);
    addInternalWord("ow.Write",    owWriteFunc);
    addInternalWord("ow.Read",     owReadFunc);
    addInternalWord("ow.WriteBit", owWriteBitFunc);
    addInternalWord("ow.ReadBit",  owReadBitFunc);
    addInternalWord("ow.Select",   owSelectFunc);
    addInternalWord("ow.Skip",     owSkipFunc);
    addInternalWord("ow.Search",   owSearchFunc);
    focusTo("main");
}
#else
void owModuleInit() {}
#endif // HDL_ENABLE_OW

// ============================================================
// === БЛОК 3: ЗВУК (tone / beep / noTone) ===
// ============================================================
#ifdef HDL_ENABLE_SOUND

#define TONE_MAX_PINS 8
static struct { uint8_t pin; uint32_t freq; } g_tone_pins[TONE_MAX_PINS];
static uint8_t g_tone_count = 0;

static int8_t tone_get_channel(uint8_t pin) {
    for (uint8_t i = 0; i < g_tone_count; i++)
        if (g_tone_pins[i].pin == pin) return i;
    return -1;
}
static int8_t tone_alloc_channel(uint8_t pin) {
    if (g_tone_count >= TONE_MAX_PINS) return -1;
    if (!::ledcAttach(pin, 5000, 10)) return -1;
    g_tone_pins[g_tone_count].pin = pin;
    g_tone_pins[g_tone_count].freq = 0;
    return g_tone_count++;
}

void toneWord() {
    uint8_t pin; uint32_t freq;
    if (!popUInt8(pin) || !popUInt32(freq) || freq == 0) return;
    uint32_t duration = 0;
    if (!stack_is_empty()) {
        uint8_t* top = &stack_mem[stack_ptr];
        if (top[0] >= 4 && top[0] <= 11) popUInt32(duration);
    }
    int8_t idx = tone_get_channel(pin);
    if (idx < 0) idx = tone_alloc_channel(pin);
    if (idx < 0) return;
    ::ledcWriteTone(pin, freq);
    g_tone_pins[idx].freq = freq;
    if (duration > 0) { delay(duration); ::ledcWriteTone(pin, 0); }
}
void beepWord() {
    uint8_t pin; uint32_t freq;
    if (!popUInt8(pin) || !popUInt32(freq)) return;
    uint32_t duration = 50;
    if (!stack_is_empty()) {
        uint8_t* top = &stack_mem[stack_ptr];
        if (top[0] >= 4 && top[0] <= 11) popUInt32(duration);
    }
    int8_t idx = tone_get_channel(pin);
    if (idx < 0) idx = tone_alloc_channel(pin);
    if (idx < 0) return;
    ::ledcWriteTone(pin, freq);
    delay(duration);
    ::ledcWriteTone(pin, 0);
}
void noToneWord() {
    uint8_t pin;
    if (!popUInt8(pin)) return;
    ::ledcWriteTone(pin, 0);
}

void soundInit() {
    focusTo("audio");
    addInternalWord("tone",   toneWord);
    addInternalWord("beep",   beepWord);
    addInternalWord("noTone", noToneWord);
    focusTo("main");
}
#else
void soundInit() {}
#endif // HDL_ENABLE_SOUND

// ============================================================
// === БЛОК 4: LEDC PWM (ESP32) ===
// ============================================================
#ifdef HDL_ENABLE_LEDC

static uint32_t g_last_ledc_freq = 5000;
static uint8_t  g_last_ledc_res  = 8;

void ledcSetupWord() {
    uint8_t resolution, channel; uint32_t freq;
    if (!popUInt8(resolution) || !popUInt32(freq) || !popUInt8(channel)) return;
    g_last_ledc_freq = freq;
    g_last_ledc_res  = resolution;
    (void)channel;
}
void ledcAttachWord() {
    uint8_t channel, pin;
    if (!popUInt8(channel) || !popUInt8(pin)) return;
    (void)channel;
    ::ledcAttach(pin, g_last_ledc_freq, g_last_ledc_res);
}
void ledcWriteWord() {
    uint32_t duty; uint8_t pin;
    if (!popUInt32(duty) || !popUInt8(pin)) return;
    ::ledcWrite(pin, duty);
}

void ledcInit() {
    executeLine("leds cont");
    addInternalWord("ledcSetup",  ledcSetupWord);
    addInternalWord("ledcAttach", ledcAttachWord);
    addInternalWord("ledcWrite",  ledcWriteWord);
    executeLine("main");
}
#else
void ledcInit() {}
#endif // HDL_ENABLE_LEDC


// ============================================================
// === БЛОК 5: GPIO (базовые пины) — РЕАЛИЗАЦИЯ ФУНКЦИЙ ======
// ============================================================
#ifdef HDL_ENABLE_GPIO

// === КОНСТАНТЫ-СЛОВА (кладут u8 на стек) ===
 void lowWord()         { pushUInt8(LOW); }
 void highWord()        { pushUInt8(HIGH); }
 void inputWord()       { pushUInt8(INPUT); }
 void outputWord()      { pushUInt8(OUTPUT); }
 void inputPullupWord() { pushUInt8(INPUT_PULLUP); }
 void lsbfirstWord()    { pushUInt8(LSBFIRST); }
 void msbfirstWord()    { pushUInt8(MSBFIRST); }

// === СТРОКОВЫЕ КОНСТАНТЫ (кладут STRING на стек) ===
void crWord()    { pushStringRaw("\r"); }
void lfWord()    { pushStringRaw("\n"); }
void crlfWord()  { pushStringRaw("\r\n"); }
void crlf2Word() { pushStringRaw("\r\n\r\n"); }

// === pinMode : pin mode → ===
// R2L: mode → pin → pinMode
void pinModeWord() {
    uint8_t pin, mode;
    if (!popUInt8(mode) || !popUInt8(pin)) {
        pushBool(false);
        return;
    }
    ::pinMode(pin, mode);
    pushBool(true);
}

// === digitalWrite : pin value → ===
// R2L: value → pin → digitalWrite
void digitalWriteWord() {
    uint8_t pin, val;
    if (!popUInt8(val) || !popUInt8(pin)) {
        pushBool(false);
        return;
    }
    ::digitalWrite(pin, val ? HIGH : LOW);
    pushBool(true);
}

// === digitalRead : pin → u8 ===
void digitalReadWord() {
    uint8_t pin;
    if (!popUInt8(pin)) {
        pushUInt8(0);
        return;
    }
    pushUInt8((uint8_t)::digitalRead(pin));
}

// === analogWrite : pin value → ===
// value — u32 (для совместимости с разным разрешением ШИМ)
void analogWriteWord() {
    uint8_t pin;
    uint32_t val;
    if (!popUInt32(val) || !popUInt8(pin)) {
        pushBool(false);
        return;
    }
#if defined(ESP32)
    // ESP32: ledcWriteTone или analogWrite (Arduino Core 3.x)
    ::analogWrite(pin, (uint8_t)(val & 0xFF));
#else
    ::analogWrite(pin, (uint8_t)(val & 0xFF));
#endif
    pushBool(true);
}

// === analogRead : pin → u16 ===
void analogReadWord() {
    uint8_t pin;
    if (!popUInt8(pin)) { pushUInt16(0); return; }
    
    int raw = 0;
    bool ok = false;
    
#if defined(ESP32) && defined(HDL_ENABLE_ADC) && HDL_ADC_ONESHOT
    // 🔑 Используем НОВЫЙ драйвер (тот же, что в блоке ADC)
    // Проверка ADC2 + Wi-Fi: на классическом ESP32 ADC2 заблокирован при Wi-Fi
    if (pin == 4 || pin == 0 || pin == 2 || pin == 15 ||
        pin == 13 || pin == 12 || pin == 14 || pin == 27 ||
        pin == 25 || pin == 26) {
        // Это пины ADC2
        if (WiFi.status() == WL_CONNECTED) {
            pushUInt16(0);  // ADC2 недоступен при Wi-Fi
            return;
        }
    }
    //ok = adc_read_oneshot(pin, raw);
#else
    // Другие чипы (S2/S3/C3/C5/C6/H2) или ADC отключён — legacy-драйвер
    raw = ::analogRead(pin);
    ok = true;
#endif
    
    pushUInt16(ok ? (uint16_t)(raw & 0xFFFF) : 0);
}

// === amv : pin → u32 (милливольты) ===
void amvWord() {
    uint8_t pin;
    if (!popUInt8(pin)) { pushUInt32(0); return; }
    
    int raw = 0;
    bool ok = false;
    
#if defined(ESP32) && defined(HDL_ENABLE_ADC) && HDL_ADC_ONESHOT
    // 🔑 Тот же драйвер, что в блоке ADC
    if (pin == 4 || pin == 0 || pin == 2 || pin == 15 ||
        pin == 13 || pin == 12 || pin == 14 || pin == 27 ||
        pin == 25 || pin == 26) {
        if (WiFi.status() == WL_CONNECTED) {
            pushUInt32(0);
            return;
        }
    }
   // ok = adc_read_oneshot(pin, raw);
#else
    raw = ::analogRead(pin);
    ok = true;
#endif
    
    if (!ok) { pushUInt32(0); return; }
    
#if defined(ESP32)
    // 12 бит → 4095, Vref ≈ 3300 мВ
    uint32_t mv = ((uint32_t)(raw & 0xFFF) * 3300UL) / 4095UL;
#else
    // ESP8266: 10 бит → 1023, Vref ≈ 1000 мВ
    uint32_t mv = ((uint32_t)(raw & 0x3FF) * 1000UL) / 1023UL;
#endif
    pushUInt32(mv);
}

// === pulseIn : pin value timeout → u32 (микросекунды) ===
// R2L: timeout → value → pin → pulseIn
void pulseInFunc() {
    uint8_t pin, val;
    uint32_t timeout;
    if (!popUInt32(timeout) || !popUInt8(val) || !popUInt8(pin)) {
        pushUInt32(0);
        return;
    }
    unsigned long duration = ::pulseIn(pin, val ? HIGH : LOW, timeout);
    pushUInt32((uint32_t)duration);
}

// === shiftOut : dataPin clockPin bitOrder value → ===
// R2L: value → bitOrder → clockPin → dataPin → shiftOut
void shiftOutWord() {
    uint8_t dataPin, clockPin, bitOrder, value;
    if (!popUInt8(value) || !popUInt8(bitOrder) ||
        !popUInt8(clockPin) || !popUInt8(dataPin)) {
        pushBool(false);
        return;
    }
    ::shiftOut(dataPin, clockPin, bitOrder, value);
    pushBool(true);
}



void gpioInit() {
    focusTo("gpio");
    addInternalWord("pinMode",      pinModeWord);
    addInternalWord("digitalWrite", digitalWriteWord);
    addInternalWord("analogWrite",  analogWriteWord);
    addInternalWord("digitalRead",  digitalReadWord);
    addInternalWord("analogRead",   analogReadWord);
//    addInternalWord("amv",          amvWord);
    addInternalWord("pulseIn",      pulseInFunc);
    addInternalWord("shiftOut",     shiftOutWord);
    addInternalWord("LOW",          lowWord);
    addInternalWord("HIGH",         highWord);
    addInternalWord("INPUT",        inputWord);
    addInternalWord("OUTPUT",       outputWord);
    addInternalWord("INPUT_PULLUP", inputPullupWord);
    addInternalWord("LSBFIRST",     lsbfirstWord);
    addInternalWord("MSBFIRST",     msbfirstWord);
    focusTo("main");

    focusTo("io");
    addInternalWord("CR",    crWord);
    addInternalWord("LF",    lfWord);
    addInternalWord("CRLF",  crlfWord);
    addInternalWord("CRLF2", crlf2Word);
    focusTo("main");
}
#else
void gpioInit() {}
#endif // HDL_ENABLE_GPIO

// ============================================================
// === БЛОК 6: I2C (Wire) ===
// ============================================================
#ifdef HDL_ENABLE_I2C
#include <Wire.h>
static bool i2cInitialized = false;

void i2cInitFunc() {
    uint8_t scl, sda;
    if (!popUInt8(scl) || !popUInt8(sda) || sda == scl) { pushBool(false); return; }
    Wire.begin(sda, scl); Wire.setClock(100000);
    i2cInitialized = true; pushBool(true);
}
void i2cInitClokFunc() {
    uint32_t freq; uint8_t scl, sda;
    if (!popUInt32(freq) || !popUInt8(scl) || !popUInt8(sda) || sda == scl) { pushBool(false); return; }
    if (freq == 0) freq = 100000;
    Wire.begin(sda, scl); Wire.setClock(freq);
    i2cInitialized = true; pushBool(true);
}
void i2cWriteFunc() {
    uint8_t dev;
    if (!popUInt8(dev) || !i2cInitialized || stack_is_empty()) { pushBool(false); return; }
    uint8_t* top = &stack_mem[stack_ptr];
    const uint8_t* ptr = nullptr; uint16_t len = 0, sz = 0;
    if (!extractPayload(top, ptr, len, sz)) { pushBool(false); return; }
    stack_ptr += sz;
    const uint16_t CHUNK = 128; uint16_t sent = 0;
    while (sent < len) {
        uint16_t chunk = (len - sent > CHUNK) ? CHUNK : (len - sent);
        Wire.beginTransmission(dev);
        Wire.write(ptr + sent, chunk);
        if (Wire.endTransmission(true) != 0) { pushBool(false); return; }
        sent += chunk;
    }
    pushBool(true);
}
void i2cWriteRegFunc() {
    uint8_t val, reg, dev;
    if (!popUInt8(dev) || !popUInt8(reg) || !popUInt8(val)) { pushBool(false); return; }
    if (!i2cInitialized) { pushBool(false); return; }
    Wire.beginTransmission(dev); Wire.write(reg); Wire.write(val);
    pushBool(Wire.endTransmission(true) == 0);
}
void i2cReadFunc() {
    uint16_t a, l; uint8_t dev;
    if (!popUInt8(dev) || !popAddrInfo(a, l)) { pushBool(false); return; }
    if (!i2cInitialized || a + l > DATA_POOL_SIZE || l == 0) { pushBool(false); return; }
    Wire.requestFrom((uint8_t)dev, (uint8_t)l);
    uint16_t cnt = 0;
    while (Wire.available() && cnt < l) { data_pool[a + cnt] = Wire.read(); cnt++; }
    pushBool(cnt == l);
}
void i2cReadRegFunc() {
    uint16_t a, l; uint8_t reg, dev;
    if (!popUInt8(dev) || !popUInt8(reg) || !popAddrInfo(a, l)) { pushBool(false); return; }
    if (!i2cInitialized || a + l > DATA_POOL_SIZE || l == 0) { pushBool(false); return; }
    Wire.beginTransmission(dev); Wire.write(reg);
    if (Wire.endTransmission(false) != 0) { pushBool(false); return; }
    if (Wire.requestFrom((uint8_t)dev, (uint8_t)l) < l) { pushBool(false); return; }
    for (uint8_t i = 0; i < l; i++) data_pool[a + i] = Wire.read();
    pushBool(true);
}
void i2cScanFunc() {
    uint16_t a, max;
    if (!popAddrInfo(a, max)) { pushUInt8(0); return; }
    if (!i2cInitialized || a + max > DATA_POOL_SIZE) { pushUInt8(0); return; }
    uint8_t count = 0;
    for (uint8_t addr = 0x01; addr <= 0x7F && count < max; addr++) {
        Wire.beginTransmission(addr);
        if (Wire.endTransmission() == 0) {
            if (a + count < DATA_POOL_SIZE) data_pool[a + count] = addr;
            count++;
        }
    }
    pushUInt8(count);
}

void i2cInit() {
    focusTo("i2c");
    addInternalWord("i2c.Init",     i2cInitFunc);
    addInternalWord("i2c.Initclok", i2cInitClokFunc);
    addInternalWord("i2c.Write",    i2cWriteFunc);
    addInternalWord("i2c.WriteReg", i2cWriteRegFunc);
    addInternalWord("i2c.Read",     i2cReadFunc);
    addInternalWord("i2c.ReadReg",  i2cReadRegFunc);
    addInternalWord("i2c.Scan",     i2cScanFunc);
    focusTo("main");
}
#else
void i2cInit() {}
#endif // HDL_ENABLE_I2C
