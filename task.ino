void taskInit() {
  String tmp = "cont task";
  executeLine(tmp);
  addInternalWord("+task", addTaskWord);
  addInternalWord("-task", removeTaskWord);
  addInternalWord("+loop", addLoopWord);
  addInternalWord("-loop", removeLoopWord);
  tmp = "main";
  executeLine(tmp);
}

void addLoopWord(uint16_t addr) {
  // Имя (строка)
  uint8_t nameType, nameLen;
  const uint8_t* nameData;
  if (!peekStackTop(&nameType, &nameLen, &nameData) || nameType != TYPE_STRING) {
    dropTop(0);
    outputStream->println("⚠️ +loop: expected word name in quotes");
    return;
  }
  dropTop(0);

  String name((char*)nameData, nameLen);

  // Поиск слова
  uint16_t wordAddr = findWordAddress(name.c_str());
  if (wordAddr == 0xFFFF) {
    outputStream->printf("⚠️ +loop: word '%s' not found\n", name.c_str());
    return;
  }

  // Добавление (без дубликатов, если нужно — можно добавить)
  if (loopWordCount < MAX_LOOP_WORDS) {
    loopWords[loopWordCount++] = wordAddr;
    //outputStream->printf("+loop: added '%s'\n", name.c_str());
  } else {
    outputStream->println("⚠️ +loop: queue full");
  }
}

void removeLoopWord(uint16_t addr) {
  // Имя (строка)
  uint8_t nameType, nameLen;
  const uint8_t* nameData;
  if (!peekStackTop(&nameType, &nameLen, &nameData) || nameType != TYPE_STRING) {
    dropTop(0);
    outputStream->println("⚠️ -loop: expected word name in quotes");
    return;
  }
  dropTop(0);

  String name((char*)nameData, nameLen);

  // Поиск слова в словаре, чтобы получить его адрес
  uint16_t wordAddr = findWordAddress(name.c_str());
  if (wordAddr == 0xFFFF) {
    outputStream->printf("⚠️ -loop: word '%s' not found in dictionary\n", name.c_str());
    return;
  }

  // Поиск ПОСЛЕДНЕГО вхождения в loopWords (идём с конца)
  int8_t lastIdx = -1;
  for (int8_t i = loopWordCount - 1; i >= 0; i--) {
    if (loopWords[i] == wordAddr) {
      lastIdx = i;
      break;
    }
  }

  if (lastIdx == -1) {
    outputStream->printf("⚠️ -loop: word '%s' not in +loop queue\n", name.c_str());
    return;
  }

  // Удаляем элемент по индексу lastIdx
  for (uint8_t j = lastIdx; j < loopWordCount - 1; j++) {
    loopWords[j] = loopWords[j + 1];
  }
  loopWordCount--;

  //outputStream->printf("-loop: removed last occurrence of '%s'\n", name.c_str());
}


void addTaskWord(uint16_t addr) {
  uint8_t type, len;
  const uint8_t* data;
  
  // Имя (строка)
  if (!peekStackTop(&type, &len, &data) || type != TYPE_STRING) { dropTop(0); outputStream->println("⚠️ +Task: string expected"); return; }
  String name = String((char*)data, len);
  dropTop(0);
  
  // Интервал
  if (!peekStackTop(&type, &len, &data)) return;
  dropTop(0);
  uint32_t interval = 0;
  if (type == TYPE_INT && len == 4) memcpy(&interval, data, 4);
  else if (type == TYPE_UINT16 && len == 2) { uint16_t v; memcpy(&v, data, 2); interval = v; }
  else if (type == TYPE_UINT8 && len == 1) interval = data[0];
  else { outputStream->println("⚠️ +Task: invalid interval"); return; }

  // Найти слово
  uint16_t wordAddr = 0;
  for (uint16_t p = 0; p < dictLen; ) {
    uint16_t next = dictionary[p] | (dictionary[p+1] << 8);
    uint8_t nlen = dictionary[p+2];
    if (nlen == name.length() && memcmp(&dictionary[p+3], name.c_str(), nlen) == 0) {
      wordAddr = p; break;
    }
    if (next <= p) break;
    p = next;
  }
  if (!wordAddr) { outputStream->println("⚠️ +Task: word not found"); return; }

  // Обновить или добавить
  for (int i = 0; i < MAX_TASKS; i++) {
    if (tasks[i].active) {
      uint8_t tlen = dictionary[tasks[i].wordAddr + 2];
      if (tlen == name.length() && memcmp(&dictionary[tasks[i].wordAddr + 3], name.c_str(), tlen) == 0) {
        tasks[i].interval = interval;
        tasks[i].lastRun = millis();
        outputStream->printf("Task updated: %s\n", name.c_str());
        return;
      }
    }
  }
  for (int i = 0; i < MAX_TASKS; i++) {
    if (!tasks[i].active) {
      tasks[i] = {wordAddr, interval, millis(), true};
      //outputStream->printf("Task added: %s\n", name.c_str());
      return;
    }
  }
  outputStream->println("⚠️ +Task: full");
}

void removeTaskWord(uint16_t addr) {
  uint8_t nameType, nameLen;
  const uint8_t* nameData;
  if (!peekStackTop(&nameType, &nameLen, &nameData)) return;
  if (nameType != TYPE_STRING) {
    outputStream->println("⚠️ -Task: expected string in quotes");
    return;
  }
  dropTop(0);

  String wordName = String((char*)nameData, nameLen);

  for (int i = 0; i < MAX_TASKS; i++) {
    if (tasks[i].active) {
      uint8_t taskNameLen = dictionary[tasks[i].wordAddr + 2];
      if ((size_t)taskNameLen == wordName.length()) {
        bool match = true;
        for (uint8_t j = 0; j < taskNameLen; j++) {
          if (dictionary[tasks[i].wordAddr + 3 + j] != wordName[j]) {
            match = false;
            break;
          }
        }
        if (match) {
          tasks[i].active = false;
          //outputStream->printf("Task removed: %s\n", wordName.c_str());
          return;
        }
      }
    }
  }

  outputStream->println("⚠️ -Task: task not found");
}
