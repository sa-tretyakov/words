void strInit() {
  String tmp = "cont strings";
  executeLine(tmp);
  // Слова GPIO
  addInternalWord("charAt", charAtWord);
  addInternalWord("char", charWord);
  addInternalWord("len", lenWord);
  addInternalWord("digit", digitWord);
  addInternalWord(">str", toStrWord);
  tmp = "main";
  executeLine(tmp);

}

void charAtWord(uint16_t addr) {
  // 1. Проверяем, что есть хотя бы 2 элемента
  if (stackTop < 4) {
    outputStream->println("⚠️ charAt: строка и индекс ожидаются на стеке");
    return;
  }

  // 2. Берём СТРОКУ (верх стека)
  uint8_t strType = stack[stackTop - 1];
  uint8_t strLen = stack[stackTop - 2];
  if (strType != TYPE_STRING) {
    outputStream->println("⚠️ charAt: верх стека должен быть строкой");
    return;
  }
  if (strLen > stackTop - 2) {
    handleStackUnderflow();
    return;
  }
  const uint8_t* strData = &stack[stackTop - 2 - strLen];

  // Удаляем строку со стека
  dropTop(0);

  // 3. Берём ИНДЕКС (теперь на верху)
  if (stackTop < 2) {
    outputStream->println("⚠️ charAt: индекс отсутствует");
    return;
  }

  uint8_t idxType = stack[stackTop - 1];
  uint8_t idxLen = stack[stackTop - 2];
  if (idxLen > stackTop - 2) {
    handleStackUnderflow();
    return;
  }
  const uint8_t* idxData = &stack[stackTop - 2 - idxLen];

  int32_t index = 0;
  if (idxType == TYPE_INT && idxLen == 4) memcpy(&index, idxData, 4);
  else if (idxType == TYPE_UINT8 && idxLen == 1) index = idxData[0];
  else if (idxType == TYPE_INT8 && idxLen == 1) index = (int8_t)idxData[0];
  else if (idxType == TYPE_UINT16 && idxLen == 2) {
    uint16_t v;
    memcpy(&v, idxData, 2);
    index = v;
  }
  else if (idxType == TYPE_INT16 && idxLen == 2) {
    int16_t v;
    memcpy(&v, idxData, 2);
    index = v;
  }
  else {
    outputStream->println("⚠️ charAt: индекс должен быть целым");
    dropTop(0);
    return;
  }

  dropTop(0);

  // 4. Проверка границ
  if (index < 0 || index >= strLen) {
    outputStream->println("⚠️ charAt: индекс вне диапазона");
    pushUInt8(0);
    return;
  }

  // 5. Результат
  pushUInt8(strData[index]);
}

void charWord(uint16_t addr) {
  uint8_t type, len;
  const uint8_t* data;
  if (!peekStackTop(&type, &len, &data)) return;
  
  // Поддерживаем только uint8 и int (в пределах 0-255)
  uint8_t code = 0;
  if (type == TYPE_UINT8 && len == 1) {
    code = data[0];
  }
  else if (type == TYPE_INT && len == 4) {
    int32_t v; memcpy(&v, data, 4);
    if (v < 0 || v > 255) {
      outputStream->println("⚠️ char: value out of range 0-255");
      return;
    }
    code = (uint8_t)v;
  }
  else {
    outputStream->println("⚠️ char: expected uint8 or int");
    return;
  }
  
  dropTop(0);
  
  // Кладём как строку длиной 1
  if (isStackOverflow(1 + 2)) {
    handleStackOverflow();
    return;
  }
  stack[stackTop++] = code;
  stack[stackTop++] = 1;
  stack[stackTop++] = TYPE_STRING;
}


void lenWord(uint16_t addr) {
  if (stackTop < 2) {
    outputStream->println("⚠️ len: значение отсутствует");
    return;
  }

  uint8_t type = stack[stackTop - 1];
  uint8_t len = stack[stackTop - 2];

  // Защита от повреждённого стека
  if (len > stackTop - 2) {
    handleStackUnderflow();
    return;
  }

  // Для массива: len = count * elemSize, но мы возвращаем длину блока данных
  if (type == TYPE_ARRAY) {
    if (len < 3) {
      pushUInt16(0);
      dropTop(0);
      return;
    }
    const uint8_t* data = &stack[stackTop - 2 - len];
    uint8_t elemType = data[0];
    uint16_t count = data[1] | (data[2] << 8);
    uint8_t elemSize = 1;
    if (elemType == TYPE_UINT16 || elemType == TYPE_INT16) elemSize = 2;
    else if (elemType == TYPE_INT) elemSize = 4;
    pushUInt16(count * elemSize);
    dropTop(0);
    return;
  }

  // Для всех остальных типов: длина уже известна
  dropTop(0);
  pushUInt16(len);
}

void digitWord(uint16_t addr) {
  if (stackTop < 2) {
    outputStream->println("⚠️ digit: значение отсутствует");
    return;
  }

  uint8_t type = stack[stackTop - 1];
  uint8_t len = stack[stackTop - 2];

  if (len > stackTop - 2) {
    handleStackUnderflow();
    return;
  }

  const uint8_t* data = &stack[stackTop - 2 - len];
  int32_t result = -1;

  if (type == TYPE_STRING) {
    if (len == 0) {
      // Пустая строка → ошибка
    } else {
      char c = (char)data[0];
      if (c >= '0' && c <= '9') {
        result = c - '0';
      }
    }
  }
  else if (type == TYPE_UINT8 && len == 1) {
    char c = (char)data[0];
    if (c >= '0' && c <= '9') {
      result = c - '0';
    }
  }
  else if (type == TYPE_INT8 && len == 1) {
    char c = (char)(int8_t)data[0];
    if (c >= '0' && c <= '9') {
      result = c - '0';
    }
  }

  dropTop(0);

  if (result >= 0) {
    pushInt(result);
  } else {
    outputStream->println("⚠️ digit: символ не является цифрой '0'-'9'");
    pushInt(0); // или pushInt(-1) для ошибки
  }
}

void toStrWord(uint16_t addr) {
  if (stackTop < 2) {
    pushString(""); // или ошибка, но лучше пустая строка
    return;
  }

  uint8_t type = stack[stackTop - 1];
  uint8_t len = stack[stackTop - 2];

  if (len > stackTop - 2) {
    dropTop(0);
    pushString("");
    return;
  }

  const uint8_t* data = &stack[stackTop - 2 - len];
  String s = "";

  if (type == TYPE_INT && len == 4) {
    int32_t v; memcpy(&v, data, 4); s = String(v);
  }
  else if (type == TYPE_UINT8 && len == 1) {
    s = String(data[0]);
  }
  else if (type == TYPE_INT8 && len == 1) {
    s = String((int8_t)data[0]);
  }
  else if (type == TYPE_UINT16 && len == 2) {
    uint16_t v; memcpy(&v, data, 2); s = String(v);
  }
  else if (type == TYPE_INT16 && len == 2) {
    int16_t v; memcpy(&v, data, 2); s = String(v);
  }
  else if (type == TYPE_FLOAT && len == 4) {
    float v; memcpy(&v, data, 4); s = String(v, 6);
  }
  else {
    s = ""; // неподдерживаемый тип → пустая строка
  }

  dropTop(0);
  pushString(s.c_str());
}
