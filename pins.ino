// === pins.ino — GPIO, звук, LEDC, I2C (поддержка ESP32 Core 2.x и 3.x) ===
// Все функции используют нативные хелперы ядра: popUInt8, popUInt32, pushBool, pushStringRaw.
#include <Arduino.h>

// === Захватываем значения ядра на этапе компиляции ===
constexpr uint8_t HDL_INPUT        = (uint8_t)INPUT;
constexpr uint8_t HDL_OUTPUT       = (uint8_t)OUTPUT;
constexpr uint8_t HDL_INPUT_PULLUP = (uint8_t)INPUT_PULLUP;
constexpr uint8_t HDL_LOW          = (uint8_t)LOW;
constexpr uint8_t HDL_HIGH         = (uint8_t)HIGH;

// === ФУНКЦИИ-ОБЁРТКИ ДЛЯ КОНСТАНТ ===
void lowWord()         { pushUInt8(HDL_LOW); }
void highWord()        { pushUInt8(HDL_HIGH); }
void inputWord()       { pushUInt8(HDL_INPUT); }
void outputWord()      { pushUInt8(HDL_OUTPUT); }
void inputPullupWord() { pushUInt8(HDL_INPUT_PULLUP); }
void lsbfirstWord()    { pushUInt8(0); }
void msbfirstWord()    { pushUInt8(1); }
void crWord()          { pushStringRaw("\r"); }
void lfWord()          { pushStringRaw("\n"); }
void crlfWord()        { pushStringRaw("\r\n"); }
void crlf2Word()       { pushStringRaw("\r\n\r\n"); }

// === РЕГИСТРАЦИЯ GPIO ===
void gpioInit() {
    focusTo("gpio");
    addInternalWord("pinMode",      pinModeWord);       // pin mode → void.
    addInternalWord("digitalWrite", digitalWriteWord);  // pin value → void.
    addInternalWord("analogWrite",  analogWriteWord);   // pin value → void.
    addInternalWord("digitalRead",  digitalReadWord);   // pin → u8.
    addInternalWord("analogRead",   analogReadWord);    // pin → u16.
    addInternalWord("amv",          amvWord);           // pin → u32 (милливольты).
    addInternalWord("pulseIn",      pulseInFunc);       // pin state [timeout] → u32.
    addInternalWord("shiftOut",     shiftOutWord);      // dataPin clockPin bitOrder value → void.
    addInternalWord("chip",         chipWord);          // → STRING (имя чипа).
    addInternalWord("chip.id", chipIdFunc);
    addInternalWord("LOW",          lowWord);           // → u8 (0).
    addInternalWord("HIGH",         highWord);          // → u8 (1).
    addInternalWord("INPUT",        inputWord);         // → u8 (INPUT).
    addInternalWord("OUTPUT",       outputWord);        // → u8 (OUTPUT).
    addInternalWord("INPUT_PULLUP", inputPullupWord);   // → u8 (INPUT_PULLUP).
    addInternalWord("LSBFIRST",     lsbfirstWord);      // → u8 (0).
    addInternalWord("MSBFIRST",     msbfirstWord);      // → u8 (1).
    focusTo("main");
    focusTo("io");
    addInternalWord("CR",    crWord);                   // → STRING "\r".
    addInternalWord("LF",    lfWord);                   // → STRING "\n".
    addInternalWord("CRLF",  crlfWord);                 // → STRING "\r\n".
    addInternalWord("CRLF2", crlf2Word);                // → STRING "\r\n\r\n".
    focusTo("audio");
    addInternalWord("tone",   toneWord);                // pin freq [duration] → void.
    addInternalWord("beep",   beepWord);                // pin freq [duration] → void.
    addInternalWord("noTone", noToneWord);              // pin → void.
    executeLine("leds cont");
    addInternalWord("ledcSetup",  ledcSetupWord);       // channel freq resolution → void.
    addInternalWord("ledcAttach", ledcAttachWord);      // pin channel → void.
    addInternalWord("ledcWrite",  ledcWriteWord);       // ch_or_pin duty → void.
    focusTo("main");
}

// === GPIO ===
void pinModeWord() {
    uint8_t mode, pin;
    if (!popUInt8(mode) || !popUInt8(pin)) return;
    ::pinMode(pin, mode);
}

void digitalWriteWord() {
    uint8_t val, pin;
    if (!popUInt8(val) || !popUInt8(pin)) return;
    ::digitalWrite(pin, val);
}

void analogWriteWord() {
    uint8_t val, pin;
    if (!popUInt8(val) || !popUInt8(pin)) return;
    ::analogWrite(pin, val);
}

void digitalReadWord() {
    uint8_t pin;
    if (!popUInt8(pin)) return;
    pushUInt8(::digitalRead(pin));
}

void analogReadWord() {
    uint8_t pin;
    if (!popUInt8(pin)) return;
    uint16_t val = (uint16_t)::analogRead(pin);
    uint8_t out[3] = {6, (uint8_t)(val & 0xFF), (uint8_t)(val >> 8)};
    stack_push(out, 3);
}

void amvWord() {
    uint8_t pin;
    if (!popUInt8(pin)) return;
#if defined(ESP32)
    pushInt32(::analogReadMilliVolts(pin));
#else
    // Грубая аппроксимация для ESP8266 (1023 = ~1000mV)
    pushInt32((::analogRead(pin) * 1000) / 1023);
#endif
}

void pulseInFunc() {
    uint8_t state, pin;
    if (!popUInt8(state) || !popUInt8(pin)) return;
    uint32_t timeout = 1000000UL;
    if (!stack_is_empty()) {
        uint8_t* top = &stack_mem[stack_ptr];
        if (top[0] >= 4 && top[0] <= 11) popUInt32(timeout);
    }
    unsigned long duration = ::pulseIn(pin, state, timeout);
    uint8_t out[5] = {9};
    memcpy(&out[1], &duration, 4);
    stack_push(out, 5);
}

void shiftOutWord() {
    uint8_t value, bitOrder, clockPin, dataPin;
    if (!popUInt8(value) || !popUInt8(bitOrder) || !popUInt8(clockPin) || !popUInt8(dataPin)) return;
    ::shiftOut(dataPin, clockPin, bitOrder, value);
}

void chipWord() {
    pushStringRaw(getChipName());
}

// === ЗВУКОВАЯ ПЕРИФЕРИЯ ===
#if defined(ESP32) && defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
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
#endif

void toneWord() {
    uint8_t pin;
    uint32_t freq;
    if (!popUInt8(pin) || !popUInt32(freq) || freq == 0) return;
    uint32_t duration = 0;
    if (!stack_is_empty()) {
        uint8_t* top = &stack_mem[stack_ptr];
        if (top[0] >= 4 && top[0] <= 11) popUInt32(duration);
    }
#if defined(ESP32) && defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
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
    ::tone(pin, freq, duration);
#endif
}

void beepWord() {
    uint8_t pin;
    uint32_t freq;
    if (!popUInt8(pin) || !popUInt32(freq)) return;
    uint32_t duration = 50;
    if (!stack_is_empty()) {
        uint8_t* top = &stack_mem[stack_ptr];
        if (top[0] >= 4 && top[0] <= 11) popUInt32(duration);
    }
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
    uint8_t pin;
    if (!popUInt8(pin)) return;
#if defined(ESP32) && defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
    ::ledcWriteTone(pin, 0);
#else
    ::noTone(pin);
#endif
}

// === LEDC PWM (ESP32) ===
#if defined(ESP32) && defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
static uint32_t g_last_ledc_freq = 5000;
static uint8_t  g_last_ledc_res  = 8;
#endif

void ledcSetupWord() {
    uint8_t resolution, channel;
    uint32_t freq;
    if (!popUInt8(resolution) || !popUInt32(freq) || !popUInt8(channel)) return;
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
    uint8_t channel, pin;
    if (!popUInt8(channel) || !popUInt8(pin)) return;
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
    uint32_t duty;
    uint8_t ch_or_pin;
    if (!popUInt32(duty) || !popUInt8(ch_or_pin)) return;
#if defined(ESP32)
    ::ledcWrite(ch_or_pin, duty);
#endif
}

// === I2C ===
#include <Wire.h>

static bool i2cInitialized = false;

void i2cInitFunc() {
    uint8_t scl, sda;
    if (!popUInt8(scl) || !popUInt8(sda) || sda == scl) { pushBool(false); return; }
    Wire.begin(sda, scl);
    Wire.setClock(100000);
    i2cInitialized = true;
    pushBool(true);
}

void i2cInitClokFunc() {
    uint32_t freq;
    uint8_t scl, sda;
    if (!popUInt32(freq) || !popUInt8(scl) || !popUInt8(sda) || sda == scl) { pushBool(false); return; }
    if (freq == 0) freq = 100000;
    Wire.begin(sda, scl);
    Wire.setClock(freq);
    i2cInitialized = true;
    pushBool(true);
}

void i2cWriteFunc() {
  uint8_t dev;
  if (!popUInt8(dev) || !i2cInitialized || stack_is_empty()) { pushBool(false); return; }

  uint8_t* top = &stack_mem[stack_ptr];
  const uint8_t* ptr = nullptr;
  uint16_t len = 0;
  uint16_t sz = elem_size(top);
  uint8_t tag = top[0];

  // ЧИСЛА (u8, i8, u16, i16, u24, u32, i32, f)
  if (tag >= 4 && tag <= 11) {
    ptr = &top[1]; len = sz - 1;
  }
  // STRING / NAME
  else if (tag == 0x0E || tag == 0x0D) {
    ptr = &top[2]; len = top[1];
  }
  // $STRING
  else if (tag == 15) {
    len = top[1];
    uint16_t a = top[2] | (top[3] << 8);
    if (a + len > DATA_POOL_SIZE) { pushBool(false); return; }
    ptr = &data_pool[a];
  }
  // ARRAY / REF_ARR
  else if (tag == 17 || tag == 20) {
    if (sz != 6) { pushBool(false); return; }
    uint16_t a = top[1] | (top[2] << 8);
    len = top[3] | (top[4] << 8);
    uint8_t esz = type_registry[top[5]].size;
    uint32_t total = (uint32_t)len * esz;
    if (a + total > DATA_POOL_SIZE) { pushBool(false); return; }
    ptr = &data_pool[a];
    len = (uint16_t)total;
  }
  else { pushBool(false); return; }

  stack_ptr += sz;

  // Разбиение на куски по 128 байт
  const uint16_t CHUNK = 128;
  uint16_t sent = 0;
  while (sent < len) {
    uint16_t chunk = (len - sent > CHUNK) ? CHUNK : (len - sent);
    Wire.beginTransmission(dev);
    Wire.write(ptr + sent, chunk);
    if (Wire.endTransmission(true) != 0) { pushBool(false); return; }
    sent += chunk;
  }
  pushBool(true);
}

void i2cReadFunc() {
    uint16_t a, l;
    uint8_t dev;
    if (!popAddrInfo(a, l) || !popUInt8(dev)) { pushBool(false); return; }
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
    uint16_t a, l;
    uint8_t reg, dev;
    if (!popAddrInfo(a, l) || !popUInt8(reg) || !popUInt8(dev)) { pushBool(false); return; }
    if (!i2cInitialized || a + l > DATA_POOL_SIZE || l == 0) { pushBool(false); return; }
    Wire.beginTransmission(dev);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) { pushBool(false); return; }
    
    // ИСПРАВЛЕНО: явное приведение типов для ESP8266
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
    addInternalWord("i2c.Init",     i2cInitFunc);      // sda scl → BOOL.
    addInternalWord("i2c.Initclok", i2cInitClokFunc);  // sda scl freq → BOOL.
    addInternalWord("i2c.Write",    i2cWriteFunc);     // array dev → BOOL.
    addInternalWord("i2c.Read",     i2cReadFunc);      // array dev → BOOL.
    addInternalWord("i2c.ReadReg",  i2cReadRegFunc);   // array reg dev → BOOL.
    addInternalWord("i2c.Scan",     i2cScanFunc);      // array → u8 (количество найденных).
    focusTo("main");
}
