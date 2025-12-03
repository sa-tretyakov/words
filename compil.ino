// Возвращает смещение слова в словаре по его имени.
// Если слово не найдено — возвращает 0xFFFF.
uint16_t findWordAddress(const char* name) {
  uint8_t nameLen = strlen(name);
  uint16_t ptr = 0;

  while (ptr < dictLen) {
    if (ptr + 2 >= DICT_SIZE) break;
    uint16_t nextPtr = dictionary[ptr] | (dictionary[ptr + 1] << 8);
    if (nextPtr == 0 || nextPtr <= ptr || nextPtr > DICT_SIZE) break;

    if (ptr + 3 > DICT_SIZE) break;
    uint8_t len = dictionary[ptr + 2];
    if (len != nameLen || ptr + 3 + len > DICT_SIZE) {
      ptr = nextPtr;
      continue;
    }

    // Сравниваем имя
    bool match = true;
    for (uint8_t i = 0; i < nameLen; i++) {
      if (dictionary[ptr + 3 + i] != name[i]) {
        match = false;
        break;
      }
    }
    if (match) {
      return ptr; // ← смещение слова в словаре
    }

    ptr = nextPtr;
  }

  return 0xFFFF; // не найдено
}

void interpretToken(const String& token) {
  // === СТРОКИ В КАВЫЧКАХ ===
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
      // Интерпретация через tmpLit
      storeValueToVariable(ADDR_TMP_LIT, (uint8_t*)strContent.c_str(), (uint8_t)len, TYPE_STRING);
      executeAt(ADDR_TMP_LIT);
    }
    return;
  }

  // === ОСТАЛЬНЫЕ ТОКЕНЫ (числа, слова) ===
  String tokenOrig = token;
  ValueType forcedType = TYPE_UNDEFINED;
  String tempToken = token;

  // Определяем, есть ли суффикс типа
  if (tempToken.endsWith("i32")) {
    forcedType = TYPE_INT;
    tempToken.remove(tempToken.length() - 3);
  }
  else if (tempToken.endsWith("i16")) {
    forcedType = TYPE_INT16;
    tempToken.remove(tempToken.length() - 3);
  }
  else if (tempToken.endsWith("u16")) {
    forcedType = TYPE_UINT16;
    tempToken.remove(tempToken.length() - 3);
  }
  else if (tempToken.endsWith("i8")) {
    forcedType = TYPE_INT8;
    tempToken.remove(tempToken.length() - 2);
  }
  else if (tempToken.endsWith("u8")) {
    forcedType = TYPE_UINT8;
    tempToken.remove(tempToken.length() - 2);
  }

  // Если после удаления суффикса пусто — это не число, это слово
  if (tempToken.length() == 0) {
    lookupAndExecute(tokenOrig);
    return;
  }

  bool hasDot = false, isHex = false;
  bool isNumber = isValidNumber(tempToken, hasDot, isHex);

  if (!isNumber) {
    lookupAndExecute(tokenOrig);
    return;
  }

  if (hasDot) {
    if (forcedType != TYPE_UNDEFINED) {
      outputStream->printf("⚠️ Float literals cannot have type suffixes: %s\n", tokenOrig.c_str());
      return;
    }
    float f = tempToken.toFloat();
    storeValueToVariable(ADDR_TMP_LIT, (uint8_t*)&f, 4, TYPE_FLOAT);
    executeAt(ADDR_TMP_LIT);
  } else {
    long val;
    if (isHex) {
      val = strtol(tempToken.c_str(), nullptr, 16);
    } else {
      val = atol(tempToken.c_str());
    }

    uint8_t type, len;
    uint8_t data[4];

    if (forcedType == TYPE_UNDEFINED) {
      // Автоматический выбор типа
      if (isHex) {
        if (val >= 0 && val <= UINT8_MAX) {
          type = TYPE_UINT8;
          len = 1;
          data[0] = (uint8_t)val;
        }
        else if (val >= 0 && val <= UINT16_MAX) {
          type = TYPE_UINT16;
          len = 2;
          uint16_t v16 = (uint16_t)val;
          memcpy(data, &v16, 2);
        }
        else {
          type = TYPE_INT;
          len = 4;
          int32_t v = (val < INT32_MIN) ? INT32_MIN : (val > INT32_MAX) ? INT32_MAX : (int32_t)val;
          memcpy(data, &v, 4);
        }
      } else {
        type = TYPE_INT; len = 4;
        int32_t v = (val < INT32_MIN) ? INT32_MIN : (val > INT32_MAX) ? INT32_MAX : (int32_t)val;
        memcpy(data, &v, 4);
      }
    } else {
      // Явно указан тип — строгая проверка диапазона
      switch (forcedType) {
        case TYPE_INT8: {
            if (val < INT8_MIN || val > INT8_MAX) {
                outputStream->printf("⚠️ Value %ld out of range for i8 (-128..127): %s\n", val, tokenOrig.c_str());
                return;
            }
            type = TYPE_INT8; len = 1;
            data[0] = (int8_t)val;
            break;
          }
        case TYPE_UINT8: {
            if (val < 0 || val > UINT8_MAX) {
                outputStream->printf("⚠️ Value %ld out of range for u8 (0..255): %s\n", val, tokenOrig.c_str());
                return;
            }
            type = TYPE_UINT8; len = 1;
            data[0] = (uint8_t)val;
            break;
          }
        case TYPE_INT16: {
            if (val < INT16_MIN || val > INT16_MAX) {
                outputStream->printf("⚠️ Value %ld out of range for i16 (-32768..32767): %s\n", val, tokenOrig.c_str());
                return;
            }
            type = TYPE_INT16; len = 2;
            int16_t v16 = (int16_t)val;
            memcpy(data, &v16, 2);
            break;
          }
        case TYPE_UINT16: {
            if (val < 0 || val > UINT16_MAX) {
                outputStream->printf("⚠️ Value %ld out of range for u16 (0..65535): %s\n", val, tokenOrig.c_str());
                return;
            }
            type = TYPE_UINT16; len = 2;
            uint16_t u16 = (uint16_t)val;
            memcpy(data, &u16, 2);
            break;
          }
        case TYPE_INT: {
            if (val < INT32_MIN || val > INT32_MAX) {
                outputStream->printf("⚠️ Value %ld out of range for i32: %s\n", val, tokenOrig.c_str());
                return;
            }
            type = TYPE_INT; len = 4;
            int32_t v32 = (int32_t)val;
            memcpy(data, &v32, 4);
            break;
          }
        default: {
            outputStream->printf("⚠️ Internal error: %s\n", tokenOrig.c_str());
            return;
          }
      }
    }

    // Сохраняем значение во временную переменную и исполняем
    storeValueToVariable(ADDR_TMP_LIT, data, len, type);
    executeAt(ADDR_TMP_LIT);
  }
}
