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
