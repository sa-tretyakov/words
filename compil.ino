// Возвращает смещение слова в словаре по его имени.
// Если слово не найдено — возвращает 0xFFFF.
uint16_t findWordAddress(const char* name) {
  uint8_t nameLen = strlen(name);
  uint16_t ptr = 0;

  while (ptr < dictLen) {
    if (ptr + 2 >= DICT_SIZE) break;
    uint16_t nextPtr = dictionary[ptr] | (dictionary[ptr + 1] << 8);
    if (nextPtr == 0 || nextPtr <= ptr || nextPtr > DICT_SIZE) break;

    if (ptr + 3 > DICT_SIZE) break;
    uint8_t len = dictionary[ptr + 2];
    if (len != nameLen || ptr + 3 + len > DICT_SIZE) {
      ptr = nextPtr;
      continue;
    }

    // Сравниваем имя
    bool match = true;
    for (uint8_t i = 0; i < nameLen; i++) {
      if (dictionary[ptr + 3 + i] != name[i]) {
        match = false;
        break;
      }
    }
    if (match) {
      return ptr; // ← смещение слова в словаре
    }

    ptr = nextPtr;
  }

  return 0xFFFF; // не найдено
}


void colonWord(uint16_t addr) {
  // Следующий токен — имя слова
  if (stackTop < 2) return;
  uint8_t nameLen = stack[stackTop - 2];
  uint8_t nameType = stack[stackTop - 1];
  if (nameType != TYPE_NAME) return;
  if (nameLen > stackTop - 2) return;

  size_t nameStart = stackTop - 2 - nameLen;
  const char* name = (const char*)&stack[nameStart];

  // Создаём запись external-слова
  // Формат: [next][nameLen][name][storage=0][context=0][тело... 00 00]
  size_t headerSize = 2 + 1 + nameLen + 1 + 1; // next+len+name+storage+context
  size_t recordSize = headerSize + 2; // минимум: 00 00 (пустое тело)

  if (dictLen + recordSize > DICT_SIZE) {
    Serial.println("⚠️ Dictionary full");
    return;
  }

  uint8_t* pos = &dictionary[dictLen];
  uint16_t nextOffset = dictLen + recordSize;

  pos[0] = (nextOffset >> 0) & 0xFF;
  pos[1] = (nextOffset >> 8) & 0xFF;
  pos[2] = nameLen;
  memcpy(&pos[3], name, nameLen);
  pos[3 + nameLen] = 0; // storage = 0 (external)
  pos[3 + nameLen + 1] = 0; // context = 0

  // Пустое тело: 00 00
  pos[headerSize] = 0x00;
  pos[headerSize + 1] = 0x00;

  compileTarget = dictLen;
  dictLen = nextOffset;
  stackTop = nameStart;

  compiling = true;
}

void semicolonWord(uint16_t addr) {
  if (!compiling) {
    Serial.println("⚠️ ; outside compilation");
    return;
  }

  compiling = false;
  // Тело уже собрано в executeLine (см. ниже)
}
