
// === pins.ino — ПОЛНАЯ ВЕРСИЯ (поддержка ESP32 Core 2.x и 3.x) ===
#include <Arduino.h>

// === Захватываем значения ядра на этапе компиляции ===
constexpr uint8_t HDL_INPUT        = (uint8_t)INPUT;
constexpr uint8_t HDL_OUTPUT       = (uint8_t)OUTPUT;
constexpr uint8_t HDL_INPUT_PULLUP = (uint8_t)INPUT_PULLUP;
constexpr uint8_t HDL_LOW          = (uint8_t)LOW;
constexpr uint8_t HDL_HIGH         = (uint8_t)HIGH;

// === Вспомогательные функции ===
inline void pushUInt8(uint8_t val) {
    uint8_t buf[2] = {4, val};
    stack_push(buf, 2);
}

// === ФУНКЦИИ-ОБЁРТКИ ДЛЯ КОНСТАНТ (вместо лямбд) ===
void lowWord()         { pushUInt8(HDL_LOW); }
void highWord()        { pushUInt8(HDL_HIGH); }
void inputWord()       { pushUInt8(HDL_INPUT); }
void outputWord()      { pushUInt8(HDL_OUTPUT); }
void inputPullupWord() { pushUInt8(HDL_INPUT_PULLUP); }
void lsbfirstWord()    { pushUInt8(0); }
void msbfirstWord()    { pushUInt8(1); }

void crWord()    { uint8_t buf[3] = {0x0E, 1, '\r'}; stack_push(buf, 3); }
void lfWord()    { uint8_t buf[3] = {0x0E, 1, '\n'}; stack_push(buf, 3); }
void crlfWord()  { uint8_t buf[4] = {0x0E, 2, '\r', '\n'}; stack_push(buf, 4); }
void crlf2Word() { uint8_t buf[6] = {0x0E, 4, '\r', '\n', '\r', '\n'}; stack_push(buf, 6); }

// === РЕГИСТРАЦИЯ GPIO ===
void gpioInit() {
    executeLine("gpio cont");
    
    addInternalWord("pinMode",      pinModeWord);
    addInternalWord("digitalWrite", digitalWriteWord);
    addInternalWord("analogWrite",  analogWriteWord);
    addInternalWord("digitalRead",  digitalReadWord);
    addInternalWord("analogRead",   analogReadWord);
    addInternalWord("amv",          amvWord);
    addInternalWord("pulseIn",      pulseInFunc);
    addInternalWord("shiftOut",     shiftOutWord);
    addInternalWord("chip",         chipWord);
    
    addInternalWord("LOW",          lowWord);
    addInternalWord("HIGH",         highWord);
    addInternalWord("INPUT",        inputWord);
    addInternalWord("OUTPUT",       outputWord);
    addInternalWord("INPUT_PULLUP", inputPullupWord);
    addInternalWord("LSBFIRST",     lsbfirstWord);
    addInternalWord("MSBFIRST",     msbfirstWord);
    
    executeLine("io cont");
    addInternalWord("CR",    crWord);
    addInternalWord("LF",    lfWord);
    addInternalWord("CRLF",  crlfWord);
    addInternalWord("CRLF2", crlf2Word);
    
    executeLine("audio cont");
    addInternalWord("tone",   toneWord);
    addInternalWord("beep",   beepWord);
    addInternalWord("noTone", noToneWord);
    
    executeLine("leds cont");
    addInternalWord("ledcSetup",  ledcSetupWord);
    addInternalWord("ledcAttach", ledcAttachWord);
    addInternalWord("ledcWrite",  ledcWriteWord);
    
    executeLine("main");
}

// === GPIO ===
void pinModeWord() {
    uint8_t buf[8]; uint16_t sz;
    sz = stack_pop(buf, sizeof(buf)); if (sz < 2) return; uint8_t pin = buf[1];
    sz = stack_pop(buf, sizeof(buf)); if (sz < 2) return; uint8_t mode = buf[1];
    ::pinMode(pin, mode);
}

void digitalWriteWord() {
    uint8_t buf[8]; uint16_t sz;
    sz = stack_pop(buf, sizeof(buf)); if (sz < 2) return; uint8_t pin = buf[1];
    sz = stack_pop(buf, sizeof(buf)); if (sz < 2) return; uint8_t val = buf[1];
    ::digitalWrite(pin, val);
}

void analogWriteWord() {
    uint8_t buf[8]; uint16_t sz;
    sz = stack_pop(buf, sizeof(buf)); if (sz < 2) return; uint8_t pin = buf[1];
    sz = stack_pop(buf, sizeof(buf)); if (sz < 2) return; uint8_t val = buf[1];
    ::analogWrite(pin, val);
}

void digitalReadWord() {
    uint8_t buf[8];
    uint16_t sz = stack_pop(buf, sizeof(buf)); if (sz < 2) return;
    uint8_t pin = buf[1];
    uint8_t val = ::digitalRead(pin);
    uint8_t out[2] = {4, val};
    stack_push(out, 2);
}

void analogReadWord() {
    uint8_t buf[8];
    uint16_t sz = stack_pop(buf, sizeof(buf)); if (sz < 2) return;
    uint8_t pin = buf[1];
    uint16_t val = (uint16_t)::analogRead(pin);
    uint8_t out[3] = {6, (uint8_t)(val & 0xFF), (uint8_t)(val >> 8)};
    stack_push(out, 3);
}

void amvWord() {
    uint8_t buf[8];
    uint16_t sz = stack_pop(buf, sizeof(buf)); if (sz < 2) return;
    uint8_t pin = buf[1];
    int32_t mV = ::analogReadMilliVolts(pin);
    if (mV < 0) mV = 0;
    uint8_t out[5] = {9, (uint8_t)mV, (uint8_t)(mV >> 8), (uint8_t)(mV >> 16), (uint8_t)(mV >> 24)};
    stack_push(out, 5);
}

void pulseInFunc() {
    uint8_t buf[8]; uint16_t sz;
    sz = stack_pop(buf, sizeof(buf)); if (sz < 2) return; uint8_t pin = buf[1];
    sz = stack_pop(buf, sizeof(buf)); if (sz < 2) return; uint8_t state = buf[1];
    
    unsigned long timeout = 1000000UL;
    if (!stack_is_empty()) {
        uint8_t* top = &stack_mem[stack_ptr];
        if (top[0] >= 4 && top[0] <= 11) {
            sz = stack_pop(buf, sizeof(buf));
            if (sz >= 2) {
                uint32_t val = 0;
                uint16_t d = sz - 1;
                for (uint16_t i = 0; i < d && i < 4; i++)
                    val |= (uint32_t)buf[1 + i] << (i * 8);
                timeout = (unsigned long)val;
            }
        }
    }
    unsigned long duration = ::pulseIn(pin, state, timeout);
    uint8_t out[5] = {9, (uint8_t)duration, (uint8_t)(duration >> 8),
                      (uint8_t)(duration >> 16), (uint8_t)(duration >> 24)};
    stack_push(out, 5);
}

void shiftOutWord() {
    uint8_t buf[8]; uint16_t sz;
    sz = stack_pop(buf, sizeof(buf)); if (sz < 2) return; uint8_t dataPin = buf[1];
    sz = stack_pop(buf, sizeof(buf)); if (sz < 2) return; uint8_t clockPin = buf[1];
    sz = stack_pop(buf, sizeof(buf)); if (sz < 2) return; uint8_t bitOrder = buf[1];
    sz = stack_pop(buf, sizeof(buf)); if (sz < 2) return; uint8_t value = buf[1];
    ::shiftOut(dataPin, clockPin, bitOrder, value);
}

void chipWord() {
    const char* chipName;
#if defined(CONFIG_IDF_TARGET_ESP32)
    chipName = "esp32";
#elif defined(CONFIG_IDF_TARGET_ESP32S2)
    chipName = "esp32-s2";
#elif defined(CONFIG_IDF_TARGET_ESP32S3)
    chipName = "esp32-s3";
#elif defined(CONFIG_IDF_TARGET_ESP32C3)
    chipName = "esp32-c3";
#elif defined(CONFIG_IDF_TARGET_ESP32C2)
    chipName = "esp32-c2";
#elif defined(CONFIG_IDF_TARGET_ESP32C5)
    chipName = "esp32-c5";
#elif defined(CONFIG_IDF_TARGET_ESP32C6)
    chipName = "esp32-c6";
#elif defined(CONFIG_IDF_TARGET_ESP32H2)
    chipName = "esp32-h2";
#else
    chipName = "esp32-?";
#endif
    uint8_t len = strlen(chipName);
    if (len > 255) len = 255;
    uint8_t buf[257];
    buf[0] = 0x0E;
    buf[1] = len;
    memcpy(&buf[2], chipName, len);
    stack_push(buf, 2 + len);
}

// === ЗВУКОВАЯ ПЕРИФЕРИЯ ===
// В ядре 3.x ::tone() использует legacy RMT, который конфликтует с driver_ng.
// Поэтому для ядра 3.x реализуем tone через LEDC (новый драйвер).

#if defined(ESP32) && defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
// Таблица занятых пинов для tone (через LEDC)
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
    // Используем каналы 8..15, чтобы не конфликтовать с ручным ledcAttach (0..7)
    if (!::ledcAttach(pin, 5000, 10)) return -1;
    g_tone_pins[g_tone_count].pin = pin;
    g_tone_pins[g_tone_count].freq = 0;
    return g_tone_count++;
}
#endif

static uint32_t _decode_freq(uint8_t* buf, uint16_t sz) {
    uint32_t freq = 0;
    uint16_t d = sz - 1;
    for (uint16_t k = 0; k < d && k < 4; k++)
        freq |= (uint32_t)buf[1 + k] << (k * 8);
    return freq;
}

static unsigned long _decode_duration() {
    unsigned long duration = 0;
    if (!stack_is_empty()) {
        uint8_t* top = &stack_mem[stack_ptr];
        if (top[0] >= 4 && top[0] <= 11) {
            uint8_t buf[8];
            uint16_t sz = stack_pop(buf, sizeof(buf));
            uint16_t d = sz - 1;
            for (uint16_t k = 0; k < d && k < 4; k++)
                duration |= (uint32_t)buf[1 + k] << (k * 8);
        }
    }
    return duration;
}

void toneWord() {
    uint8_t buf[8]; uint16_t sz;
    sz = stack_pop(buf, sizeof(buf)); if (sz < 2) return; uint8_t pin = buf[1];
    sz = stack_pop(buf, sizeof(buf)); if (sz < 2) return;
    uint32_t freq = _decode_freq(buf, sz);
    if (freq == 0) return;
    unsigned long duration = _decode_duration();

#if defined(ESP32) && defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
    // Ядро 3.x: через LEDC (без legacy RMT)
    int8_t idx = tone_get_channel(pin);
    if (idx < 0) idx = tone_alloc_channel(pin);
    if (idx < 0) return;
    ::ledcWriteTone(pin, freq);
    g_tone_pins[idx].freq = freq;
    if (duration > 0) {
        delay(duration);
        ::ledcWriteTone(pin, 0);
    }
#else
    // Ядро 2.x: обычная ::tone()
    ::tone(pin, freq, duration);
#endif
}

void beepWord() {
    uint8_t buf[8]; uint16_t sz;
    sz = stack_pop(buf, sizeof(buf)); if (sz < 2) return; uint8_t pin = buf[1];
    sz = stack_pop(buf, sizeof(buf)); if (sz < 2) return;
    uint32_t freq = _decode_freq(buf, sz);
    unsigned long duration = _decode_duration();
    if (duration == 0) duration = 50;

#if defined(ESP32) && defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
    int8_t idx = tone_get_channel(pin);
    if (idx < 0) idx = tone_alloc_channel(pin);
    if (idx < 0) return;
    ::ledcWriteTone(pin, freq);
    delay(duration);
    ::ledcWriteTone(pin, 0);
#else
    ::tone(pin, freq, duration);
#endif
}

void noToneWord() {
    uint8_t buf[8];
    uint16_t sz = stack_pop(buf, sizeof(buf)); if (sz < 2) return;
    uint8_t pin = buf[1];
#if defined(ESP32) && defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
    ::ledcWriteTone(pin, 0);
#else
    ::noTone(pin);
#endif
}

// === LEDC PWM (ESP32) — ПОДДЕРЖКА ДВУХ ЯДЕР ===
#if defined(ESP32) && defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
static uint32_t g_last_ledc_freq = 5000;
static uint8_t  g_last_ledc_res  = 8;
#endif

void ledcSetupWord() {
    uint8_t buf[8]; uint16_t sz;
    sz = stack_pop(buf, sizeof(buf)); if (sz < 2) return; uint8_t channel = buf[1];
    sz = stack_pop(buf, sizeof(buf)); if (sz < 2) return;
    uint32_t freq = _decode_freq(buf, sz);
    sz = stack_pop(buf, sizeof(buf)); if (sz < 2) return; uint8_t resolution = buf[1];

#if defined(ESP32)
    #if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
        g_last_ledc_freq = freq;
        g_last_ledc_res  = resolution;
        (void)channel;
    #else
        ::ledcSetup(channel, freq, resolution);
    #endif
#endif
}

void ledcAttachWord() {
    uint8_t buf[8]; uint16_t sz;
    sz = stack_pop(buf, sizeof(buf)); if (sz < 2) return; uint8_t pin = buf[1];
    sz = stack_pop(buf, sizeof(buf)); if (sz < 2) return; uint8_t channel = buf[1];

#if defined(ESP32)
    #if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
        ::ledcAttach(pin, g_last_ledc_freq, g_last_ledc_res);
    #else
        ::ledcAttachPin(pin, channel);
    #endif
#else
    (void)pin; (void)channel;
#endif
}

void ledcWriteWord() {
    uint8_t buf[8]; uint16_t sz;
    sz = stack_pop(buf, sizeof(buf)); if (sz < 2) return; uint8_t ch_or_pin = buf[1];
    sz = stack_pop(buf, sizeof(buf)); if (sz < 2) return;
    uint32_t duty = _decode_freq(buf, sz);
#if defined(ESP32)
    ::ledcWrite(ch_or_pin, duty);
#endif
}

// === I2C ===
#include <Wire.h>
extern uint8_t data_pool[];
static bool i2cInitialized = false;

void i2cInitFunc() {
    uint8_t buf[8];
    if (stack_is_empty()) { pushBool(false); return; }
    if (stack_pop(buf, sizeof(buf)) < 2) { pushBool(false); return; }
    uint8_t sda = buf[1];
    if (stack_is_empty()) { pushBool(false); return; }
    if (stack_pop(buf, sizeof(buf)) < 2) { pushBool(false); return; }
    uint8_t scl = buf[1];
    if (sda == scl) { pushBool(false); return; }
    Wire.begin(sda, scl);
    Wire.setClock(100000);
    i2cInitialized = true;
    pushBool(true);
}

void i2cInitClokFunc() {
    uint8_t buf[8];
    if (stack_is_empty()) { pushBool(false); return; }
    if (stack_pop(buf, sizeof(buf)) < 2) { pushBool(false); return; }
    uint8_t sda = buf[1];
    if (stack_is_empty()) { pushBool(false); return; }
    if (stack_pop(buf, sizeof(buf)) < 2) { pushBool(false); return; }
    uint8_t scl = buf[1];
    if (stack_is_empty()) { pushBool(false); return; }
    uint16_t sz = stack_pop(buf, sizeof(buf));
    if (sz < 2) { pushBool(false); return; }
    uint32_t freq = 0;
    uint16_t d = sz - 1;
    for (uint16_t i = 0; i < d && i < 4; i++)
        freq |= (uint32_t)buf[1 + i] << (i * 8);
    if (freq == 0) freq = 100000;
    if (sda == scl) { pushBool(false); return; }
    Wire.begin(sda, scl);
    Wire.setClock(freq);
    i2cInitialized = true;
    pushBool(true);
}

void i2cWriteFunc() {
    uint8_t buf[8];
    if (stack_is_empty()) { pushBool(false); return; }
    if (stack_pop(buf, sizeof(buf)) < 2) { pushBool(false); return; }
    uint8_t dev = buf[1];
    if (stack_is_empty()) { pushBool(false); return; }
    if (stack_pop(buf, sizeof(buf)) != 6 || buf[0] != 17) { pushBool(false); return; }
    uint16_t a = buf[1] | (buf[2] << 8);
    uint16_t l = buf[3] | (buf[4] << 8);
    if (!i2cInitialized || a + l > DATA_POOL_SIZE) { pushBool(false); return; }
    Wire.beginTransmission(dev);
    Wire.write(&data_pool[a], (size_t)l);
    pushBool(Wire.endTransmission(true) == 0);
}

void i2cReadFunc() {
    uint8_t buf[8];
    if (stack_is_empty()) { pushBool(false); return; }
    if (stack_pop(buf, sizeof(buf)) < 2) { pushBool(false); return; }
    uint8_t dev = buf[1];
    if (stack_is_empty()) { pushBool(false); return; }
    if (stack_pop(buf, sizeof(buf)) != 6 || buf[0] != 17) { pushBool(false); return; }
    uint16_t a = buf[1] | (buf[2] << 8);
    uint16_t l = buf[3] | (buf[4] << 8);
    if (!i2cInitialized || a + l > DATA_POOL_SIZE || l == 0) { pushBool(false); return; }
    Wire.requestFrom((uint8_t)dev, (uint8_t)l);
    uint16_t cnt = 0;
    while (Wire.available() && cnt < l) {
        data_pool[a + cnt] = Wire.read();
        cnt++;
    }
    pushBool(cnt == l);
}

void i2cReadRegFunc() {
    uint8_t buf[8];
    if (stack_pop(buf, sizeof(buf)) < 2) { pushBool(false); return; }
    uint8_t dev = buf[1];
    if (stack_pop(buf, sizeof(buf)) < 2) { pushBool(false); return; }
    uint8_t reg = buf[1];
    if (stack_pop(buf, sizeof(buf)) != 6 || buf[0] != 17) { pushBool(false); return; }
    uint16_t a = buf[1] | (buf[2] << 8);
    uint16_t l = buf[3] | (buf[4] << 8);
    if (!i2cInitialized || a + l > DATA_POOL_SIZE || l == 0) { pushBool(false); return; }
    Wire.beginTransmission(dev);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) { pushBool(false); return; }
    if (Wire.requestFrom(dev, l) < l) { pushBool(false); return; }
    for (uint8_t i = 0; i < l; i++) data_pool[a + i] = Wire.read();
    pushBool(true);
}

void i2cScanFunc() {
    uint8_t buf[8];
    if (stack_is_empty()) { uint8_t z[2] = {4, 0}; stack_push(z, 2); return; }
    if (stack_pop(buf, sizeof(buf)) != 6 || buf[0] != 17) {
        uint8_t z[2] = {4, 0}; stack_push(z, 2); return;
    }
    uint16_t a = buf[1] | (buf[2] << 8);
    uint16_t max = buf[3] | (buf[4] << 8);
    if (!i2cInitialized || a + max > DATA_POOL_SIZE) {
        uint8_t z[2] = {4, 0}; stack_push(z, 2); return;
    }
    uint8_t count = 0;
    for (uint8_t addr = 0x01; addr <= 0x7F && count < max; addr++) {
        Wire.beginTransmission(addr);
        if (Wire.endTransmission() == 0) {
            if (a + count < DATA_POOL_SIZE) data_pool[a + count] = addr;
            count++;
        }
    }
    uint8_t res[2] = {4, count};
    stack_push(res, 2);
}

void i2cInit() {
    executeLine("i2c cont");
    addInternalWord("i2c.Init",      i2cInitFunc);
    addInternalWord("i2c.Initclok",  i2cInitClokFunc);
    addInternalWord("i2c.Write",     i2cWriteFunc);
    addInternalWord("i2c.Read",      i2cReadFunc);
    addInternalWord("i2c.ReadReg",   i2cReadRegFunc);
    addInternalWord("i2c.Scan",      i2cScanFunc);
    executeLine("main");
}
