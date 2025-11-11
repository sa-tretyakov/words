void addTaskWord(uint16_t addr) {
  uint8_t type, len;
  const uint8_t* data;
  
  // Имя (строка)
  if (!peekStackTop(&type, &len, &data) || type != TYPE_STRING) { dropTop(0); Serial.println("⚠️ +Task: string expected"); return; }
  String name = String((char*)data, len);
  dropTop(0);
  
  // Интервал
  if (!peekStackTop(&type, &len, &data)) return;
  dropTop(0);
  uint32_t interval = 0;
  if (type == TYPE_INT && len == 4) memcpy(&interval, data, 4);
  else if (type == TYPE_UINT16 && len == 2) { uint16_t v; memcpy(&v, data, 2); interval = v; }
  else if (type == TYPE_UINT8 && len == 1) interval = data[0];
  else { Serial.println("⚠️ +Task: invalid interval"); return; }

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
  if (!wordAddr) { Serial.println("⚠️ +Task: word not found"); return; }

  // Обновить или добавить
  for (int i = 0; i < MAX_TASKS; i++) {
    if (tasks[i].active) {
      uint8_t tlen = dictionary[tasks[i].wordAddr + 2];
      if (tlen == name.length() && memcmp(&dictionary[tasks[i].wordAddr + 3], name.c_str(), tlen) == 0) {
        tasks[i].interval = interval;
        tasks[i].lastRun = millis();
        Serial.printf("Task updated: %s\n", name.c_str());
        return;
      }
    }
  }
  for (int i = 0; i < MAX_TASKS; i++) {
    if (!tasks[i].active) {
      tasks[i] = {wordAddr, interval, millis(), true};
      //Serial.printf("Task added: %s\n", name.c_str());
      return;
    }
  }
  Serial.println("⚠️ +Task: full");
}

void removeTaskWord(uint16_t addr) {
  uint8_t nameType, nameLen;
  const uint8_t* nameData;
  if (!peekStackTop(&nameType, &nameLen, &nameData)) return;
  if (nameType != TYPE_STRING) {
    Serial.println("⚠️ -Task: expected string in quotes");
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
          //Serial.printf("Task removed: %s\n", wordName.c_str());
          return;
        }
      }
    }
  }

  Serial.println("⚠️ -Task: task not found");
}
