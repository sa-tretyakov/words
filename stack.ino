// --- НАЧАЛО: printStackCompact, обновлённая для TYPE_ADDRINFO с elemType ---
void printStackCompact() {
  // --- Стек ---
  outputStream->print("context:");
  outputStream->print(currentContext);
  if (stackTop == 0) {
    outputStream->print(" []");
  } else {
    // (ваш существующий код печати стека — без изменений)
    const int MAX_ELEMENTS = 256;
    struct Element {
      char prefix;
      String repr;
    };
    Element elements[MAX_ELEMENTS];
    int count = 0;
    size_t tempTop = stackTop;

    while (tempTop >= 2 && count < MAX_ELEMENTS) {
      uint8_t len = stack[tempTop - 2];
      uint8_t type = stack[tempTop - 1];
      if (len > tempTop - 2) break;
      size_t dataStart = tempTop - 2 - len;
      char prefix = '?';
      String repr;

      switch (type) {
        case TYPE_INT: if (len == 4) { int32_t v; memcpy(&v, &stack[dataStart], 4); repr = String(v); prefix = 'I'; } break;
        case TYPE_FLOAT: if (len == 4) { float v; memcpy(&v, &stack[dataStart], 4); repr = String(v, 6); prefix = 'F'; } break;
        case TYPE_STRING: {
          repr = "";
          for (size_t i = 0; i < len; i++) {
            char c = stack[dataStart + i];
            repr += (c >= 32 && c <= 126) ? c : '?';
          }
          prefix = 'S';
          break;
        }
        case TYPE_BOOL: if (len == 1) { repr = stack[dataStart] ? "true" : "false"; prefix = 'B'; } break;
        case TYPE_INT8: { int8_t v = static_cast<int8_t>(stack[dataStart]); repr = String(v); prefix = '8'; } break;
        case TYPE_UINT8: { uint8_t v = stack[dataStart]; repr = String(v); prefix = 'U'; } break;
        case TYPE_INT16: { int16_t v; memcpy(&v, &stack[dataStart], 2); repr = String(v); prefix = 'W'; } break;
        case TYPE_UINT16: { uint16_t v; memcpy(&v, &stack[dataStart], 2); repr = String(v); prefix = 'w'; } break;
        case TYPE_NAME: {
          repr = "";
          for (size_t i = 0; i < len; i++) {
            char c = stack[dataStart + i];
            repr += (c >= 32 && c <= 126) ? c : '?';
          }
          prefix = 'N';
          break;
        }
        case TYPE_ARRAY: {
          if (len >= 3) {
            uint8_t elemType = stack[dataStart];
            uint16_t count = stack[dataStart + 1] | (stack[dataStart + 2] << 8);
            String typeStr = (elemType == TYPE_UINT8) ? "u8" : (elemType == TYPE_INT8) ? "i8" : (elemType == TYPE_UINT16) ? "u16" : (elemType == TYPE_INT16) ? "i16" : (elemType == TYPE_INT) ? "i32" : "?";
            repr = typeStr + "[" + String(count) + "]";
            prefix = 'A';
          } else { repr = "?"; prefix = 'A'; }
          break;
        }
        // --- ИЗМЕНЁННЫЙ СЛУЧАЙ: TYPE_ADDRINFO ---
        case TYPE_ADDRINFO: { // Предположим, TYPE_ADDRINFO = 12
          if (len == 5) { // ADDRINFO теперь должен занимать 5 байт
            uint16_t addr = stack[dataStart] | (stack[dataStart + 1] << 8);
            uint16_t size = stack[dataStart + 2] | (stack[dataStart + 3] << 8);
            uint8_t elemType = stack[dataStart + 4];
            // Преобразуем elemType в строку (можно сделать функцию typeNameToString для универсальности)
            String elemTypeStr = (elemType == TYPE_UINT8) ? "u8" : (elemType == TYPE_INT8) ? "i8" :
                                 (elemType == TYPE_UINT16) ? "u16" : (elemType == TYPE_INT16) ? "i16" :
                                 (elemType == TYPE_INT) ? "i32" : "unk";
            repr = "0x" + String(addr, HEX) + "," + String(size) + "," + elemTypeStr;
            prefix = 'X'; // Префикс остаётся 'X'
          } else {
            repr = "?[bad_len:" + String(len) + "]";
            prefix = 'X';
          }
          break;
        }
        // --- КОНЕЦ ИЗМЕНЁННОГО СЛУЧАЯ ---
        case TYPE_MARKER: {
          repr = "";
          for (size_t i = 0; i < len; i++) {
            char c = stack[dataStart + i];
            repr += (c >= 32 && c <= 126) ? c : '?';
          }
          prefix = 'M';
          break;
        }
      }

      if (prefix != '?' || type == TYPE_MARKER) { // TYPE_MARKER может иметь len=1, что ломает цикл, если он '?'.
        elements[count].prefix = prefix;
        elements[count].repr = repr;
        count++;
      } else break; // Прерываем цикл, если тип неизвестен и не MARKER
      tempTop = dataStart;
    }

    outputStream->print(" [");
    for (int i = count - 1; i >= 0; i--) {
      if (i < count - 1) outputStream->print(' ');
      outputStream->print(elements[i].prefix);
      outputStream->print('(');
      outputStream->print(elements[i].repr);
      outputStream->print(')');
    }
    outputStream->print(']');
  }

  // --- Состояние системы (seetime + задачи) ---
  outputStream->println(); // новая строка для состояния
  outputStream->print("⏱️ seetime: ");
  outputStream->print(seetimeMode ? "ON" : "OFF");
  outputStream->print(" | +task: ");

  // Собираем список активных задач
  bool first = true;
  for (int i = 0; i < MAX_TASKS; i++) {
    if (tasks[i].active) {
      if (!first) outputStream->print(", ");
      first = false;

      uint16_t wordAddr = tasks[i].wordAddr;
      if (wordAddr + 2 < DICT_SIZE) {
        uint8_t nameLen = dictionary[wordAddr + 2];
        if (nameLen > 0 && wordAddr + 3 + nameLen <= DICT_SIZE) {
          for (uint8_t j = 0; j < nameLen; j++) {
            char c = dictionary[wordAddr + 3 + j];
            if (c >= 32 && c <= 126) outputStream->print(c);
            else outputStream->print('?');
          }
          outputStream->print('(');
          outputStream->print(tasks[i].interval);
          outputStream->print("ms)");
        }
      }
    }
  }
  if (first) outputStream->print("[]"); // нет задач

  outputStream->println();
  outputStream->println("ok>");
}
// --- КОНЕЦ: printStackCompact, обновлённая для TYPE_ADDRINFO с elemType ---


void contFunc(uint16_t addr) {
  // Читаем значение (номер контекста) из записи в словаре
  uint8_t nameLen = dictionary[addr + 2];
  // Значение начинается после: [next][nameLen][name][storage][context] → смещение = 3 + nameLen + 2
  uint32_t value =
    dictionary[addr + 3 + nameLen + 2 + 0] |
    (dictionary[addr + 3 + nameLen + 2 + 1] << 8) |
    (dictionary[addr + 3 + nameLen + 2 + 2] << 16) |
    (dictionary[addr + 3 + nameLen + 2 + 3] << 24);
  
  // Устанавливаем текущий контекст
  currentContext = (uint8_t)value;
}


void seetimeWord(uint16_t addr) {
  uint8_t type, len;
  const uint8_t* data;
  
  // Берём значение с вершины стека (оно уже там, потому что строка: "seetime true")
  if (!peekStackTop(&type, &len, &data)) {
    outputStream->println("⚠️ seetime: ожидается true или false после команды");
    return;
  }

  // Преобразуем ЛЮБОЙ тип к логическому значению (уже есть в коде!)
  bool enable = valueToBool(type, len, data);

  // Убираем аргумент со стека
  dropTop(0);

  // Применяем режим
  seetimeMode = enable;

  outputStream->print("⏱️ seetime: ");
  outputStream->println(enable ? "ON" : "OFF");
}

void whileFunc(uint16_t addr) {
  // 1. Pop смещение выхода (i16 или int32)
  int32_t offsetExit;
  if (!popInt32FromAny(&offsetExit)) {
    // Если не смогли прочитать — выходим (ошибка)
    return;
  }

  // 2. Pop условие (должно быть уже на стеке)
  uint8_t condType, condLen;
  const uint8_t* condData;
  if (!peekStackTop(&condType, &condLen, &condData)) {
    return;
  }
  dropTop(0); // убираем условие со стека

  // 3. Преобразуем условие в bool

bool condition = valueToBool(condType, condLen, condData);

  // 4. Если условие ЛОЖНО — выходим из цикла (делаем goto с offsetExit)
  if (!condition) {
    // Кладём смещение на стек и вызываем goto
    pushInt(offsetExit);
    gotoFunc(addr);
  }
  // Если ИСТИННО — ничего не делаем, выполнение идёт к телу
}



void gotoFunc(uint16_t addr) {
  int32_t offset;
  if (!popInt32FromAny(&offset)) return;
  jumpOffset = offset;
  shouldJump = true;
}
