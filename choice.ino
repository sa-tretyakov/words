



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
  if (stackTop == 0) {
    readVariableAsValue(addr);
    return;
  }

  uint8_t Type, Len;
  const uint8_t* Data;
  if (!peekStackTop(&Type, &Len, &Data)) return;

  if (Type != TYPE_MARKER) {
    readVariableAsValue(addr);
    return;
  }

  // Доступ к элементу массива: word [ index ] или word [ index ] = value
  if (Len == 1 && Data[0] == '[') {
    handleArrayAccess(addr);
    return;
  }

  // Простое присваивание: word = value
  if (Len == 1 && Data[0] == '=') {
    handleAssignment(addr);
    return;
  }

  // Арифметические операции
  if (Len == 1 && Data[0] == '+') { dropTop(0); applyBinaryOp(addr, OP_ADD); return; }
  if (Len == 1 && Data[0] == '-') { dropTop(0); applyBinaryOp(addr, OP_SUB); return; }
  if (Len == 1 && Data[0] == '*') { dropTop(0); applyBinaryOp(addr, OP_MUL); return; }
  if (Len == 1 && Data[0] == '/') { dropTop(0); applyBinaryOp(addr, OP_DIV); return; }
  if (Len == 1 && Data[0] == '%') { dropTop(0); applyBinaryOp(addr, OP_MOD); return; } // ← новая строка

  // Сравнения — ИСПРАВЛЕНО: сначала кладём значение переменной, потом вызываем applyCompareOp
  if (Len == 2 && Data[0] == '=' && Data[1] == '=') {
    dropTop(0);
    readVariableAsValue(addr); // кладём значение переменной на стек
    applyCompareOp(CMP_EQ);
    return;
  }
  if (Len == 2 && Data[0] == '!' && Data[1] == '=') {
    dropTop(0);
    readVariableAsValue(addr);
    applyCompareOp(CMP_NE);
    return;
  }
  if (Len == 1 && Data[0] == '<') {
    dropTop(0);
    readVariableAsValue(addr);
    applyCompareOp(CMP_LT);
    return;
  }
  if (Len == 1 && Data[0] == '>') {
    dropTop(0);
    readVariableAsValue(addr);
    applyCompareOp(CMP_GT);
    return;
  }
  if (Len == 2 && Data[0] == '<' && Data[1] == '=') {
    dropTop(0);
    readVariableAsValue(addr);
    applyCompareOp(CMP_LE);
    return;
  }
  if (Len == 2 && Data[0] == '>' && Data[1] == '=') {
    dropTop(0);
    readVariableAsValue(addr);
    applyCompareOp(CMP_GE);
    return;
  }

  // Составное присваивание
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

  // По умолчанию
  readVariableAsValue(addr);
}




 
