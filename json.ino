void loadJson(const char* jsonStr) {
  if (!jsonStr) return;
  size_t len = strlen(jsonStr);
  if (len < 2 || jsonStr[0] != '{' || jsonStr[len-1] != '}') return;
  size_t i = 1; // пропускаем '{'
  while (i < len - 1) {
    // Пропускаем пробелы
    while (i < len - 1 && jsonStr[i] == ' ') i++;
    if (i >= len - 1) break;
    // Ожидаем кавычки (начало ключа)
    if (jsonStr[i] != '"') break;
    i++;
    // Читаем ключ
    size_t keyStart = i;
    while (i < len - 1 && jsonStr[i] != '"') {
      if (jsonStr[i] == '\\') i++; // экранирование
      i++;
    }
    size_t keyLen = i - keyStart;
    if (jsonStr[i] != '"') break;
    i++; // пропускаем закрывающую кавычку
    // Пропускаем пробелы и ':'
    while (i < len - 1 && jsonStr[i] == ' ') i++;
    if (i >= len - 1 || jsonStr[i] != ':') break;
    i++;
    while (i < len - 1 && jsonStr[i] == ' ') i++;
    // Определяем тип значения
    String valueStr;
    if (jsonStr[i] == '"') {
      // Строка
      i++;
      size_t valStart = i;
      while (i < len - 1 && jsonStr[i] != '"') {
        if (jsonStr[i] == '\\') i++;
        i++;
      }
      size_t valLen = i - valStart;
      if (jsonStr[i] != '"') break;
      i++;
      valueStr = "\"";
      for (size_t j = 0; j < valLen; j++) {
        char c = jsonStr[valStart + j];
        if (c == '\\' && j + 1 < valLen) {
          j++;
          char next = jsonStr[valStart + j];
          if (next == '"' || next == '\\') {
            valueStr += next;
          } else {
            valueStr += '\\';
            valueStr += next;
          }
        } else {
          valueStr += c;
        }
      }
      valueStr += "\"";
    }
    else if (jsonStr[i] == 't' && i + 3 < len && strncmp(&jsonStr[i], "true", 4) == 0) {
      valueStr = "high";
      i += 4;
    }
    else if (jsonStr[i] == 'f' && i + 4 < len && strncmp(&jsonStr[i], "false", 5) == 0) {
      valueStr = "low";
      i += 5;
    }
    else {
      // Число (включая отрицательные и float)
      size_t valStart = i;
      while (i < len - 1 && 
             (isdigit(jsonStr[i]) || jsonStr[i] == '.' || 
              jsonStr[i] == '-' || jsonStr[i] == '+' || 
              jsonStr[i] == 'e' || jsonStr[i] == 'E')) {
        i++;
      }
      if (valStart == i) break;
      String num(&jsonStr[valStart], i - valStart);
      valueStr = num;
    }
    // Пропускаем запятую и пробелы
    while (i < len - 1 && jsonStr[i] == ' ') i++;
    if (i < len - 1 && jsonStr[i] == ',') i++;
    // Формируем ключ как строку
    String key(&jsonStr[keyStart], keyLen);
    // Генерируем команды: сначала var, потом присваивание
    String cmd1 = "var "+key;
    String cmd2 = key+" = "+valueStr;
    // Выполняем
    executeLine(cmd1);
    executeLine(cmd2);
  }
}

void jsonWord(uint16_t addr) {
  jsonOutput->print("{");
  uint16_t ptr = 0;
  bool first = true;

  while (ptr < dictLen) {
    if (ptr + 2 > DICT_SIZE) break;
    uint16_t nextPtr = dictionary[ptr] | (dictionary[ptr + 1] << 8);
    if (nextPtr == 0 || nextPtr <= ptr || nextPtr > DICT_SIZE) break;

    if (ptr + 3 > DICT_SIZE) break;
    uint8_t nameLen = dictionary[ptr + 2];
    if (nameLen == 0 || ptr + 3 + nameLen > DICT_SIZE) break;

    uint8_t storage = dictionary[ptr + 3 + nameLen];
    uint8_t context = dictionary[ptr + 3 + nameLen + 1];
    uint8_t storageType = storage & 0x7F;

    if ((storageType == STORAGE_POOLED || 
         storageType == STORAGE_CONST || 
         storageType == STORAGE_CONT) && 
        context == currentContext) {

      // Имя
      if (!first) jsonOutput->print(",");
      jsonOutput->print("\"");
      for (uint8_t i = 0; i < nameLen; i++) {
        char c = dictionary[ptr + 3 + i];
        if (c == '"' || c == '\\') jsonOutput->print('\\');
        jsonOutput->print(c);
      }
      jsonOutput->print("\":");

      // Значение
      if (storageType == STORAGE_CONT || storageType == STORAGE_CONST) {
        const uint8_t* valData = &dictionary[ptr + 3 + nameLen + 2];
        int32_t v; memcpy(&v, valData, 4);
        jsonOutput->print(v);
      }
      else {
        uint32_t poolRef =
          dictionary[ptr + 3 + nameLen + 2 + 0] |
          (dictionary[ptr + 3 + nameLen + 2 + 1] << 8) |
          (dictionary[ptr + 3 + nameLen + 2 + 2] << 16) |
          (dictionary[ptr + 3 + nameLen + 2 + 3] << 24);

        if (poolRef == 0xFFFFFFFF) {
          jsonOutput->print("null");
        }
        else if (poolRef >= DATA_POOL_SIZE) {
          jsonOutput->print("null");
        }
        else {
          uint8_t varType = dataPool[poolRef];
          uint8_t varLen = dataPool[poolRef + 1];
          if (poolRef + 2 + varLen > DATA_POOL_SIZE) {
            jsonOutput->print("null");
          }
          else {
            if (varType == TYPE_INT && varLen == 4) {
              int32_t v; memcpy(&v, &dataPool[poolRef + 2], 4);
              jsonOutput->print(v);
            }
            else if (varType == TYPE_UINT8 && varLen == 1) {
              uint8_t v = dataPool[poolRef + 2];
              jsonOutput->print(v);
            }
            else if (varType == TYPE_INT8 && varLen == 1) {
              int8_t v = (int8_t)dataPool[poolRef + 2];
              jsonOutput->print(v);
            }
            else if (varType == TYPE_UINT16 && varLen == 2) {
              uint16_t v; memcpy(&v, &dataPool[poolRef + 2], 2);
              jsonOutput->print(v);
            }
            else if (varType == TYPE_INT16 && varLen == 2) {
              int16_t v; memcpy(&v, &dataPool[poolRef + 2], 2);
              jsonOutput->print(v);
            }
            else if (varType == TYPE_FLOAT && varLen == 4) {
              float v; memcpy(&v, &dataPool[poolRef + 2], 4);
              jsonOutput->print(v, 6);
            }
            else if (varType == TYPE_BOOL && varLen == 1) {
              bool v = (dataPool[poolRef + 2] != 0);
              jsonOutput->print(v ? "true" : "false");
            }
            else if (varType == TYPE_STRING) {
              jsonOutput->print("\"");
              for (uint8_t i = 0; i < varLen; i++) {
                char c = dataPool[poolRef + 2 + i];
                if (c == '"' || c == '\\') jsonOutput->print('\\');
                jsonOutput->print(c);
              }
              jsonOutput->print("\"");
            }
            else {
              jsonOutput->print("null");
            }
          }
        }
      }

      first = false;
    }

    ptr = nextPtr;
  }

  jsonOutput->print("}");
}

void jsonToSerialWord(uint16_t addr) {
  jsonOutput = &Serial;
  if (jsonFile) {
    jsonFile.close();
  }
}

void jsonToFile(uint16_t addr) {
  // Имя файла должно быть на стеке
  uint8_t nameType, nameLen;
  const uint8_t* nameData;
  if (!peekStackTop(&nameType, &nameLen, &nameData)) return;
  if (nameType != TYPE_STRING && nameType != TYPE_NAME) return;
  dropTop(0);

  char filename[nameLen + 1];
  memcpy(filename, nameData, nameLen);
  filename[nameLen] = '\0';

  jsonFile = FILESYSTEM.open(filename, "w");
  if (jsonFile) {
    jsonOutput = &jsonFile;
  } else {
    Serial.println("⚠️ jsonToFile: can't open file");
    jsonOutput = &Serial;
  }
}
