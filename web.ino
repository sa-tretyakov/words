extern HDLStream g_stream;
void webInit() {
  focusTo("web");
  addInternalWord("onHTTP", initHTTP);
  addInternalWord("HTTP", h_http);
  addInternalWord("onSoket", word_onSoket);
#ifdef pril // #endif  
 addInternalWord("onSoketPril", word_onSoketPril);
 addInternalWord("SoketPril", word_SoketPril); 
#endif
  addInternalWord("flush", word_flush_ws); // ← Исправлено имя
  focusTo("streams");
  addInternalWord("out>ws", word_out_ws);
  focusTo("main");
}

void h_http() {
  HTTP.handleClient();
}


void initHTTP() {
  initFS();
  // Кэшировать файлы для быстрой работы
  HTTP.serveStatic("/css/", FILESYSTEM, "/css/", "max-age=31536000"); // кеширование на 1 год
  HTTP.serveStatic("/js/", FILESYSTEM, "/js/", "max-age=31536000"); // кеширование на 1 год
  HTTP.serveStatic("/img/", FILESYSTEM, "/img/", "max-age=31536000"); // кеширование на 1 год
  //HTTP.serveStatic("/lang/", FILESYSTEM, "/lang/", "max-age=31536000"); // кеширование на 1 год

  HTTP.begin();
}

// Инициализация FFS
void initFS() {
  HTTP.on("/list", HTTP_GET, handleFileList);
  HTTP.on("/edit", HTTP_GET, []() {
    if (!handleFileRead("/edit.htm")) http404send();
  });
  HTTP.on("/edit", HTTP_PUT, handleFileCreate);
  HTTP.on("/edit", HTTP_DELETE, handleFileDelete);
  HTTP.on("/edit", HTTP_POST, []() {
    httpOkText("");
  }, handleFileUpload);
// === УНИВЕРСАЛЬНЫЙ HDL-РОУТЕР ===
HTTP.onNotFound([]() {
    String uri = HTTP.uri();
    
    // 1. Статический файл
    if (handleFileRead(uri)) return;
    
    // 2. Поиск слова HDL
    String name = uri;
    if (name.startsWith("/")) name.remove(0, 1);
    
    uint16_t addr = dict_find(name.c_str());
    if (addr == 0xFFFF) {
        http404send();
        return;
    }
    
    // 3. Исполнение с RAII-гардом
    String contentType = getContentType("/" + name);
    HTTP.setContentLength(CONTENT_LENGTH_UNKNOWN);
    HTTP.send(200, contentType, "");
    
    {
        OutputGuard guard;
        g_stream.attachHttp(&HTTP);
        currentOutput = &g_stream;
        guard.owns_stream = true;
        
        executeLine(name.c_str());
        // ← даже если executeLine упадёт/сделает abort, гард восстановит currentOutput
    }
});
}

// Здесь функции для работы с файловой системой
String getContentType(String filename) {
  if (HTTP.hasArg("download")) return "application/octet-stream";
  else if (filename.endsWith(".htm")) return "text/html";
  else if (filename.endsWith(".html")) return "text/html";
  else if (filename.endsWith(".wrd")) return "text/html";
  else if (filename.endsWith(".json")) return "application/json";
  else if (filename.endsWith(".css")) return "text/css";
  else if (filename.endsWith(".js")) return "application/javascript";
  else if (filename.endsWith(".png")) return "image/png";
  else if (filename.endsWith(".gif")) return "image/gif";
  else if (filename.endsWith(".jpg")) return "image/jpeg";
  else if (filename.endsWith(".ico")) return "image/x-icon";
  else if (filename.endsWith(".xml")) return "text/xml";
  else if (filename.endsWith(".pdf")) return "application/x-pdf";
  else if (filename.endsWith(".zip")) return "application/x-zip";
  else if (filename.endsWith(".gz")) return "application/x-gzip";
  return "text/plain";
}

void handleFileUpload() {
  if (HTTP.uri() != "/edit") return;
  HTTPUpload& upload = HTTP.upload();
  if (upload.status == UPLOAD_FILE_START) {
    String filename = upload.filename;
    if (!filename.startsWith("/")) filename = "/" + filename;
    fsUploadFile = SPIFFS.open(filename, "w");
    filename = String();
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (fsUploadFile)
      fsUploadFile.write(upload.buf, upload.currentSize);
  } else if (upload.status == UPLOAD_FILE_END) {
    if (fsUploadFile)
      fsUploadFile.close();
  }
}

void handleFileDelete() {
  if (HTTP.args() == 0) return http500send("BAD ARGS");
  String path = HTTP.arg(0);
  if (path == "/")
    return http500send("BAD PATH");
  if (!SPIFFS.exists(path))
    return http404send();
  SPIFFS.remove(path);
  httpOkText("");
  path = String();
}

void handleFileCreate() {
  if (HTTP.args() == 0)
    return http500send("BAD ARGS");
  String path = HTTP.arg(0);
  if (path == "/")
    return http500send("BAD PATH");
  if (SPIFFS.exists(path))
    return http500send("FILE EXISTS");
  File file = SPIFFS.open(path, "w");
  if (file)
    file.close();
  else
    return http500send("CREATE FAILED");
  httpOkText("");
  path = String();
}

// 3. Единый обработчик чтения/отдачи файла
bool handleFileRead(String path) {
  // 1. Гарантируем, что путь начинается с "/"
  if (!path.startsWith("/")) {
    path = "/" + path;
  }

  // 2. Обработка путей, заканчивающихся на "/" (ищем index.htm)
  if (path.endsWith("/")) {
    path += "index.htm";
  }

  // Определяем контент-тип
  String contentType = getContentType(path);

  // 3. Проверяем наличие обычного файла
  if (FILESYSTEM.exists(path)) {
    File file = FILESYSTEM.open(path, "r"); // "r" обязателен для ESP8266
    if (file) {
      HTTP.streamFile(file, contentType);
      file.close();
      return true;
    }
  }

  // 4. Если обычного файла нет, пробуем найти сжатую версию (.gz)
  String pathGz = path + ".gz";
  if (FILESYSTEM.exists(pathGz)) {
    File file = FILESYSTEM.open(pathGz, "r");
    if (file) {
      HTTP.streamFile(file, contentType);
      file.close();
      return true;
    }
  }

  // 5. Файл не найден
  return false;
}

String FileList(String path) {

    // ── 1. Нормализация пути ──────────────────────────────
    if (!path.startsWith("/")) path = "/" + path;
    while (path.length() > 1 && path.endsWith("/")) {
        path = path.substring(0, path.length() - 1);
    }
    if (path == "//") path = "/";

    // ── 2. Открытие директории ────────────────────────────
    File root = FILESYSTEM.open(path, "r");
    if (!root) return "[]";
    if (!root.isDirectory()) {
        root.close();
        return "[]";
    }

    // ── 3. Перебор файлов ─────────────────────────────────
    String output = "[";
    bool first = true;

    File file = root.openNextFile();
    while (file) {

        // ── 3a. Получаем ПОЛНЫЙ путь (платформозависимо) ────
        String fullPath;
#if defined(ESP8266)
        String rawName = String(file.name());
        if (rawName.startsWith("/")) {
            fullPath = rawName;
        } else {
            fullPath = (path == "/") ? "/" + rawName : path + "/" + rawName;
        }
#else
        fullPath = String(file.path());
#endif

        bool isDir = file.isDirectory();

        // ── 3b. Что показывать как "name" ───────────────────
        String displayName;
        if (isDir) {
            int lastSlash = fullPath.lastIndexOf('/');
            displayName = (lastSlash != -1) ? fullPath.substring(lastSlash + 1) : fullPath;
        } else {
            displayName = fullPath;
        }

        // 🔑 КЛЮЧЕВОЕ: убираем ВЕДУЩИЙ слеш для единообразия
        //   "/lang/ru.txt"  → "lang/ru.txt"
        //   "/help/en.txt"  → "help/en.txt"
        //   "README.TXT"    → "README.TXT"   (SD — без изменений)
        // Редактор сам добавит "/" при формировании URL
        if (displayName.startsWith("/")) {
            displayName = displayName.substring(1);
        }

        // ── 3c. Пропускаем скрытые ──────────────────────────
        String checkName = displayName;
        int lastSlash = checkName.lastIndexOf('/');
        if (lastSlash != -1) checkName = checkName.substring(lastSlash + 1);
        
        if (checkName.length() > 0 && !checkName.startsWith(".")) {

            if (!first) output += ",";
            first = false;

            output += "{\"type\":\"";
            output += isDir ? "dir" : "file";
            output += "\",\"name\":\"";
            output += displayName;
            output += "\",\"size\":";
            output += isDir ? "0" : String((unsigned long)file.size());
            output += "}";
        }

        file = root.openNextFile();
    }

    output += "]";
    root.close();
    return output;
}
// 2. Единый обработчик запроса списка файлов от веб-интерфейса
void handleFileList() {
  if (!HTTP.hasArg("dir")) {
    HTTP.send(500, "text/plain", "BAD ARGS");
    return;
  }

  String path = HTTP.arg("dir");
  if (path.length() == 0) path = "/";

  HTTP.sendHeader("Cache-Control", "no-cache"); // Запрет кэширования списка
  HTTP.send(200, "application/json", FileList(path));
}


void httpOkText() {
  HTTP.send(200, "text/plain", "Ok");
}
void httpOkText(String text) {
  HTTP.send(200, "text/plain", text);
}
void httpOkHtml(String text) {
  HTTP.send(200, "text/html", text);
}
void httpOkJson(String text) {
  HTTP.send(200, "application/json", text);
}
void http500send(String text) {
  HTTP.send(500, "text/plain", text);
}
void http404send() {
  HTTP.send(404, "text/plain", "FileNotFound");
}




// ✅ Точная сигнатура, ожидаемая библиотекой
void wsServerEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
    switch (type) {
case WStype_DISCONNECTED:
    g_stream.wsClientDisconnected();
    if (!g_stream.isWsConnected()) {
        g_stream.detach(true); // ← ДОБАВИТЬ true: клиентов больше нет, чистим состояние
    }
    break;
            
        case WStype_CONNECTED: {
            // 🔑 Увеличиваем счетчик клиентов
            g_stream.wsClientConnected();
            
            g_stream.attachWs(&wsServer);
            
            // Отправляем промпт WS-клиенту
            Print* saved = currentOutput;
            currentOutput = &g_stream;
            printStack();
            printActiveTasks();
            word_prompt();
            g_stream.flush();
            currentOutput = saved;
            break;
        }
           
        case WStype_TEXT: {
            if (length > 0 && length < 255) {
                memcpy(ws_cmd, payload, length);
                ws_cmd[length] = '\0';
                ws_cmd_ready = true;
            }
            break;
        }
          
        default:
            break;
    }
}
void word_onSoket() {
  wsServer.begin();
  wsServer.onEvent(wsServerEvent);
  currentOutput->println("WebSocket server started on port 82");
}


void word_out_ws() {
g_stream.attachWs(&wsServer);
currentOutput = &g_stream;
}

// ✅ ИСПРАВЛЕНО: Имя функции совпадает с регистрацией
void word_flush_ws() {
if (currentOutput == &g_stream) g_stream.flush();

  
}

// prilServer
#ifdef pril // #endif

void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {

    switch(type) {
        case WStype_DISCONNECTED:
            break;
        case WStype_CONNECTED:
            {
                IPAddress ip = prilServer.remoteIP(num);
    prilServer.sendTXT(num, "Connected");
            }
            break;
        case WStype_TEXT:
   if (length > 0) {
        String command = String((const char *)payload);
        currentOutput->println(command);
      }         
            // prilServer.sendTXT(num, "message here");
            // prilServer.broadcastTXT("message here");
            break;
        case WStype_BIN:
            // prilServer.sendBIN(num, payload, length);
            break;
  case WStype_ERROR:      
  case WStype_FRAGMENT_TEXT_START:
  case WStype_FRAGMENT_BIN_START:
  case WStype_FRAGMENT:
  case WStype_FRAGMENT_FIN:
      break;
    }

}
void word_onSoketPril() {
    prilServer.begin();
    prilServer.onEvent(webSocketEvent);
  currentOutput->println("WebSocket server started on port 81");
}

void word_SoketPril() {
  prilServer.loop();
}
#endif
