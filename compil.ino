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
