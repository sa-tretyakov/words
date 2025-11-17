void varsInit() {
  String tmp = "cont vars";
  executeLine(tmp);
  addInternalWord("var", varWord);
  addInternalWord("const", constWord);
  addInternalWord("array", arrayFunc);
  addMarkerWord("[");
  addMarkerWord("]");
  addInternalWord("let", letWord);
  addMarkerWord("u8");
  addMarkerWord("i8");
  addMarkerWord("u16");
  addMarkerWord("i16");
  addMarkerWord("i32");
   tmp = "main";
  executeLine(tmp);
}




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
  uint8_t typeMarkerType, typeMarkerLen;
  const uint8_t* typeMarkerData;
  if (!peekStackTop(&typeMarkerType, &typeMarkerLen, &typeMarkerData)) {
    Serial.println("⚠️ array: expected type marker (e.g., u8, i8, u16, i16, i32)");
    uint8_t dummy[3] = {TYPE_UINT8, 0, 0};
    pushValue(dummy, 3, TYPE_ARRAY);
    return;
  }
  if (typeMarkerType != TYPE_MARKER) {
     Serial.println("⚠️ array: expected type marker (e.g., u8, i8, u16, i16, i32), got type:"); Serial.println(typeMarkerType);
     uint8_t dummy[3] = {TYPE_UINT8, 0, 0};
     pushValue(dummy, 3, TYPE_ARRAY);
     dropTop(0);
     return;
  }
  String typeName = String((char*)typeMarkerData, typeMarkerLen);

  uint8_t elemType = TYPE_UINT8; // Значение по умолчанию
  if (typeName == "u8") elemType = TYPE_UINT8;
  else if (typeName == "i8") elemType = TYPE_INT8;
  else if (typeName == "u16") elemType = TYPE_UINT16;
  else if (typeName == "i16") elemType = TYPE_INT16;
  else if (typeName == "i32") elemType = TYPE_INT;

  if (elemType != TYPE_UINT8 && elemType != TYPE_INT8 &&
      elemType != TYPE_UINT16 && elemType != TYPE_INT16 &&
      elemType != TYPE_INT) {
    Serial.println("⚠️ array: element type must be u8, i8, u16, i16, or i32");
    uint8_t dummy[3] = {TYPE_UINT8, 0, 0};
    pushValue(dummy, 3, TYPE_ARRAY);
    dropTop(0);
    return;
  }
  dropTop(0);

  if (stackTop < 2) {
    Serial.println("⚠️ array: expected count after type marker");
    uint8_t dummy[3] = {elemType, 0, 0};
    pushValue(dummy, 3, TYPE_ARRAY);
    return;
  }

  int32_t count_raw;
  if (!popInt32FromAny(&count_raw)) {
     Serial.println("⚠️ array: count must be an integer");
     uint8_t dummy[3] = {elemType, 0, 0};
     pushValue(dummy, 3, TYPE_ARRAY);
     return;
  }

  uint16_t count = 0;
  if (count_raw < 0) {
      Serial.println("⚠️ array: count must be >= 0");
      count = 0;
  } else if (count_raw > 65535) {
      Serial.println("⚠️ array: count too large (max 65535)");
      count = 65535;
  } else {
      count = (uint16_t)count_raw;
  }

  if (count == 0) {
    Serial.println("⚠️ array: count must be > 0");
    count = 1;
  }


  uint8_t arrayStub[3];
  arrayStub[0] = elemType;
  arrayStub[1] = (uint8_t)(count & 0xFF);
  arrayStub[2] = (uint8_t)(count >> 8);

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
