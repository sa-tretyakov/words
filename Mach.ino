void mathInit() {
  String tmp = "cont math";
  executeLine(tmp);
  addMarkerWord("=");
  addMarkerWord("+");
  addMarkerWord("-");
  addMarkerWord("*");
  addMarkerWord("/");
  addMarkerWord("+=");
  addMarkerWord("-=");
  addMarkerWord("*=");
  addMarkerWord("/=");
  addMarkerWord("%");
 tmp = "main";
  executeLine(tmp);
}


void tmpLit() {
  // Создаём временное слово "tmpLit"
  uint8_t name[] = {'t', 'm', 'p', 'L', 'i', 't'};
  uint8_t nameLen = 6;
  size_t recordSize = 2 + 1 + nameLen + 1 + 1 + 4 + 4; // next+len+name+storage+context+poolRef+funcPtr

  if (dictLen + recordSize <= DICT_SIZE) {
    uint8_t* pos = &dictionary[dictLen];
    uint16_t next = dictLen + recordSize;

    pos[0] = (next >> 0) & 0xFF;
    pos[1] = (next >> 8) & 0xFF;
    pos[2] = nameLen;
    memcpy(&pos[3], name, nameLen);
    pos[3 + nameLen] = 0x80 | STORAGE_POOLED; // internal + pooled
    pos[3 + nameLen + 1] = 0; // context

    // === СРАЗУ ВЫДЕЛЯЕМ МЕСТО В dataPool (256 байт) ===
    uint32_t poolRef = 0xFFFFFFFF;
    if (dataPoolPtr + 256 <= DATA_POOL_SIZE) {
      poolRef = dataPoolPtr;
      dataPoolPtr += 256; // резервируем блок
    } else {
      outputStream->println("⚠️ dataPool full for tmpLit");
    }
    memcpy(&pos[3 + nameLen + 2], &poolRef, 4);

    uint32_t funcAddr = (uint32_t)mychoiceFunc;
    memcpy(&pos[3 + nameLen + 2 + 4], &funcAddr, 4);

    ADDR_TMP_LIT = dictLen;
    dictLen = next;
  }
}

void notWord(uint16_t addr) {
  // Поддерживаемые типы
  if (stackTop < 2) return;

  uint8_t len = stack[stackTop - 2];
  uint8_t type = stack[stackTop - 1];

  // Читаем значение в зависимости от типа
  if (type == TYPE_BOOL && len == 1) {
    bool val = popBool();
    pushBool(!val);
  }
  else if (type == TYPE_INT && len == 4) {
    int32_t val = popInt();
    pushInt(val ? 0 : 1);
  }
  else if (type == TYPE_UINT8 && len == 1) {
    uint8_t val = popUInt8();
    pushUInt8(val ? 0 : 1);
  }
  else if (type == TYPE_INT8 && len == 1) {
    int8_t val = popInt8();
    pushInt8(val ? 0 : 1);
  }
  else if (type == TYPE_UINT16 && len == 2) {
    uint16_t val = popUInt16();
    pushUInt16(val ? 0 : 1);
  }
  else if (type == TYPE_INT16 && len == 2) {
    int16_t val = popInt16();
    pushInt16(val ? 0 : 1);
  }
  else if (type == TYPE_FLOAT && len == 4) {
    float val = popFloat();
    pushFloat(val ? 0.0f : 1.0f);
  }
  else {
    // Неподдерживаемый тип — ничего не делаем
    return;
  }
}

bool compareResult(int cmp, uint8_t op) {
  switch (op) {
    case CMP_EQ: return (cmp == 0);
    case CMP_NE: return (cmp != 0);
    case CMP_LT: return (cmp < 0);
    case CMP_GT: return (cmp > 0);
    case CMP_LE: return (cmp <= 0);
    case CMP_GE: return (cmp >= 0);
    default: return false;
  }
}

bool peekStackTop(uint8_t* outType, uint8_t* outLen, const uint8_t** outData) {
  if (stackTop < 2) return false;
  *outLen = stack[stackTop - 2];
  *outType = stack[stackTop - 1];
  if (*outLen > stackTop - 2) return false;
  *outData = &stack[stackTop - 2 - *outLen];
  return true;
}

bool readVariableValue(uint16_t addr, uint8_t* outType, uint8_t* outLen, const uint8_t** outData) {
  uint8_t nameLen = dictionary[addr + 2];
  uint32_t poolRef =
    dictionary[addr + 3 + nameLen + 2 + 0] |
    (dictionary[addr + 3 + nameLen + 2 + 1] << 8) |
    (dictionary[addr + 3 + nameLen + 2 + 2] << 16) |
    (dictionary[addr + 3 + nameLen + 2 + 3] << 24);
  if (poolRef == 0xFFFFFFFF) return false;
  if (poolRef >= DATA_POOL_SIZE) return false;
  *outType = dataPool[poolRef];
  *outLen = dataPool[poolRef + 1];
  if (poolRef + 2 + *outLen > DATA_POOL_SIZE) return false;
  *outData = &dataPool[poolRef + 2];
  return true;
}

void pushValue(const uint8_t* data, uint8_t len, uint8_t type) {
  if (isStackOverflow(len + 2)) {
    handleStackOverflow();
    return;
  }
  memcpy(&stack[stackTop], data, len);
  stackTop += len;
  stack[stackTop++] = len;
  stack[stackTop++] = type;
}

void storeValueToVariable(uint16_t addr, const uint8_t* data, uint8_t len, uint8_t type) {
  uint8_t nameLen = dictionary[addr + 2];
  uint32_t oldPoolRef =
    dictionary[addr + 3 + nameLen + 2 + 0] |
    (dictionary[addr + 3 + nameLen + 2 + 1] << 8) |
    (dictionary[addr + 3 + nameLen + 2 + 2] << 16) |
    (dictionary[addr + 3 + nameLen + 2 + 3] << 24);

  // === Особая обработка для tmpLit: всегда перезаписываем в заранее выделенном блоке ===
  if (addr == ADDR_TMP_LIT) {
    // Убедимся, что блок выделен (при первом вызове)
    if (oldPoolRef == 0xFFFFFFFF) {
      // Выделяем блок максимального размера (256 байт)
      if (dataPoolPtr + 256 > DATA_POOL_SIZE) {
        outputStream->println("⚠️ dataPool full for tmpLit");
        return;
      }
      uint16_t newOffset = dataPoolPtr;
      dataPoolPtr += 256;
      // Обновляем poolRef в словаре
      dictionary[addr + 3 + nameLen + 2 + 0] = (newOffset >> 0) & 0xFF;
      dictionary[addr + 3 + nameLen + 2 + 1] = (newOffset >> 8) & 0xFF;
      dictionary[addr + 3 + nameLen + 2 + 2] = 0;
      dictionary[addr + 3 + nameLen + 2 + 3] = 0;
      oldPoolRef = newOffset;
    }

    // Проверяем, что данных хватает
    if (oldPoolRef + 2 + len <= DATA_POOL_SIZE) {
      dataPool[oldPoolRef] = type;
      dataPool[oldPoolRef + 1] = len;
      memcpy(&dataPool[oldPoolRef + 2], data, len);
    }
    return;
  }

  // === Обычная логика для других переменных ===
  if (oldPoolRef != 0xFFFFFFFF && oldPoolRef < DATA_POOL_SIZE) {
    uint8_t oldType = dataPool[oldPoolRef];
    uint8_t oldLen = dataPool[oldPoolRef + 1];
    if (oldType == type && oldLen == len) {
      memcpy(&dataPool[oldPoolRef + 2], data, len);
      return;
    }
  }

  // Выделяем новое место
  uint16_t needed = 2 + len;
  if (dataPoolPtr + needed > DATA_POOL_SIZE) {
    outputStream->println("⚠️ dataPool full");
    return;
  }

  uint16_t newOffset = dataPoolPtr;
  dataPool[dataPoolPtr++] = type;
  dataPool[dataPoolPtr++] = len;
  memcpy(&dataPool[dataPoolPtr], data, len);
  dataPoolPtr += len;

  dictionary[addr + 3 + nameLen + 2 + 0] = (newOffset >> 0) & 0xFF;
  dictionary[addr + 3 + nameLen + 2 + 1] = (newOffset >> 8) & 0xFF;
  dictionary[addr + 3 + nameLen + 2 + 2] = 0;
  dictionary[addr + 3 + nameLen + 2 + 3] = 0;
}


void applyBinaryOp(uint16_t addr, uint8_t op) {
  uint8_t argType, argLen;
  const uint8_t* argData;
  if (!peekStackTop(&argType, &argLen, &argData)) {
    uint8_t Type, Len;
    const uint8_t* Data;
    if (readVariableValue(addr, &Type, &Len, &Data)) {
      pushValue(Data, Len, Type);
    }
    return;
  }

  uint8_t varType, varLen;
  const uint8_t* varData;
  if (!readVariableValue(addr, &varType, &varLen, &varData)) {
    return;
  }

  // === int32 ===
  if (argType == TYPE_INT && varType == TYPE_INT && argLen == 4 && varLen == 4) {
    int32_t a, b;
    memcpy(&a, argData, 4);   // правый операнд (со стека)
    memcpy(&b, varData, 4);   // левый операнд (значение переменной)
    dropTop(0);
    switch (op) {
      case OP_ADD: pushInt(b + a); break;
      case OP_SUB: pushInt(b - a); break;
      case OP_MUL: pushInt(b * a); break;
      case OP_DIV: pushInt(a != 0 ? b / a : 0); break;
      case OP_MOD: pushInt(a != 0 ? b % a : 0); break;
    }
    return;
  }

  // === int8 ===
  if (argType == TYPE_INT8 && varType == TYPE_INT8 && argLen == 1 && varLen == 1) {
    int8_t a = (int8_t)argData[0];   // правый
    int8_t b = (int8_t)varData[0];   // левый
    dropTop(0);
    switch (op) {
      case OP_ADD: {
          if ((b > 0 && a > INT8_MAX - b) || (b < 0 && a < INT8_MIN - b)) {
            pushInt((int32_t)b + (int32_t)a);
          } else {
            pushInt8(b + a);
          }
          break;
        }
      case OP_SUB: {
          if ((a > 0 && b < INT8_MIN + a) || (a < 0 && b > INT8_MAX + a)) {
            pushInt((int32_t)b - (int32_t)a);
          } else {
            pushInt8(b - a);
          }
          break;
        }
      case OP_MUL: {
          int32_t res = (int32_t)b * (int32_t)a;
          if (res >= INT8_MIN && res <= INT8_MAX) {
            pushInt8((int8_t)res);
          } else {
            pushInt(res);
          }
          break;
        }
      case OP_DIV: {
          if (a != 0) {
            pushInt8(b / a);
          } else {
            pushInt8(0);
          }
          break;
        }
      case OP_MOD: {
          if (a != 0) {
            pushInt8(b % a);
          } else {
            pushInt8(0);
          }
          break;
        }
    }
    return;
  }

  // === uint8 ===
  if (argType == TYPE_UINT8 && varType == TYPE_UINT8 && argLen == 1 && varLen == 1) {
    uint8_t a = argData[0];   // правый
    uint8_t b = varData[0];   // левый
    dropTop(0);
    switch (op) {
      case OP_ADD: {
          if (b > UINT8_MAX - a) {
            pushInt((int32_t)b + (int32_t)a);
          } else {
            pushUInt8(b + a);
          }
          break;
        }
      case OP_SUB: {
          if (b < a) {
            pushInt((int32_t)b - (int32_t)a);
          } else {
            pushUInt8(b - a);
          }
          break;
        }
      case OP_MUL: {
          uint32_t res = (uint32_t)b * (uint32_t)a;
          if (res <= UINT8_MAX) {
            pushUInt8((uint8_t)res);
          } else {
            pushInt((int32_t)res);
          }
          break;
        }
      case OP_DIV: {
          if (a != 0) {
            pushUInt8(b / a);
          } else {
            pushUInt8(0);
          }
          break;
        }
      case OP_MOD: {
          if (a != 0) {
            pushUInt8(b % a);
          } else {
            pushUInt8(0);
          }
          break;
        }
    }
    return;
  }

  // === int16 ===
  if (argType == TYPE_INT16 && varType == TYPE_INT16 && argLen == 2 && varLen == 2) {
    int16_t a, b;
    memcpy(&a, argData, 2);
    memcpy(&b, varData, 2);
    dropTop(0);
    switch (op) {
      case OP_ADD: {
          if ((b > 0 && a > INT16_MAX - b) || (b < 0 && a < INT16_MIN - b)) {
            pushInt((int32_t)b + (int32_t)a);
          } else {
            pushInt16(b + a);
          }
          break;
        }
      case OP_SUB: {
          if ((a > 0 && b < INT16_MIN + a) || (a < 0 && b > INT16_MAX + a)) {
            pushInt((int32_t)b - (int32_t)a);
          } else {
            pushInt16(b - a);
          }
          break;
        }
      case OP_MUL: {
          int32_t res = (int32_t)b * (int32_t)a;
          if (res >= INT16_MIN && res <= INT16_MAX) {
            pushInt16((int16_t)res);
          } else {
            pushInt(res);
          }
          break;
        }
      case OP_DIV: {
          if (a != 0) {
            pushInt16(b / a);
          } else {
            pushInt16(0);
          }
          break;
        }
      case OP_MOD: {
          if (a != 0) {
            pushInt16(b % a);
          } else {
            pushInt16(0);
          }
          break;
        }
    }
    return;
  }

  // === uint16 ===
  if (argType == TYPE_UINT16 && varType == TYPE_UINT16 && argLen == 2 && varLen == 2) {
    uint16_t a, b;
    memcpy(&a, argData, 2);
    memcpy(&b, varData, 2);
    dropTop(0);
    switch (op) {
      case OP_ADD: {
          if (b > UINT16_MAX - a) {
            pushInt((int32_t)b + (int32_t)a);
          } else {
            pushUInt16(b + a);
          }
          break;
        }
      case OP_SUB: {
          if (b < a) {
            pushInt((int32_t)b - (int32_t)a);
          } else {
            pushUInt16(b - a);
          }
          break;
        }
      case OP_MUL: {
          uint32_t res = (uint32_t)b * (uint32_t)a;
          if (res <= UINT16_MAX) {
            pushUInt16((uint16_t)res);
          } else {
            pushInt((int32_t)res);
          }
          break;
        }
      case OP_DIV: {
          if (a != 0) {
            pushUInt16(b / a);
          } else {
            pushUInt16(0);
          }
          break;
        }
      case OP_MOD: {
          if (a != 0) {
            pushUInt16(b % a);
          } else {
            pushUInt16(0);
          }
          break;
        }
    }
    return;
  }

  // === float ===
  if (argType == TYPE_FLOAT && varType == TYPE_FLOAT && argLen == 4 && varLen == 4) {
    float a, b;
    memcpy(&a, argData, 4);   // правый
    memcpy(&b, varData, 4);   // левый
    dropTop(0);
    switch (op) {
      case OP_ADD: pushFloat(b + a); break;
      case OP_SUB: pushFloat(b - a); break;
      case OP_MUL: pushFloat(b * a); break;
      case OP_DIV: pushFloat(a != 0.0f ? b / a : 0.0f); break;
      // % не поддерживается для float — игнорируем
    }
    return;
  }

  // === string (только сложение) ===
  if (op == OP_ADD && argType == TYPE_STRING && varType == TYPE_STRING) {
    uint8_t newLen = varLen + argLen;
    if (newLen > 255) newLen = 255;

    uint8_t result[255];
    memcpy(result, varData, varLen);               // левый: значение переменной
    uint8_t secondPart = (newLen > varLen) ? (newLen - varLen) : 0;
    if (secondPart > argLen) secondPart = argLen;
    memcpy(result + varLen, argData, secondPart);  // правый: со стека

    dropTop(0);
    pushValue(result, newLen, TYPE_STRING);
    return;
  }

  // === bool (только сложение) ===
  if (op == OP_ADD && argType == TYPE_BOOL && varType == TYPE_BOOL && argLen == 1 && varLen == 1) {
    int32_t a = argData[0] ? 1 : 0;   // правый
    int32_t b = varData[0] ? 1 : 0;   // левый
    dropTop(0);
    pushInt(b + a);
    return;
  }

  // === Общий fallback для целых типов (разные типы) ===
  if (op == OP_MOD &&
      (argType == TYPE_INT || argType == TYPE_UINT8 || argType == TYPE_INT8 ||
       argType == TYPE_UINT16 || argType == TYPE_INT16) &&
      (varType == TYPE_INT || varType == TYPE_UINT8 || varType == TYPE_INT8 ||
       varType == TYPE_UINT16 || varType == TYPE_INT16)) {

    // Преобразуем правый операнд (делитель)
    int32_t a = 0;
    if (argType == TYPE_INT && argLen == 4) memcpy(&a, argData, 4);
    else if (argType == TYPE_UINT8 && argLen == 1) a = argData[0];
    else if (argType == TYPE_INT8 && argLen == 1) a = (int8_t)argData[0];
    else if (argType == TYPE_UINT16 && argLen == 2) { uint16_t v; memcpy(&v, argData, 2); a = v; }
    else if (argType == TYPE_INT16 && argLen == 2) { int16_t v; memcpy(&v, argData, 2); a = v; }

    // Преобразуем левый операнд (делимое)
    int32_t b = 0;
    if (varType == TYPE_INT && varLen == 4) memcpy(&b, varData, 4);
    else if (varType == TYPE_UINT8 && varLen == 1) b = varData[0];
    else if (varType == TYPE_INT8 && varLen == 1) b = (int8_t)varData[0];
    else if (varType == TYPE_UINT16 && varLen == 2) { uint16_t v; memcpy(&v, varData, 2); b = v; }
    else if (varType == TYPE_INT16 && varLen == 2) { int16_t v; memcpy(&v, varData, 2); b = v; }

    dropTop(0);
    if (a != 0) {
      pushInt(b % a);
    } else {
      pushInt(0);
    }
    return;
  }

  // Не поддерживается — кладём значение переменной
  uint8_t Type, Len;
  const uint8_t* Data;
  if (readVariableValue(addr, &Type, &Len, &Data)) {
    pushValue(Data, Len, Type);
  }
}


void applyCompareOp(uint8_t op) {
  uint8_t rightType, rightLen;
  const uint8_t* rightData;
  if (!peekStackTop(&rightType, &rightLen, &rightData)) {
    pushBool(false);
    return;
  }
  dropTop(0);

  uint8_t leftType, leftLen;
  const uint8_t* leftData;
  if (!peekStackTop(&leftType, &leftLen, &leftData)) {
    pushBool(false);
    return;
  }
  dropTop(0);

  // === Проверка: разные числовые типы? ===
  auto isNumeric = [](uint8_t type) {
    return type == TYPE_INT || type == TYPE_FLOAT ||
           type == TYPE_UINT8 || type == TYPE_INT8 ||
           type == TYPE_UINT16 || type == TYPE_INT16;
  };

  // Если типы разные, но оба числовые — преобразуем к float
  if (leftType != rightType && isNumeric(leftType) && isNumeric(rightType)) {
    // Преобразуем left → float
    float leftF, rightF;
    if (leftType == TYPE_FLOAT && leftLen == 4) memcpy(&leftF, leftData, 4);
    else if (leftType == TYPE_INT && leftLen == 4) { int32_t v; memcpy(&v, leftData, 4); leftF = (float)v; }
    else if (leftType == TYPE_UINT8 && leftLen == 1) leftF = (float)leftData[0];
    else if (leftType == TYPE_INT8 && leftLen == 1) leftF = (float)(int8_t)leftData[0];
    else if (leftType == TYPE_UINT16 && leftLen == 2) { uint16_t v; memcpy(&v, leftData, 2); leftF = (float)v; }
    else if (leftType == TYPE_INT16 && leftLen == 2) { int16_t v; memcpy(&v, leftData, 2); leftF = (float)v; }
    else { pushBool(false); return; }

    // Преобразуем right → float
    if (rightType == TYPE_FLOAT && rightLen == 4) memcpy(&rightF, rightData, 4);
    else if (rightType == TYPE_INT && rightLen == 4) { int32_t v; memcpy(&v, rightData, 4); rightF = (float)v; }
    else if (rightType == TYPE_UINT8 && rightLen == 1) rightF = (float)rightData[0];
    else if (rightType == TYPE_INT8 && rightLen == 1) rightF = (float)(int8_t)rightData[0];
    else if (rightType == TYPE_UINT16 && rightLen == 2) { uint16_t v; memcpy(&v, rightData, 2); rightF = (float)v; }
    else if (rightType == TYPE_INT16 && rightLen == 2) { int16_t v; memcpy(&v, rightData, 2); rightF = (float)v; }
    else { pushBool(false); return; }

    // Теперь подменяем данные на float и идём в ваш код
    uint8_t floatData[4];
    memcpy(floatData, &leftF, 4);
    leftData = floatData;
    leftType = TYPE_FLOAT;
    leftLen = 4;

    memcpy(floatData + 4, &rightF, 4); // используем другой буфер
    rightData = floatData + 4;
    rightType = TYPE_FLOAT;
    rightLen = 4;
  }

  // === ДАЛЕЕ — ВАШ ИСХОДНЫЙ КОД БЕЗ ЕДИНОГО ИЗМЕНЕНИЯ ===
  bool result = false;
  if (leftType == TYPE_FLOAT && rightType == TYPE_FLOAT && leftLen == 4 && rightLen == 4) {
    float left, right;
    memcpy(&left, leftData, 4);
    memcpy(&right, rightData, 4);
    int cmp = (left < right) - (left > right);
    result = compareResult(cmp, op);
  }
  else if (leftType == TYPE_INT && rightType == TYPE_INT && leftLen == 4 && rightLen == 4) {
    int32_t left, right;
    memcpy(&left, leftData, 4);
    memcpy(&right, rightData, 4);
    int cmp = (left < right) - (left > right);
    result = compareResult(cmp, op);
  }
  else if (leftType == TYPE_INT8 && rightType == TYPE_INT8 && leftLen == 1 && rightLen == 1) {
    int8_t left = (int8_t)leftData[0];
    int8_t right = (int8_t)rightData[0];
    int cmp = (left < right) - (left > right);
    result = compareResult(cmp, op);
  }
  else if (leftType == TYPE_UINT8 && rightType == TYPE_UINT8 && leftLen == 1 && rightLen == 1) {
    uint8_t left = leftData[0];
    uint8_t right = rightData[0];
    int cmp = (left < right) - (left > right);
    result = compareResult(cmp, op);
  }
  else if (leftType == TYPE_INT16 && rightType == TYPE_INT16 && leftLen == 2 && rightLen == 2) {
    int16_t left, right;
    memcpy(&left, leftData, 2);
    memcpy(&right, rightData, 2);
    int cmp = (left < right) - (left > right);
    result = compareResult(cmp, op);
  }
  else if (leftType == TYPE_UINT16 && rightType == TYPE_UINT16 && leftLen == 2 && rightLen == 2) {
    uint16_t left, right;
    memcpy(&left, leftData, 2);
    memcpy(&right, rightData, 2);
    int cmp = (left < right) - (left > right);
    result = compareResult(cmp, op);
  }
  else if (leftType == TYPE_STRING && rightType == TYPE_STRING) {
    const char* left = (const char*)leftData;
    const char* right = (const char*)rightData;
    size_t leftLenStr = leftLen;
    size_t rightLenStr = rightLen;
    int cmp = strncmp(right, left, leftLenStr < rightLenStr ? leftLenStr : rightLenStr);
    if (cmp == 0) cmp = (rightLenStr - leftLenStr);
    result = compareResult(cmp, op);
  }
  else if (leftType == TYPE_BOOL && rightType == TYPE_BOOL && leftLen == 1 && rightLen == 1) {
    bool left = (leftData[0] != 0);
    bool right = (rightData[0] != 0);
    int cmp = (left < right) - (left > right);
    result = compareResult(cmp, op);
  }
  else {
    if (op == CMP_EQ) result = false;
    else if (op == CMP_NE) result = true;
    else result = false;
  }

  pushBool(result);
}
