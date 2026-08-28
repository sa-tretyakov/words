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

const char webForth[] PROGMEM = R"raw(<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <title>Forth console</title>
</head>
<body>
<script>
let ws;
function connect(){
  ws = new WebSocket("ws:/"+"/"+location.hostname+":82",["arduino"]);
  ws.onmessage = e => { c.value += e.data; c.scrollTop = c.scrollHeight; };
  ws.onclose = ws.onerror = () => setTimeout(connect, 2000);
}
connect();

function sendCmd(e){
  if(e.keyCode===13){
    if (e.shiftKey) return;    
    e.preventDefault();
    let s = c.value.split("ok>").pop();
    ws.readyState===1 ? ws.send(s) : c.value+="\n[Нет соединения]\n";
  }
}
</script>

<textarea id="c" cols="30" rows="30" style="width:100%;height:100%;" onkeypress="sendCmd(event)"></textarea>
</body>
</html>)raw";
void initHTTP() {
  initFS();
  // Кэшировать файлы для быстрой работы
  HTTP.serveStatic("/css/", FILESYSTEM, "/css/", "max-age=31536000"); // кеширование на 1 год
  HTTP.serveStatic("/js/", FILESYSTEM, "/js/", "max-age=31536000"); // кеширование на 1 год
  HTTP.serveStatic("/img/", FILESYSTEM, "/img/", "max-age=31536000"); // кеширование на 1 год
  //HTTP.serveStatic("/lang/", FILESYSTEM, "/lang/", "max-age=31536000"); // кеширование на 1 год
  // ------------------Редактор FORTH
  HTTP.on("/forth", HTTP_GET, []() {
    httpOkHtml(webForth);
  });

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

bool handleFileRead(String path) {
  String setIndex =  "index.htm";
  if (setIndex == "") setIndex = "index.htm";
  if (path.endsWith("/")) path += setIndex;
  String contentType = getContentType(path);
  String pathWithGz = path + ".gz";
  if (SPIFFS.exists(pathWithGz) || SPIFFS.exists(path)) {
    if (SPIFFS.exists(pathWithGz))
      path += ".gz";
    File file = SPIFFS.open(path, "r");
    size_t sent = HTTP.streamFile(file, contentType);
    file.close();
    return true;
  }
  return false;
}

void handleFileList() {
  if (!HTTP.hasArg("dir")) {
    http500send("BAD ARGS");//
    return;
  }
  String path = HTTP.arg("dir");
  httpOkJson(FileList(path));
}

String FileList(String path) {
  File root = SPIFFS.open(path);
  path = String();

  String output = "[";
  if (root.isDirectory()) {
    File file = root.openNextFile();
    while (file) {
      if (output != "[") {
        output += ',';
      }
      output += "{\"type\":\"";
      output += (file.isDirectory()) ? "dir" : "file";
      output += "\",\"name\":\"";
      output += String(file.path()).substring(1);
      output += "\"}";
      file = root.openNextFile();
    }
  }
  output += "]";
  return output;
}


// Создаем список файлов каталога
#if defined(ESP8266)
String FileList(String path) {
  Dir dir = SPIFFS.openDir(path);
  path = String();
  String output = "[";
  while (dir.next()) {
    File entry = dir.openFile("r");
    if (output != "[") output += ',';
    bool isDir = false;
    output += "{\"type\":\"";
    output += (isDir) ? "dir" : "file";
    output += "\",\"name\":\"";
    output += String(entry.name()).substring(1);
    output += "\"}";
    entry.close();
  }
  output += "]";
  return output;
}

#else
#if FILESYSTEM == SPIFFS

#else
bool handleFileRead(String path) {
  Serial.println("handleFileRead: " + path);

  // --- НАЧАЛО ИЗМЕНЕНИЙ ---

  // 1. Гарантируем, что путь начинается с "/"
  if (!path.startsWith("/")) {
    path = "/" + path;
  }

  // 2. Обработка путей, заканчивающихся на "/"
  if (path.endsWith("/")) {
    // Если это просто корень сайта "/", ищем index.html в корне
    if (path == "/") {
      path += "index.htm";
    }
    // Если это подпапка (например, "/css/"), тоже ищем там index.html
    else {
      path += "index.htm";
    }
  }

  // --- КОНЕЦ ИЗМЕНЕНИЙ ---

  // Определяем контент-тип сразу по запрошенному пути (например, /style.css)
  String contentType = getContentType(path);

  // 3. Проверяем наличие обычного файла
  if (FILESYSTEM.exists(path)) {
    File file = FILESYSTEM.open(path, "r");
    if (file) {
      HTTP.streamFile(file, contentType);
      file.close();
      return true;
    }
  }

  // 4. Если обычного файла нет, пробуем найти сжатую версию (.gz)
  String pathGz = path + ".gz";

  if (FILESYSTEM.exists(pathGz)) {
    //Serial.println("нашел gz: " + pathGz);
    File file = FILESYSTEM.open(pathGz, "r");
    if (file) {
      // Отправляем файл.
      HTTP.streamFile(file, contentType);
      file.close();
      return true;
    }
  }
  // 5. Файл не найден ни в обычном, ни в сжатом виде
  return false;
}
void handleFileList() {
  // 1. Проверяем, передан ли аргумент 'dir' (путь к папке)
  if (!HTTP.hasArg("dir")) {
    HTTP.send(500, "text/plain", "BAD ARGS");
    return;
  }

  String path = HTTP.arg("dir");

  // Если путь пустой или некорректный, считаем это корнем
  if (path.length() == 0) path = "/";

  // 2. Открываем директорию
  File root = FILESYSTEM.open(path);

  // Если открыли не директорию (например, файл), возвращаем ошибку
  if (!root || !root.isDirectory()) {
    HTTP.send(500, "text/plain", "NOT A DIRECTORY");
    return;
  }

  // 3. Начинаем формировать JSON ответ
  // Формат: [ {"type":"file","name":"index.html"}, {"type":"dir","name":"css"} ]
  String output = "[";
  bool first = true;

  File file = root.openNextFile();
  while (file) {
    // Получаем имя файла/папки
    String fileName = String(file.name());

    // file.name() возвращает полный путь, например "/css/style.css".
    // Нам нужно оставить только имя последнего элемента.
    int lastSlash = fileName.lastIndexOf('/');
    if (lastSlash != -1) {
      fileName = fileName.substring(lastSlash + 1);
    }

    // Пропускаем системные скрытые файлы (начинаются с точки), если нужно
    // if (fileName.startsWith(".")) { file = root.openNextFile(); continue; }

    if (!first) {
      output += ",";
    }
    first = false;

    output += "{\"type\":\"";
    if (file.isDirectory()) {
      output += "dir";
    } else {
      output += "file";
    }

    output += "\",\"name\":\"";
    output += fileName;
    output += "\"}";

    // Переходим к следующему файлу
    file = root.openNextFile();
  }

  output += "]";

  // Закрываем корневую директорию
  root.close();

  // 4. Отправляем результат
  HTTP.sendHeader("Cache-Control", "no-cache"); // Чтобы браузер не кэшировал список файлов
  HTTP.send(200, "application/json", output);
}
#endif
#endif

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
