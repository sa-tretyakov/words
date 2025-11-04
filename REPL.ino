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
    // Internal word
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
    // External word
    uint16_t nextPtr = dictionary[addr] | (dictionary[addr + 1] << 8);
    uint16_t bodyStart = addr + 3 + nameLen + 2;
    uint16_t pos = bodyStart;

    while (pos < nextPtr) {
      if (pos + 2 > nextPtr) break; // Защита от выхода за пределы

      uint16_t targetAddr = dictionary[pos] | (dictionary[pos + 1] << 8);
      
      // Обработка маркера IF (0xFFFD)
      if (targetAddr == 0xFFFD) {
        uint8_t type, len;
        const uint8_t* data;
        if (!peekStackTop(&type, &len, &data)) {
          pos += 2; // Выполняем end
          continue;
        }

        bool condition = false;
        if (type == TYPE_BOOL && len == 1) condition = data[0];
        else if (type == TYPE_INT && len == 4) { int32_t v; memcpy(&v, data, 4); condition = (v != 0); }
        else if (type == TYPE_UINT8 && len == 1) condition = (data[0] != 0);
        else if (type == TYPE_INT8 && len == 1) condition = ((int8_t)data[0] != 0);
        else if (type == TYPE_UINT16 && len == 2) { uint16_t v; memcpy(&v, data, 2); condition = (v != 0); }
        else if (type == TYPE_INT16 && len == 2) { int16_t v; memcpy(&v, data, 2); condition = (v != 0); }
        else if (type == TYPE_FLOAT && len == 4) { float v; memcpy(&v, data, 4); condition = (v != 0.0f); }

        dropTop(0);

        if (condition) {
          // Истина → пропускаем end (0xFFFE)
          pos += 4; // IF (2) + end (2)
        } else {
          // Ложь → выполняем end
          pos += 2; // указываем на end
        }
        continue;
      }

      // Обработка маркера end (0xFFFE)
      if (targetAddr == 0xFFFE) {
        return;
      }

      // Обработка литерала (0xFFFF)
      if (targetAddr == 0xFFFF) {
        pos += 2;
        if (pos + 2 > nextPtr) break;
        uint8_t len = dictionary[pos++];
        uint8_t type = dictionary[pos++];
        if (pos + len > nextPtr) break;
        pushValue(&dictionary[pos], len, type);
        pos += len;
        continue;
      }

      // Обычное слово
      executeAt(targetAddr);
      pos += 2;
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

void bodyWord(uint16_t addr) {
  // Читаем имя слова со стека
  uint8_t type, len;
  const uint8_t* data;
  if (!peekStackTop(&type, &len, &data)) return;
  if (type != TYPE_STRING) {
    Serial.println("⚠️ body: expected string in quotes");
    return;
  }
  dropTop(0);

  String wordName = String((char*)data, len);

  // Ищем слово в словаре
  uint16_t wordAddr = 0;
  bool found = false;
  uint16_t ptr = 0;
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

  // Только external-слова
  uint8_t nameLen = dictionary[wordAddr + 2];
  uint8_t storage = dictionary[wordAddr + 3 + nameLen];
  if (storage != 0) {
    Serial.println("⚠️ body: not an external word");
    return;
  }

  // Выводим тело
  uint16_t nextPtr = dictionary[wordAddr] | (dictionary[wordAddr + 1] << 8);
  uint16_t pos = wordAddr + 2 + 1 + nameLen + 1 + 1; // next + nameLen + name + storage + context

  Serial.print("Body: ");
  bool first = true;

  while (pos < nextPtr) {
    if (pos + 2 > nextPtr) break;

    uint16_t targetAddr = dictionary[pos] | (dictionary[pos + 1] << 8);

    // Специальные маркеры
    if (targetAddr == 0xFFFD) {
      if (!first) Serial.print(" ");
      Serial.print("if");
      first = false;
      pos += 2;
      continue;
    }
    if (targetAddr == 0xFFFE) {
      if (!first) Serial.print(" ");
      Serial.print("end");
      first = false;
      pos += 2;
      continue;
    }

    // Литералы
    if (targetAddr == 0xFFFF) {
      pos += 2;
      if (pos + 2 > nextPtr) break;
      uint8_t lenLit = dictionary[pos++];
      uint8_t typeLit = dictionary[pos++];
      if (pos + lenLit > nextPtr) break;

      if (!first) Serial.print(" ");
      Serial.print("<lit:");

      if (typeLit == TYPE_STRING) {
        Serial.print("\"");
        for (uint8_t i = 0; i < lenLit; i++) {
          char c = dictionary[pos + i];
          if (c >= 32 && c <= 126) {
            Serial.print(c);
          } else {
            Serial.printf("\\x%02X", (uint8_t)c);
          }
        }
        Serial.print("\"");
      }
      else if (typeLit == TYPE_INT && lenLit == 4) {
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
      else {
        Serial.print("?");
      }

      Serial.print(">");
      pos += lenLit;
      first = false;
      continue;
    }

    // Обычное слово
    uint8_t targetNameLen = dictionary[targetAddr + 2];
    if (targetNameLen == 0 || targetAddr + 3 + targetNameLen > DICT_SIZE) {
      if (!first) Serial.print(" ");
      Serial.print("<bad>");
      first = false;
      pos += 2;
      continue;
    }

    if (!first) Serial.print(" ");
    for (uint8_t i = 0; i < targetNameLen; i++) {
      char c = dictionary[targetAddr + 3 + i];
      if (c >= 32 && c <= 126) {
        Serial.print(c);
      } else {
        Serial.printf("\\x%02X", (uint8_t)c);
      }
    }
    first = false;
    pos += 2;
  }

  Serial.println();
}
