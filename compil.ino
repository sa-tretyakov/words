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
      Serial.println("⚠️ Строка слишком длинная");
      return;
    }

    if (compiling) {
      // Компиляция строки как литерала
      if (dictLen + 6 + len > DICT_SIZE) {
        Serial.println("⚠️ Dictionary full (string literal)");
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
      Serial.printf("⚠️ Float literals cannot have type suffixes: %s\n", tokenOrig.c_str());
      return;
    }
    float f = tempToken.toFloat();
    storeValueToVariable(ADDR_TMP_LIT, (uint8_t*)&f, 4, TYPE_FLOAT);
    executeAt(ADDR_TMP_LIT);
  } else {
    long val;
    if (isHex) val = strtol(tempToken.c_str(), nullptr, 16);
    else val = atol(tempToken.c_str());

    uint8_t type, len;
    uint8_t data[4];

    if (forcedType == TYPE_UNDEFINED) {
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
        type = TYPE_INT; len = 4; int32_t v = (val < INT32_MIN) ? INT32_MIN : (val > INT32_MAX) ? INT32_MAX : (int32_t)val; memcpy(data, &v, 4);
      }
    } else {
      switch (forcedType) {
        case TYPE_INT8: {
            type = TYPE_INT8; len = 1;
            int8_t v8 = (val < INT8_MIN) ? INT8_MIN : (val > INT8_MAX) ? INT8_MAX : (int8_t)val;
            data[0] = v8;
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
            return;
          }
      }
    }

    storeValueToVariable(ADDR_TMP_LIT, data, len, type);
    executeAt(ADDR_TMP_LIT);
  }
}
