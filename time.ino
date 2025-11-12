void timeInit() {
  String tmp = "cont time";
  executeLine(tmp);
  addInternalWord("delayMicroseconds", delayMicrosecondsFunc);
  tmp = "main";
  executeLine(tmp);
}

void delayMicrosecondsFunc(uint16_t addr) {
  // Читаем количество микросекунд
  if (stackTop < 2) {
    Serial.println("⚠️ delayMicroseconds: value expected");
    return;
  }

  uint32_t us = 0;
  uint8_t type = stack[stackTop - 1];

  if (type == TYPE_INT) {
    us = (uint32_t)popInt();
  } else if (type == TYPE_UINT16) {
    us = (uint32_t)popUInt16();
  } else if (type == TYPE_UINT8) {
    us = (uint32_t)popUInt8();
  } else if (type == TYPE_INT16) {
    int16_t v = popInt16();
    us = (v < 0) ? 0 : (uint32_t)v;
  } else if (type == TYPE_INT8) {
    int8_t v = popInt8();
    us = (v < 0) ? 0 : (uint32_t)v;
  } else {
    Serial.println("⚠️ delayMicroseconds: integer expected");
    // Удаляем неизвестный тип
    uint8_t len, t;
    popMetadata(len, t);
    if (len <= stackTop) stackTop -= len;
    return;
  }

  // Ограничиваем до максимума (16-битное значение для ESP32)
  if (us > 65535) us = 65535;

  ::delayMicroseconds((unsigned int)us);
}
