void pinsInit() {
     String tmp = "cont pins";
   executeLine(tmp);
  // Слова GPIO
  addInternalWord("pinMode", pinModeWord,currentContext);
  addInternalWord("digitalWrite", digitalWriteWord,currentContext);
  addInternalWord("analogWrite", analogWriteWord,currentContext);
  addInternalWord("digitalRead", digitalReadWord,currentContext);
  addInternalWord("analogRead", analogReadWord,currentContext);
  addInternalWord("amv", amvWord,currentContext);
  addInternalWord("pulseIn", pulseInFunc,currentContext);
    
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
    Serial.println("⚠️ pinMode: invalid pin");
    return;
  }

  uint8_t modeType, modeLen;
  const uint8_t* modeData;
  if (!peekStackTop(&modeType, &modeLen, &modeData)) return;
  dropTop(0);

  uint8_t mode;
  if (!valueToUint8(modeType, modeLen, modeData, &mode)) {
    Serial.println("⚠️ pinMode: invalid mode");
    return;
  }

  ::pinMode(pin, mode);
}

void digitalWriteWord(uint16_t addr) {
  uint8_t pin;
  if (!popPin(&pin)) {
    Serial.println("⚠️ digitalWrite: invalid pin");
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
    Serial.println("⚠️ digitalWrite: invalid value");
    return;
  }

  ::digitalWrite(pin, val ? HIGH : LOW);
}

void analogWriteWord(uint16_t addr) {
  uint8_t pin;
  if (!popPin(&pin)) {
    Serial.println("⚠️ analogWrite: invalid pin");
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
    Serial.println("⚠️ analogWrite: invalid value type");
    return;
  }

  if (value < 0) value = 0;
  if (value > 1023) value = 1023;

  ::analogWrite(pin, value);
}

void digitalReadWord(uint16_t addr) {
  uint8_t pin;
  if (!popPin(&pin)) {
    Serial.println("⚠️ digitalRead: invalid pin");
    return;
  }
  int val = ::digitalRead(pin);
  pushBool(val != LOW);
}

void analogReadWord(uint16_t addr) {
  uint8_t pin;
  if (!popPin(&pin)) {
    Serial.println("⚠️ analogRead: invalid pin");
    return;
  }
  int val = ::analogRead(pin);
  pushInt(val);
}

void amvWord(uint16_t addr) {
  uint8_t pin;
  if (!popPin(&pin)) {
    Serial.println("⚠️ amv: invalid pin");
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
    Serial.println("⚠️ pulseIn: invalid pin");
    pushInt(0);
    return;
  }

  // 2. state (должен быть 0 или 1)
  if (stackTop < 2) {
    Serial.println("⚠️ pulseIn: state expected");
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
    Serial.println("⚠️ pulseIn: state must be 0 or 1");
    uint8_t len, type; popMetadata(len, type); stackTop -= len;
    pushInt(0);
    return;
  }
  if (state > 1) state = 1;

  // 3. timeout
  if (stackTop < 2) {
    Serial.println("⚠️ pulseIn: timeout expected");
    pushInt(0);
    return;
  }
  int32_t timeout = popInt();

  // Выполняем
  unsigned long duration = ::pulseIn(pin, state, (unsigned long)timeout);
  pushInt((int32_t)duration);
}
