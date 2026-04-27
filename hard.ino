

bool valueToUint8(uint8_t type, uint8_t len, const uint8_t* data, uint8_t* out) {
  int32_t val;
  switch (type) {
    case TYPE_INT:
      if (len == 4) { memcpy(&val, data, 4); }
      else return false;
      break;
    case TYPE_INT8:
      if (len == 1) { val = (int8_t)data[0]; }
      else return false;
      break;
    case TYPE_UINT8:
      if (len == 1) { val = data[0]; }
      else return false;
      break;
    case TYPE_INT16:
      if (len == 2) { val = (int16_t)(data[0] | (data[1] << 8)); }
      else return false;
      break;
    case TYPE_UINT16:
      if (len == 2) { val = (uint16_t)(data[0] | (data[1] << 8)); }
      else return false;
      break;
    default:
      return false;
  }
  if (val < 0 || val > 255) return false;
  *out = (uint8_t)val;
  return true;
}

void pushMarkerFunc(uint16_t addr) {
  if (addr + 2 >= DICT_SIZE) return;
  uint8_t nameLen = dictionary[addr + 2];
  if (nameLen == 0 || nameLen > 32 || addr + 3 + nameLen > DICT_SIZE) return;

  const char* name = (const char*)&dictionary[addr + 3];
  if (isStackOverflow(nameLen + 2)) {
    handleStackOverflow();
    return;
  }

  memcpy(&stack[stackTop], name, nameLen);
  stackTop += nameLen;
  stack[stackTop++] = nameLen;
  stack[stackTop++] = TYPE_MARKER;
}

void dropTop(uint16_t addr) {
  if (stackTop < 2) return;
  uint8_t len = stack[stackTop - 2];
  uint8_t type = stack[stackTop - 1];
  if (len > stackTop - 2) return;
  stackTop = stackTop - 2 - len;
}

// dup: ( a -- a a ) дублирует вершину стека
void dupFunc(uint16_t addr) {
    // 1. Читаем метаданные с вершины стека
    uint8_t type = stack[stackTop - 1];  // тип
    uint8_t len  = stack[stackTop - 2];  // длина данных
    
    // 2. Полная длина элемента: данные + 2 байта метаданных
    uint8_t total = len + 2;
    
    // 3. Проверка переполнения
    if (stackTop + total > STACK_SIZE) return;
    
    // 4. Копируем весь блок (данные + метаданные)
    memcpy(&stack[stackTop], &stack[stackTop - total], total);
    
    // 5. Сдвигаем указатель стека
    stackTop += total;
}

// dup2: ( a b -- a b a b ) дублирует два верхних элемента
void dup2Func(uint16_t addr) {
    if (stackTop < 3) return;
    
    // 1. Длина верхнего элемента (Item1)
    uint8_t len1 = stack[stackTop - 2];
    uint8_t total1 = len1 + 2;
    
    if (stackTop < total1 + 3) return; // Второго элемента нет
    
    // 2. Длина второго элемента (Item2)
    uint8_t len2 = stack[stackTop - total1 - 2];
    uint8_t total2 = len2 + 2;
    
    // 3. Проверка переполнения
    if (stackTop + total1 + total2 > STACK_SIZE) return;
    
    // 4. Копируем Item2, затем Item1 на вершину
    // dest > src → memcpy безопасен
    memcpy(&stack[stackTop], &stack[stackTop - total1 - total2], total2);
    memcpy(&stack[stackTop + total2], &stack[stackTop - total1], total1);
    
    // 5. Обновляем указатель
    stackTop += total1 + total2;
}


// Вспомогательная: реверс байтов в диапазоне [start, end)
static void _reverse(uint8_t* start, uint8_t* end) {
    while (start < --end) {
        uint8_t t = *start; *start++ = *end; *end = t;
    }
}

// swap: ( a b -- b a ) меняет местами два верхних элемента, in-place
void swapFunc(uint16_t addr) {
    if (stackTop < 6) return;  // минимум 2 элемента (3+3 байта)

    // 1. Размеры блоков
    uint8_t sizeB = stack[stackTop - 2] + 2;
    uint8_t sizeA = stack[stackTop - sizeB - 2] + 2;
    uint16_t addrA = stackTop - sizeA - sizeB;

    // 2. Вращение [A][B] → [B][A] тремя реверсами
    _reverse(&stack[addrA], &stack[addrA + sizeA]);           // реверс A
    _reverse(&stack[addrA + sizeA], &stack[stackTop]);        // реверс B
    _reverse(&stack[addrA], &stack[stackTop]);                // реверс всего блока
}





void dumpDataPool(uint16_t offset, uint16_t len) {
  if (offset + len > DATA_POOL_SIZE) {
    len = DATA_POOL_SIZE - offset;
  }
  outputStream->printf("dataPool[%u..%u]:\n", offset, offset + len - 1);
  for (uint16_t i = 0; i < len; i++) {
    if (i % 16 == 0) {
      if (i > 0) outputStream->println();
      outputStream->printf("%04X: ", offset + i);
    }
    outputStream->printf("%02X ", dataPool[offset + i]);
  }
  if (len % 16 != 0) outputStream->println();
  outputStream->println();
}


void nopFunc(uint16_t addr){
  
  }

void resetFunc(uint16_t addr){
  ESP.restart();
  }
  
void oopsFunc(uint16_t addr){
  stackTop = 0;
  }
 
