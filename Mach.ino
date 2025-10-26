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
  if (isStackOverflow(len + 2)) { handleStackOverflow(); return; }
  memcpy(&stack[stackTop], data, len);
  stackTop += len;
  stack[stackTop++] = len;
  stack[stackTop++] = type;
}

void storeValueToVariable(uint16_t addr, const uint8_t* data, uint8_t len, uint8_t type) {
  // Читаем текущий poolRef
  uint8_t nameLen = dictionary[addr + 2];
  uint32_t oldPoolRef =
    dictionary[addr + 3 + nameLen + 2 + 0] |
    (dictionary[addr + 3 + nameLen + 2 + 1] << 8) |
    (dictionary[addr + 3 + nameLen + 2 + 2] << 16) |
    (dictionary[addr + 3 + nameLen + 2 + 3] << 24);

  // Если уже инициализирована и тип совпадает — перезаписываем
  if (oldPoolRef != 0xFFFFFFFF && oldPoolRef < DATA_POOL_SIZE) {
    uint8_t oldType = dataPool[oldPoolRef];
    uint8_t oldLen = dataPool[oldPoolRef + 1];
    if (oldType == type && oldLen == len) {
      // Перезаписываем данные
      memcpy(&dataPool[oldPoolRef + 2], data, len);
      return;
    }
  }

  // Иначе — выделяем новое место
  uint16_t needed = 2 + len;
  if (dataPoolPtr + needed > DATA_POOL_SIZE) {
    Serial.println("⚠️ dataPool full");
    return;
  }

  uint16_t newOffset = dataPoolPtr;
  dataPool[dataPoolPtr++] = type;
  dataPool[dataPoolPtr++] = len;
  memcpy(&dataPool[dataPoolPtr], data, len);
  dataPoolPtr += len;

  // Обновляем poolRef
  dictionary[addr + 3 + nameLen + 2 + 0] = (newOffset >> 0) & 0xFF;
  dictionary[addr + 3 + nameLen + 2 + 1] = (newOffset >> 8) & 0xFF;
  dictionary[addr + 3 + nameLen + 2 + 2] = (newOffset >> 16) & 0xFF;
  dictionary[addr + 3 + nameLen + 2 + 3] = (newOffset >> 24) & 0xFF;
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
    memcpy(&a, argData, 4);
    memcpy(&b, varData, 4);
    dropTop(0);
    switch (op) {
      case OP_ADD: pushInt(a + b); break;
      case OP_SUB: pushInt(a - b); break;
      case OP_MUL: pushInt(a * b); break;
      case OP_DIV: pushInt(b != 0 ? a / b : 0); break;
    }
    return;
  }

  // === int8 ===
  if (argType == TYPE_INT8 && varType == TYPE_INT8 && argLen == 1 && varLen == 1) {
    int8_t a = (int8_t)argData[0];
    int8_t b = (int8_t)varData[0];
    dropTop(0);
    switch (op) {
      case OP_ADD: {
        if ((b > 0 && a > INT8_MAX - b) || (b < 0 && a < INT8_MIN - b)) {
          pushInt((int32_t)a + (int32_t)b);
        } else {
          pushInt8(a + b);
        }
        break;
      }
      case OP_SUB: {
        if ((b > 0 && a < INT8_MIN + b) || (b < 0 && a > INT8_MAX + b)) {
          pushInt((int32_t)a - (int32_t)b);
        } else {
          pushInt8(a - b);
        }
        break;
      }
      case OP_MUL: {
        int32_t res = (int32_t)a * (int32_t)b;
        if (res >= INT8_MIN && res <= INT8_MAX) {
          pushInt8((int8_t)res);
        } else {
          pushInt(res);
        }
        break;
      }
      case OP_DIV: {
        if (b != 0) {
          pushInt8(a / b);
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
    uint8_t a = argData[0];
    uint8_t b = varData[0];
    dropTop(0);
    switch (op) {
      case OP_ADD: {
        if (a > UINT8_MAX - b) {
          pushInt((int32_t)a + (int32_t)b);
        } else {
          pushUInt8(a + b);
        }
        break;
      }
      case OP_SUB: {
        if (a < b) {
          pushInt((int32_t)a - (int32_t)b);
        } else {
          pushUInt8(a - b);
        }
        break;
      }
      case OP_MUL: {
        uint32_t res = (uint32_t)a * (uint32_t)b;
        if (res <= UINT8_MAX) {
          pushUInt8((uint8_t)res);
        } else {
          pushInt((int32_t)res);
        }
        break;
      }
      case OP_DIV: {
        if (b != 0) {
          pushUInt8(a / b);
        } else {
          pushUInt8(0);
        }
        break;
      }
    }
    return;
  }

  // === float ===
  if (argType == TYPE_FLOAT && varType == TYPE_FLOAT && argLen == 4 && varLen == 4) {
    float a, b;
    memcpy(&a, argData, 4);
    memcpy(&b, varData, 4);
    dropTop(0);
    switch (op) {
      case OP_ADD: pushFloat(a + b); break;
      case OP_SUB: pushFloat(a - b); break;
      case OP_MUL: pushFloat(a * b); break;
      case OP_DIV: pushFloat(b != 0.0f ? a / b : 0.0f); break;
    }
    return;
  }

  // === string (только сложение) ===
  if (op == OP_ADD && argType == TYPE_STRING && varType == TYPE_STRING) {
    uint8_t newLen = varLen + argLen;
    if (newLen > 255) newLen = 255;

    uint8_t result[255];
    memcpy(result, varData, varLen);
    uint8_t secondPart = (newLen > varLen) ? (newLen - varLen) : 0;
    if (secondPart > argLen) secondPart = argLen;
    memcpy(result + varLen, argData, secondPart);

    dropTop(0);
    pushValue(result, newLen, TYPE_STRING);
    return;
  }

  // === bool (только сложение) ===
  if (op == OP_ADD && argType == TYPE_BOOL && varType == TYPE_BOOL && argLen == 1 && varLen == 1) {
    int32_t a = argData[0] ? 1 : 0;
    int32_t b = varData[0] ? 1 : 0;
    dropTop(0);
    pushInt(a + b);
    return;
  }

  // Не поддерживается — кладём значение переменной
  uint8_t Type, Len;
  const uint8_t* Data;
  if (readVariableValue(addr, &Type, &Len, &Data)) {
    pushValue(Data, Len, Type);
  }
}
void applyCompareOp(uint16_t addr, uint8_t op) {
  uint8_t argType, argLen;
  const uint8_t* argData;
  if (!peekStackTop(&argType, &argLen, &argData)) {
    pushBool(false);
    return;
  }

  uint8_t varType, varLen;
  const uint8_t* varData;
  if (!readVariableValue(addr, &varType, &varLen, &varData)) {
    pushBool(false);
    return;
  }

  bool result = false;

  // === int32 ===
  if (argType == TYPE_INT && varType == TYPE_INT && argLen == 4 && varLen == 4) {
    int32_t a, b;
    memcpy(&a, argData, 4);
    memcpy(&b, varData, 4);
    int cmp = (a > b) - (a < b);
    result = compareResult(cmp, op);
    dropTop(0);
    pushBool(result);
    return;
  }

  // === int8 ===
  if (argType == TYPE_INT8 && varType == TYPE_INT8 && argLen == 1 && varLen == 1) {
    int8_t a = (int8_t)argData[0];
    int8_t b = (int8_t)varData[0];
    int cmp = (a > b) - (a < b);
    result = compareResult(cmp, op);
    dropTop(0);
    pushBool(result);
    return;
  }

  // === uint8 ===
  if (argType == TYPE_UINT8 && varType == TYPE_UINT8 && argLen == 1 && varLen == 1) {
    uint8_t a = argData[0];
    uint8_t b = varData[0];
    int cmp = (a > b) - (a < b);
    result = compareResult(cmp, op);
    dropTop(0);
    pushBool(result);
    return;
  }

  // === float ===
  if (argType == TYPE_FLOAT && varType == TYPE_FLOAT && argLen == 4 && varLen == 4) {
    float a, b;
    memcpy(&a, argData, 4);
    memcpy(&b, varData, 4);
    int cmp = (a > b) - (a < b);
    result = compareResult(cmp, op);
    dropTop(0);
    pushBool(result);
    return;
  }

  // === string ===
  if (argType == TYPE_STRING && varType == TYPE_STRING) {
    int cmp = strncmp((const char*)argData, (const char*)varData, argLen < varLen ? argLen : varLen);
    if (cmp == 0) cmp = (argLen - varLen);
    result = compareResult(cmp, op);
    dropTop(0);
    pushBool(result);
    return;
  }

  // === bool ===
  if (argType == TYPE_BOOL && varType == TYPE_BOOL && argLen == 1 && varLen == 1) {
    bool a = (argData[0] != 0);
    bool b = (varData[0] != 0);
    int cmp = (a > b) - (a < b);
    result = compareResult(cmp, op);
    dropTop(0);
    pushBool(result);
    return;
  }

  // Типы не совпадают
  dropTop(0);
  if (op == CMP_EQ) {
    pushBool(false);
  } else if (op == CMP_NE) {
    pushBool(true);
  } else {
    pushBool(false);
  }
}
