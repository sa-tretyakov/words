void switchContextFunc(uint16_t addr) {
  // Читаем сохранённый номер контекста из тела слова
  uint8_t nameLen = dictionary[addr + 2];
  uint32_t storedContext =
    dictionary[addr + 3 + nameLen + 2 + 0] |
    (dictionary[addr + 3 + nameLen + 2 + 1] << 8) |
    (dictionary[addr + 3 + nameLen + 2 + 2] << 16) |
    (dictionary[addr + 3 + nameLen + 2 + 3] << 24);

  // Устанавливаем текущий контекст
  currentContext = (uint8_t)storedContext;

  // Опционально: можно класть подтверждение на стек, но не обязательно
  // pushUInt8(currentContext);
}

void printDictionary(uint16_t addr) {
  printDictSection(dictionary, dictLen, DICT_SIZE, "=== Основной словарь ===", "@");
  printDictSection(tempDictionary, tempDictLen, TEMP_DICT_SIZE, "=== Временный словарь ===", "@T");
}

void printDictSection(
  const uint8_t* dict,
  uint16_t dictLen,
  uint16_t maxSize,
  const char* title,
  const char* addrPrefix
) {
  if (dictLen == 0) return;

  outputStream->println();
  outputStream->println(title);
  
  uint16_t ptr = 0;
  while (ptr < dictLen) {
    if (ptr + 2 > maxSize) break;
    uint16_t nextPtr = dict[ptr] | (dict[ptr + 1] << 8);
    if (nextPtr == 0 || nextPtr <= ptr || nextPtr > maxSize) break;
    if (ptr + 3 > maxSize) break;
    uint8_t nameLen = dict[ptr + 2];
    if (nameLen == 0 || ptr + 3 + nameLen + 2 > maxSize) break;

    uint8_t storage = dict[ptr + 3 + nameLen];
    uint8_t context = dict[ptr + 3 + nameLen + 1];

    outputStream->printf("%s%04X->%s%04X: [%02d]", addrPrefix, ptr, addrPrefix, nextPtr, nameLen);
    
    for (uint8_t i = 0; i < nameLen; i++) {
      char c = dict[ptr + 3 + i];
      if (c >= 32 && c <= 126) outputStream->print(c);
      else outputStream->printf("\\x%02X", (uint8_t)c);
    }

    outputStream->print("[");
    if (storage & 0x80) {
      uint8_t stype = storage & 0x7F;
      if (stype == STORAGE_EMBEDDED) outputStream->print("C");
      else if (stype == STORAGE_NAMED) outputStream->print("N");
      else if (stype == STORAGE_POOLED) outputStream->print("V");
      else if (stype == STORAGE_CONST) outputStream->print("K");
      else if (stype == STORAGE_CONT) outputStream->print("T");
      else outputStream->print("I");
    } else {
      outputStream->print("X");
    }
    outputStream->print("]");
    outputStream->printf("[%d]", context);

    uint16_t restStart = ptr + 3 + nameLen + 2;
    if (nextPtr > restStart) {
      outputStream->print(" ");
      printBytes(&dict[restStart], nextPtr - restStart);
    }
    outputStream->println();
    ptr = nextPtr;
  }
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
  pos[3 + nameLen + 1] = currentContext; // context = 0

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


bool addInternalWord(const char* name, WordFunc func) {
  return addInternalWord(name, func, currentContext);
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
  outputStream->print("Words in context ");
  outputStream->print(currentContext);
  outputStream->println(":");
  
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
      outputStream->println();
      lineLen = 0;
    }

    for (uint8_t i = 0; i < nameLen; i++) {
      outputStream->print((char)dictionary[ptr + 3 + i]);
    }
    outputStream->print(' ');
    lineLen += wordLen;
    count++;

    ptr = nextPtr;
  }

  if (lineLen > 0) outputStream->println();
  outputStream->printf("(%d words)\n", count);
}
