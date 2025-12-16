/*
void contWord(uint16_t addr) {
  uint8_t nameType, nameLen;
  const uint8_t* nameData;
  if (!peekStackTop(&nameType, &nameLen, &nameData) || nameType != TYPE_NAME) {
    outputStream->println("⚠️ cont: ожидается имя");
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
  // outputStream->printf("Контекст '%.*s' = %d активен\n", nameLen, nameData, maxCont);
}
*/
