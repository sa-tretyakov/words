//void varsInit() {
//  String tmp = "cont vars";
//  executeLine(tmp);
//  addInternalWord("var", varWord);
//  addInternalWord("const", constWord);
//  addInternalWord("array", arrayFunc);
//  addInternalWord("let", letWord);
//}

void contWord(uint16_t addr) {
  uint8_t nameType, nameLen;
  const uint8_t* nameData;
  if (!peekStackTop(&nameType, &nameLen, &nameData) || nameType != TYPE_NAME) {
    Serial.println("⚠️ cont: ожидается имя");
    return;
  }
  dropTop(0);

  // Увеличиваем счётчик и СРАЗУ активируем новый контекст
  maxCont++;
  currentContext = maxCont;  // ← вот оно — немедленное переключение!

  // Создаём слово-константу (как `const`), но с типом STORAGE_CONT
  size_t recordSize = 2 + 1 + nameLen + 1 + 1 + 4 + 4; // next + len + name + storage + context + value + funcPtr
  if (dictLen + recordSize > DICT_SIZE) {
    maxCont--;
    currentContext = maxCont - 1; // откат
    return;
  }

  uint8_t* pos = &dictionary[dictLen];
  uint16_t nextOffset = dictLen + recordSize;

  pos[0] = (nextOffset >> 0) & 0xFF;
  pos[1] = (nextOffset >> 8) & 0xFF;
  pos[2] = nameLen;
  memcpy(&pos[3], nameData, nameLen);
  pos[3 + nameLen] = 0x80 | STORAGE_CONT;
  pos[3 + nameLen + 1] = 0; // контекст слова = глобальный

  // Значение = номер контекста
  uint32_t value = maxCont;
  memcpy(&pos[3 + nameLen + 2], &value, 4);

  // Функция — для возможности вызова позже
  uint32_t funcAddr = (uint32_t)contFunc;
  memcpy(&pos[3 + nameLen + 2 + 4], &funcAddr, 4);

  dictLen = nextOffset;

  // Опционально: можно сообщить пользователю
  // Serial.printf("Контекст '%.*s' = %d активен\n", nameLen, nameData, maxCont);
}
void varWord(uint16_t callerAddr) {
  if (stackTop < 2) return;
  uint8_t topLen = stack[stackTop - 2];
  uint8_t topType = stack[stackTop - 1];
  if (topType != TYPE_NAME) return;
  if (topLen > stackTop - 2) return;

  size_t nameStart = stackTop - 2 - topLen;
  const char* name = (const char*)&stack[nameStart];

  // Размер: next(2) + nameLen(1) + name(topLen) + storage(1) + context(1) + poolRef(4) + funcPtr(4)
  size_t recordSize = 2 + 1 + topLen + 1 + 1 + 4 + 4;
  if (dictLen + recordSize > DICT_SIZE) {
    Serial.println("⚠️ Dictionary full");
    return;
  }

  uint8_t* pos = &dictionary[dictLen];
  uint16_t nextOffset = dictLen + recordSize;

  pos[0] = (nextOffset >> 0) & 0xFF;
  pos[1] = (nextOffset >> 8) & 0xFF;
  pos[2] = topLen;
  memcpy(&pos[3], name, topLen);
  pos[3 + topLen] = 0x80 | STORAGE_POOLED; // storage = 0x82
  pos[3 + topLen + 1] = currentContext;    // ← сохраняем текущий контекст

  uint32_t poolRef = 0xFFFFFFFF;
  memcpy(&pos[3 + topLen + 2], &poolRef, 4);

  uint32_t funcAddr = (uint32_t)mychoiceFunc;
  memcpy(&pos[3 + topLen + 2 + 4], &funcAddr, 4);

  dictLen = nextOffset;
  stackTop = nameStart;
}
void constWord(uint16_t addr) {
  if (stackTop < 2) return;
  uint8_t topLen = stack[stackTop - 2];
  uint8_t topType = stack[stackTop - 1];
  if (topType != TYPE_NAME) return;
  if (topLen > stackTop - 2) return;

  size_t nameStart = stackTop - 2 - topLen;
  const char* name = (const char*)&stack[nameStart];

  size_t recordSize = 2 + 1 + topLen + 1 + 1 + 4 + 4;
  if (dictLen + recordSize > DICT_SIZE) {
    Serial.println("⚠️ Dictionary full");
    return;
  }

  uint8_t* pos = &dictionary[dictLen];
  uint16_t nextOffset = dictLen + recordSize;

  pos[0] = (nextOffset >> 0) & 0xFF;
  pos[1] = (nextOffset >> 8) & 0xFF;
  pos[2] = topLen;
  memcpy(&pos[3], name, topLen);
  pos[3 + topLen] = 0x80 | STORAGE_CONST; // = 0x83
  pos[3 + topLen + 1] = currentContext; // context

  uint32_t poolRef = 0xFFFFFFFF; // ← именно так
  memcpy(&pos[3 + topLen + 2], &poolRef, 4);

  uint32_t funcAddr = (uint32_t)mychoiceFunc;
  memcpy(&pos[3 + topLen + 2 + 4], &funcAddr, 4);

  dictLen = nextOffset;
  stackTop = nameStart;
}
void arrayFunc(uint16_t addr) {
  // Ожидаем на стеке одно значение: count с типом элемента (например, 5u8)
  if (stackTop < 2) {
    Serial.println("⚠️ array: expected count with type suffix (e.g. 5u8)");
    // Кладём пустую заготовку (0 элементов)
    uint8_t dummy[3] = {TYPE_UINT8, 0, 0};
    pushValue(dummy, 3, TYPE_ARRAY);
    return;
  }

  uint8_t valueLen = stack[stackTop - 2];
  uint8_t valueType = stack[stackTop - 1];

  // Поддерживаем только целочисленные типы как тип элемента
  if (valueType != TYPE_UINT8 && valueType != TYPE_INT8 &&
      valueType != TYPE_UINT16 && valueType != TYPE_INT16 &&
      valueType != TYPE_INT) {
    Serial.println("⚠️ array: element type must be u8, i8, u16, i16, or i32");
    uint8_t dummy[3] = {TYPE_UINT8, 0, 0};
    pushValue(dummy, 3, TYPE_ARRAY);
    dropTop(0);
    return;
  }

  // Читаем количество элементов (всегда беззнаковое)
  uint16_t count = 0;
  if (valueType == TYPE_UINT8) {
    count = popUInt8();
  } else if (valueType == TYPE_INT8) {
    int8_t v = popInt8();
    count = (v < 0) ? 0 : (uint16_t)v;
  } else if (valueType == TYPE_UINT16) {
    count = popUInt16();
  } else if (valueType == TYPE_INT16) {
    int16_t v = popInt16();
    count = (v < 0) ? 0 : (uint16_t)v;
  } else if (valueType == TYPE_INT) {
    int32_t v = popInt();
    if (v < 0) v = 0;
    if (v > 65535) v = 65535;
    count = (uint16_t)v;
  }

  // Ограничиваем разумным максимумом (можно изменить)
  if (count == 0) {
    Serial.println("⚠️ array: count must be > 0");
    count = 1;
  }
  if (count > 1024) {
    Serial.println("⚠️ array: count too large (max 1024)");
    count = 1024;
  }

  // Формируем заготовку: [elemType][count_L][count_H]
  uint8_t arrayStub[3];
  arrayStub[0] = valueType;               // тип элемента
  arrayStub[1] = (uint8_t)(count & 0xFF); // младший байт
  arrayStub[2] = (uint8_t)(count >> 8);   // старший байт

  // Кладём на стек как TYPE_ARRAY (длина = 3)
  pushValue(arrayStub, 3, TYPE_ARRAY);
}
void letWord(uint16_t callerAddr) {
  if (stackTop < 2) return;
  uint8_t topLen = stack[stackTop - 2];
  uint8_t topType = stack[stackTop - 1];
  if (topType != TYPE_NAME) return;
  if (topLen > stackTop - 2) return;

  size_t nameStart = stackTop - 2 - topLen;
  const char* name = (const char*)&stack[nameStart];

  // Размер записи: next(2) + nameLen(1) + name(topLen) + storage(1) + context(1) + poolRef(4) + funcPtr(4)
  size_t recordSize = 2 + 1 + topLen + 1 + 1 + 4 + 4;
  if (tempDictLen + recordSize > TEMP_DICT_SIZE) {
    Serial.println("⚠️ Temp dictionary full");
    return;
  }

  uint8_t* pos = &tempDictionary[tempDictLen];
  uint16_t nextOffset = tempDictLen + recordSize;

  pos[0] = (nextOffset >> 0) & 0xFF;
  pos[1] = (nextOffset >> 8) & 0xFF;
  pos[2] = topLen;
  memcpy(&pos[3], name, topLen);
  pos[3 + topLen] = 0x80 | STORAGE_POOLED; // как var
  pos[3 + topLen + 1] = currentContext;    // текущий контекст

  uint32_t poolRef = 0xFFFFFFFF;
  memcpy(&pos[3 + topLen + 2], &poolRef, 4);

  uint32_t funcAddr = (uint32_t)mychoiceFunc;
  memcpy(&pos[3 + topLen + 2 + 4], &funcAddr, 4);

  tempDictLen = nextOffset;
  stackTop = nameStart;
}
