// === pins.ino — GPIO, звук, LEDC, I2C (поддержка ESP32 Core 2.x и 3.x) ===
// Все функции используют нативные хелперы ядра: popUInt8, popUInt32, pushBool, pushStringRaw.
#include <Arduino.h>
// ============================================================
// === 1-Wire ТРАНСПОРТ (ow.*) ===
// ============================================================
//
// Использование в HDL:
//   4 ow.Init .              \ инициализация на пине 4 → true/false
//   ow.Reset .               \ presence pulse → true если есть устройство
//   0xCCu8 ow.Write          \ Skip ROM
//   0x44u8 ow.Write          \ Convert T
//   ow.Skip                  \ альтернатива 0xCC
//   myRomArr ow.Select .     \ Match ROM (массив из 8 u8)
//   ow.Read .                \ прочитать байт
//   ow.ReadBit .             \ прочитать бит
//   1u8 ow.WriteBit          \ записать бит
//   myBuf ow.Search .        \ найти все устройства → количество
//
// CRC проверяется общим словом crc8 из ядра:
//   myRomArr crc8 .          \ должно быть 0 для валидного ROM-кода

static OneWire* g_ow = nullptr;

// ------------------------------------------------------------
// Хелпер: извлечь массив u8/i8 со стека (снимает элемент)
// ------------------------------------------------------------
static bool popByteArray(const uint8_t*& out_data, uint16_t& out_len) {
    if (stack_is_empty()) return false;
    uint8_t* top = &stack_mem[stack_ptr];
    const uint8_t* data;
    uint16_t len, sz;
    if (!extractPayload(top, data, len, sz)) {
        stack_ptr += elem_size(top);
        return false;
    }
    // Требуем ARRAY или REF_ARR
    if (top[0] != 17 && top[0] != 20) {
        stack_ptr += sz;
        return false;
    }
    // Требуем размер элемента = 1 байт (u8 или i8)
    uint8_t type_id = top[5];
    if (type_registry[type_id].size != 1) {
        stack_ptr += sz;
        return false;
    }
    out_data = data;
    out_len = len;
    stack_ptr += sz;
    return true;
}

// ------------------------------------------------------------
// ow.Init : pin → BOOL
// ------------------------------------------------------------
void owInitFunc() {
    uint8_t pin;
    if (!popUInt8(pin)) { pushBool(false); return; }
    if (g_ow) { delete g_ow; g_ow = nullptr; }
    g_ow = new OneWire(pin);
    pushBool(g_ow != nullptr);
}

// ------------------------------------------------------------
// ow.Reset → BOOL (presence pulse)
// ------------------------------------------------------------
void owResetFunc() {
    if (!g_ow) { pushBool(false); return; }
    pushBool(g_ow->reset() != 0);
}

// ------------------------------------------------------------
// ow.Write : u8 →
// ------------------------------------------------------------
void owWriteFunc() {
    uint8_t b;
    if (!popUInt8(b) || !g_ow) return;
    g_ow->write(b, 0);
}

// ------------------------------------------------------------
// ow.Read → u8
// ------------------------------------------------------------
void owReadFunc() {
    if (!g_ow) { pushUInt8(0xFF); return; }
    pushUInt8((uint8_t)g_ow->read());
}

// ------------------------------------------------------------
// ow.WriteBit : u8 →
// ------------------------------------------------------------
void owWriteBitFunc() {
    uint8_t b;
    if (!popUInt8(b) || !g_ow) return;
    g_ow->write_bit(b & 1);
}

// ------------------------------------------------------------
// ow.ReadBit → u8
// ------------------------------------------------------------
void owReadBitFunc() {
    if (!g_ow) { pushUInt8(0); return; }
    pushUInt8((uint8_t)g_ow->read_bit());
}

// ------------------------------------------------------------
// ow.Select : arr(8 u8) → BOOL (Match ROM)
// ------------------------------------------------------------
void owSelectFunc() {
    if (!g_ow) { pushBool(false); return; }
    const uint8_t* data; uint16_t len;
    if (!popByteArray(data, len) || len < 8) {
        pushBool(false);
        return;
    }
    g_ow->select(data);
    pushBool(true);
}

// ------------------------------------------------------------
// ow.Skip → BOOL (Skip ROM)
// ------------------------------------------------------------
void owSkipFunc() {
    if (!g_ow) { pushBool(false); return; }
    g_ow->skip();
    pushBool(true);
}

// ------------------------------------------------------------
// ow.Search : arr(u8) → u8 (количество найденных устройств)
// Массив должен быть минимум на 8*N байт, где N — ожидаемое число устройств.
// Автоматически вызывает reset_search перед поиском.
// ------------------------------------------------------------
void owSearchFunc() {
    if (!g_ow) { pushUInt8(0); return; }
    if (stack_is_empty()) { pushUInt8(0); return; }

    uint8_t* top = &stack_mem[stack_ptr];
    uint16_t sz = elem_size(top);

    // Требуем ARRAY или REF_ARR
    if (top[0] != 17 && top[0] != 20) {
        stack_ptr += sz;
        pushUInt8(0);
        return;
    }
    uint16_t base  = top[1] | (top[2] << 8);
    uint16_t count = top[3] | (top[4] << 8);
    uint8_t  type_id = top[5];
    stack_ptr += sz;

    // Требуем размер элемента = 1 байт
    if (type_registry[type_id].size != 1) { pushUInt8(0); return; }
    if (base + count > DATA_POOL_SIZE)    { pushUInt8(0); return; }

    uint8_t max_devs = count / 8;
    uint8_t found = 0;
    uint8_t rom[8];

    g_ow->reset_search();
    while (found < max_devs && g_ow->search(rom)) {
        memcpy(&data_pool[base + found * 8], rom, 8);
        found++;
    }
    pushUInt8(found);
}

// ------------------------------------------------------------
// Регистрация модуля
// ------------------------------------------------------------
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
  uint16_t len = 0, sz = 0;

  if (!extractPayload(top, ptr, len, sz)) { pushBool(false); return; }
  stack_ptr += sz;

  // Разбиение на куски по 128 байт (аппаратное ограничение Wire)
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
void i2cWriteRegFunc() {
  uint8_t val, reg, dev;
  if (!popUInt8(dev) || !popUInt8(reg) || !popUInt8(val) ) { pushBool(false); return; }
  if (!i2cInitialized) { pushBool(false); return; }
  
  Wire.beginTransmission(dev);
  Wire.write(reg);
  Wire.write(val);
  pushBool(Wire.endTransmission(true) == 0);
}
void i2cReadFunc() {
    uint16_t a, l;
    uint8_t dev;
    if (!popUInt8(dev)  || !popAddrInfo(a, l) ) { pushBool(false); return; }
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
    if (!popUInt8(dev) || !popUInt8(reg) || !popAddrInfo(a, l) ) { pushBool(false); return; }
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
    addInternalWord("i2c.WriteReg", i2cWriteRegFunc);
    addInternalWord("i2c.Read",     i2cReadFunc);      // array dev → BOOL.
    addInternalWord("i2c.ReadReg",  i2cReadRegFunc);   // array reg dev → BOOL.
    addInternalWord("i2c.Scan",     i2cScanFunc);      // array → u8 (количество найденных).
    focusTo("main");
}
