void udpOpenFunc(WordEntry* self, WordEntry** body, int* ip) {
  if (dataStack.empty()) {
    Serial.println("udpOpen: port expected");
    return;
  }
  WordEntry* portWord = dataStack.back(); dataStack.pop_back();
  if (portWord->type != INT) {
    Serial.println("udpOpen: port must be int");
    return;
  }
  uint16_t port = (uint16_t)*(int32_t*)portWord->value;

  // Ищем свободный сокет
  for (int i = 0; i < MAX_UDP_SOCKETS; i++) {
    if (!udpInUse[i]) {
      if (udpSockets[i].begin(port)) {
        udpInUse[i] = true;
        // Возвращаем хэндл (i+1, чтобы 0 не был "ложным")
        WordEntry* handle = getOrCreateLiteral(String(i + 1));
        if (handle) dataStack.push_back(handle);
        Serial.println("UDP socket " + String(i) + " opened on port " + String(port));
        return;
      } else {
        Serial.println("udpOpen: failed to bind port " + String(port));
        break;
      }
    }
  }
  // Нет свободных сокетов или ошибка
  WordEntry* nullHandle = getOrCreateLiteral("0");
  if (nullHandle) dataStack.push_back(nullHandle);
}
void udpSendFunc(WordEntry* self, WordEntry** body, int* ip) {
  if (dataStack.size() < 4) {
    Serial.println("udpSend: handle host port message");
    return;
  }
  WordEntry* msg = dataStack.back(); dataStack.pop_back();
  WordEntry* port = dataStack.back(); dataStack.pop_back();
  WordEntry* host = dataStack.back(); dataStack.pop_back();
  WordEntry* handle = dataStack.back(); dataStack.pop_back();

  if (handle->type != INT || host->type != STRING || 
      port->type != INT || msg->type != STRING) {
    Serial.println("udpSend: invalid args");
    return;
  }

  int h = *(int32_t*)handle->value - 1;
  if (h < 0 || h >= MAX_UDP_SOCKETS || !udpInUse[h]) {
    Serial.println("udpSend: invalid handle");
    return;
  }

  const char* hostStr = (char*)host->value;
  uint16_t portNum = (uint16_t)*(int32_t*)port->value;
  const char* msgStr = (char*)msg->value;

  udpSockets[h].beginPacket(hostStr, portNum);
  udpSockets[h].print(msgStr);  // ✅ print принимает const char*
  udpSockets[h].endPacket();
}
void udpRecvFunc(WordEntry* self, WordEntry** body, int* ip) {
  if (dataStack.empty()) {
    Serial.println("udpRecv: handle expected");
    return;
  }
  WordEntry* handle = dataStack.back(); dataStack.pop_back();
  if (handle->type != INT) {
    Serial.println("udpRecv: handle must be int");
    return;
  }

  int h = *(int32_t*)handle->value - 1;
  if (h < 0 || h >= MAX_UDP_SOCKETS || !udpInUse[h]) {
    dataStack.push_back(getOrCreateLiteral("\"\""));
    return;
  }

  int size = udpSockets[h].parsePacket();
  if (size > 0) {
    char buf[256];
    int len = udpSockets[h].read(buf, 255);
    buf[len > 0 ? len : 0] = '\0';
    WordEntry* lit = getOrCreateLiteral("\"" + String(buf) + "\"");
    if (lit) { dataStack.push_back(lit); return; }
  }
  dataStack.push_back(getOrCreateLiteral("\"\""));
}
void udpCloseFunc(WordEntry* self, WordEntry** body, int* ip) {
  if (dataStack.empty()) return;
  WordEntry* handle = dataStack.back(); dataStack.pop_back();
  if (handle->type != INT) return;

  int h = *(int32_t*)handle->value - 1;
  if (h >= 0 && h < MAX_UDP_SOCKETS && udpInUse[h]) {
    udpSockets[h].stop();
    udpInUse[h] = false;
    Serial.println("UDP socket " + String(h) + " closed");
  }
}
void udpSendRawFunc(WordEntry* self, WordEntry** body, int* ip) {
  if (dataStack.size() < 3) {
    Serial.println("udpSendRaw: host port message");
    return;
  }
  WordEntry* host = dataStack.back(); dataStack.pop_back();
  WordEntry* port = dataStack.back(); dataStack.pop_back();
  WordEntry* msg = dataStack.back(); dataStack.pop_back();
  
  

  if (host->type != STRING || port->type != INT || msg->type != STRING) {
    Serial.println("udpSendRaw: invalid args");
    return;
  }

  static WiFiUDP udp; // один раз для всех отправок
  udp.beginPacket((char*)host->value, *(int32_t*)port->value);
  udp.print((char*)msg->value);
  udp.endPacket();
  Serial.println("UDP raw sent");
}
