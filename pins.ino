void ledsInit() {
  String tmp = "cont leds";
  executeLine(tmp);
  addInternalWord("ledcSetup", ledcSetupWord);
  addInternalWord("ledcAttach", ledcAttachWord);
  addInternalWord("ledcWrite", ledcWriteWord);
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

SPISettings currentSPISettings;
void spiInit() {
  String tmp = "cont spi";
  executeLine(tmp);
  addInternalWord("spiBegin", spiBeginFunc);
  addInternalWord("spiSettings", spiSettingsFunc);
  addInternalWord("spiBeginTransaction", spiBeginTransactionFunc);
  addInternalWord("spiTransfer", spiTransferFunc);
  addInternalWord("spiEndTransaction", spiEndTransactionFunc);
}

void spiBeginFunc(uint16_t addr) {
  uint8_t SCK; if (!popAsUInt8(&SCK)) return; 
  uint8_t MISO; if (!popAsUInt8(&MISO)) return;
  uint8_t MOSI; if (!popAsUInt8(&MOSI)) return;
  uint8_t SS; if (!popAsUInt8(&SS)) return;
  SPI.begin(SCK, MISO, MOSI, SS);
}

void spiSettingsFunc(uint16_t addr) {
  uint8_t mode; if (!popAsUInt8(&mode)) return;
  uint8_t bitOrder; if (!popAsUInt8(&bitOrder)) return;
  int32_t clock; if (!popInt32FromAny(&clock)) return;
  currentSPISettings = SPISettings(clock, bitOrder, mode);
}
void spiBeginTransactionFunc(uint16_t addr) {
SPI.beginTransaction(currentSPISettings);
}


void spiTransferFunc(uint16_t addr) {
  // Объявляем переменные для анализа верхнего элемента стека
  uint8_t type, len;
  const uint8_t* data;

  // Пытаемся получить информацию о верхнем элементе стека:
  // - type: тип значения (TYPE_INT, TYPE_STRING и т.д.)
  // - len: длина данных в байтах
  // - data: указатель на конец (последний байт) данных значения на стеке
  if (!peekStackTop(&type, &len, &data)) {
    // Если стек пуст или повреждён — снимаем "мусор" (если есть) и выходим
    dropTop(0);
    return;
  }

  // ----------- Обработка массива (TYPE_ADDRINFO) ------------
  // TYPE_ADDRINFO — специальный тип, описывающий массив в dataPool (5 байт: адрес, длина, тип элемента)
// ----------- TYPE_ADDRINFO: массив из dataPool ------------
if (type == TYPE_ADDRINFO && len == 5) {
  dropTop(0); // убрали TYPE_ADDRINFO

  uint16_t address = data[0] | (data[1] << 8);      // адрес данных
  uint16_t totalBytes = data[2] | (data[3] << 8);   // полная длина в байтах
  uint8_t elemType = data[4];                       // тип элемента

  // Определяем размер одного элемента
  uint8_t elemSize = 1;
  if (elemType == TYPE_UINT16 || elemType == TYPE_INT16) elemSize = 2;
  else if (elemType == TYPE_INT || elemType == TYPE_FLOAT) elemSize = 4;
  // TYPE_UINT8, TYPE_INT8, TYPE_BOOL → 1 байт

  // Проверяем корректность длины
  if (totalBytes % elemSize != 0) return;
  uint16_t totalCount = totalBytes / elemSize;

  // Проверяем границы dataPool
  if (address >= DATA_POOL_SIZE || address + totalBytes > DATA_POOL_SIZE) {
    return;
  }

  // Читаем количество элементов (не байт!) со стека
  uint32_t count = totalCount; // по умолчанию — весь массив
  uint8_t countType, countLen;
  const uint8_t* countData;
  if (peekStackTop(&countType, &countLen, &countData)) {
    if (countType == TYPE_UINT8 && countLen == 1) {
      count = countData[0];
      dropTop(0);
    }
    else if (countType == TYPE_INT && countLen == 4) {
      memcpy(&count, countData - 3, 4);
      dropTop(0);
    }
  }

  // Ограничиваем по количеству элементов
  if (count > totalCount) count = totalCount;

  // Вычисляем количество байт для отправки
  uint32_t bytesToSend = count * elemSize;
  if (address + bytesToSend > DATA_POOL_SIZE) return;

  // Отправляем
  for (uint32_t i = 0; i < bytesToSend; i++) {
    SPI.transfer(dataPool[address + i]);
  }
  return;
}
  // ----------- Обработка строки (TYPE_STRING) ------------
  if (type == TYPE_STRING) {
    // Снимаем со стека строку (данные + len + type)
    dropTop(0);
    // Дополнительная проверка: не выходит ли длина за границы стека
    if (len > stackTop) return;

    // Указатель на начало строки в стеке (data указывает на конец, поэтому вычисляем начало)
    const char* str = (const char*)&stack[stackTop - len];

    // Отправляем каждый символ строки через SPI
    for (uint8_t i = 0; i < len; i++) {
      SPI.transfer(str[i]);
    }

    // Удаляем данные строки из стека (len байт — данные уже убраны dropTop, но здесь уточнение)
    stackTop -= len;
    return;
  }

  // ----------- Обработка целых чисел и bool ------------
  // Поддерживаем все целочисленные типы и bool
  if ((type == TYPE_UINT8 && len == 1) ||
      (type == TYPE_INT8 && len == 1) ||
      (type == TYPE_UINT16 && len == 2) ||
      (type == TYPE_INT16 && len == 2) ||
      (type == TYPE_INT && len == 4) ||
      (type == TYPE_BOOL && len == 1)) {
    // Снимаем значение со стека
    dropTop(0);

    // Отправляем байты значения в порядке little-endian:
    // data указывает на последний байт значения на стеке,
    // поэтому читаем назад: data[0], data[-1], data[-2]...
    for (uint8_t i = 0; i < len; i++) {
      SPI.transfer(data[-(int)i]);
    }
    return;
  }

  // ----------- Обработка чисел с плавающей точкой (float) ------------
  if (type == TYPE_FLOAT && len == 4) {
    // Снимаем значение со стека
    dropTop(0);

    // Восстанавливаем float из 4 байт (little-endian: младший байт первый)
    float f;
    memcpy(&f, data - 3, 4); // data указывает на 4-й байт, поэтому отступаем на 3 назад

    // Приводим float к массиву байтов
    uint8_t* bytes = (uint8_t*)&f;

    // Отправляем все 4 байта через SPI
    for (int i = 0; i < 4; i++) {
      SPI.transfer(bytes[i]);
    }
    return;
  }

  // ----------- Обработка неизвестного типа ------------
  // Если тип не поддерживается — просто убираем его со стека и выходим
  dropTop(0);
}


void spiEndTransactionFunc(uint16_t addr) {
   SPI.endTransaction();
}
