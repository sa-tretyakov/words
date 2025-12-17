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
  HTTP.serveStatic("/css/", SPIFFS, "/css/", "max-age=31536000"); // кеширование на 1 год
  HTTP.serveStatic("/js/", SPIFFS, "/js/", "max-age=31536000"); // кеширование на 1 год
  HTTP.serveStatic("/img/", SPIFFS, "/img/", "max-age=31536000"); // кеширование на 1 год
  //HTTP.serveStatic("/lang/", SPIFFS, "/lang/", "max-age=31536000"); // кеширование на 1 год
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

void handleFileList() {
  if (!HTTP.hasArg("dir")) {
    //HTTP.send(500, "text/plain", "BAD ARGS");
    http500send("BAD ARGS");//
    return;
  }
  String path = HTTP.arg("dir");
  //HTTP.send(200, "application/json", FileList(path));
  httpOkJson(FileList(path));
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
      }
      break;
    case WStype_TEXT:
      int numC = length;
      for (int i = 0; i < numC; i++) {
        command += char(payload[i]);
      }
      command +=" ";
       //command.replace("\r\n", "\n");
       //command.replace("\n", " ");
       //command.replace("  ", " ");
       //if (command.length()==1 && command==" ") command="";
       //outputStream->println(command);
      break;
  }
}

String invitationPrint() {
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
