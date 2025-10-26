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
      Serial.println("⚠️ char: value out of range 0-255");
      return;
    }
    code = (uint8_t)v;
  }
  else {
    Serial.println("⚠️ char: expected uint8 or int");
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



bool valueToUint8(uint8_t type, uint8_t len, const uint8_t* data, uint8_t* out) {
  int32_t val;
  switch (type) {
    case TYPE_INT:
      if (len == 4) { memcpy(&val, data, 4); }
      else return false;
      break;
    case TYPE_INT8:
      if (len == 1) { val = (int8_t)data[0]; }
      else return false;
      break;
    case TYPE_UINT8:
      if (len == 1) { val = data[0]; }
      else return false;
      break;
    case TYPE_INT16:
      if (len == 2) { val = (int16_t)(data[0] | (data[1] << 8)); }
      else return false;
      break;
    case TYPE_UINT16:
      if (len == 2) { val = (uint16_t)(data[0] | (data[1] << 8)); }
      else return false;
      break;
    default:
      return false;
  }
  if (val < 0 || val > 255) return false;
  *out = (uint8_t)val;
  return true;
}

void mychoiceFunc(uint16_t addr) {
  // 1. Если стек пуст — читаем значение
  if (stackTop == 0) {
    uint8_t nameLen = dictionary[addr + 2];
    uint8_t storage = dictionary[addr + 3 + nameLen];
    uint8_t storageType = storage & 0x7F;

    if (storageType == STORAGE_CONT) {
      const uint8_t* valData = &dictionary[addr + 3 + nameLen + 2];
      pushValue(valData, 4, TYPE_INT);
    }
    else if (storageType == STORAGE_CONST || storageType == STORAGE_POOLED) {
      uint8_t Type, Len;
      const uint8_t* Data;
      if (readVariableValue(addr, &Type, &Len, &Data)) {
        pushValue(Data, Len, Type);
      }
    }
    return;
  }

  // 2. Читаем вершину стека
  uint8_t Type, Len;
  const uint8_t* Data;
  if (!peekStackTop(&Type, &Len, &Data)) return;

  // 3. Если не маркер — читаем значение
  if (Type != TYPE_MARKER) {
    uint8_t nameLen = dictionary[addr + 2];
    uint8_t storage = dictionary[addr + 3 + nameLen];
    uint8_t storageType = storage & 0x7F;

    if (storageType == STORAGE_CONT) {
      const uint8_t* valData = &dictionary[addr + 3 + nameLen + 2];
      pushValue(valData, 4, TYPE_INT);
    }
    else if (storageType == STORAGE_CONST || storageType == STORAGE_POOLED) {
      if (readVariableValue(addr, &Type, &Len, &Data)) {
        pushValue(Data, Len, Type);
      }
    }
    return;
  }

  // 4. Обработка маркеров
  if (Len == 1 && Data[0] == '=') {
    dropTop(0);
    if (peekStackTop(&Type, &Len, &Data)) {
      uint8_t nameLen = dictionary[addr + 2];
      uint8_t storage = dictionary[addr + 3 + nameLen];
      uint8_t storageType = storage & 0x7F;

      if (storageType == STORAGE_CONST) {
        uint32_t poolRef =
          dictionary[addr + 3 + nameLen + 2 + 0] |
          (dictionary[addr + 3 + nameLen + 2 + 1] << 8) |
          (dictionary[addr + 3 + nameLen + 2 + 2] << 16) |
          (dictionary[addr + 3 + nameLen + 2 + 3] << 24);

        if (poolRef != 0xFFFFFFFF) {
          Serial.println("⚠️ const: already initialized");
          dropTop(0);
          return;
        }
      }
      else if (storageType == STORAGE_CONT) {
        Serial.println("⚠️ cont: assignment not allowed");
        dropTop(0);
        return;
      }

      storeValueToVariable(addr, Data, Len, Type);
      dropTop(0);
    }
    return;
  }

  // Арифметика
  if (Len == 1 && Data[0] == '+') { dropTop(0); applyBinaryOp(addr, OP_ADD); return; }
  if (Len == 1 && Data[0] == '-') { dropTop(0); applyBinaryOp(addr, OP_SUB); return; }
  if (Len == 1 && Data[0] == '*') { dropTop(0); applyBinaryOp(addr, OP_MUL); return; }
  if (Len == 1 && Data[0] == '/') { dropTop(0); applyBinaryOp(addr, OP_DIV); return; }

  // Сравнения
  if (Len == 2 && Data[0] == '=' && Data[1] == '=') { dropTop(0); applyCompareOp(addr, CMP_EQ); return; }
  if (Len == 2 && Data[0] == '!' && Data[1] == '=') { dropTop(0); applyCompareOp(addr, CMP_NE); return; }
  if (Len == 1 && Data[0] == '<') { dropTop(0); applyCompareOp(addr, CMP_LT); return; }
  if (Len == 1 && Data[0] == '>') { dropTop(0); applyCompareOp(addr, CMP_GT); return; }
  if (Len == 2 && Data[0] == '<' && Data[1] == '=') { dropTop(0); applyCompareOp(addr, CMP_LE); return; }
  if (Len == 2 && Data[0] == '>' && Data[1] == '=') { dropTop(0); applyCompareOp(addr, CMP_GE); return; }

  // Неизвестный маркер — читаем значение
  uint8_t nameLen = dictionary[addr + 2];
  uint8_t storage = dictionary[addr + 3 + nameLen];
  uint8_t storageType = storage & 0x7F;

  if (storageType == STORAGE_CONT) {
    const uint8_t* valData = &dictionary[addr + 3 + nameLen + 2];
    pushValue(valData, 4, TYPE_INT);
  }
  else if (storageType == STORAGE_CONST || storageType == STORAGE_POOLED) {
    if (readVariableValue(addr, &Type, &Len, &Data)) {
      pushValue(Data, Len, Type);
    }
  }
}

void pushMarkerFunc(uint16_t addr) {
  if (addr + 2 >= DICT_SIZE) return;
  uint8_t nameLen = dictionary[addr + 2];
  if (nameLen == 0 || nameLen > 32 || addr + 3 + nameLen > DICT_SIZE) return;

  const char* name = (const char*)&dictionary[addr + 3];
  if (isStackOverflow(nameLen + 2)) {
    handleStackOverflow();
    return;
  }

  memcpy(&stack[stackTop], name, nameLen);
  stackTop += nameLen;
  stack[stackTop++] = nameLen;
  stack[stackTop++] = TYPE_MARKER;
}


void dropTop(uint16_t addr) {
  if (stackTop < 2) return;
  uint8_t len = stack[stackTop - 2];
  uint8_t type = stack[stackTop - 1];
  if (len > stackTop - 2) return;
  stackTop = stackTop - 2 - len;
}
void printTop(uint16_t addr) {
  if (stackTop < 2) {
    Serial.print("<empty>");
    return;
  }

  uint8_t len = stack[stackTop - 2];
  uint8_t type = stack[stackTop - 1];

  if (len > stackTop - 2) {
    Serial.print("<corrupted>");
    return;
  }

  size_t dataStart = stackTop - 2 - len;

  switch (type) {
    case TYPE_INT: {
        if (len == 4) {
          int32_t val; memcpy(&val, &stack[dataStart], 4);
          Serial.print(val);
        } else Serial.print("<bad int>");
        break;
      }
    case TYPE_FLOAT: {
        if (len == 4) {
          float val; memcpy(&val, &stack[dataStart], 4);
          Serial.print(val, 6);
        } else Serial.print("<bad float>");
        break;
      }
    case TYPE_STRING: {
        for (size_t i = 0; i < len; i++) {
          char c = stack[dataStart + i];
          //if (c >= 32 && c <= 126) 
          Serial.print(c);
          //else Serial.printf("\\x%02X", (uint8_t)c);
        }
        break;
      }
    case TYPE_BOOL: {
        if (len == 1) {
          Serial.print(stack[dataStart] ? "true" : "false");
        } else Serial.print("<bad bool>");
        break;
      }
    case TYPE_INT8: {
        int8_t val = (int8_t)stack[dataStart];
        Serial.print(val);
        break;
      }
    case TYPE_UINT8: {
        uint8_t val = stack[dataStart];
        Serial.print(val);
        break;
      }
    case TYPE_INT16: {
        int16_t val; memcpy(&val, &stack[dataStart], 2);
        Serial.print(val);
        break;
      }
    case TYPE_UINT16: {
        uint16_t val; memcpy(&val, &stack[dataStart], 2);
        Serial.print(val);
        break;
      }
    default:
      Serial.print("<unknown>");
      break;
  }
  dropTop(0);
}



void dumpDataPool(uint16_t offset, uint16_t len) {
  if (offset + len > DATA_POOL_SIZE) {
    len = DATA_POOL_SIZE - offset;
  }
  Serial.printf("dataPool[%u..%u]:\n", offset, offset + len - 1);
  for (uint16_t i = 0; i < len; i++) {
    if (i % 16 == 0) {
      if (i > 0) Serial.println();
      Serial.printf("%04X: ", offset + i);
    }
    Serial.printf("%02X ", dataPool[offset + i]);
  }
  if (len % 16 != 0) Serial.println();
  Serial.println();
}
void dumpDataPoolWord(uint16_t addr) {
  Serial.printf("dataPool (used %u/%u):\n", dataPoolPtr, DATA_POOL_SIZE);
  for (uint16_t i = 0; i < dataPoolPtr; i++) {
    if (i % 16 == 0) {
      if (i > 0) Serial.println();
      Serial.printf("%04X: ", i);
    }
    Serial.printf("%02X ", dataPool[i]);
  }
  if (dataPoolPtr % 16 != 0) Serial.println();
  Serial.println();
}
