void executeAt(uint16_t addr) {
  if (addr + 2 >= DICT_SIZE) return;
  uint8_t nameLen = dictionary[addr + 2];
  if (addr + 3 + nameLen + 1 >= DICT_SIZE) return; // + storage + context
  uint8_t storage = dictionary[addr + 3 + nameLen];
  // context = dictionary[addr + 3 + nameLen + 1]; // пока не используется
  if (storage & 0x80) {
    uint16_t nextPtr = dictionary[addr] | (dictionary[addr + 1] << 8);
    if (nextPtr < addr + 7 || nextPtr > DICT_SIZE) return; // минимум: next+len+name+stor+ctx+funcPtr
    uint32_t funcAddr =
      dictionary[nextPtr - 4] |
      (dictionary[nextPtr - 3] << 8) |
      (dictionary[nextPtr - 2] << 16) |
      (dictionary[nextPtr - 1] << 24);

    WordFunc func = (WordFunc)funcAddr;
    func(addr);
  }      else {
    // external word
    uint16_t nameLen = dictionary[addr + 2];
    uint16_t bodyStart = addr + 3 + nameLen + 2; // + storage + context
    uint16_t pos = bodyStart;

    while (pos + 2 <= DICT_SIZE) {
      uint16_t targetAddr = dictionary[pos] | (dictionary[pos + 1] << 8);
      if (targetAddr == 0) break; // конец тела
      executeAt(targetAddr);
      pos += 2;
    }
  }
}
void executeLine(String& line) {
  // Поддержка прямого JSON: { ... }
  if (line.startsWith("{")) {
    if (line.endsWith("}")) {
      loadJson(line.c_str());
      return;
    }
    return;
  }

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
    // Проверяем: есть ли ";" в токенах?
    for (int i = 0; i < tokenCount; i++) {
      if (tokens[i] == ";") {
        compiling = false;
        return;
      }
    }



    // Компилируем СПРАВА НАЛЕВО (как выполняется строка)
    for (int i = tokenCount - 1; i >= 0; i--) {
      String& token = tokens[i];

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
            uint16_t bodyStart = compileTarget + headerSize;
            uint16_t bodyEnd = dictLen;

            if (bodyEnd + 2 > DICT_SIZE) {
              Serial.println("⚠️ Dictionary full during compile");
              found = true;
              break;
            }

            // Сдвигаем завершающие 00 00
            dictionary[bodyEnd] = dictionary[bodyEnd - 2];
            dictionary[bodyEnd + 1] = dictionary[bodyEnd - 1];

            // Записываем смещение
            dictionary[bodyEnd - 2] = (ptr >> 0) & 0xFF;
            dictionary[bodyEnd - 1] = (ptr >> 8) & 0xFF;

            // Обновляем nextOffset
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
    return;
  }

  // === ОБЫЧНЫЙ РЕЖИМ (СПРАВА НАЛЕВО) ===
  for (int i = tokenCount - 1; i >= 0; i--) {
    String& token = tokens[i];

    if (token.startsWith("\"") && token.endsWith("\"") && token.length() >= 2) {
      String strContent = token.substring(1, token.length() - 1);
      pushString(strContent.c_str());
    }
    else if (token.equalsIgnoreCase("low")) {
      pushBool(false);
    }
    else if (token.equalsIgnoreCase("high")) {
      pushBool(true);
    }
    else {
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

      bool hasDot;
      bool isNumber = isValidNumber(token, hasDot);

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
        pushFloat(f);
      } else {
        long val = atol(token.c_str());
        if (forcedType == TYPE_UNDEFINED) {
          if (val < INT32_MIN) val = INT32_MIN;
          if (val > INT32_MAX) val = INT32_MAX;
          pushInt((int32_t)val);
        } else {
          switch (forcedType) {
            case TYPE_INT8:
              if (val < INT8_MIN || val > INT8_MAX) val = val < INT8_MIN ? INT8_MIN : INT8_MAX;
              pushInt8((int8_t)val);
              break;
            case TYPE_UINT8:
              if (val < 0 || val > UINT8_MAX) val = val < 0 ? 0 : UINT8_MAX;
              pushUInt8((uint8_t)val);
              break;
            case TYPE_INT16:
              if (val < INT16_MIN || val > INT16_MAX) val = val < INT16_MIN ? INT16_MIN : INT16_MAX;
              pushInt16((int16_t)val);
              break;
            case TYPE_UINT16:
              if (val < 0 || val > UINT16_MAX) val = val < 0 ? 0 : UINT16_MAX;
              pushUInt16((uint16_t)val);
              break;
            case TYPE_INT:
              if (val < INT32_MIN || val > INT32_MAX) val = val < INT32_MIN ? INT32_MIN : INT32_MAX;
              pushInt((int32_t)val);
              break;
            default:
              Serial.printf("⚠️ Internal error parsing: %s\n", tokenOrig.c_str());
              break;
          }
        }
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

    uint8_t targetNameLen = dictionary[targetAddr + 2];
    if (!first) Serial.print(" ");
    for (uint8_t i = 0; i < targetNameLen; i++) {
      Serial.print((char)dictionary[targetAddr + 3 + i]);
    }
    first = false;

    pos += 2;
  }
  Serial.println();
}
