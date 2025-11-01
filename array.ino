bool writeArrayElement(uint16_t poolRef, int32_t index, const uint8_t* valueData, uint8_t valueType) {
  // Проверка границ poolRef
  if (poolRef >= DATA_POOL_SIZE) return false;

  // Читаем заголовок массива
  uint8_t type = dataPool[poolRef];
  if (type != TYPE_ARRAY) return false;

  if (poolRef + 1 >= DATA_POOL_SIZE) return false;
  uint8_t len = dataPool[poolRef + 1];

  if (poolRef + 5 > DATA_POOL_SIZE) return false;
  uint8_t elemType = dataPool[poolRef + 2];
  uint16_t elemCount = dataPool[poolRef + 3] | (dataPool[poolRef + 4] << 8);

  // Проверка индекса
  if (index < 0 || index >= elemCount) return false;

  // Определяем размер элемента
  uint8_t elemSize = 0;
  if (elemType == TYPE_UINT8 || elemType == TYPE_INT8) elemSize = 1;
  else if (elemType == TYPE_UINT16 || elemType == TYPE_INT16) elemSize = 2;
  else if (elemType == TYPE_INT) elemSize = 4;
  else return false;

  // Проверка длины блока
  if (len != 5 + elemCount * elemSize) return false;

  // Смещение данных
  uint16_t dataStart = poolRef + 5;
  uint16_t elemOffset = index * elemSize;

  if (dataStart + elemOffset + elemSize > DATA_POOL_SIZE) return false;

  // Преобразуем значение в целевой тип
  uint8_t srcData[4] = {0};
  bool canAssign = false;

  if (elemType == TYPE_UINT8) {
    if (valueType == TYPE_UINT8 && /*len*/ 1) { srcData[0] = valueData[0]; canAssign = true; }
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
    memcpy(&dataPool[dataStart + elemOffset], srcData, elemSize);
    return true;
  }

  return false;
}


// Читает элемент массива и кладёт его на стек.
void readArrayElementByAddr(uint16_t addr, int32_t index) {
  uint8_t elemType;
  uint16_t elemCount;
  uint16_t poolRef;
  if (!readArrayHeader(addr, &elemType, &elemCount, &poolRef)) {
    pushZeroForType(TYPE_INT); // или elemType, но его нет
    return;
  }

  if (index < 0 || index >= elemCount) {
    pushZeroForType(elemType);
    return;
  }

  // === Читаем ПОЛНЫЙ блок массива из dataPool ===
  if (poolRef >= DATA_POOL_SIZE) {
    pushZeroForType(elemType);
    return;
  }

  uint8_t type = dataPool[poolRef];
  if (type != TYPE_ARRAY) {
    pushZeroForType(elemType);
    return;
  }

  uint8_t len = dataPool[poolRef + 1];
  if (poolRef + 2 + len > DATA_POOL_SIZE) {
    pushZeroForType(elemType);
    return;
  }

  const uint8_t* fullArrayData = &dataPool[poolRef];

  // Передаём полный блок в readArrayElement
  readArrayElement(fullArrayData, len, elemType, index);
}

// Записывает значение из стека в массив по индексу.
// Возвращает true, если успешно (и убирает значение со стека).
// Записывает значение из стека в элемент массива по заданному индексу.
// Аргументы:
//   addr   — адрес слова-массива в словаре (например, "buf")
//   index  — индекс элемента для записи (например, 3)
// Возвращает:
//   true  — если запись успешна,
//   false — если ошибка (некорректный массив, индекс, нет значения на стеке).
bool writeArrayElementByAddr(uint16_t addr, int32_t index) {
  // Объявляем переменные для метаданных массива
  uint8_t elemType;   // тип элемента (u8, i16 и т.д.)
  uint16_t elemCount; // количество элементов в массиве
  uint16_t poolRef;   // адрес блока массива в dataPool
  // Пытаемся прочитать заголовок массива из переменной
  if (!readArrayHeader(addr, &elemType, &elemCount, &poolRef)) {
    // Если переменная не существует или не является массивом — выходим
    return false;
  }
  // === ПРОВЕРКА ГРАНИЦ ИНДЕКСА ===
  // Убеждаемся, что индекс находится в допустимом диапазоне [0, elemCount)
  if (index < 0 || index >= elemCount) {
    // Индекс вне границ — убираем значение со стека (чтобы не остался мусор)
    uint8_t valType, valLen;
    const uint8_t* valData;
    if (peekStackTop(&valType, &valLen, &valData)) {
      dropTop(0); // удаляем значение, даже если оно не нужно
    }
    Serial.println("Запись не возможна indrx error");
    return false; // запись невозможна
  }

  // === ЧТЕНИЕ ЗНАЧЕНИЯ ДЛЯ ЗАПИСИ ===
  // Смотрим, что лежит на верхушке стека (это и будет новое значение элемента)
  uint8_t valType, valLen;
  const uint8_t* valData;
  if (!peekStackTop(&valType, &valLen, &valData)) {
    // Стек пуст — некуда записывать
    Serial.println("Запись не возможна no data");
    return false;
  }

  // === ВЫПОЛНЕНИЕ ЗАПИСИ ===
  // Передаём: адрес блока массива, индекс, данные значения и его тип
  // Функция writeArrayElement сама преобразует тип и запишет в правильное место
  writeArrayElement(poolRef, index, valData, valType);
   //Serial.println("Запись!!!");
  // === ОЧИСТКА СТЕКА ===
  // Убираем значение со стека, потому что оно уже использовано для записи
  // Без этого — значение останется на стеке и вызовет побочные эффекты
  dropTop(0);

  // Запись успешна
  return true;
}
// Читает заголовок массива и возвращает true при успехе.
// Заполняет elemType, elemCount, poolRef.

/*
 * Читает метаданные массива (заголовок) из переменной в словаре.
 * 
 * Аргументы:
 *   addr       — адрес слова-переменной в словаре (например, "buf")
 *   elemType   — [выход] тип элемента массива (TYPE_UINT8, TYPE_INT и т.д.)
 *   elemCount  — [выход] количество элементов в массиве
 *   poolRef    — [выход] адрес блока массива в dataPool
 * 
 * Возвращает:
 *   true  — если переменная существует, является массивом и данные корректны,
 *   false — иначе (не массив, ошибка памяти и т.д.).
 */
bool readArrayHeader(uint16_t addr, uint8_t* elemType, uint16_t* elemCount, uint16_t* poolRef) {
  // 1. Читаем длину имени переменной из словаря
  //    Формат записи в словаре: [next][nameLen][name...][storage][context][poolRef][funcPtr]
  uint8_t nameLen = dictionary[addr + 2];

  // 2. Читаем байт storage (содержит тип хранения + флаг internal)
  uint8_t storage = dictionary[addr + 3 + nameLen];

  // 3. Извлекаем чистый тип хранения (младшие 7 бит)
  uint8_t storageType = storage & 0x7F;

  // 4. Проверяем: переменная должна быть либо var (POOLED), либо const (CONST)
  if (storageType != STORAGE_POOLED && storageType != STORAGE_CONST) {
    return false; // не переменная — не массив
  }
  // 5. Читаем значение переменной из dataPool через readVariableValue
  //    Эта функция возвращает указатель на блок данных в dataPool
  uint8_t varType, varLen;
  const uint8_t* varData;
  if (!readVariableValue(addr, &varType, &varLen, &varData)) {
    return false; // ошибка чтения (например, poolRef повреждён)
  }
  // 6. Проверяем: значение должно быть массивом и иметь минимум 5 байт
  //    Формат блока массива: [TYPE_ARRAY][len][elemType][count_L][count_H][данные...]
  //    → нужно как минимум 5 байт: type+len+elemType+count(2)
  if (varType != TYPE_ARRAY || varLen < 5) {
    return false; // не массив или повреждён
  }
  // 7. Извлекаем тип элемента (1-й байт в блоке: индекс 0)
  *elemType = varData[0];
  // 8. Извлекаем количество элементов (4-й и 5-й байты, little-endian)
  *elemCount = varData[1] | (varData[3] << 8);
  // 9. Читаем poolRef из записи слова в словаре
  //    poolRef — это 4-байтный указатель на блок массива в dataPool,
  //    но у нас он хранится только в младших 2 байтах (достаточно для 2KB)
  *poolRef = 
    dictionary[addr + 3 + nameLen + 2 + 0] |        // младший байт
    (dictionary[addr + 3 + nameLen + 2 + 1] << 8);  // старший байт

  // 10. Успешно прочитали все метаданные
  return true;
}

void readArrayElement(const uint8_t* fullArrayData, uint8_t arrayLen, uint8_t elemType, int32_t index) {
  // fullArrayData = [TYPE_ARRAY][len][elemType][count_L][count_H][данные...]
  uint8_t elemSize = 1;
  if (elemType == TYPE_UINT16 || elemType == TYPE_INT16) elemSize = 2;
  else if (elemType == TYPE_INT) elemSize = 4;

  uint16_t dataStart = 5; // заголовок = 5 байт
  uint16_t elemPos = dataStart + index * elemSize;

  if (elemPos + elemSize > arrayLen) {
    pushZeroForType(elemType);
    return;
  }

  const uint8_t* data = &fullArrayData[elemPos];

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
  }
}

void createArrayAndAssign(uint16_t addr, uint8_t elemType, uint16_t elemCount) {
  uint8_t elemSize = 1;
  if (elemType == TYPE_UINT16 || elemType == TYPE_INT16) elemSize = 2;
  else if (elemType == TYPE_INT) elemSize = 4;
  else if (elemType != TYPE_UINT8 && elemType != TYPE_INT8) return;

  if (elemCount == 0 || elemCount > 1024) return;

  uint32_t dataSize = (uint32_t)elemCount * elemSize;
  uint16_t totalSize = 5 + (uint16_t)dataSize; // 5 = TYPE_ARRAY + len + elemType + count(2)

  if (totalSize > 255 || dataPoolPtr + totalSize > DATA_POOL_SIZE) {
    return;
  }

  uint16_t newOffset = dataPoolPtr;
  dataPool[dataPoolPtr++] = TYPE_ARRAY;
  dataPool[dataPoolPtr++] = (uint8_t)totalSize;
  dataPool[dataPoolPtr++] = elemType;
  dataPool[dataPoolPtr++] = (uint8_t)(elemCount & 0xFF);
  dataPool[dataPoolPtr++] = (uint8_t)(elemCount >> 8);
  memset(&dataPool[dataPoolPtr], 0, dataSize);
  dataPoolPtr += dataSize;

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
  dropTop(0); // '['

  int32_t index;
  if (!popInt32FromAny(&index)) {
    // Индекс уже удалён, но был некорректным — выходим
    return;
  }

  if (!popMarkerIf(']')) {
    return;
  }

  if (!popMarkerIf('=')) {
    // ЧТЕНИЕ
    readArrayElementByAddr(addr, index);
  } else {
    // ЗАПИСЬ
    writeArrayElementByAddr(addr, index);
  }
}

void readVariableAsValue(uint16_t addr) {
  uint8_t nameLen = dictionary[addr + 2];
  uint8_t storage = dictionary[addr + 3 + nameLen];
  uint8_t storageType = storage & 0x7F;

  if (storageType == STORAGE_CONT) {
    const uint8_t* valData = &dictionary[addr + 3 + nameLen + 2];
    pushValue(valData, 4, TYPE_INT);
  }
  else if (storageType == STORAGE_CONST || storageType == STORAGE_POOLED) {
    uint8_t varType, varLen;
    const uint8_t* varData;
    if (readVariableValue(addr, &varType, &varLen, &varData)) {
      if (varType == TYPE_ARRAY) {
        if (varLen >= 3) { // ← было 4, теперь 3 (минимум: elemType + count_L + count_H)
          uint8_t stub[3] = {varData[0], varData[1], varData[2]}; // ← было [1][2][3], теперь [0][1][2]
          pushValue(stub, 3, TYPE_ARRAY);
        } else {
          uint8_t stub[3] = {TYPE_UINT8, 0, 0};
          pushValue(stub, 3, TYPE_ARRAY);
        }
      } else {
        pushValue(varData, varLen, varType);
      }
    }
  }
}
