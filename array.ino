void loadGammaWord(uint16_t addr) {
    // Стек (сверху вниз): dst_block → gamma_x100
    uint16_t dst_addr, dst_len;
    if (!popAddrInfo(&dst_addr, &dst_len)) return;
    if (dst_len < 256) return;

    uint16_t gamma_x100;
    if (!popAsUInt16(&gamma_x100)) return;
    if (gamma_x100 < 100 || gamma_x100 > 500) return;

    uint16_t steps = gamma_x100 / 100;
    if (steps > 5) steps = 5;
    if (steps < 1) steps = 1;

    // ✅ ИНДЕКС ПУЛА → РЕАЛЬНЫЙ RAM-УКАЗАТЕЛЬ
    uint8_t* dst = &dataPool[dst_addr];

    for (uint16_t i = 0; i < 256; i++) {
        uint32_t res = i;
        uint32_t v = i;
        for (uint16_t k = 1; k < steps; k++) {
            res = (res * v) / 255;
        }
        dst[i] = (res > 255) ? 255 : (uint8_t)res;
    }
}

void mapScaleWord(uint16_t addr) {
    // HDL: mapScale источник приёмник таблица яркость
    // Стек (снизу→вверх): [яркость] [таблица] [приёмник] [источник]
    // Извлечение (сверху→вниз): источник → приёмник → таблица → яркость

    // 1. Источник
    uint16_t src_addr, src_len;
    if (!popAddrInfo(&src_addr, &src_len)) {
        if (outputStream) outputStream->println("⚠️ mapScale: не удалось извлечь источник (ожидался блок)");
        return;
    }

    // 2. Приёмник
    uint16_t dst_addr, dst_len;
    if (!popAddrInfo(&dst_addr, &dst_len)) {
        if (outputStream) outputStream->println("⚠️ mapScale: не удалось извлечь приёмник (ожидался блок)");
        return;
    }
    if (src_len != dst_len) {
        if (outputStream) outputStream->printf("⚠️ mapScale: длины не совпадают (src=%d, dst=%d)\n", src_len, dst_len);
        return;
    }
    if (src_len == 0) {
        if (outputStream) outputStream->println("⚠️ mapScale: пустой блок (длина 0)");
        return;
    }

    // 3. Таблица коррекции
    uint16_t lut_addr, lut_len;
    if (!popAddrInfo(&lut_addr, &lut_len)) {
        if (outputStream) outputStream->println("⚠️ mapScale: не удалось извлечь таблицу (ожидался блок или 0)");
        return;
    }
    // Если таблица указана, но короче 256 — предупреждаем, но не прерываем (может быть укороченная)
    if (lut_addr != 0 && lut_len < 256) {
        if (outputStream) outputStream->printf("⚠️ mapScale: таблица короткая (%d < 256), возможны артефакты\n", lut_len);
    }

    // 4. Яркость
    uint8_t coeff;
    if (!popAsUInt8(&coeff)) {
        if (outputStream) outputStream->println("⚠️ mapScale: не удалось извлечь яркость (ожидалось uint8 0..255)");
        return;
    }

    // Преобразование индексов пула в указатели
    const uint8_t* src = &dataPool[src_addr];
    uint8_t* dst       = &dataPool[dst_addr];
    const uint8_t* lut = (lut_addr != 0) ? &dataPool[lut_addr] : nullptr;

    // Быстрый путь: копирование без изменений
    if (coeff == 255 && lut == nullptr) {
        if (outputStream) outputStream->println("ℹ️ mapScale: быстрый путь — копирование (coeff=255, no LUT)");
        memcpy(dst, src, src_len);
        return;
    }

    // Быстрый путь: гашение
    if (coeff == 0) {
        if (outputStream) outputStream->println("ℹ️ mapScale: быстрый путь — гашение (coeff=0)");
        memset(dst, 0, src_len);
        return;
    }

    // Отладочный вывод перед циклом (можно закомментировать в продакшене)
    // if (outputStream) outputStream->printf("▶ mapScale: len=%d, coeff=%d, lut=%s\n", src_len, coeff, lut ? "ON" : "OFF");

    // Основной цикл: сначала коррекция, потом яркость
    for (uint16_t i = 0; i < src_len; i++) {
        uint8_t v = lut ? lut[src[i]] : src[i];
        
        // Округляем результат деления вместо отбрасывания дробной части
        uint16_t res = (v * coeff + 128) >> 8;
        
        // Мягкий минимум: если источник был >0, результат минимум 1
        dst[i] = (res == 0 && v > 0) ? 1 : res;
    }
}



// fillPattern: dest src -- (void)
// Заполняет dest значением или блоком src. Тип определяется по метаданным стека.
void fillPatternFunc(uint16_t addr) {
    uint16_t dst_addr, dst_len;
    if (!popAddrInfo(&dst_addr, &dst_len)) return;

    if (stackTop < 3) return; // Минимум: данные(≥1) + len + type

    // 1. Читаем тип и длину шаблона ПРЯМО со стека
    uint8_t src_type = stack[stackTop - 1];
    uint8_t src_len  = stack[stackTop - 2];

    uint8_t tmp_buf[8];        // Буфер для скаляров (u8/u16/u24/u32/float...)
    uint16_t block_len = 0;
    const uint8_t* src_ptr = nullptr;

    // 2. Ветка по типу
    if (src_type == TYPE_ADDRINFO) {  // ← Замени на свой константный тип ссылки
        uint16_t tmp_addr;
        if (!popAddrInfo(&tmp_addr, &block_len)) return;
        src_ptr = &dataPool[tmp_addr];
    } else {
        // Скаляр: копируем байты в буфер и снимаем со стека
        if (src_len > sizeof(tmp_buf)) { stackTop -= (src_len + 2); return; }
        memcpy(tmp_buf, &stack[stackTop - 2 - src_len], src_len);
        stackTop -= (src_len + 2);  // Атомарно снимаем скаляр
        src_ptr = tmp_buf;
        block_len = src_len;
    }

    // 3. Валидация
    if (block_len == 0 || block_len > dst_len) {
        outputStream->println("⚠️ fillPattern: шаблон больше буфера или пустой");
        return;
    }
    if (src_type == TYPE_ADDRINFO) {
        uint16_t src_addr = src_ptr - dataPool;
        if (src_addr + block_len > DATA_POOL_SIZE) {
            outputStream->println("⚠️ fillPattern: массив за пределами dataPool");
            return;
        }
    }

    // 4. Заполнение (универсальный цикл)
    uint16_t full = dst_len / block_len;
    uint16_t rem  = dst_len % block_len;

    for (uint16_t i = 0; i < full; i++) {
        memcpy(&dataPool[dst_addr + i * block_len], src_ptr, block_len);
    }
    if (rem > 0) {
        memcpy(&dataPool[dst_addr + full * block_len], src_ptr, rem);
    }
}

bool writeArrayElement(uint16_t poolRef, uint16_t elemCount, uint8_t elemType, int32_t index, const uint8_t* valueData, uint8_t valueType) {
  if (index < 0 || index >= elemCount) {
      return false;
  }

  uint8_t elemSize = 0;
  if (elemType == TYPE_UINT8 || elemType == TYPE_INT8) elemSize = 1;
  else if (elemType == TYPE_UINT16 || elemType == TYPE_INT16) elemSize = 2;
  else if (elemType == TYPE_INT) elemSize = 4;
  else {
       return false;
  }

  uint32_t dataStart = poolRef + 4;
  uint32_t elemOffset = (uint32_t)index * elemSize;
  uint32_t writeAddr = dataStart + elemOffset;

  if (writeAddr + elemSize > DATA_POOL_SIZE) {
      return false;
  }

  uint8_t srcData[4] = {0};
  bool canAssign = false;

  if (elemType == TYPE_UINT8) {
    if (valueType == TYPE_UINT8 && 1) { srcData[0] = valueData[0]; canAssign = true; }
    else if (valueType == TYPE_INT) { int32_t v; memcpy(&v, valueData, 4); srcData[0] = (v < 0) ? 0 : (v > 255 ? 255 : (uint8_t)v); canAssign = true; }
    else if (valueType == TYPE_INT8) { int8_t v = (int8_t)valueData[0]; srcData[0] = (v < 0) ? 0 : (uint8_t)v; canAssign = true; }
    else if (valueType == TYPE_UINT16) { uint16_t v; memcpy(&v, valueData, 2); srcData[0] = (v > 255) ? 255 : (uint8_t)v; canAssign = true; }
    else if (valueType == TYPE_INT16) { int16_t v; memcpy(&v, valueData, 2); srcData[0] = (v < 0) ? 0 : (v > 255 ? 255 : (uint8_t)v); canAssign = true; }
  }
  else if (elemType == TYPE_INT8) {
    if (valueType == TYPE_INT8) { srcData[0] = valueData[0]; canAssign = true; }
    else if (valueType == TYPE_INT) { int32_t v; memcpy(&v, valueData, 4); srcData[0] = (v < -128) ? -128 : (v > 127 ? 127 : (int8_t)v); canAssign = true; }
    else if (valueType == TYPE_UINT8) { srcData[0] = (int8_t)valueData[0]; canAssign = true; }
    else if (valueType == TYPE_UINT16) { uint16_t v; memcpy(&v, valueData, 2); srcData[0] = (v > 127) ? 127 : (int8_t)v; canAssign = true; }
    else if (valueType == TYPE_INT16) { int16_t v; memcpy(&v, valueData, 2); srcData[0] = (v < -128) ? -128 : (v > 127 ? 127 : (int8_t)v); canAssign = true; }
  }
  else if (elemType == TYPE_UINT16) {
    if (valueType == TYPE_UINT16) { memcpy(srcData, valueData, 2); canAssign = true; }
    else if (valueType == TYPE_INT) { int32_t v; memcpy(&v, valueData, 4); uint16_t clamped = (v < 0) ? 0 : (v > 65535 ? 65535 : (uint16_t)v); memcpy(srcData, &clamped, 2); canAssign = true; }
    else if (valueType == TYPE_UINT8) { uint16_t v = valueData[0]; memcpy(srcData, &v, 2); canAssign = true; }
    else if (valueType == TYPE_INT8) { int8_t v = (int8_t)valueData[0]; uint16_t u = (v < 0) ? 0 : (uint16_t)v; memcpy(srcData, &u, 2); canAssign = true; }
    else if (valueType == TYPE_INT16) { int16_t v; memcpy(&v, valueData, 2); uint16_t clamped = (v < 0) ? 0 : (uint16_t)v; memcpy(srcData, &clamped, 2); canAssign = true; }
  }
  else if (elemType == TYPE_INT16) {
    if (valueType == TYPE_INT16) { memcpy(srcData, valueData, 2); canAssign = true; }
    else if (valueType == TYPE_INT) { int32_t v; memcpy(&v, valueData, 4); int16_t clamped = (v < -32768) ? -32768 : (v > 32767 ? 32767 : (int16_t)v); memcpy(srcData, &clamped, 2); canAssign = true; }
    else if (valueType == TYPE_UINT8) { int16_t v = valueData[0]; memcpy(srcData, &v, 2); canAssign = true; }
    else if (valueType == TYPE_INT8) { int16_t v = (int8_t)valueData[0]; memcpy(srcData, &v, 2); canAssign = true; }
    else if (valueType == TYPE_UINT16) { uint16_t v; memcpy(&v, valueData, 2); int16_t clamped = (v > 32767) ? 32767 : (int16_t)v; memcpy(srcData, &clamped, 2); canAssign = true; }
  }
  else if (elemType == TYPE_INT) {
    if (valueType == TYPE_INT) { memcpy(srcData, valueData, 4); canAssign = true; }
    else if (valueType == TYPE_UINT8) { int32_t v = valueData[0]; memcpy(srcData, &v, 4); canAssign = true; }
    else if (valueType == TYPE_INT8) { int32_t v = (int8_t)valueData[0]; memcpy(srcData, &v, 4); canAssign = true; }
    else if (valueType == TYPE_UINT16) { int32_t v; memcpy(&v, valueData, 2); memcpy(srcData, &v, 4); canAssign = true; }
    else if (valueType == TYPE_INT16) { int32_t v; memcpy(&v, valueData, 2); memcpy(srcData, &v, 4); canAssign = true; }
  }

  if (canAssign) {
    memcpy(&dataPool[writeAddr], srcData, elemSize);
    return true;
  }
  return false;
}


void readArrayElementByAddr(uint16_t addr, int32_t index) {
  uint8_t elemType;
  uint16_t elemCount;
  uint16_t poolRef;

  if (!readArrayHeader(addr, &elemType, &elemCount, &poolRef)) {
      pushZeroForType(TYPE_INT);
      return;
  }

  if (index < 0 || index >= elemCount) {
      pushZeroForType(elemType);
      return;
  }

  readArrayElement(poolRef, elemCount, elemType, index);
}



void writeArrayElementByAddr(uint16_t addr, int32_t index) {
  uint8_t elemType;
  uint16_t elemCount;
  uint16_t poolRef;

  if (!readArrayHeader(addr, &elemType, &elemCount, &poolRef)) {
      uint8_t valType, valLen;
      const uint8_t* valData;
      if (peekStackTop(&valType, &valLen, &valData)) {
          dropTop(0);
      }
      return;
  }

  if (index < 0 || index >= elemCount) {
      uint8_t valType, valLen;
      const uint8_t* valData;
      if (peekStackTop(&valType, &valLen, &valData)) {
          dropTop(0);
      }
      return;
  }

  uint8_t valueType, valueLen;
  const uint8_t* valueData;
  if (!peekStackTop(&valueType, &valueLen, &valueData)) {
      return;
  }

  bool success = writeArrayElement(poolRef, elemCount, elemType, index, valueData, valueType);

  dropTop(0);
}




bool readArrayHeader(uint16_t addr, uint8_t* elemType, uint16_t* elemCount, uint16_t* poolRef) {
  uint8_t nameLen = dictionary[addr + 2];
  uint8_t storage = dictionary[addr + 3 + nameLen];
  uint8_t storageType = storage & 0x7F;

  if (storageType != STORAGE_POOLED && storageType != STORAGE_CONST) {
    return false;
  }

  uint16_t localPoolRef =
    dictionary[addr + 3 + nameLen + 2 + 0] |
    (dictionary[addr + 3 + nameLen + 2 + 1] << 8);

  if (localPoolRef >= DATA_POOL_SIZE) {
      return false;
  }

  uint8_t type = dataPool[localPoolRef];
  if (type != TYPE_ARRAY) {
      return false;
  }

  *elemType = dataPool[localPoolRef + 3];

  uint16_t dataLen = dataPool[localPoolRef + 1] | (dataPool[localPoolRef + 2] << 8);

  uint8_t elemSize = 1;
  if (*elemType == TYPE_UINT16 || *elemType == TYPE_INT16) elemSize = 2;
  else if (*elemType == TYPE_INT) elemSize = 4;
  else if (*elemType != TYPE_UINT8 && *elemType != TYPE_INT8) {
       return false;
  }

  if (dataLen % elemSize != 0) {
      return false;
  }
  *elemCount = dataLen / elemSize;

  *poolRef = localPoolRef;

  return true;
}




void readArrayElement(uint16_t poolRef, uint16_t elemCount, uint8_t elemType, int32_t index) {
  if (index < 0 || index >= elemCount) {
    pushZeroForType(elemType);
    return;
  }

  uint8_t elemSize = 0;
  if (elemType == TYPE_UINT8 || elemType == TYPE_INT8) elemSize = 1;
  else if (elemType == TYPE_UINT16 || elemType == TYPE_INT16) elemSize = 2;
  else if (elemType == TYPE_INT) elemSize = 4;
  else {
       pushZeroForType(elemType);
       return;
  }

  uint32_t dataStart = poolRef + 4;
  uint32_t elemOffset = (uint32_t)index * elemSize;
  uint32_t readAddr = dataStart + elemOffset;

  if (readAddr + elemSize > DATA_POOL_SIZE) {
    pushZeroForType(elemType);
    return;
  }

  const uint8_t* data = &dataPool[readAddr];

  if (elemType == TYPE_UINT8) {
    pushUInt8(data[0]);
  } else if (elemType == TYPE_INT8) {
    pushInt8((int8_t)data[0]);
  } else if (elemType == TYPE_UINT16) {
    uint16_t v; memcpy(&v, data, 2); pushUInt16(v);
  } else if (elemType == TYPE_INT16) {
    int16_t v; memcpy(&v, data, 2); pushInt16(v);
  } else if (elemType == TYPE_INT) {
    int32_t v; memcpy(&v, data, 4); pushInt(v);
  } else {
       pushZeroForType(elemType);
  }
}



void createArrayAndAssign(uint16_t addr, uint8_t elemType, uint16_t elemCount) {
  uint8_t elemSize = 1;
  if (elemType == TYPE_UINT16 || elemType == TYPE_INT16) elemSize = 2;
  else if (elemType == TYPE_INT) elemSize = 4;
  else if (elemType != TYPE_UINT8 && elemType != TYPE_INT8) {
       return;
  }

  uint32_t actualDataSize = (uint32_t)elemCount * elemSize;
  if (elemCount != 0 && actualDataSize / elemCount != elemSize) {
      return;
  }

  uint16_t dataLen = (uint16_t)actualDataSize;
  if (actualDataSize != dataLen) {
      return;
  }

  uint32_t totalSize32 = 1 /* TYPE_ARRAY */ + 2 /* dataLen_L/H */ + 1 /* elemType */ + actualDataSize;
  if (totalSize32 > DATA_POOL_SIZE) {
      return;
  }

  uint16_t totalSize = (uint16_t)totalSize32;

  if (dataPoolPtr + totalSize > DATA_POOL_SIZE) {
    return;
  }

  uint16_t newOffset = dataPoolPtr;
  dataPool[dataPoolPtr++] = TYPE_ARRAY;
  dataPool[dataPoolPtr++] = (uint8_t)(dataLen & 0xFF);
  dataPool[dataPoolPtr++] = (uint8_t)(dataLen >> 8);
  dataPool[dataPoolPtr++] = elemType;

  if (actualDataSize > 0) {
    memset(&dataPool[dataPoolPtr], 0, actualDataSize);
    dataPoolPtr += actualDataSize;
  }

  uint8_t nameLen = dictionary[addr + 2];
  dictionary[addr + 3 + nameLen + 2 + 0] = (newOffset >> 0) & 0xFF;
  dictionary[addr + 3 + nameLen + 2 + 1] = (newOffset >> 8) & 0xFF;
  dictionary[addr + 3 + nameLen + 2 + 2] = 0;
  dictionary[addr + 3 + nameLen + 2 + 3] = 0;
}




void handleAssignment(uint16_t addr) {
  // Запрет присваивания временному слову (литералам)
  if (addr == ADDR_TMP_LIT) {
    // Просто убираем '=' и значение, ничего не делая
    dropTop(0); // убираем '='
    uint8_t Type, Len;
    const uint8_t* Data;
    if (peekStackTop(&Type, &Len, &Data)) {
      dropTop(0); // убираем значение
    }
    return;
  }

  dropTop(0); // убираем '='
  uint8_t Type, Len;
  const uint8_t* Data;
  if (!peekStackTop(&Type, &Len, &Data)) {
    return;
  }

  uint8_t nameLen = dictionary[addr + 2];
  uint8_t storage = dictionary[addr + 3 + nameLen];
  uint8_t storageType = storage & 0x7F;

  // Только переменные (var, const) могут принимать присваивание
  if (storageType != STORAGE_POOLED && storageType != STORAGE_CONST) {
    // Убираем значение и выходим (игнорируем присваивание)
    dropTop(0);
    return;
  }

  // Защита констант
  if (storageType == STORAGE_CONST) {
    uint32_t poolRef =
      dictionary[addr + 3 + nameLen + 2 + 0] |
      (dictionary[addr + 3 + nameLen + 2 + 1] << 8) |
      (dictionary[addr + 3 + nameLen + 2 + 2] << 16) |
      (dictionary[addr + 3 + nameLen + 2 + 3] << 24);
    if (poolRef != 0xFFFFFFFF) {
      dropTop(0);
      return;
    }
  }
 // === ОСНОВНОЕ ИЗМЕНЕНИЕ: TYPE_NAME → TYPE_STRING ===
  if (Type == TYPE_NAME) {
    // Преобразуем имя в строку и сохраняем как TYPE_STRING
    if (Len <= 255) {
      storeValueToVariable(addr, Data, Len, TYPE_STRING);
    }
    dropTop(0);
    return;
  }
  // Создание массива
  if (Type == TYPE_ARRAY && Len == 3) {
    createArrayAndAssign(addr, Data[0], Data[1] | (Data[2] << 8));
    dropTop(0);
    return;
  }

  // Обычное присваивание
  storeValueToVariable(addr, Data, Len, Type);
  dropTop(0);
}



void handleArrayAccess(uint16_t addr) {
  dropTop(0); // убираем '['
  if (popMarkerIf(']')) {
    if (!popMarkerIf('=')) {
      return;
    }

    uint8_t elemType;
    uint16_t elemCount, poolRef;

    if (!readArrayHeader(addr, &elemType, &elemCount, &poolRef)) {
      while (stackTop >= 2) {
        uint8_t t = stack[stackTop - 1], l = stack[stackTop - 2];
        if (l > stackTop - 2) break;
        dropTop(0);
      }
      return;
    }

    size_t valuesCount = 0;
    size_t tempTop = stackTop;
    while (tempTop >= 2) {
      uint8_t l = stack[tempTop - 2];
      if (l > tempTop - 2) break;
      valuesCount++;
      tempTop -= 2 + l;
    }

    if (valuesCount != elemCount) {
      while (stackTop >= 2) {
        uint8_t t = stack[stackTop - 1], l = stack[stackTop - 2];
        if (l > stackTop - 2) break;
        dropTop(0);
      }
      return;
    }

    for (uint16_t i = 0; i < elemCount; i++) {
      uint8_t valType, valLen;
      const uint8_t* valData;
      if (!peekStackTop(&valType, &valLen, &valData)) break;
      dropTop(0);
      writeArrayElement(poolRef, elemCount, elemType, i, valData, valType);
    }
    return;
  }

  int32_t index;
  if (!popInt32FromAny(&index)) {
    return;
  }

  if (!popMarkerIf(']')) {
    return;
  }

  if (!popMarkerIf('=')) {
    readArrayElementByAddr(addr, index);
  } else {
    writeArrayElementByAddr(addr, index);
  }
}


// --- НАЧАЛО: readVariableAsValue (очищенная) ---
void readVariableAsValue(uint16_t addr) {
  uint8_t nameLen = dictionary[addr + 2];
  uint8_t storage = dictionary[addr + 3 + nameLen];
  uint8_t storageType = storage & 0x7F;

  // Обработка TYPE_NAME-констант (poolRef = 0xFFFFFFFE)
  if (storageType == STORAGE_CONST) {
    uint32_t poolRef =
      dictionary[addr + 3 + nameLen + 2 + 0] |
      (dictionary[addr + 3 + nameLen + 2 + 1] << 8) |
      (dictionary[addr + 3 + nameLen + 2 + 2] << 16) |
      (dictionary[addr + 3 + nameLen + 2 + 3] << 24);

    if (poolRef == 0xFFFFFFFE) {
      const char* name = (const char*)&dictionary[addr + 3];
      pushValue((const uint8_t*)name, nameLen, TYPE_NAME);
      return;
    }
  }

  if (storageType == STORAGE_CONT) {
    const uint8_t* valData = &dictionary[addr + 3 + nameLen + 2];
    pushValue(valData, 4, TYPE_INT);
  }
  else if (storageType == STORAGE_CONST || storageType == STORAGE_POOLED) {
    uint8_t varType, varLen;
    const uint8_t* varData;
    if (readVariableValue(addr, &varType, &varLen, &varData)) {
      if (varType == TYPE_ARRAY) {
          uint32_t poolRef =
            dictionary[addr + 3 + nameLen + 2 + 0] |
            (dictionary[addr + 3 + nameLen + 2 + 1] << 8) |
            (dictionary[addr + 3 + nameLen + 2 + 2] << 16) |
            (dictionary[addr + 3 + nameLen + 2 + 3] << 24);

          if (poolRef == 0xFFFFFFFF || poolRef >= DATA_POOL_SIZE) {
              uint8_t dummy[5] = {0, 0, 0, 0, 0};
              pushValue(dummy, 5, TYPE_ADDRINFO);
              return;
          }

          uint16_t address = poolRef + 4;
          uint16_t dataLen = dataPool[poolRef + 1] | (dataPool[poolRef + 2] << 8);
          uint8_t elemType = dataPool[poolRef + 3];

          uint8_t addrInfoData[5];
          addrInfoData[0] = (uint8_t)(address & 0xFF);
          addrInfoData[1] = (uint8_t)(address >> 8);
          addrInfoData[2] = (uint8_t)(dataLen & 0xFF);
          addrInfoData[3] = (uint8_t)(dataLen >> 8);
          addrInfoData[4] = elemType;

          pushValue(addrInfoData, 5, TYPE_ADDRINFO);
      } else {
        pushValue(varData, varLen, varType);
      }
    }
  }
}
// --- КОНЕЦ: readVariableAsValue (очищенная) ---
