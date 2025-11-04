void printDictionary(uint16_t addr) {
  uint16_t ptr = 0;

  while (ptr < dictLen) {
    if (ptr + 2 > DICT_SIZE) break;
    uint16_t nextPtr = dictionary[ptr] | (dictionary[ptr + 1] << 8);
    if (nextPtr == 0 || nextPtr <= ptr || nextPtr > DICT_SIZE) break;

    if (ptr + 3 > DICT_SIZE) break;
    uint8_t nameLen = dictionary[ptr + 2];
    if (nameLen == 0 || ptr + 3 + nameLen + 2 > DICT_SIZE) break;

    uint8_t storage = dictionary[ptr + 3 + nameLen];
    uint8_t context  = dictionary[ptr + 3 + nameLen + 1];

    Serial.printf("@%04X->%04X: [%02d]", ptr, nextPtr, nameLen);

    // Печатаем имя как единое слово
    for (uint8_t i = 0; i < nameLen; i++) {
      char c = dictionary[ptr + 3 + i];
      if (c >= 32 && c <= 126) {
        Serial.print(c);
      } else {
        Serial.printf("\\x%02X", (uint8_t)c);
      }
    }

    // === ПЕЧАТЬ ТИПА СЛОВА ===
    Serial.print("[");
    if (storage & 0x80) {
      // Internal word — тип в младших 7 битах
      uint8_t stype = storage & 0x7F;
      if (stype == STORAGE_EMBEDDED) Serial.print("C");
      else if (stype == STORAGE_NAMED) Serial.print("N");
      else if (stype == STORAGE_POOLED) Serial.print("V");
      else if (stype == STORAGE_CONST) Serial.print("K");
      else if (stype == STORAGE_CONT) Serial.print("T");
      else Serial.print("I");
    } else {
      // External word — storage = 0, печатаем как [X]
      Serial.print("X");
    }
    Serial.print("]");

    // Печатаем контекст
    Serial.printf("[%d]", context);

    // Печатаем данные (poolRef, funcPtr, тело и т.д.)
    uint16_t restStart = ptr + 3 + nameLen + 2;
    if (nextPtr > restStart) {
      Serial.print(" ");
      printBytes(&dictionary[restStart], nextPtr - restStart);
    }
    Serial.println();

    ptr = nextPtr;
  }
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


bool addMarkerWord(const char* name) {
  size_t nameLen = strlen(name);
  if (nameLen == 0 || nameLen > 255) return false;

  // Размер: next(2) + nameLen(1) + name(nameLen) + storage(1) + context(1) + funcPtr(4)
  size_t recordSize = 2 + 1 + nameLen + 1 + 1 + 4;
  if (dictLen + recordSize > DICT_SIZE) return false;

  uint8_t* pos = &dictionary[dictLen];
  uint16_t nextOffset = dictLen + recordSize;

  pos[0] = (nextOffset >> 0) & 0xFF;
  pos[1] = (nextOffset >> 8) & 0xFF;
  pos[2] = nameLen;
  memcpy(&pos[3], name, nameLen);
  pos[3 + nameLen] = 0x80 | STORAGE_NAMED; // = 0x81
  pos[3 + nameLen + 1] = 0; // context = 0

  uint32_t funcAddr = (uint32_t)pushMarkerFunc;
  memcpy(&pos[3 + nameLen + 2], &funcAddr, 4);

  dictLen = nextOffset;
  return true;
}

void pushMarker(const char* name) {
  if (!name) return;
  size_t len = strlen(name);
  if (len > 255) len = 255;
  if (isStackOverflow(len + 2)) handleStackOverflow();
  memcpy(&stack[stackTop], name, len);
  stackTop += len;
  stack[stackTop++] = (uint8_t)len;
  stack[stackTop++] = TYPE_MARKER;
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

bool addInternalWord(const char* name, WordFunc func) {
  return addInternalWord(name, func, 0);
  }
bool addInternalWord(const char* name, WordFunc func, uint8_t context) {
  size_t nameLen = strlen(name);
  if (nameLen == 0 || nameLen > 255) return false;

  // 2 (next) + 1 (nameLen) + nameLen + 1 (storage) + 1 (context) + 4 (funcPtr)
  size_t recordSize = 2 + 1 + nameLen + 1 + 1 + 4;
  if (dictLen + recordSize > DICT_SIZE) return false;

  uint8_t* pos = &dictionary[dictLen];
  uint16_t next = dictLen + recordSize;

  pos[0] = (next >> 0) & 0xFF;
  pos[1] = (next >> 8) & 0xFF;
  pos[2] = (uint8_t)nameLen;
  memcpy(pos + 3, name, nameLen);
  pos[3 + nameLen] = 0x80 | STORAGE_EMBEDDED; // storage = 0x80
  pos[3 + nameLen + 1] = context; // ← ЭТОТ БАЙТ — КОНТЕКСТ (был пропущен)

  uint32_t addr = (uint32_t)func;
  memcpy(pos + 3 + nameLen + 2, &addr, 4); // funcPtr после context

  dictLen = next;
  return true;
}

void wordsWord(uint16_t addr) {
  Serial.print("Words in context ");
  Serial.print(currentContext);
  Serial.println(":");
  
  uint16_t ptr = 0;
  int count = 0;
  int lineLen = 0;
  const int MAX_LINE_LEN = 60;

  while (ptr < dictLen) {
    if (ptr + 2 > DICT_SIZE) break;
    uint16_t nextPtr = dictionary[ptr] | (dictionary[ptr + 1] << 8);
    if (nextPtr == 0 || nextPtr <= ptr || nextPtr > DICT_SIZE) break;

    if (ptr + 3 > DICT_SIZE) break;
    uint8_t nameLen = dictionary[ptr + 2];
    if (nameLen == 0 || ptr + 3 + nameLen > DICT_SIZE) break;

    // Читаем контекст: после storage(1)
    uint8_t context = dictionary[ptr + 3 + nameLen + 1];

    // Пропускаем, если не наш контекст
    if (context != currentContext) {
      ptr = nextPtr;
      continue;
    }

    // Длина с пробелом
    int wordLen = nameLen + 1;

    if (lineLen + wordLen > MAX_LINE_LEN && lineLen > 0) {
      Serial.println();
      lineLen = 0;
    }

    for (uint8_t i = 0; i < nameLen; i++) {
      Serial.print((char)dictionary[ptr + 3 + i]);
    }
    Serial.print(' ');
    lineLen += wordLen;
    count++;

    ptr = nextPtr;
  }

  if (lineLen > 0) Serial.println();
  Serial.printf("(%d words)\n", count);
}

void contWord(uint16_t addr) {
  uint8_t nameType, nameLen;
  const uint8_t* nameData;
  if (!peekStackTop(&nameType, &nameLen, &nameData)) return;
  if (nameType != TYPE_NAME) return;
  dropTop(0);
  maxCont++;              // увеличиваем счётчик
  currentContext = maxCont; // контекст = текущее значение maxCont
  size_t recordSize = 2 + 1 + nameLen + 1 + 1 + 4 + 4;
  if (dictLen + recordSize > DICT_SIZE) {
    maxCont--;            // откат
    currentContext = maxCont - 1;
    return;
  }
  uint8_t* pos = &dictionary[dictLen];
  uint16_t nextOffset = dictLen + recordSize;
  pos[0] = (nextOffset >> 0) & 0xFF;
  pos[1] = (nextOffset >> 8) & 0xFF;
  pos[2] = nameLen;
  memcpy(&pos[3], nameData, nameLen);
  pos[3 + nameLen] = 0x80 | STORAGE_CONT; // 0x84
  pos[3 + nameLen + 1] = 0;
  memcpy(&pos[3 + nameLen + 2], &maxCont, 4); // значение = maxCont
  uint32_t funcAddr = (uint32_t)mychoiceFunc;
  memcpy(&pos[3 + nameLen + 2 + 4], &funcAddr, 4);
  dictLen = nextOffset;
}

void contextWord(uint16_t addr) {
  uint8_t valType, valLen;
  const uint8_t* valData;
  if (!peekStackTop(&valType, &valLen, &valData)) return;
  dropTop(0);
  // Преобразуем в uint8_t
  uint8_t ctx = 0;
  if (valType == TYPE_INT && valLen == 4) {
    int32_t v; memcpy(&v, valData, 4);
    if (v < 0) v = 0;
    if (v > 255) v = 255;
    ctx = (uint8_t)v;
  }
  else if (valType == TYPE_UINT8 && valLen == 1) {
    ctx = valData[0];
  }
  else if (valType == TYPE_INT8 && valLen == 1) {
    int8_t v = (int8_t)valData[0];
    if (v < 0) v = 0;
    ctx = (uint8_t)v;
  }
  else if (valType == TYPE_UINT16 && valLen == 2) {
    uint16_t v; memcpy(&v, valData, 2);
    if (v > 255) v = 255;
    ctx = (uint8_t)v;
  }
  else if (valType == TYPE_INT16 && valLen == 2) {
    int16_t v; memcpy(&v, valData, 2);
    if (v < 0) v = 0;
    if (v > 255) v = 255;
    ctx = (uint8_t)v;
  }
  else {
    Serial.println("⚠️ context: invalid value");
    return;
  }
  currentContext = ctx;
}
