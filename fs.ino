bool popFilename(char* outPath, size_t outSize) {
  uint8_t nameType, nameLen;
  const uint8_t* nameData;
  if (!peekStackTop(&nameType, &nameLen, &nameData)) {
    Serial.println("⚠️ filename: stack empty");
    return false;
  }

  if (nameType != TYPE_STRING && nameType != TYPE_NAME) {
    Serial.println("⚠️ filename: expected string or name");
    return false;
  }

  if (nameLen >= outSize - 1) {
    Serial.println("⚠️ filename: too long");
    return false;
  }

  // Копируем и добавляем '/'
  if (nameLen > 0 && nameData[0] != '/') {
    outPath[0] = '/';
    memcpy(outPath + 1, nameData, nameLen);
    outPath[nameLen + 1] = '\0';
  } else {
    memcpy(outPath, nameData, nameLen);
    outPath[nameLen] = '\0';
  }

  dropTop(0);
  return true;
}
void listFilesWord(uint16_t addr) {
  Serial.println("FS files:");
  File root = FILESYSTEM.open("/");
  if (!root) {
    Serial.println("⚠️ Failed to open root directory");
    return;
  }

  File file = root.openNextFile();
  while (file) {
    Serial.print(file.name());
    if (file.isDirectory()) {
      Serial.println("/");
    } else {
      Serial.printf(" (%d bytes)\n", file.size());
    }
    file = root.openNextFile();
  }
  root.close();
}

void catWord(uint16_t addr) {
  char filename[256];
  if (!popFilename(filename, sizeof(filename))) return;
  File file = FILESYSTEM.open(filename, "r");
  if (!file) {
    Serial.printf("⚠️ File not found: %s\n", filename);
    return;
  }
  while (file.available()) {
    String line = file.readStringUntil('\n');
    Serial.println(line);
  }
  file.close();
}

void loadWord(uint16_t addr) {
  char filename[256];
  if (!popFilename(filename, sizeof(filename))) return;
  File file = FILESYSTEM.open(filename, "r");
  if (!file) {
    Serial.printf("⚠️ File not found: %s\n", filename);
    return;
  }
  while (file.available()) {
    String line = file.readStringUntil('\n');
    line.trim();
    if (line.length() > 0) {
      executeLine(line);
    }
  }
  file.close();
}
