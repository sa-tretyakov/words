void ledsInit() {
  String tmp = "cont leds";
  executeLine(tmp);
  addInternalWord("ledcSetup", ledcSetupWord,currentContext);
  addInternalWord("ledcAttach", ledcAttachWord,currentContext);
  addInternalWord("ledcWrite", ledcWriteWord,currentContext);
}

void ledcSetupWord(uint16_t addr) {
  uint8_t channel; if (!popAsUInt8(&channel)) return;  
  int32_t freq; if (!popInt32FromAny(&freq)) return;  // ✅ int32_t  
  uint8_t resolution; if (!popAsUInt8(&resolution)) return;
  ledcSetup(channel, freq, resolution);
}
void ledcAttachWord(uint16_t addr) {
  uint8_t pin; if (!popAsUInt8(&pin)) return;  
  uint8_t channel; if (!popAsUInt8(&channel)) return;
  ledcAttachPin(pin, channel);
}
void ledcWriteWord(uint16_t addr) {
  uint8_t channel; if (!popAsUInt8(&channel)) return;
  int32_t value; if (!popInt32FromAny(&value)) return;  // ✅ int32_t
  ledcWrite(channel, value);
}

void gpioInit() {
  String tmp = "cont gpio";
  executeLine(tmp);
  // Слова GPIO
  addInternalWord("pinMode", pinModeWord);
  addInternalWord("digitalWrite", digitalWriteWord);
  addInternalWord("analogWrite", analogWriteWord);
  addInternalWord("digitalRead", digitalReadWord);
  addInternalWord("analogRead", analogReadWord);
  addInternalWord("amv", amvWord);
  addInternalWord("pulseIn", pulseInFunc);
  addInternalWord("shiftOut", shiftOutWord);
  addInternalWord("LOW", [](uint16_t) {pushUInt8(LOW);});
  addInternalWord("HIGH", [](uint16_t) {pushUInt8(HIGH);});
  addInternalWord("INPUT", [](uint16_t) {pushUInt8(INPUT);});
  addInternalWord("OUTPUT", [](uint16_t) {pushUInt8(OUTPUT);});
  addInternalWord("INPUT_PULLUP", [](uint16_t) {pushUInt8(INPUT_PULLUP);});
  addInternalWord("on", [](uint16_t) { pushBool(true); });
  addInternalWord("off", [](uint16_t) { pushBool(false); }); 
   tmp = "main";
  executeLine(tmp);

}
bool popPin(uint8_t* outPin) {
  uint8_t pinType, pinLen;
  const uint8_t* pinData;
  if (!peekStackTop(&pinType, &pinLen, &pinData)) return false;
  dropTop(0);
  return valueToUint8(pinType, pinLen, pinData, outPin);
}

void pinModeWord(uint16_t addr) {
  uint8_t pin;
  if (!popPin(&pin)) {
    outputStream->println("⚠️ pinMode: invalid pin");
    return;
  }

  uint8_t modeType, modeLen;
  const uint8_t* modeData;
  if (!peekStackTop(&modeType, &modeLen, &modeData)) return;
  dropTop(0);

  uint8_t mode;
  if (!valueToUint8(modeType, modeLen, modeData, &mode)) {
    outputStream->println("⚠️ pinMode: invalid mode");
    return;
  }

  ::pinMode(pin, mode);
}

void digitalWriteWord(uint16_t addr) {
  uint8_t pin;
  if (!popPin(&pin)) {
    outputStream->println("⚠️ digitalWrite: invalid pin");
    return;
  }

  uint8_t valueType, valueLen;
  const uint8_t* valueData;
  if (!peekStackTop(&valueType, &valueLen, &valueData)) return;
  dropTop(0);

  int32_t val = 0;
  if (valueType == TYPE_BOOL && valueLen == 1) {
    val = (valueData[0] != 0);
  }
  else if (valueType == TYPE_INT && valueLen == 4) {
    memcpy(&val, valueData, 4);
  }
  else if (valueType == TYPE_UINT8 && valueLen == 1) {
    val = valueData[0];
  }
  else if (valueType == TYPE_INT8 && valueLen == 1) {
    val = (int8_t)valueData[0];
  }
  else if (valueType == TYPE_UINT16 && valueLen == 2) {
    uint16_t v; memcpy(&v, valueData, 2); val = v;
  }
  else if (valueType == TYPE_INT16 && valueLen == 2) {
    int16_t v; memcpy(&v, valueData, 2); val = v;
  }
  else {
    outputStream->println("⚠️ digitalWrite: invalid value");
    return;
  }

  ::digitalWrite(pin, val ? HIGH : LOW);
}

void analogWriteWord(uint16_t addr) {
  uint8_t pin;
  if (!popPin(&pin)) {
    outputStream->println("⚠️ analogWrite: invalid pin");
    return;
  }

  uint8_t valueType, valueLen;
  const uint8_t* valueData;
  if (!peekStackTop(&valueType, &valueLen, &valueData)) return;
  dropTop(0);

  int32_t value = 0;
  if (valueType == TYPE_INT && valueLen == 4) {
    memcpy(&value, valueData, 4);
  }
  else if (valueType == TYPE_UINT16 && valueLen == 2) {
    uint16_t v; memcpy(&v, valueData, 2); value = v;
  }
  else if (valueType == TYPE_INT16 && valueLen == 2) {
    int16_t v; memcpy(&v, valueData, 2); value = v;
  }
  else if (valueType == TYPE_UINT8 && valueLen == 1) {
    value = valueData[0];
  }
  else if (valueType == TYPE_INT8 && valueLen == 1) {
    value = (int8_t)valueData[0];
  }
  else {
    outputStream->println("⚠️ analogWrite: invalid value type");
    return;
  }

  if (value < 0) value = 0;
  if (value > 1023) value = 1023;

  ::analogWrite(pin, value);
}

void digitalReadWord(uint16_t addr) {
  uint8_t pin;
  if (!popPin(&pin)) {
    outputStream->println("⚠️ digitalRead: invalid pin");
    return;
  }
  int val = ::digitalRead(pin);
  pushBool(val != LOW);
}

void analogReadWord(uint16_t addr) {
  uint8_t pin;
  if (!popPin(&pin)) {
    outputStream->println("⚠️ analogRead: invalid pin");
    return;
  }
  int val = ::analogRead(pin);
  pushInt(val);
}

void amvWord(uint16_t addr) {
  uint8_t pin;
  if (!popPin(&pin)) {
    outputStream->println("⚠️ amv: invalid pin");
    return;
  }

  // analogReadMilliVolts возвращает uint32_t (милливольты)
  uint32_t millivolts = ::analogReadMilliVolts(pin);

  // Кладём на стек как uint32 → но у нас нет TYPE_UINT32,
  // поэтому используем TYPE_INT (если значение < 2^31)
  if (millivolts <= INT32_MAX) {
    pushInt((int32_t)millivolts);
  } else {
    // На ESP32 analogReadMilliVolts даёт до ~3300, так что это безопасно
    pushInt(INT32_MAX);
  }
}

void pulseInFunc(uint16_t addr) {
  // Синтаксис: pulseIn pin state timeout
  // Стек перед вызовом: [timeout][state][pin] → верх = pin
  // 1. pin
  uint8_t pin;
  if (!popPin(&pin)) {
    outputStream->println("⚠️ pulseIn: invalid pin");
    pushInt(0);
    return;
  }

  // 2. state (должен быть 0 или 1)
  if (stackTop < 2) {
    outputStream->println("⚠️ pulseIn: state expected");
    pushInt(0);
    return;
  }
  uint8_t state = 0;
  uint8_t stType = stack[stackTop - 1];
  if (stType == TYPE_BOOL) {
    state = popBool() ? 1 : 0;
  } else if (stType == TYPE_UINT8) {
    state = popUInt8();
  } else if (stType == TYPE_INT) {
    state = (popInt() != 0) ? 1 : 0;
  } else {
    outputStream->println("⚠️ pulseIn: state must be 0 or 1");
    uint8_t len, type; popMetadata(len, type); stackTop -= len;
    pushInt(0);
    return;
  }
  if (state > 1) state = 1;

  // 3. timeout
  if (stackTop < 2) {
    outputStream->println("⚠️ pulseIn: timeout expected");
    pushInt(0);
    return;
  }
  int32_t timeout = popInt();

  // Выполняем
  unsigned long duration = ::pulseIn(pin, state, (unsigned long)timeout);
  pushInt((int32_t)duration);
}

void shiftOutWord(uint16_t addr) {

  // dataPin
  if (stackTop < 2) return;
  uint8_t dataPin;
  if (!popAsUInt8(&dataPin)) return;

  // clockPin
  if (stackTop < 2) return;
  uint8_t clockPin;
  if (!popAsUInt8(&clockPin)) return;

  // bitOrder
  if (stackTop < 2) return;
  uint8_t bitOrder;
  if (!popAsUInt8(&bitOrder)) return;

  // value
  if (stackTop < 2) return;
  int32_t value;
  if (!popInt32FromAny(&value)) return;
  uint8_t val = (uint8_t)(value & 0xFF);
  ::shiftOut(dataPin, clockPin, bitOrder, val);
}
