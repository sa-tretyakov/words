void handleStringToken(const String& token) {
  if (token.startsWith("\"") && token.endsWith("\"") && token.length() >= 2) {
    String strContent = token.substring(1, token.length() - 1);
    size_t len = strContent.length();
    if (len > 255) {
      outputStream->println("⚠️ Строка слишком длинная");
      return;
    }

    if (compiling) {
      // Компиляция строки как литерала
      if (dictLen + 6 + len > DICT_SIZE) {
        outputStream->println("⚠️ Dictionary full (string literal)");
        return;
      }
      dictionary[dictLen++] = 0xFF; // 0xFFFF
      dictionary[dictLen++] = 0xFF;
      dictionary[dictLen++] = (uint8_t)len;
      dictionary[dictLen++] = TYPE_STRING;
      memcpy(&dictionary[dictLen], strContent.c_str(), len);
      dictLen += len;
    } else {
      // Интерпретация
      storeValueToVariable(ADDR_TMP_LIT, (uint8_t*)strContent.c_str(), (uint8_t)len, TYPE_STRING);
      executeAt(ADDR_TMP_LIT);
    }
  } else {
    // Не строка — обычный токен
    if (compiling) {
      compileToken(token);
    } else {
      interpretToken(token);
    }
  }
}

void compileCode(uint16_t code) {
  if (dictLen + 2 > DICT_SIZE) {
    outputStream->println("⚠️ Dictionary full — compilation aborted");
    compiling = false; // останавливаем компиляцию
    return;
  }
  dictionary[dictLen++] = (uint8_t)(code & 0xFF);      // младший байт
  dictionary[dictLen++] = (uint8_t)((code >> 8) & 0xFF); // старший байт
}

uint16_t findWord(const String& token) {
  uint16_t ptr = 0;
  while (ptr < dictLen) {
    if (ptr + 2 > DICT_SIZE) break;
    uint16_t nextPtr = dictionary[ptr] | (dictionary[ptr + 1] << 8);
    if (nextPtr == 0 || nextPtr <= ptr || nextPtr > DICT_SIZE) break;

    uint8_t nameLen = dictionary[ptr + 2];
    if (nameLen == 0 || ptr + 3 + nameLen > DICT_SIZE) break;

    if ((size_t)nameLen == token.length()) {
      bool match = true;
      for (uint8_t j = 0; j < nameLen; j++) {
        if (dictionary[ptr + 3 + j] != token[j]) {
          match = false;
          break;
        }
      }
      if (match) return ptr;
    }
    ptr = nextPtr;
  }
  return 0;
}

void compileFloatLiteral(const String& valueStr, const String& originalToken) {
  if (valueStr.length() == 0) return;
  float f = valueStr.toFloat();
  uint8_t type = TYPE_FLOAT;
  uint8_t len = 4;
  uint8_t data[4];
  memcpy(data, &f, 4);

  if (dictLen + 6 > DICT_SIZE) { // FF FF len type data
    outputStream->println("⚠️ Dictionary full (float)");
    return;
  }

  dictionary[dictLen++] = 0xFF;
  dictionary[dictLen++] = 0xFF;
  dictionary[dictLen++] = len;
  dictionary[dictLen++] = type;
  memcpy(&dictionary[dictLen], data, len);
  dictLen += len;
}


void compileIntegerLiteral(const String& valueStr, const String& originalToken, bool isHex) {
  long val;
  if (isHex) {
    val = strtol(valueStr.c_str(), nullptr, 16);
  } else {
    val = atol(valueStr.c_str());
  }

  ValueType forcedType = TYPE_UNDEFINED;
  String tempToken = originalToken;
  if (tempToken.endsWith("i32")) forcedType = TYPE_INT;
  else if (tempToken.endsWith("i16")) forcedType = TYPE_INT16;
  else if (tempToken.endsWith("u16")) forcedType = TYPE_UINT16;
  else if (tempToken.endsWith("i8")) forcedType = TYPE_INT8;
  else if (tempToken.endsWith("u8")) forcedType = TYPE_UINT8;

  uint8_t type, len;
  uint8_t data[4];

  if (forcedType == TYPE_UNDEFINED) {
    if (isHex) {
      if (val >= 0 && val <= UINT8_MAX) {
        type = TYPE_UINT8; len = 1; data[0] = (uint8_t)val;
      } else if (val >= 0 && val <= UINT16_MAX) {
        type = TYPE_UINT16; len = 2; uint16_t v16 = (uint16_t)val; memcpy(data, &v16, 2);
      } else {
        type = TYPE_INT; len = 4; int32_t v = (val < INT32_MIN) ? INT32_MIN : (val > INT32_MAX) ? INT32_MAX : (int32_t)val; memcpy(data, &v, 4);
      }
    } else {
      type = TYPE_INT; len = 4; int32_t v = (val < INT32_MIN) ? INT32_MIN : (val > INT32_MAX) ? INT32_MAX : (int32_t)val; memcpy(data, &v, 4);
    }
  } else {
    switch (forcedType) {
      case TYPE_INT8: {
          if (val < INT8_MIN || val > INT8_MAX) {
              outputStream->printf("⚠️ Value %ld out of range for i8 (-128..127): %s\n", val, originalToken.c_str());
              return;
          }
          type = TYPE_INT8; len = 1; data[0] = (int8_t)val;
          break;
        }
      case TYPE_UINT8: {
          if (val < 0 || val > UINT8_MAX) {
              outputStream->printf("⚠️ Value %ld out of range for u8 (0..255): %s\n", val, originalToken.c_str());
              return;
          }
          type = TYPE_UINT8; len = 1; data[0] = (uint8_t)val;
          break;
        }
      case TYPE_INT16: {
          if (val < INT16_MIN || val > INT16_MAX) {
              outputStream->printf("⚠️ Value %ld out of range for i16 (-32768..32767): %s\n", val, originalToken.c_str());
              return;
          }
          type = TYPE_INT16; len = 2; int16_t v16 = (int16_t)val; memcpy(data, &v16, 2);
          break;
        }
      case TYPE_UINT16: {
          if (val < 0 || val > UINT16_MAX) {
              outputStream->printf("⚠️ Value %ld out of range for u16 (0..65535): %s\n", val, originalToken.c_str());
              return;
          }
          type = TYPE_UINT16; len = 2; uint16_t u16 = (uint16_t)val; memcpy(data, &u16, 2);
          break;
        }
      case TYPE_INT: {
          if (val < INT32_MIN || val > INT32_MAX) {
              outputStream->printf("⚠️ Value %ld out of range for i32: %s\n", val, originalToken.c_str());
              return;
          }
          type = TYPE_INT; len = 4; int32_t v32 = (int32_t)val; memcpy(data, &v32, 4);
          break;
        }
      default: {
          outputStream->printf("⚠️ Internal error in token: %s\n", originalToken.c_str());
          return;
        }
    }
  }

  if (dictLen + 6 > DICT_SIZE) {
    outputStream->println("⚠️ Dictionary full (int)");
    return;
  }

  dictionary[dictLen++] = 0xFF;
  dictionary[dictLen++] = 0xFF;
  dictionary[dictLen++] = len;
  dictionary[dictLen++] = type;
  memcpy(&dictionary[dictLen], data, len);
  dictLen += len;
}


  void compileToken(const String & token) {
   // === СПЕЦИАЛЬНАЯ ОБРАБОТКА ЦИКЛОВ ===

  if (token == "{") {
    if (loopDepth >= MAX_LOOP_NESTING) {
      outputStream->println("⚠️ Loop nesting too deep");
      return;
    }
    loopStack[loopDepth].conditionStart = dictLen;
//    outputStream->print("conditionStart=");
//    outputStream->println(dictLen,HEX);
    return;
  }

  if (token == "while") {
    if (loopDepth >= MAX_LOOP_NESTING) {
      outputStream->println("⚠️ Loop nesting error");
      return;
    }

    // Резервируем место под литерал смещения выхода (6 байт)
    dictionary[dictLen++] = 0xFF;
    dictionary[dictLen++] = 0xFF;
    dictionary[dictLen++] = 2;
    dictionary[dictLen++] = TYPE_INT16;
    dictionary[dictLen++] = 0;
    dictionary[dictLen++] = 0;

    // Запоминаем позицию заглушки
    loopStack[loopDepth].patchPos = dictLen - 6;
    loopStack[loopDepth].afterWhilePos = dictLen + 2;

    // Слово "while"
    dictionary[dictLen++] = addrWhile & 0xFF;
    dictionary[dictLen++] = (addrWhile >> 8) & 0xFF;

    loopDepth++;
    return;
  }

  if (token == "}") {
    if (loopDepth == 0) {
      outputStream->println("⚠️ Unmatched }");
      return;
    }
    loopDepth--;
    LoopInfo& info = loopStack[loopDepth];

    // Запоминаем позицию под литерал смещения возврата
    uint16_t offsetBackPos = dictLen;

    // Резервируем 6 байт под литерал
    for (int i = 0; i < 6; i++) {
      dictionary[dictLen++] = 0;
    }

    // Записываем goto (2 байта)
    dictionary[dictLen++] = addrGoto & 0xFF;
    dictionary[dictLen++] = (addrGoto >> 8) & 0xFF;

    // Теперь dictLen — конец тела
    uint16_t finalPos = dictLen;

    // Смещение возврата: от конца до начала условия
    int16_t offsetBack = (int16_t)(info.conditionStart - finalPos+2);

    // Патчим литерал смещения возврата
    dictionary[offsetBackPos + 0] = 0xFF;
    dictionary[offsetBackPos + 1] = 0xFF;
    dictionary[offsetBackPos + 2] = 2;
    dictionary[offsetBackPos + 3] = TYPE_INT16;
    dictionary[offsetBackPos + 4] = (uint8_t)(offsetBack & 0xFF);
    dictionary[offsetBackPos + 5] = (uint8_t)((offsetBack >> 8) & 0xFF);

    // Патчим смещение выхода (после while)
    int16_t offsetExit = (int16_t)(finalPos - (info.patchPos + 6));
    dictionary[info.patchPos + 4] = (uint8_t)(offsetExit & 0xFF);
    dictionary[info.patchPos + 5] = (uint8_t)((offsetExit >> 8) & 0xFF);

    return;
  }
    if (token == "end") {
      compileCode(0xFFFE);
      //compileEndMarker();
      return;
    }
    if (token == "if") {
      compileCode(0xFFFD);
      compileCode(0xFFFE);
      //compileIfMarker();
      return;
    }

    String tempToken = token;
    ValueType forcedType = TYPE_UNDEFINED;
    if (tempToken.endsWith("i32")) tempToken.remove(tempToken.length() - 3);
    else if (tempToken.endsWith("i16")) tempToken.remove(tempToken.length() - 3);
    else if (tempToken.endsWith("u16")) tempToken.remove(tempToken.length() - 3);
    else if (tempToken.endsWith("i8")) tempToken.remove(tempToken.length() - 2);
    else if (tempToken.endsWith("u8")) tempToken.remove(tempToken.length() - 2);

    bool hasDot = false, isHex = false;
    if (tempToken.length() > 0 && isValidNumber(tempToken, hasDot, isHex)) {
      if (hasDot) {
        compileFloatLiteral(tempToken, token);
      } else {
        compileIntegerLiteral(tempToken, token, isHex);
      }
      return;
    }

    uint16_t wordAddr = findWord(token);
    if (wordAddr != 0) {
      if (dictLen + 2 > DICT_SIZE) {
        outputStream->println("⚠️ Dictionary full (word ref)");
        return;
      }
      dictionary[dictLen++] = (wordAddr >> 0) & 0xFF;
      dictionary[dictLen++] = (wordAddr >> 8) & 0xFF;
    } else {
      outputStream->printf("⚠️ Word not found: %s\n", token.c_str());
    }
  }

void executeLineTokens(String& line) {
  const int MAX_TOKENS = 32;
  String tokens[MAX_TOKENS];
  int tokenCount = 0;

  tokenizeLine(line, tokens, tokenCount);

  if (compiling) {
    for (int i = 0; i < tokenCount; i++) {
      if (tokens[i] == ";") {
        uint16_t finalNext = dictLen;
        dictionary[compileTarget] = (finalNext >> 0) & 0xFF;
        dictionary[compileTarget + 1] = (finalNext >> 8) & 0xFF;
        compiling = false;
        return;
      }
    }
    for (int i = tokenCount - 1; i >= 0; i--) {
      handleStringToken(tokens[i]); // ← обрабатывает и строки, и слова
    }
  } else {
    for (int i = tokenCount - 1; i >= 0; i--) {
      handleStringToken(tokens[i]);
    }
  }
}


  void tokenizeLine(const String & line, String tokens[], int& tokenCount) {
    tokenCount = 0;
    int start = 0;
    int end = 0;
    int len = line.length();

    while (start < len && tokenCount < 32) {
      while (start < len && line[start] == ' ') start++;
      if (start >= len) break;

      end = start;
      bool inQuotes = false;
      while (end < len) {
        char c = line[end];
        if (c == '"' && (end == 0 || line[end - 1] != '\\')) {
          inQuotes = !inQuotes;
        }
        if (!inQuotes && c == ' ') break;
        end++;
      }

      String token = line.substring(start, end);
      if (token.length() > 0) {
        tokens[tokenCount++] = token;
      }
      start = end;
    }
  }

  void colonWord(uint16_t addr) {
    if (stackTop < 2) return;
    uint8_t nameLen = stack[stackTop - 2];
    uint8_t nameType = stack[stackTop - 1];
    if (nameType != TYPE_NAME) return;
    if (nameLen > stackTop - 2) return;

    size_t nameStart = stackTop - 2 - nameLen;
    const char* name = (const char*)&stack[nameStart];

    size_t headerSize = 2 + 1 + nameLen + 1 + 1;
    if (dictLen + headerSize > DICT_SIZE) {
      outputStream->println("⚠️ Dictionary full");
      return;
    }

    uint8_t* pos = &dictionary[dictLen];
    pos[0] = 0; // next будет заполнен позже
    pos[1] = 0;
    pos[2] = nameLen;
    memcpy(&pos[3], name, nameLen);
    pos[3 + nameLen] = 0; // storage = 0 (external)
    pos[3 + nameLen + 1] = currentContext; // context = 0

    compileTarget = dictLen;
    dictLen += headerSize;
    stackTop = nameStart;

    compiling = true;
  }

  void semicolonWord(uint16_t addr) {
    if (!compiling) {
      outputStream->println("⚠️ ; outside compilation");
      return;
    }

    uint16_t finalNext = dictLen;
    dictionary[compileTarget] = (finalNext >> 0) & 0xFF;
    dictionary[compileTarget + 1] = (finalNext >> 8) & 0xFF;

    compiling = false;
  }
