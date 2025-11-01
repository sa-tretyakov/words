



// === ВСПОМОГАТЕЛЬНЫЕ ФУНКЦИИ ДЛЯ mychoiceFunc ===

void pushZeroForType(uint8_t elemType) {
  if (elemType == TYPE_UINT8 || elemType == TYPE_INT8) pushUInt8(0);
  else if (elemType == TYPE_UINT16 || elemType == TYPE_INT16) pushUInt16(0);
  else pushInt(0);
}






// === ОСНОВНАЯ ФУНКЦИЯ ===

/*
 * mychoiceFunc — центральный обработчик вызова слова в Words.
 * 
 * Вызывается при обращении к любому слову (переменной, константе, функции).
 * Поведение зависит от содержимого стека:
 *   - если стек пуст → возвращает значение слова,
 *   - если на стеке маркер (например, =, +, [) → применяет операцию,
 *   - если на стеке обычное значение → возвращает значение слова.
 * 
 * Аргумент:
 *   addr — смещение слова в словаре.
 */
void mychoiceFunc(uint16_t addr) {
  // Случай 1: стек пуст — просто возвращаем значение переменной/константы
  if (stackTop == 0) {
    readVariableAsValue(addr);
    return;
  }
  // Случай 2: читаем верхушку стека
  uint8_t Type, Len;
  const uint8_t* Data;
  if (!peekStackTop(&Type, &Len, &Data)) return;

  // Случай 3: на стеке не маркер — возвращаем значение слова
  if (Type != TYPE_MARKER) {
    readVariableAsValue(addr);
    return;
  }

  // Случай 4: на стеке маркер — обрабатываем операцию

  // Доступ к элементу массива: word [ index ] или word [ index ] = value
  if (Len == 1 && Data[0] == '[') {
    //Serial.println("Обработка массива");
    handleArrayAccess(addr);
    return;
  }

  // Простое присваивание: word = value
  if (Len == 1 && Data[0] == '=') {
    //Serial.println("Простое присваевание");
    handleAssignment(addr);
    return;
  }

  // Арифметические операции: word + value, word - value, и т.д.
  if (Len == 1 && Data[0] == '+') { dropTop(0); applyBinaryOp(addr, OP_ADD); return; }
  if (Len == 1 && Data[0] == '-') { dropTop(0); applyBinaryOp(addr, OP_SUB); return; }
  if (Len == 1 && Data[0] == '*') { dropTop(0); applyBinaryOp(addr, OP_MUL); return; }
  if (Len == 1 && Data[0] == '/') { dropTop(0); applyBinaryOp(addr, OP_DIV); return; }

  // Операции сравнения: word == value, word != value, и т.д.
  if (Len == 2 && Data[0] == '=' && Data[1] == '=') { dropTop(0); applyCompareOp(addr, CMP_EQ); return; }
  if (Len == 2 && Data[0] == '!' && Data[1] == '=') { dropTop(0); applyCompareOp(addr, CMP_NE); return; }
  if (Len == 1 && Data[0] == '<') { dropTop(0); applyCompareOp(addr, CMP_LT); return; }
  if (Len == 1 && Data[0] == '>') { dropTop(0); applyCompareOp(addr, CMP_GT); return; }
  if (Len == 2 && Data[0] == '<' && Data[1] == '=') { dropTop(0); applyCompareOp(addr, CMP_LE); return; }
  if (Len == 2 && Data[0] == '>' && Data[1] == '=') { dropTop(0); applyCompareOp(addr, CMP_GE); return; }

  // Составное присваивание: word += value, word -= value, и т.д.
  if (Len == 2 && Data[0] == '+' && Data[1] == '=') {
    dropTop(0);
    if (!peekStackTop(&Type, &Len, &Data)) return;
    applyBinaryOp(addr, OP_ADD);
    if (peekStackTop(&Type, &Len, &Data)) { storeValueToVariable(addr, Data, Len, Type); dropTop(0); }
    return;
  }
  if (Len == 2 && Data[0] == '-' && Data[1] == '=') {
    dropTop(0);
    if (!peekStackTop(&Type, &Len, &Data)) return;
    applyBinaryOp(addr, OP_SUB);
    if (peekStackTop(&Type, &Len, &Data)) { storeValueToVariable(addr, Data, Len, Type); dropTop(0); }
    return;
  }
  if (Len == 2 && Data[0] == '*' && Data[1] == '=') {
    dropTop(0);
    if (!peekStackTop(&Type, &Len, &Data)) return;
    applyBinaryOp(addr, OP_MUL);
    if (peekStackTop(&Type, &Len, &Data)) { storeValueToVariable(addr, Data, Len, Type); dropTop(0); }
    return;
  }
  if (Len == 2 && Data[0] == '/' && Data[1] == '=') {
    dropTop(0);
    if (!peekStackTop(&Type, &Len, &Data)) return;
    applyBinaryOp(addr, OP_DIV);
    if (peekStackTop(&Type, &Len, &Data)) { storeValueToVariable(addr, Data, Len, Type); dropTop(0); }
    return;
  }

  // По умолчанию: неизвестный маркер — просто возвращаем значение слова
  readVariableAsValue(addr);
}

 
