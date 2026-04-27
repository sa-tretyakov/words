void filesInit() {  
  String tmp = "cont files";
  executeLine(tmp);
  addInternalWord("dir", listFilesWord);
  addInternalWord("ls", listFilesWord);
  addInternalWord("cat", catWord);
  addInternalWord("type", catWord);
  addInternalWord("load", loadWord);
  addInternalWord("pwd", pwdWord);
  addInternalWord("cd", cdWord);
 tmp = "main";
  executeLine(tmp);
}



// Глобальная переменная — текущий рабочий каталог
extern char currentDir[256];  // определена где-то в .cpp файле как: char currentDir[256] = "/";

bool popFilename(char* outPath, size_t outSize) {
  uint8_t nameType, nameLen;
  const uint8_t* nameData;

  if (!peekStackTop(&nameType, &nameLen, &nameData)) {
    outputStream->println("⚠️ filename: stack empty");
    return false;
  }

  if (nameType != TYPE_STRING && nameType != TYPE_NAME) {
    outputStream->println("⚠️ filename: expected string or name");
    return false;
  }

  // Копируем входное имя во временный буфер (с нуль-терминатором)
  char inputName[256];
  if (nameLen >= sizeof(inputName)) {
    outputStream->println("⚠️ filename: input name too long");
    dropTop(0);
    return false;
  }
  memcpy(inputName, nameData, nameLen);
  inputName[nameLen] = '\0';

  dropTop(0);  // убираем имя с вершины стека

  // Случай 1: абсолютный путь — просто копируем (но проверяем длину)
  if (inputName[0] == '/') {
    if (strlen(inputName) >= outSize) {
      outputStream->println("⚠️ filename: resolved path too long");
      return false;
    }
    strcpy(outPath, inputName);
    return true;
  }

  // Случай 2: относительный путь — резолвим относительно currentDir

  // Формируем путь: currentDir + '/' + inputName
  int written;
  if (strcmp(currentDir, "/") == 0) {
    // currentDir — корень → путь: /filename
    written = snprintf(outPath, outSize, "/%s", inputName);
  } else {
    // currentDir — не корень → путь: /a/b/filename
    written = snprintf(outPath, outSize, "%s/%s", currentDir, inputName);
  }

  // Проверяем, уложились ли в буфер
  if (written < 0 || (size_t)written >= outSize) {
    outputStream->println("⚠️ filename: resolved path too long");
    return false;
  }

  return true;
}




void pwdWord(uint16_t addr) {
  outputStream->println(currentDir);
}

void cdWord(uint16_t addr) {
  uint8_t nameType, nameLen;
  const uint8_t* nameData;

  // 1. Проверка стека
  if (!peekStackTop(&nameType, &nameLen, &nameData)) {
    outputStream->println("⚠️ cd: stack empty");
    return;
  }

  // 2. Проверка типа
  if (nameType != TYPE_STRING && nameType != TYPE_NAME) {
    outputStream->println("⚠️ cd: expected string or name");
    dropTop(0);
    return;
  }

  // 3. Копируем имя
  char input[256];
  if (nameLen >= sizeof(input)) {
    outputStream->println("⚠️ cd: name too long");
    dropTop(0);
    return;
  }
  memcpy(input, nameData, nameLen);
  input[nameLen] = '\0';
  dropTop(0);

  // 4. ТВОЁ ПРАВИЛО: "." → переходим в корень
  if (strcmp(input, ".") == 0) {
    strcpy(currentDir, "/");
    return;
  }

  // 5. ТВОЁ ПРАВИЛО: ".." → подняться на уровень выше
  if (strcmp(input, "..") == 0) {
    if (strcmp(currentDir, "/") != 0) { // не в корне
      char* lastSlash = strrchr(currentDir, '/');
      if (lastSlash == currentDir) {
        // Было: "/abc" → становится "/"
        strcpy(currentDir, "/");
      } else if (lastSlash != nullptr) {
        // Было: "/a/b/c" → обрезаем до "/a/b"
        *lastSlash = '\0';
      }
    }
    // Если уже в корне — остаёмся в "/"
    return;
  }

  // 6. Обычный путь: абсолютный или относительный
  if (input[0] == '/') {
    // Абсолютный путь
    if (strlen(input) >= sizeof(currentDir)) {
      outputStream->println("⚠️ cd: path too long");
      return;
    }
    strcpy(currentDir, input);
  } else {
    // Относительный путь
    if (strcmp(currentDir, "/") == 0) {
      snprintf(currentDir, sizeof(currentDir), "/%s", input);
    } else {
      snprintf(currentDir, sizeof(currentDir), "%s/%s", currentDir, input);
    }
  }

  // 7. Убираем trailing slashes (кроме корня)
  size_t len = strlen(currentDir);
  while (len > 1 && currentDir[len - 1] == '/') {
    currentDir[--len] = '\0';
  }
}


void listFilesWord(uint16_t addr) {
  // Нормализуем currentDir: убираем дублирующие слеши, гарантируем формат
  String dirPath = currentDir;
  if (dirPath == "/") {
    dirPath = "";  // LittleFS: корень = пустая строка при сравнении
  } else if (dirPath.endsWith("/")) {
    dirPath.remove(dirPath.length() - 1);  // убираем финальный /
  }
  size_t prefixLen = dirPath.length();

  const int MAX_ITEMS = 32;
  struct DirEntry {
    char name[64];
    bool isDir;
    uint32_t size;
  };
  DirEntry items[MAX_ITEMS];
  int itemCount = 0;

  // Открываем нужную директорию (не всегда корень!)
  File dir = FILESYSTEM.open(currentDir);
  if (!dir || !dir.isDirectory()) {
    outputStream->printf("⚠️ Cannot open directory: %s\n", currentDir);
    return;
  }

  File file = dir.openNextFile();
  while (file && itemCount < MAX_ITEMS) {
    const char* fullname = file.name();
    
    // 🔧 КЛЮЧЕВОЙ ФИКС: нормализация имени для сравнения
    String entryName = fullname;
    
    // LittleFS может вернуть имя без ведущего /, SPIFFS — с /
    // Убираем ведущий / для унификации, если он есть
    if (entryName.startsWith("/")) {
      entryName = entryName.substring(1);
    }
    
    // Если мы в корне (currentDir = "/"), prefixLen = 0, сравниваем с начала
    // Если в поддиректории, проверяем, что файл начинается с пути директории
    bool isDirectChild = false;
    
    if (prefixLen == 0) {
      // Корневая директория: берём всё, что не содержит /
      isDirectChild = (strchr(entryName.c_str(), '/') == nullptr);
    } else {
      // Проверяем, что имя начинается с currentDir + /
      String expectedPrefix = dirPath + "/";
      if (entryName.startsWith(expectedPrefix)) {
        String rel = entryName.substring(expectedPrefix.length());
        isDirectChild = (strchr(rel.c_str(), '/') == nullptr);
      }
    }
    
    if (isDirectChild) {
      // Извлекаем имя файла/директории
      String displayName;
      if (prefixLen == 0) {
        displayName = entryName;
      } else {
        String expectedPrefix = dirPath + "/";
        displayName = entryName.substring(expectedPrefix.length());
      }
      
      if (displayName.length() > 0 && displayName.length() < sizeof(items[0].name)) {
        // Проверяем дубликаты (для поддиректорий)
        bool exists = false;
        for (int i = 0; i < itemCount; i++) {
          if (strcmp(items[i].name, displayName.c_str()) == 0) {
            exists = true;
            break;
          }
        }
        
        if (!exists) {
          strncpy(items[itemCount].name, displayName.c_str(), sizeof(items[0].name) - 1);
          items[itemCount].name[sizeof(items[0].name) - 1] = '\0';
          items[itemCount].isDir = file.isDirectory();
          items[itemCount].size = file.size();
          itemCount++;
        }
      }
    }
    file = dir.openNextFile();
  }
  dir.close();

  // Вывод результатов
  if (itemCount == 0) {
    outputStream->println("(empty)");
  } else {
    for (int i = 0; i < itemCount; i++) {
      if (items[i].isDir) {
        outputStream->printf("%s/\n", items[i].name);
      } else {
        outputStream->printf("%s (%u bytes)\n", items[i].name, (unsigned)items[i].size);
      }
    }
  }
}
void catWord(uint16_t addr) {
  char filename[256];
  if (!popFilename(filename, sizeof(filename))) return;
  File file = FILESYSTEM.open(filename, "r");
  if (!file) {
    outputStream->printf("⚠️ File not found: %s\n", filename);
    return;
  }
  while (file.available()) {
    String line = file.readStringUntil('\n');
    outputStream->println(line);
  }
  file.close();
}

void loadWord(uint16_t addr) {
  char filename[256];
  if (!popFilename(filename, sizeof(filename))) return;
  File file = FILESYSTEM.open(filename, "r");
  if (!file) {
    outputStream->printf("⚠️ File not found: %s\n", filename);
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
