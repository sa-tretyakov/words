void dirFunc(WordEntry* self, WordEntry** body, int* ip) {
  Serial.println("\nFiles on filesystem:");

#ifdef LittleFSM
  File root = LittleFS.open("/");
  if (!root) {
    Serial.println("Failed to open root directory");
    return;
  }
  File file = root.openNextFile();
  while (file) {
    String fileName = file.name();
    // Убираем начальный слеш, если есть
    if (fileName.startsWith("/")) {
      fileName = fileName.substring(1);
    }
    Serial.print("  ");
    Serial.print(fileName);
    Serial.print(" (");
    Serial.print(file.size());
    Serial.println(" bytes)");
    file = root.openNextFile();
  }
  root.close();
#else
  File root = SPIFFS.open("/");
  if (!root) {
    Serial.println("Failed to open root directory");
    return;
  }
  File file = root.openNextFile();
  while (file) {
    String fileName = file.name();
    if (fileName.startsWith("/")) {
      fileName = fileName.substring(1);
    }
    Serial.print("  ");
    Serial.print(fileName);
    Serial.print(" (");
    Serial.print(file.size());
    Serial.println(" bytes)");
    file = root.openNextFile();
  }
  root.close();
#endif

  Serial.println();
}

void delFunc(WordEntry* self, WordEntry** body, int* ip) {
  if (dataStack.empty()) {
    Serial.println("del: expected filename (string)");
    return;
  }

  WordEntry* fnameWord = dataStack.back(); dataStack.pop_back();
  if (fnameWord->type != STRING) {
    Serial.println("del: filename must be a string");
    return;
  }

  String filename = String((char*)fnameWord->value);
  // Убедимся, что путь начинается с "/"
  if (!filename.startsWith("/")) {
    filename = "/" + filename;
  }

#ifdef LittleFSM
  if (LittleFS.exists(filename) && LittleFS.remove(filename)) {
    Serial.println("Deleted: " + filename);
  } else {
    Serial.println("del: failed to delete " + filename);
  }
#else
  if (SPIFFS.exists(filename) && SPIFFS.remove(filename)) {
    Serial.println("Deleted: " + filename);
  } else {
    Serial.println("del: failed to delete " + filename);
  }
#endif
}
void typeFunc(WordEntry* self, WordEntry** body, int* ip) {
  if (dataStack.empty()) {
    Serial.println("type: expected filename (string)");
    return;
  }

  WordEntry* fnameWord = dataStack.back(); dataStack.pop_back();
  if (fnameWord->type != STRING) {
    Serial.println("type: filename must be a string");
    return;
  }

  String filename = String((char*)fnameWord->value);
  if (!filename.startsWith("/")) {
    filename = "/" + filename;
  }

#ifdef LittleFSM
  File file = LittleFS.open(filename, "r");
#else
  File file = SPIFFS.open(filename, "r");
#endif

  if (!file) {
    Serial.println("type: file not found: " + filename);
    return;
  }

  Serial.println("\n--- " + filename + " ---");
  while (file.available()) {
    Serial.write(file.read());
  }
  Serial.println("\n--- end of " + filename + " ---");
  file.close();
}
