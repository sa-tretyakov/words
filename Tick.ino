void addTickFunc(WordEntry* self, WordEntry** body, int* ip) {
  if (dataStack.size() < 2) {
    Serial.println("addTick: expected 2 arguments (interval_ms word_name)");
    return;
  }
  WordEntry* intervalWord = dataStack.back(); dataStack.pop_back(); // 1000
  WordEntry* wordNameWord = dataStack.back(); dataStack.pop_back(); // "myTask"


  if (intervalWord->type != INT || wordNameWord->type != STRING) {
    Serial.println("addTick: interval(int) word_name(string) expected");
    return;
  }

  int32_t interval = *(int32_t*)intervalWord->value;
  String wordName = String((char*)wordNameWord->value);

  if (interval <= 0) {
    Serial.println("addTick: interval must be > 0");
    return;
  }

  // Проверяем, существует ли слово
  if (dictionary.find(wordName) == dictionary.end()) {
    Serial.println("addTick: word '" + wordName + "' not found");
    return;
  }

  // Добавляем или обновляем задачу
  bool found = false;
  for (auto& task : tickTasks) {
    if (task.wordName == wordName) {
      task.intervalMs = interval;
      task.lastRunMs = millis();
      task.active = true;
      found = true;
      break;
    }
  }

  if (!found) {
    TickTask task;
    task.wordName = wordName;
    task.intervalMs = interval;
    task.lastRunMs = millis();
    task.active = true;
    tickTasks.push_back(task);
  }

  Serial.println("addTick: added '" + wordName + "' every " + String(interval) + " ms");
}

void delTickFunc(WordEntry* self, WordEntry** body, int* ip) {
  if (dataStack.empty()) {
    Serial.println("delTick: expected word_name (string)");
    return;
  }

  WordEntry* wordNameWord = dataStack.back(); dataStack.pop_back();
  if (wordNameWord->type != STRING) {
    Serial.println("delTick: word_name must be string");
    return;
  }

  String wordName = String((char*)wordNameWord->value);
  bool found = false;

  for (auto& task : tickTasks) {
    if (task.wordName == wordName) {
      task.active = false;
      found = true;
      break;
    }
  }

  if (found) {
    Serial.println("delTick: stopped '" + wordName + "'");
  } else {
    Serial.println("delTick: task '" + wordName + "' not found");
  }
}
