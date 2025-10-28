bool isValidNumber(const String& s, bool& hasDot, bool& isHex) {
  if (s.length() == 0) return false;
  hasDot = false;
  isHex = false;
  bool hasDigit = false;
  int start = 0;

  // Обработка знака
  if (s[0] == '+' || s[0] == '-') {
    start = 1;
    if (start >= s.length()) return false;
  }

  // Проверка на шестнадцатеричный формат: 0x или 0X
  if (s.length() >= (size_t)(start + 3) && 
      s[start] == '0' && 
      (s[start + 1] == 'x' || s[start + 1] == 'X')) {
    isHex = true;
    start += 2; // пропускаем "0x"
    if (start >= s.length()) return false;
    for (int i = start; i < s.length(); i++) {
      char c = s[i];
      if (!isxdigit(c)) {
        // Проверяем суффиксы типов
        String suffix = s.substring(i);
        if (suffix == "u8" || suffix == "i8" || 
            suffix == "u16" || suffix == "i16" || 
            suffix == "i32") {
          return true;
        }
        return false;
      }
      hasDigit = true;
    }
    return hasDigit;
  }

  // Десятичные числа
  for (int i = start; i < s.length(); i++) {
    char c = s[i];
    if (c == '.') {
      if (hasDot) return false;
      hasDot = true;
    } else if (isdigit(c)) {
      hasDigit = true;
    } else {
      // Проверяем суффиксы типов
      String suffix = s.substring(i);
      if (suffix == "u8" || suffix == "i8" || 
          suffix == "u16" || suffix == "i16" || 
          suffix == "i32") {
        return true;
      }
      return false;
    }
  }
  return hasDigit;
}

void executeAt(uint16_t addr) {
  if (addr + 2 >= DICT_SIZE) return;
  uint8_t nameLen = dictionary[addr + 2];
  if (addr + 3 + nameLen + 1 >= DICT_SIZE) return;
  uint8_t storage = dictionary[addr + 3 + nameLen];
  if (storage & 0x80) {
    uint16_t nextPtr = dictionary[addr] | (dictionary[addr + 1] << 8);
    if (nextPtr < addr + 7 || nextPtr > DICT_SIZE) return;
    uint32_t funcAddr =
      dictionary[nextPtr - 4] |
      (dictionary[nextPtr - 3] << 8) |
      (dictionary[nextPtr - 2] << 16) |
      (dictionary[nextPtr - 1] << 24);
    WordFunc func = (WordFunc)funcAddr;
    func(addr);
  } else {
    uint8_t nameLen = dictionary[addr + 2];
    uint16_t bodyStart = addr + 3 + nameLen + 2;
    uint16_t pos = bodyStart;

    while (pos + 2 <= DICT_SIZE) {
      uint16_t targetAddr = dictionary[pos] | (dictionary[pos + 1] << 8);
      if (targetAddr == 0) break;

      if (targetAddr == 0xFFFF) {
        pos += 2;
        if (pos + 2 > DICT_SIZE) break;
        uint8_t len = dictionary[pos++];
        uint8_t type = dictionary[pos++];
        if (pos + len > DICT_SIZE) break;
        pushValue(&dictionary[pos], len, type);
        pos += len;
      } else {
        executeAt(targetAddr);
        pos += 2;
      }
    }
  }
}

void executeLine(String& line) {
  // Обработка многострочного комментария (продолжение)
  if (insideMultilineComment) {
    size_t endPos = line.indexOf("*/");
    if (endPos == -1) {
      return; // всё в комментарии
    }
    String remainder = line.substring(endPos + 2);
    insideMultilineComment = false;
    if (remainder.length() > 0) {
      executeLine(remainder);
    }
    return;
  }

  // Удаляем однострочные комментарии // (только если не в строке)
  // Простая реализация: ищем // и обрезаем, если до него чётное число кавычек
  int quoteCount = 0;
  size_t slashSlashPos = -1;
  for (size_t i = 0; i < line.length(); i++) {
    if (line[i] == '"') {
      quoteCount++;
    } else if (i + 1 < line.length() && line[i] == '/' && line[i + 1] == '/') {
      if (quoteCount % 2 == 0) { // вне строки
        slashSlashPos = i;
        break;
      }
    }
  }
  if (slashSlashPos != -1) {
    line = line.substring(0, slashSlashPos);
  }

  // Проверка JSON (только если строка не пустая)
  if (line.length() > 0 && line.startsWith("{")) {
    if (line.endsWith("}")) {
      loadJson(line.c_str());
      return;
    }
  }

  // Обработка /* ... */
  size_t startPos = 0;
  while (startPos < line.length()) {
    size_t commentStart = line.indexOf("/*", startPos);
    if (commentStart == -1) {
      String fragment = line.substring(startPos);
      if (fragment.length() > 0) {
        executeLineTokens(fragment);
      }
      return;
    }

    if (commentStart > startPos) {
      String fragment = line.substring(startPos, commentStart);
      executeLineTokens(fragment);
    }

    size_t commentEnd = line.indexOf("*/", commentStart + 2);
    if (commentEnd == -1) {
      insideMultilineComment = true;
      return;
    }

    startPos = commentEnd + 2;
  }
}

void executeLineTokens(String& line) {
  const int MAX_TOKENS = 32;
  String tokens[MAX_TOKENS];
  int tokenCount = 0;

  int start = 0;
  int end = 0;
  int len = line.length();

  while (start < len && tokenCount < MAX_TOKENS) {
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

  // === РЕЖИМ КОМПИЛЯЦИИ ===
  if (compiling) {
    for (int i = 0; i < tokenCount; i++) {
      if (tokens[i] == ";") {
        compiling = false;
        return;
      }
    }

    for (int i = tokenCount - 1; i >= 0; i--) {
      String& token = tokens[i];
      String tokenOrig = token;
      String tempToken = token;
      ValueType forcedType = TYPE_UNDEFINED;
      bool hasDot = false;
      bool isHex = false;
      bool isNumber = false;

      // Обработка суффиксов
      if (tempToken.endsWith("i32")) {
        forcedType = TYPE_INT;
        tempToken.remove(tempToken.length() - 3);
      } else if (tempToken.endsWith("i16")) {
        forcedType = TYPE_INT16;
        tempToken.remove(tempToken.length() - 3);
      } else if (tempToken.endsWith("u16")) {
        forcedType = TYPE_UINT16;
        tempToken.remove(tempToken.length() - 3);
      } else if (tempToken.endsWith("i8")) {
        forcedType = TYPE_INT8;
        tempToken.remove(tempToken.length() - 2);
      } else if (tempToken.endsWith("u8")) {
        forcedType = TYPE_UINT8;
        tempToken.remove(tempToken.length() - 2);
      }

      if (tempToken.length() > 0) {
        isNumber = isValidNumber(tempToken, hasDot, isHex);
      }

      if (isNumber) {
        if (hasDot) {
          // FLOAT
          if (forcedType != TYPE_UNDEFINED) {
            Serial.printf("⚠️ Float literals cannot have type suffixes: %s\n", tokenOrig.c_str());
            continue;
          }
          float f = tempToken.toFloat();
          uint8_t type = TYPE_FLOAT;
          uint8_t len = 4;
          uint8_t data[4];
          memcpy(data, &f, 4);

          uint16_t bodyEnd = dictLen;
          if (bodyEnd + 2 + 1 + 1 + len + 2 > DICT_SIZE) {
            Serial.println("⚠️ Dictionary full during compile");
            continue;
          }

          dictionary[bodyEnd] = dictionary[bodyEnd - 2];
          dictionary[bodyEnd + 1] = dictionary[bodyEnd - 1];
          dictionary[bodyEnd - 2] = 0xFF;
          dictionary[bodyEnd - 1] = 0xFF;
          dictionary[bodyEnd] = len;
          dictionary[bodyEnd + 1] = type;
          memcpy(&dictionary[bodyEnd + 2], data, len);

          uint16_t newNext = bodyEnd + 2 + len + 2;
          dictionary[compileTarget] = (newNext >> 0) & 0xFF;
          dictionary[compileTarget + 1] = (newNext >> 8) & 0xFF;
          dictLen = newNext;
        } else {
          // ЦЕЛЫЕ ЧИСЛА (включая HEX)
          long val;
          if (isHex) {
            val = strtol(tempToken.c_str(), nullptr, 16);
          } else {
            val = atol(tempToken.c_str());
          }

          uint8_t type;
          uint8_t len;
          uint8_t data[4];

          if (forcedType == TYPE_UNDEFINED) {
            type = TYPE_INT;
            len = 4;
            int32_t v = (val < INT32_MIN) ? INT32_MIN : (val > INT32_MAX) ? INT32_MAX : (int32_t)val;
            memcpy(data, &v, 4);
          } else {
            switch (forcedType) {
              case TYPE_INT8: {
                  type = TYPE_INT8; len = 1;
                  data[0] = (val < INT8_MIN) ? INT8_MIN : (val > INT8_MAX) ? INT8_MAX : (int8_t)val;
                  break;
                }
              case TYPE_UINT8: {
                  type = TYPE_UINT8; len = 1;
                  data[0] = (val < 0) ? 0 : (val > UINT8_MAX) ? UINT8_MAX : (uint8_t)val;
                  break;
                }
              case TYPE_INT16: {
                  type = TYPE_INT16; len = 2;
                  int16_t v16 = (val < INT16_MIN) ? INT16_MIN : (val > INT16_MAX) ? INT16_MAX : (int16_t)val;
                  memcpy(data, &v16, 2);
                  break;
                }
              case TYPE_UINT16: {
                  type = TYPE_UINT16; len = 2;
                  uint16_t u16 = (val < 0) ? 0 : (val > UINT16_MAX) ? UINT16_MAX : (uint16_t)val;
                  memcpy(data, &u16, 2);
                  break;
                }
              case TYPE_INT: {
                  type = TYPE_INT; len = 4;
                  int32_t v32 = (val < INT32_MIN) ? INT32_MIN : (val > INT32_MAX) ? INT32_MAX : (int32_t)val;
                  memcpy(data, &v32, 4);
                  break;
                }
              default: {
                  Serial.printf("⚠️ Internal error: %s\n", tokenOrig.c_str());
                  continue;
                }
            }
          }

          uint16_t bodyEnd = dictLen;
          if (bodyEnd + 2 + 1 + 1 + len + 2 > DICT_SIZE) {
            Serial.println("⚠️ Dictionary full during compile");
            continue;
          }

          dictionary[bodyEnd] = dictionary[bodyEnd - 2];
          dictionary[bodyEnd + 1] = dictionary[bodyEnd - 1];
          dictionary[bodyEnd - 2] = 0xFF;
          dictionary[bodyEnd - 1] = 0xFF;
          dictionary[bodyEnd] = len;
          dictionary[bodyEnd + 1] = type;
          memcpy(&dictionary[bodyEnd + 2], data, len);

          uint16_t newNext = bodyEnd + 2 + len + 2;
          dictionary[compileTarget] = (newNext >> 0) & 0xFF;
          dictionary[compileTarget + 1] = (newNext >> 8) & 0xFF;
          dictLen = newNext;
        }
      } else {
        // ОБЫЧНЫЙ ПОИСК СЛОВА
        uint16_t ptr = 0;
        bool found = false;
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
            if (match) {
              uint16_t nameLenTarget = dictionary[compileTarget + 2];
              uint16_t headerSize = 2 + 1 + nameLenTarget + 1 + 1;
              uint16_t bodyEnd = dictLen;

              if (bodyEnd + 2 > DICT_SIZE) {
                Serial.println("⚠️ Dictionary full during compile");
                found = true;
                break;
              }

              dictionary[bodyEnd] = dictionary[bodyEnd - 2];
              dictionary[bodyEnd + 1] = dictionary[bodyEnd - 1];
              dictionary[bodyEnd - 2] = (ptr >> 0) & 0xFF;
              dictionary[bodyEnd - 1] = (ptr >> 8) & 0xFF;

              uint16_t newNext = bodyEnd + 2;
              dictionary[compileTarget] = (newNext >> 0) & 0xFF;
              dictionary[compileTarget + 1] = (newNext >> 8) & 0xFF;
              dictLen = newNext;

              found = true;
              break;
            }
          }
          ptr = dictionary[ptr] | (dictionary[ptr + 1] << 8);
        }

        if (!found) {
          Serial.printf("⚠️ Word not found in compile: %s\n", token.c_str());
        }
      }
    }
    return;
  }

  // === ОБЫЧНЫЙ РЕЖИМ ===
  for (int i = tokenCount - 1; i >= 0; i--) {
    String& token = tokens[i];

    if (token.startsWith("\"") && token.endsWith("\"") && token.length() >= 2) {
      String strContent = token.substring(1, token.length() - 1);
      size_t len = strContent.length();
      if (len > 255) {
        Serial.println("⚠️ Строка слишком длинная");
      } else {
        storeValueToVariable(ADDR_TMP_LIT, (uint8_t*)strContent.c_str(), (uint8_t)len, TYPE_STRING);
        executeAt(ADDR_TMP_LIT);
      }
    } else {
      String tokenOrig = token;
      ValueType forcedType = TYPE_UNDEFINED;

      if (token.endsWith("i32")) {
        forcedType = TYPE_INT;
        token.remove(token.length() - 3);
      } else if (token.endsWith("i16")) {
        forcedType = TYPE_INT16;
        token.remove(token.length() - 3);
      } else if (token.endsWith("u16")) {
        forcedType = TYPE_UINT16;
        token.remove(token.length() - 3);
      } else if (token.endsWith("i8")) {
        forcedType = TYPE_INT8;
        token.remove(token.length() - 2);
      } else if (token.endsWith("u8")) {
        forcedType = TYPE_UINT8;
        token.remove(token.length() - 2);
      }

      if (token.length() == 0) {
        lookupAndExecute(tokenOrig);
        continue;
      }

      bool hasDot = false;
      bool isHex = false;
      bool isNumber = isValidNumber(token, hasDot, isHex);

      if (!isNumber) {
        lookupAndExecute(tokenOrig);
        continue;
      }

      if (hasDot) {
        if (forcedType != TYPE_UNDEFINED) {
          Serial.printf("⚠️ Float literals cannot have type suffixes: %s\n", tokenOrig.c_str());
          continue;
        }
        float f = token.toFloat();
        storeValueToVariable(ADDR_TMP_LIT, (uint8_t*)&f, 4, TYPE_FLOAT);
        executeAt(ADDR_TMP_LIT);
      } else {
        // ЦЕЛЫЕ ЧИСЛА С HEX
        long val;
        if (isHex) {
          val = strtol(token.c_str(), nullptr, 16);
        } else {
          val = atol(token.c_str());
        }

        uint8_t type;
        uint8_t len;
        uint8_t data[4];

        if (forcedType == TYPE_UNDEFINED) {
          type = TYPE_INT;
          len = 4;
          int32_t v = (val < INT32_MIN) ? INT32_MIN : (val > INT32_MAX) ? INT32_MAX : (int32_t)val;
          memcpy(data, &v, 4);
        } else {
          switch (forcedType) {
            case TYPE_INT8: {
                type = TYPE_INT8; len = 1;
                data[0] = (val < INT8_MIN) ? INT8_MIN : (val > INT8_MAX) ? INT8_MAX : (int8_t)val;
                break;
              }
            case TYPE_UINT8: {
                type = TYPE_UINT8; len = 1;
                data[0] = (val < 0) ? 0 : (val > UINT8_MAX) ? UINT8_MAX : (uint8_t)val;
                break;
              }
            case TYPE_INT16: {
                type = TYPE_INT16; len = 2;
                int16_t v16 = (val < INT16_MIN) ? INT16_MIN : (val > INT16_MAX) ? INT16_MAX : (int16_t)val;
                memcpy(data, &v16, 2);
                break;
              }
            case TYPE_UINT16: {
                type = TYPE_UINT16; len = 2;
                uint16_t u16 = (val < 0) ? 0 : (val > UINT16_MAX) ? UINT16_MAX : (uint16_t)val;
                memcpy(data, &u16, 2);
                break;
              }
            case TYPE_INT: {
                type = TYPE_INT; len = 4;
                int32_t v32 = (val < INT32_MIN) ? INT32_MIN : (val > INT32_MAX) ? INT32_MAX : (int32_t)val;
                memcpy(data, &v32, 4);
                break;
              }
            default: {
                Serial.printf("⚠️ Internal error parsing: %s\n", tokenOrig.c_str());
                continue;
              }
          }
        }

        storeValueToVariable(ADDR_TMP_LIT, data, len, type);
        executeAt(ADDR_TMP_LIT);
      }
    }
  }
}

void bodyWord(uint16_t addr) {
  // Читаем строку со стека
  uint8_t type, len;
  const uint8_t* data;
  if (!peekStackTop(&type, &len, &data)) return;
  if (type != TYPE_STRING) {
    Serial.println("⚠️ body: expected string in quotes");
    return;
  }
  dropTop(0);

  // Преобразуем в String
  String wordName = String((char*)data, len);

  // Ищем слово в словаре
  uint16_t ptr = 0;
  bool found = false;
  uint16_t wordAddr = 0;

  while (ptr < dictLen) {
    if (ptr + 2 > DICT_SIZE) break;
    uint16_t nextPtr = dictionary[ptr] | (dictionary[ptr + 1] << 8);
    if (nextPtr == 0 || nextPtr <= ptr || nextPtr > DICT_SIZE) break;

    uint8_t nameLen = dictionary[ptr + 2];
    if (nameLen == 0 || ptr + 3 + nameLen > DICT_SIZE) break;

    if ((size_t)nameLen == wordName.length()) {
      bool match = true;
      for (uint8_t i = 0; i < nameLen; i++) {
        if (dictionary[ptr + 3 + i] != wordName[i]) {
          match = false;
          break;
        }
      }
      if (match) {
        found = true;
        wordAddr = ptr;
        break;
      }
    }
    ptr = nextPtr;
  }

  if (!found) {
    Serial.println("⚠️ body: word not found");
    return;
  }

  // Проверяем: external-слово?
  uint8_t nameLen = dictionary[wordAddr + 2];
  uint8_t storage = dictionary[wordAddr + 3 + nameLen];
  if (storage != 0) {
    Serial.println("⚠️ body: not an external word");
    return;
  }

  // Выводим тело
  uint16_t headerSize = 2 + 1 + nameLen + 1 + 1; // next+len+name+storage+context
  uint16_t pos = wordAddr + headerSize;

  Serial.print("Body: ");
  bool first = true;
  while (pos + 2 <= DICT_SIZE) {
    uint16_t targetAddr = dictionary[pos] | (dictionary[pos + 1] << 8);
    if (targetAddr == 0) break; // конец тела

    if (targetAddr == 0xFFFF) {
      // Литерал
      pos += 2;
      if (pos + 2 > DICT_SIZE) break;
      uint8_t lenLit = dictionary[pos++];
      uint8_t typeLit = dictionary[pos++];
      if (pos + lenLit > DICT_SIZE) break;

      if (!first) Serial.print(" ");
      Serial.print("<lit:");
      // Выводим данные в зависимости от типа
      if (typeLit == TYPE_INT && lenLit == 4) {
        int32_t v; memcpy(&v, &dictionary[pos], 4);
        Serial.print(v);
      }
      else if (typeLit == TYPE_UINT8 && lenLit == 1) {
        Serial.print((int)dictionary[pos]);
        Serial.print("u8");
      }
      else if (typeLit == TYPE_INT8 && lenLit == 1) {
        Serial.print((int)(int8_t)dictionary[pos]);
        Serial.print("i8");
      }
      else if (typeLit == TYPE_UINT16 && lenLit == 2) {
        uint16_t v; memcpy(&v, &dictionary[pos], 2);
        Serial.print(v);
        Serial.print("u16");
      }
      else if (typeLit == TYPE_INT16 && lenLit == 2) {
        int16_t v; memcpy(&v, &dictionary[pos], 2);
        Serial.print(v);
        Serial.print("i16");
      }
      else if (typeLit == TYPE_FLOAT && lenLit == 4) {
        float v; memcpy(&v, &dictionary[pos], 4);
        Serial.print(v, 6);
      }
      else if (typeLit == TYPE_STRING) {
        Serial.print("\"");
        for (uint8_t i = 0; i < lenLit; i++) {
          char c = dictionary[pos + i];
          if (c >= 32 && c <= 126) Serial.print(c);
          else Serial.printf("\\x%02X", (uint8_t)c);
        }
        Serial.print("\"");
      }
      else {
        Serial.print("?");
      }
      Serial.print(">");

      pos += lenLit;
      first = false;
    }
    else {
      // Обычное слово
      uint8_t targetNameLen = dictionary[targetAddr + 2];
      if (!first) Serial.print(" ");
      for (uint8_t i = 0; i < targetNameLen; i++) {
        Serial.print((char)dictionary[targetAddr + 3 + i]);
      }
      first = false;
      pos += 2;
    }
  }
  Serial.println();
}
