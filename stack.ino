void contFunc(uint16_t addr) {
  // Читаем значение (номер контекста) из записи в словаре
  uint8_t nameLen = dictionary[addr + 2];
  // Значение начинается после: [next][nameLen][name][storage][context] → смещение = 3 + nameLen + 2
  uint32_t value =
    dictionary[addr + 3 + nameLen + 2 + 0] |
    (dictionary[addr + 3 + nameLen + 2 + 1] << 8) |
    (dictionary[addr + 3 + nameLen + 2 + 2] << 16) |
    (dictionary[addr + 3 + nameLen + 2 + 3] << 24);
  
  // Устанавливаем текущий контекст
  currentContext = (uint8_t)value;
}
void seetimeWord(uint16_t addr) {
  uint8_t type, len;
  const uint8_t* data;
  
  // Берём значение с вершины стека (оно уже там, потому что строка: "seetime true")
  if (!peekStackTop(&type, &len, &data)) {
    Serial.println("⚠️ seetime: ожидается true или false после команды");
    return;
  }

  // Преобразуем ЛЮБОЙ тип к логическому значению (уже есть в коде!)
  bool enable = valueToBool(type, len, data);

  // Убираем аргумент со стека
  dropTop(0);

  // Применяем режим
  seetimeMode = enable;

  Serial.print("⏱️ seetime: ");
  Serial.println(enable ? "ON" : "OFF");
}

void whileFunc(uint16_t addr) {
  // 1. Pop смещение выхода (i16 или int32)
  int32_t offsetExit;
  if (!popInt32FromAny(&offsetExit)) {
    // Если не смогли прочитать — выходим (ошибка)
    return;
  }

  // 2. Pop условие (должно быть уже на стеке)
  uint8_t condType, condLen;
  const uint8_t* condData;
  if (!peekStackTop(&condType, &condLen, &condData)) {
    return;
  }
  dropTop(0); // убираем условие со стека

  // 3. Преобразуем условие в bool

bool condition = valueToBool(condType, condLen, condData);

  // 4. Если условие ЛОЖНО — выходим из цикла (делаем goto с offsetExit)
  if (!condition) {
    // Кладём смещение на стек и вызываем goto
    pushInt(offsetExit);
    gotoFunc(addr);
  }
  // Если ИСТИННО — ничего не делаем, выполнение идёт к телу
}



void gotoFunc(uint16_t addr) {
  int32_t offset;
  if (!popInt32FromAny(&offset)) return;
  jumpOffset = offset;
  shouldJump = true;
}
