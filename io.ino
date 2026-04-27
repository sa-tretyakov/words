void ioInit() {
  String tmp = "cont io";
  executeLine(tmp);
  addInternalWord(".", printTop);
  addInternalWord("print", printTop);
  addInternalWord("CR", [](uint16_t) { pushString("\r"); });
  addInternalWord("LF", [](uint16_t) { pushString("\n"); });
  addInternalWord("CRLF", [](uint16_t) { pushString("\r\n"); });
  addInternalWord("CRLF2", [](uint16_t) { pushString("\r\n\r\n"); });
  addInternalWord("rand", randWord);
  addInternalWord("randRange", randRangeWord);
  
  
//  tmp = ": ln\r\nprint CRLF\r\n\;\r\n";
//  executeLine(tmp);
  tmp = "main";
  executeLine(tmp);
}
// rand : ( -- n ) Случайное 32-битное число
void randWord(uint16_t addr) {
    pushInt((int32_t)random());  // Arduino/ESP стандарт
}

// randRange : ( min max -- n ) Случайное в диапазоне [min..max]
void randRangeWord(uint16_t addr) {
    int32_t max_v, min_v;
    if (!popInt32FromAny(&max_v)) return;
    if (!popInt32FromAny(&min_v)) return;
    if (min_v > max_v) { int32_t t = min_v; min_v = max_v; max_v = t; }
    pushInt(min_v + (int32_t)random(max_v - min_v + 1));
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
