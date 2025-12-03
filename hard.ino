

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
    outputStream->print("<empty>");
    return;
  }

  uint8_t len = stack[stackTop - 2];
  uint8_t type = stack[stackTop - 1];

  if (len > stackTop - 2) {
    outputStream->print("<corrupted>");
    return;
  }

  size_t dataStart = stackTop - 2 - len;

  switch (type) {
    case TYPE_INT: {
        if (len == 4) {
          int32_t val; memcpy(&val, &stack[dataStart], 4);
          outputStream->print(val);
        } else outputStream->print("<bad int>");
        break;
      }
    case TYPE_FLOAT: {
        if (len == 4) {
          float val; memcpy(&val, &stack[dataStart], 4);
          outputStream->print(val, 6);
        } else outputStream->print("<bad float>");
        break;
      }
    case TYPE_STRING: {
        for (size_t i = 0; i < len; i++) {
          char c = stack[dataStart + i];
          //if (c >= 32 && c <= 126) 
          outputStream->print(c);
          //else outputStream->printf("\\x%02X", (uint8_t)c);
        }
        break;
      }
    case TYPE_BOOL: {
        if (len == 1) {
          outputStream->print(stack[dataStart] ? "true" : "false");
        } else outputStream->print("<bad bool>");
        break;
      }
    case TYPE_INT8: {
        int8_t val = (int8_t)stack[dataStart];
        outputStream->print(val);
        break;
      }
    case TYPE_UINT8: {
        uint8_t val = stack[dataStart];
        outputStream->print(val);
        break;
      }
    case TYPE_INT16: {
        int16_t val; memcpy(&val, &stack[dataStart], 2);
        outputStream->print(val);
        break;
      }
    case TYPE_UINT16: {
        uint16_t val; memcpy(&val, &stack[dataStart], 2);
        outputStream->print(val);
        break;
      }
    default:
      outputStream->print("<unknown>");
      break;
  }
  dropTop(0);
}

void dumpDataPool(uint16_t offset, uint16_t len) {
  if (offset + len > DATA_POOL_SIZE) {
    len = DATA_POOL_SIZE - offset;
  }
  outputStream->printf("dataPool[%u..%u]:\n", offset, offset + len - 1);
  for (uint16_t i = 0; i < len; i++) {
    if (i % 16 == 0) {
      if (i > 0) outputStream->println();
      outputStream->printf("%04X: ", offset + i);
    }
    outputStream->printf("%02X ", dataPool[offset + i]);
  }
  if (len % 16 != 0) outputStream->println();
  outputStream->println();
}

void dumpDataPoolWord(uint16_t addr) {
  outputStream->printf("dataPool (used %u/%u):\n", dataPoolPtr, DATA_POOL_SIZE);
  
  // Заголовок: смещения 0-F
  outputStream->print("     ");
  for (int i = 0; i < 16; i++) {
    outputStream->printf(" %02X", i);
  }
  outputStream->println();

  for (uint16_t i = 0; i < dataPoolPtr; i++) {
    if (i % 16 == 0) {
      if (i > 0) outputStream->println();
      outputStream->printf("%04X:", i);
    }
    outputStream->printf(" %02X", dataPool[i]);
  }
  if (dataPoolPtr % 16 != 0) outputStream->println();
  outputStream->println();
}

void nopFunc(uint16_t addr){
  
  }

void resetFunc(uint16_t addr){
  ESP.restart();
  }
