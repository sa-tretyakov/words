void webInit() {
   String tmp = "cont web";
   executeLine(tmp);
  addInternalWord("onHTTP", initHTTP);
  addInternalWord("HTTP", h_http);
  addInternalWord("onSoket", initWebSocket);
  addInternalWord("Soket", h_soket);
  addInternalWord("out>serial", outToSerialWord);
  addInternalWord("out>ws", outToWsWord);
  addInternalWord("flush", flushOutputWord);
  tmp = "main";
  executeLine(tmp);
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

void initHTTP(uint16_t addr) {
  initFS();
   // Кэшировать файлы для быстрой работы
  HTTP.serveStatic("/css/", FILESYSTEM, "/css/", "max-age=31536000"); // кеширование на 1 год
  HTTP.serveStatic("/js/", FILESYSTEM, "/js/", "max-age=31536000"); // кеширование на 1 год
  HTTP.serveStatic("/img/", FILESYSTEM, "/img/", "max-age=31536000"); // кеширование на 1 год
  //HTTP.serveStatic("/lang/", FILESYSTEM, "/lang/", "max-age=31536000"); // кеширование на 1 год
   // ------------------Редактор FORTH
   HTTP.on("/forth", HTTP_GET, []() {
    //String     webForth="ok";
     httpOkHtml(webForth);
  }); 

  HTTP.begin();
}

// Инициализация FFS
void initFS() {

  //HTTP страницы для работы с FFS
  //list directory
  HTTP.on("/list", HTTP_GET, handleFileList);
  //загрузка редактора editor
  HTTP.on("/edit", HTTP_GET, []() {
    if (!handleFileRead("/edit.htm")) http404send();//HTTP.send(404, "text/plain", "FileNotFound");
  });
  //Создание файла
  HTTP.on("/edit", HTTP_PUT, handleFileCreate);
  //Удаление файла
  HTTP.on("/edit", HTTP_DELETE, handleFileDelete);
  //first callback is called after the request has ended with all parsed arguments
  //second callback handles file uploads at that location
  HTTP.on("/edit", HTTP_POST, []() {
    //HTTP.send(200, "text/plain", "");
    httpOkText("");
  }, handleFileUpload);
  //called when the url is not defined here
  //use it to load content from SPIFFS
  HTTP.onNotFound([]() {
    if (!handleFileRead(HTTP.uri()))
      http404send();//HTTP.send(404, "text/plain", "FileNotFound");
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
    //DBG_OUTPUT_PORT.print("handleFileUpload Data: "); DBG_OUTPUT_PORT.println(upload.currentSize);
    if (fsUploadFile)
      fsUploadFile.write(upload.buf, upload.currentSize);
  } else if (upload.status == UPLOAD_FILE_END) {
    if (fsUploadFile)
      fsUploadFile.close();
  }
}

void handleFileDelete() {
  if (HTTP.args() == 0) return http500send("BAD ARGS");// HTTP.send(500, "text/plain", "BAD ARGS");
  String path = HTTP.arg(0);
  if (path == "/")
    return http500send("BAD PATH");// HTTP.send(500, "text/plain", "BAD PATH");
  if (!SPIFFS.exists(path))
    return http404send();//HTTP.send(404, "text/plain", "FileNotFound");
  SPIFFS.remove(path);
  //HTTP.send(200, "text/plain", "");
  httpOkText("");
  path = String();
}

void handleFileCreate() {
  if (HTTP.args() == 0)
    return http500send("BAD ARGS");//  HTTP.send(500, "text/plain", "BAD ARGS");
  String path = HTTP.arg(0);
  if (path == "/")
    return http500send("BAD PATH");//  HTTP.send(500, "text/plain", "BAD PATH");
  if (SPIFFS.exists(path))
    return http500send("FILE EXISTS");//  HTTP.send(500, "text/plain", "FILE EXISTS");
  File file = SPIFFS.open(path, "w");
  if (file)
    file.close();
  else
    return http500send("CREATE FAILED");// HTTP.send(500, "text/plain", "CREATE FAILED");
  //HTTP.send(200, "text/plain", "");
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


// webSocket

void initWebSocket(uint16_t addr) {
  // start webSocket server
  webSocket.begin();
  webSocket.onEvent(webSocketEvent);
}
void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
  String tmp;

  switch (type) {
    case WStype_DISCONNECTED:
      tmp = "out>serial";
      executeLine(tmp);
      break;
    case WStype_CONNECTED: {
        IPAddress ip = webSocket.remoteIP(num);
        //USE_outputStream->printf("[%u] Connected from %d.%d.%d.%d url: %s\n", num, ip[0], ip[1], ip[2], ip[3], payload);
          tmp = "out>ws";
        executeLine(tmp);
        // send message to client
        String broadcast =invitationPrint();
        webSocket.sendTXT(num, broadcast);
        flushOutputWord(0);
      }
      break;
    case WStype_TEXT:
      int numC = length;
      for (int i = 0; i < numC; i++) {
        command += char(payload[i]);
      }
      command +=" ";
      break;
  }
}

String invitationPrint() {
  printStackCompact();
  String stackSrt = "\r\n";
  stackSrt += "ok>";
  return stackSrt;
}

void sendWS(String broadcast) {
  webSocket.broadcastTXT(broadcast);
}

void h_http(uint16_t addr) {
HTTP.handleClient();
}
void h_soket(uint16_t addr) {
webSocket.loop();
}

// ---------- ФУНКЦИИ ПЕРЕНАПРАВЛЕНИЯ ----------
void outToSerialWord(uint16_t addr) {
    if (outputFile) outputFile.close();
    outputStream = &Serial;
}

void outToWsWord(uint16_t addr) {
    if (outputFile) outputFile.close();
    outputStream = &wsPrint;
    // Опционально: очистить буфер
    wsPrint.flush();
}

void flushOutputWord(uint16_t addr) {
    outputStream->flush();
}
