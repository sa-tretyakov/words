
// --- НАЧАЛО: writeArrayElement для нового формата ---
bool writeArrayElement(uint16_t poolRef, uint16_t elemCount, uint8_t elemType, int32_t index, const uint8_t* valueData, uint8_t valueType) {
  Serial.printf("DBG writeArrayElement: poolRef=0x%X, index=%d, elemType=%d, valueType=%d\n", poolRef, index, elemType, valueType); // Отладка

  // Проверка индекса (повторная, на всякий случай)
  if (index < 0 || index >= elemCount) {
      Serial.println("DBG writeArrayElement: index out of bounds");
      return false;
  }

  // Определяем размер элемента
  uint8_t elemSize = 0;
  if (elemType == TYPE_UINT8 || elemType == TYPE_INT8) elemSize = 1;
  else if (elemType == TYPE_UINT16 || elemType == TYPE_INT16) elemSize = 2;
  else if (elemType == TYPE_INT) elemSize = 4;
  else {
       Serial.printf("DBG writeArrayElement: unsupported elemType %d\n", elemType);
       return false;
  }

  // Вычисляем физический адрес для записи
  uint32_t dataStart = poolRef + 4; // Начало actual_array_data
  uint32_t elemOffset = (uint32_t)index * elemSize;
  uint32_t writeAddr = dataStart + elemOffset;

  // Проверка границ (физическая)
  if (writeAddr + elemSize > DATA_POOL_SIZE) {
      Serial.println("DBG writeArrayElement: write would exceed DATA_POOL_SIZE");
      return false;
  }

  // Преобразуем значение в целевой тип
  uint8_t srcData[4] = {0};
  bool canAssign = false;

  // Логика преобразования (та же, что и раньше, но теперь мы записываем в writeAddr)
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
    Serial.printf("DBG writeArrayElement: Writing to dataPool[0x%X]: ", writeAddr); // Отладка
    for (int i = 0; i < elemSize; i++) {
        Serial.printf("%02X ", srcData[i]);
    }
    Serial.println();

    memcpy(&dataPool[writeAddr], srcData, elemSize);
    Serial.printf("DBG writeArrayElement: Successfully wrote %d bytes to 0x%X\n", elemSize, writeAddr); // Отладка
    return true;
  }

  Serial.println("DBG writeArrayElement: FAILED to assign/write."); // Отладка
  return false;
}
// --- КОНЕЦ: writeArrayElement для нового формата ---



// Читает элемент массива и кладёт его на стек.

// --- НАЧАЛО: readArrayElementByAddr (для нового формата) ---
void readArrayElementByAddr(uint16_t addr, int32_t index) {
  Serial.printf("DBG readArrayElementByAddr: addr=0x%X, index=%d\n", addr, index); // Отладка

  uint8_t elemType;
  uint16_t elemCount;
  uint16_t poolRef;

  if (!readArrayHeader(addr, &elemType, &elemCount, &poolRef)) {
      Serial.println("DBG readArrayElementByAddr: readArrayHeader failed");
      pushZeroForType(TYPE_INT); // или elemType, но лучше заглушка
      return;
  }

  Serial.printf("DBG readArrayElementByAddr: got elemType=%d, elemCount=%d, poolRef=0x%X\n", elemType, elemCount, poolRef); // Отладка

  // Проверка индекса
  if (index < 0 || index >= elemCount) {
      Serial.println("DBG readArrayElementByAddr: index out of bounds");
      pushZeroForType(elemType);
      return;
  }

  // Вызываем readArrayElement
  // readArrayElement(poolRef, elemCount, elemType, index); // <-- Она должна быть обновлена
  // Она читает и кладёт значение на стек
  readArrayElement(poolRef, elemCount, elemType, index);
}
// --- КОНЕЦ: readArrayElementByAddr (для нового формата) ---


// --- НАЧАЛО: writeArrayElementByAddr ---
void writeArrayElementByAddr(uint16_t addr, int32_t index) {
  Serial.printf("DBG writeArrayElementByAddr: addr=0x%X, index=%d\n", addr, index); // Отладка

  uint8_t elemType;
  uint16_t elemCount;
  uint16_t poolRef;

  if (!readArrayHeader(addr, &elemType, &elemCount, &poolRef)) {
      Serial.println("DBG writeArrayElementByAddr: readArrayHeader failed");
      // Убираем значение со стека, если оно там осталось
      uint8_t valType, valLen;
      const uint8_t* valData;
      if (peekStackTop(&valType, &valLen, &valData)) {
          dropTop(0);
      }
      return;
  }

  Serial.printf("DBG writeArrayElementByAddr: got elemType=%d, elemCount=%d, poolRef=0x%X\n", elemType, elemCount, poolRef); // Отладка

  // Проверка индекса
  if (index < 0 || index >= elemCount) {
      Serial.printf("DBG writeArrayElementByAddr: index %d out of bounds [0, %d)\n", index, elemCount);
      // Убираем значение со стека
      uint8_t valType, valLen;
      const uint8_t* valData;
      if (peekStackTop(&valType, &valLen, &valData)) {
          dropTop(0);
      }
      return;
  }

  // Получаем значение для записи
  uint8_t valueType, valueLen;
  const uint8_t* valueData;
  if (!peekStackTop(&valueType, &valueLen, &valueData)) {
      Serial.println("DBG writeArrayElementByAddr: cannot peek value to write");
      return;
  }

  Serial.printf("DBG writeArrayElementByAddr: value to write: type=%d, len=%d\n", valueType, valueLen); // Отладка

  // Вызываем writeArrayElement
  bool success = writeArrayElement(poolRef, elemCount, elemType, index, valueData, valueType);

  if (success) {
      Serial.println("DBG writeArrayElementByAddr: writeArrayElement succeeded");
  } else {
      Serial.println("DBG writeArrayElementByAddr: writeArrayElement failed");
  }

  // Убираем значение со стека
  dropTop(0);
}
// --- КОНЕЦ: writeArrayElementByAddr ---

// --- НАЧАЛО: readArrayHeader для нового формата ---
bool readArrayHeader(uint16_t addr, uint8_t* elemType, uint16_t* elemCount, uint16_t* poolRef) {
  Serial.printf("DBG readArrayHeader: called for addr=0x%X\n", addr); // Отладка

  uint8_t nameLen = dictionary[addr + 2];
  uint8_t storage = dictionary[addr + 3 + nameLen];
  uint8_t storageType = storage & 0x7F;

  if (storageType != STORAGE_POOLED && storageType != STORAGE_CONST) {
    Serial.println("DBG readArrayHeader: not pooled/const");
    return false;
  }

  // Получаем poolRef из словаря
  uint16_t localPoolRef =
    dictionary[addr + 3 + nameLen + 2 + 0] |
    (dictionary[addr + 3 + nameLen + 2 + 1] << 8);

  if (localPoolRef >= DATA_POOL_SIZE) {
      Serial.println("DBG readArrayHeader: poolRef out of bounds");
      return false;
  }

  Serial.printf("DBG readArrayHeader: poolRef=0x%X\n", localPoolRef); // Отладка

  // Проверяем тип
  uint8_t type = dataPool[localPoolRef];
  if (type != TYPE_ARRAY) {
      Serial.printf("DBG readArrayHeader: not TYPE_ARRAY, got %d\n", type);
      return false;
  }

  // Читаем elemType из заголовка (4-й байт: poolRef + 3)
  *elemType = dataPool[localPoolRef + 3];

  // Читаем DATALEN из заголовка (2-й и 3-й байты: poolRef + 1, poolRef + 2)
  uint16_t dataLen = dataPool[localPoolRef + 1] | (dataPool[localPoolRef + 2] << 8);

  // Вычисляем elemCount
  uint8_t elemSize = 1;
  if (*elemType == TYPE_UINT16 || *elemType == TYPE_INT16) elemSize = 2;
  else if (*elemType == TYPE_INT) elemSize = 4;
  else if (*elemType != TYPE_UINT8 && *elemType != TYPE_INT8) {
       Serial.printf("DBG readArrayHeader: unsupported elemType %d\n", *elemType);
       return false;
  }

  if (dataLen % elemSize != 0) {
      Serial.printf("DBG readArrayHeader: dataLen %d not divisible by elemSize %d\n", dataLen, elemSize);
      return false;
  }
  *elemCount = dataLen / elemSize;

  // Возвращаем poolRef
  *poolRef = localPoolRef;

  Serial.printf("DBG readArrayHeader: OK: elemType=%d, elemCount=%d, poolRef=0x%X\n", *elemType, *elemCount, *poolRef); // Отладка
  return true;
}
// --- КОНЕЦ: readArrayHeader для нового формата ---


// --- НАЧАЛО: readArrayElement для нового формата ---
// ВНИМАНИЕ: Эта версия принимает poolRef, elemCount, elemType напрямую от readArrayHeader.
void readArrayElement(uint16_t poolRef, uint16_t elemCount, uint8_t elemType, int32_t index) {
  Serial.printf("DBG readArrayElement: poolRef=0x%X, index=%d, elemType=%d\n", poolRef, index, elemType); // Отладка

  // Проверка границ (логическая)
  if (index < 0 || index >= elemCount) {
    Serial.println("DBG readArrayElement: index out of bounds");
    pushZeroForType(elemType);
    return;
  }

  // Определяем размер элемента
  uint8_t elemSize = 0;
  if (elemType == TYPE_UINT8 || elemType == TYPE_INT8) elemSize = 1;
  else if (elemType == TYPE_UINT16 || elemType == TYPE_INT16) elemSize = 2;
  else if (elemType == TYPE_INT) elemSize = 4;
  else {
       Serial.printf("DBG readArrayElement: unsupported elemType %d\n", elemType);
       pushZeroForType(elemType);
       return;
  }

  // Вычисляем физический адрес для чтения
  uint32_t dataStart = poolRef + 4; // Начало actual_array_data
  uint32_t elemOffset = (uint32_t)index * elemSize;
  uint32_t readAddr = dataStart + elemOffset;

  // Проверка выхода за границы dataPool
  if (readAddr + elemSize > DATA_POOL_SIZE) {
    Serial.println("DBG readArrayElement: read would exceed DATA_POOL_SIZE");
    pushZeroForType(elemType);
    return;
  }

  const uint8_t* data = &dataPool[readAddr];

  Serial.printf("DBG readArrayElement: Reading from dataPool[0x%X]: ", readAddr); // Отладка
  for (int i = 0; i < elemSize; i++) {
      Serial.printf("%02X ", data[i]);
  }
  Serial.println();

  // Кладём значение на стек
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
       Serial.printf("DBG readArrayElement: ERROR: unsupported elemType in final check: %d\n", elemType);
       pushZeroForType(elemType);
  }
}
// --- КОНЕЦ: readArrayElement для нового формата ---


// --- ЕЩЁ РАЗ: createArrayAndAssign для создания массива ---
void createArrayAndAssign(uint16_t addr, uint8_t elemType, uint16_t elemCount) {
  // 1. Проверки
  uint8_t elemSize = 1;
  if (elemType == TYPE_UINT16 || elemType == TYPE_INT16) elemSize = 2;
  else if (elemType == TYPE_INT) elemSize = 4;
  else if (elemType != TYPE_UINT8 && elemType != TYPE_INT8) {
       Serial.println("⚠️ createArrayAndAssign: unsupported element type");
       return;
  }

  uint32_t actualDataSize = (uint32_t)elemCount * elemSize;
  if (elemCount != 0 && actualDataSize / elemCount != elemSize) {
      Serial.println("⚠️ createArrayAndAssign: array size calculation overflow");
      return;
  }

  uint16_t dataLen = (uint16_t)actualDataSize;
  if (actualDataSize != dataLen) { // Проверка на переполнение uint16_t
      Serial.println("⚠️ createArrayAndAssign: array data length exceeds 65535 bytes");
      return;
  }

  uint32_t totalSize32 = 1 /* TYPE_ARRAY */ + 2 /* dataLen_L/H */ + 1 /* elemType */ + actualDataSize;
  if (totalSize32 > DATA_POOL_SIZE) {
      Serial.println("⚠️ createArrayAndAssign: array total size too large for dataPool");
      return;
  }

  uint16_t totalSize = (uint16_t)totalSize32;

  if (dataPoolPtr + totalSize > DATA_POOL_SIZE) {
    Serial.println("⚠️ createArrayAndAssign: dataPool full");
    return;
  }

  // 2. Запись
  uint16_t newOffset = dataPoolPtr;
  dataPool[dataPoolPtr++] = TYPE_ARRAY;           // [0] блока
  dataPool[dataPoolPtr++] = (uint8_t)(dataLen & 0xFF);     // [1] блока - dataLen_L
  dataPool[dataPoolPtr++] = (uint8_t)(dataLen >> 8);       // [2] блока - dataLen_H
  dataPool[dataPoolPtr++] = elemType;             // [3] блока - elemType

  // Записываем actual_data (нули)
  if (actualDataSize > 0) {
    memset(&dataPool[dataPoolPtr], 0, actualDataSize);
    dataPoolPtr += actualDataSize;
  }

  // 3. Обновление poolRef в словаре
  uint8_t nameLen = dictionary[addr + 2];
  dictionary[addr + 3 + nameLen + 2 + 0] = (newOffset >> 0) & 0xFF;
  dictionary[addr + 3 + nameLen + 2 + 1] = (newOffset >> 8) & 0xFF;
  dictionary[addr + 3 + nameLen + 2 + 2] = 0;
  dictionary[addr + 3 + nameLen + 2 + 3] = 0;

  Serial.printf("Created array at poolRef 0x%X: type=%d, elemCount=%d, dataLen=%d, totalSize=%d, dataStart=0x%X\n", newOffset, elemType, elemCount, dataLen, totalSize, newOffset + 4);
}
// --- КОНЕЦ: createArrayAndAssign для создания массива ---




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


// --- НАЧАЛО: handleArrayAccess (обновлённая версия для новой writeArrayElement) ---
void handleArrayAccess(uint16_t addr) {
  dropTop(0); // убираем '['
  // Проверяем: если следующий маркер — ']' и потом '=', то это массовая загрузка
  if (popMarkerIf(']')) {
    if (!popMarkerIf('=')) {
      Serial.println("⚠️ []: ожидается =");
      return;
    }

    // ИСПРАВЛЕНО: elemType — uint8_t!
    uint8_t elemType;
    uint16_t elemCount, poolRef;

    if (!readArrayHeader(addr, &elemType, &elemCount, &poolRef)) {
      Serial.println("⚠️ []: не массив");
      // Очищаем стек
      while (stackTop >= 2) {
        uint8_t t = stack[stackTop - 1], l = stack[stackTop - 2];
        if (l > stackTop - 2) break;
        dropTop(0);
      }
      return;
    }

    // Считаем количество значений на стеке
    size_t valuesCount = 0;
    size_t tempTop = stackTop;
    while (tempTop >= 2) {
      uint8_t l = stack[tempTop - 2];
      if (l > tempTop - 2) break;
      valuesCount++;
      tempTop -= 2 + l;
    }

    if (valuesCount != elemCount) {
      Serial.printf("⚠️ []: ожидается %d значений, получено %u\n", elemCount, valuesCount);
      // Очищаем стек
      while (stackTop >= 2) {
        uint8_t t = stack[stackTop - 1], l = stack[stackTop - 2];
        if (l > stackTop - 2) break;
        dropTop(0);
      }
      return;
    }

    // --- ОБНОВЛЕНИЕ: вызов writeArrayElement с новой сигнатурой ---
    // Читаем значения в прямом порядке
    for (uint16_t i = 0; i < elemCount; i++) {
      uint8_t valType, valLen;
      const uint8_t* valData;
      if (!peekStackTop(&valType, &valLen, &valData)) break;
      dropTop(0);
      // Записываем в i-й элемент, передавая elemCount и elemType
      writeArrayElement(poolRef, elemCount, elemType, i, valData, valType); // <-- НОВАЯ СИГНАТУРА
    }
    // --- КОНЕЦ ОБНОВЛЕНИЯ ---
    return;
  }

  // === СТАРЫЙ КОД: доступ по индексу ===
  int32_t index;
  if (!popInt32FromAny(&index)) {
    return;
  }

  if (!popMarkerIf(']')) {
    return;
  }

  if (!popMarkerIf('=')) {
    // ЧТЕНИЕ
    readArrayElementByAddr(addr, index); // <-- Эта функция тоже нужна для чтения
  } else {
    // ЗАПИСЬ
    writeArrayElementByAddr(addr, index);
  }
}
// --- КОНЕЦ: handleArrayAccess (обновлённая версия для новой writeArrayElement) ---


// --- НАЧАЛО: readVariableAsValue, обновлённая для TYPE_ADDRINFO с elemType ---
void readVariableAsValue(uint16_t addr) {
  Serial.printf("DBG readVariableAsValue: called for addr=0x%X\n", addr); // Отладка

  uint8_t nameLen = dictionary[addr + 2];
  uint8_t storage = dictionary[addr + 3 + nameLen];
  uint8_t storageType = storage & 0x7F;

  if (storageType == STORAGE_CONT) {
    const uint8_t* valData = &dictionary[addr + 3 + nameLen + 2];
    pushValue(valData, 4, TYPE_INT);
  }
  else if (storageType == STORAGE_CONST || storageType == STORAGE_POOLED) { // STORAGE_POOLED для var
    uint8_t varType, varLen;
    const uint8_t* varData;
    if (readVariableValue(addr, &varType, &varLen, &varData)) {
      if (varType == TYPE_ARRAY) {
          // --- НОВОЕ: Вместо формирования заготовки, формируем TYPE_ADDRINFO ---
          // 1. Получаем poolRef из словаря
          uint32_t poolRef =
            dictionary[addr + 3 + nameLen + 2 + 0] |
            (dictionary[addr + 3 + nameLen + 2 + 1] << 8) |
            (dictionary[addr + 3 + nameLen + 2 + 2] << 16) |
            (dictionary[addr + 3 + nameLen + 2 + 3] << 24);

          if (poolRef == 0xFFFFFFFF || poolRef >= DATA_POOL_SIZE) {
              Serial.println("DBG readVariableAsValue: ERROR - Invalid poolRef for TYPE_ARRAY when creating ADDRINFO");
              // Положим какую-то заглушку
              uint8_t dummy[5] = {0, 0, 0, 0, 0};
              pushValue(dummy, 5, TYPE_ADDRINFO);
              return;
          }

          // 2. Вычисляем address (адрес начала actual_data)
          uint16_t address = poolRef + 4;

          // 3. Читаем DATALEN из заголовка в dataPool
          uint16_t dataLen = dataPool[poolRef + 1] | (dataPool[poolRef + 2] << 8);

          // 4. Читаем elemType из заголовка в dataPool
          uint8_t elemType = dataPool[poolRef + 3];

          // 5. Формируем 5-байтовое значение [address_L, address_H, size_L, size_H, elemType]
          uint8_t addrInfoData[5];
          addrInfoData[0] = (uint8_t)(address & 0xFF); // address_L
          addrInfoData[1] = (uint8_t)(address >> 8);   // address_H
          addrInfoData[2] = (uint8_t)(dataLen & 0xFF); // size_L
          addrInfoData[3] = (uint8_t)(dataLen >> 8);   // size_H
          addrInfoData[4] = elemType;                  // elemType

          // 6. Кладём на стек как TYPE_ADDRINFO
          pushValue(addrInfoData, 5, TYPE_ADDRINFO);
          Serial.printf("DBG readVariableAsValue: Pushed ADDRINFO: addr=0x%X, size=%d, elemType=%d\n", address, dataLen, elemType); // Отладка
          // --- КОНЕЦ НОВОГО ---
      } else {
        // Обработка других типов (INT, STRING, BOOL и т.д.)
        // varData указывает на начало данных, varLen - длина данных
        Serial.printf("DBG readVariableAsValue: Pushing non-array value, type=%d, len=%d\n", varType, varLen); // Отладка
        pushValue(varData, varLen, varType);
      }
    }
  }
}
// --- КОНЕЦ: readVariableAsValue, обновлённая для TYPE_ADDRINFO с elemType ---
