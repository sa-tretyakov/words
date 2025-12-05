#include <Arduino.h>
#include <cstring> // для strlen и memcpy
#include <climits> // для INT8_MIN и т.п.
#include <cctype>  // для isdigit

#define FILESYSTEM SPIFFS
#define FORMAT_FILESYSTEM false

#if defined(ESP8266)
#if FILESYSTEM == SPIFFS
#include <FS.h>
#endif
#if FILESYSTEM == LittleFS
#include <LittleFS.h>
#endif
#if FILESYSTEM == SD
#include "SD.h"
#include "SPI.h"
#endif
#else
#if FILESYSTEM == FFat
#include <FFat.h>
#endif
#if FILESYSTEM == SPIFFS
#include <SPIFFS.h>
#endif
#if FILESYSTEM == LittleFS
#include <LittleFS.h>
#endif
#if FILESYSTEM == SD
#include "SD.h"
#include "SPI.h"
#endif
#endif

#include <WebServer.h>
WebServer HTTP(80);
File fsUploadFile;

#include <WebSocketsServer.h>    //https://github.com/Links2004/arduinoWebSockets
WebSocketsServer webSocket = WebSocketsServer(82);
// ---------- ОПРЕДЕЛЕНИЕ КЛАССА ----------
class WebSocketPrint : public Print {
public:
    WebSocketPrint(WebSocketsServer* ws) : webSocket(ws) {}

    size_t write(uint8_t c) override {
        buffer += (char)c;
        return 1;
    }

    size_t write(const uint8_t *buf, size_t size) override {
        buffer.concat((const char*)buf, size);
        return size;
    }

    void flush() override {
        if (webSocket && buffer.length() > 0) {
            webSocket->broadcastTXT(buffer.c_str(), buffer.length());
            buffer = "";
        }
    }

private:
    WebSocketsServer* webSocket;
    String buffer;
};
WebSocketPrint wsPrint(&webSocket);
String command;
// ========================
// Конфигурация стека
// ========================

File outputFile;
Print* outputStream = &Serial;
constexpr size_t STACK_SIZE = 2048;
uint8_t stack[STACK_SIZE];
size_t stackTop = 0; // указатель на первую свободную ячейку
uint8_t currentContext = 0; // текущий контекст (0 = global)
uint8_t maxCont = 255;
bool compiling = false;        // флаг компиляции
uint16_t compileTarget = 0;    // смещение в словаре для текущего слова
String cachedScanResult = "";
bool insideMultilineComment = false;  // Комменты
// Адрес временного слова (фиксируем при старте)
uint16_t ADDR_TMP_LIT = 0;

bool shouldJump = false;
int32_t jumpOffset = 0;
uint16_t addrGoto;
uint16_t addrWhile;
#define MAX_LOOP_NESTING 8

struct LoopInfo {
  uint16_t conditionStart; // позиция '{' — начало условия
  uint16_t afterWhilePos;  // ← ЭТО ПОЛЕ
  uint16_t patchPos;       // где записан младший байт смещения выхода
};

LoopInfo loopStack[MAX_LOOP_NESTING];
uint8_t loopDepth = 0;  // ← вот она

bool seetimeMode = false;     // пользователь включил замер?
bool seetimeActive = false;   // уже идёт замер (защита от вложенности)
// ========================
// Настройки
// ========================
// Тип хранения (зарезервировано для будущего)
#define STORAGE_EMBEDDED 0
#define STORAGE_NAMED    1
#define STORAGE_POOLED   2
#define STORAGE_CONST 3  // константа (инициализируется один раз)
#define STORAGE_CONT 4  // для cont

// ========================
// Буферы
// ========================
#define DICT_SIZE 32768
uint8_t dictionary[DICT_SIZE];
uint16_t dictLen = 0;

#define DATA_POOL_SIZE 32768
uint8_t dataPool[DATA_POOL_SIZE];
uint16_t dataPoolPtr = 0;

#define LOCAL_POOL_SIZE 8192
uint8_t localPool[LOCAL_POOL_SIZE];
uint16_t localPoolPtr = 0;

#define TEMP_DICT_SIZE 512
uint8_t tempDictionary[TEMP_DICT_SIZE];
uint16_t tempDictLen = 0;
// ========================
// Типы значений
// ========================
enum ValueType : uint8_t {
  TYPE_UNDEFINED = 0,
  TYPE_INT = 1,      // int32_t
  TYPE_FLOAT = 2,
  TYPE_STRING = 3,
  TYPE_BOOL = 4,
  TYPE_INT8 = 5,
  TYPE_UINT8 = 6,
  TYPE_INT16 = 7,
  TYPE_UINT16 = 8,
  TYPE_MARKER = 9,
  TYPE_NAME = 10,
  TYPE_ARRAY = 11,
  TYPE_ADDRINFO = 12
};
// Арифметика
#define OP_ADD 0
#define OP_SUB 1
#define OP_MUL 2
#define OP_DIV 3
#define OP_MOD 4  // ← остаток от деления

// Сравнения
#define CMP_EQ 0  // ==
#define CMP_NE 1  // !=
#define CMP_LT 2  // <
#define CMP_GT 3  // >
#define CMP_LE 4  // <=
#define CMP_GE 5  // >=
typedef void (*WordFunc)(uint16_t addr);

#define MAX_TASKS 8

struct Task {
  uint16_t wordAddr;    // смещение слова в словаре
  uint32_t interval;    // интервал в мс
  uint32_t lastRun;     // последнее выполнение (мс)
  bool active;          // активна ли задача
};

Task tasks[MAX_TASKS];

#define MAX_LOOP_WORDS 8
uint16_t loopWords[MAX_LOOP_WORDS];
uint8_t loopWordCount = 0;

// ========================
// Вспомогательные функции стека
// ========================
bool isStackOverflow(size_t needed) {
  return (stackTop + needed) > STACK_SIZE;
}

void handleStackOverflow() {
  while (true) {
    // Можно добавить мигание LED
  }
}

bool isStackEmpty() {
  return stackTop == 0;
}

void handleStackUnderflow() {
  while (true) {
    // Обработка ошибки
  }
}

void popMetadata(uint8_t& outLength, uint8_t& outType) {
  if (stackTop < 2) handleStackUnderflow();
  stackTop -= 2;
  outLength = stack[stackTop];
  outType   = stack[stackTop + 1];
}

// ========================
// PUSH функции
// ========================
void pushInt(int32_t value) {
  const size_t dataLen = sizeof(int32_t);
  if (isStackOverflow(dataLen + 2)) handleStackOverflow();
  memcpy(&stack[stackTop], &value, dataLen);
  stackTop += dataLen;
  stack[stackTop++] = dataLen;
  stack[stackTop++] = TYPE_INT;
}

void pushFloat(float value) {
  const size_t dataLen = sizeof(float);
  if (isStackOverflow(dataLen + 2)) handleStackOverflow();
  memcpy(&stack[stackTop], &value, dataLen);
  stackTop += dataLen;
  stack[stackTop++] = dataLen;
  stack[stackTop++] = TYPE_FLOAT;
}

void pushString(const char* str) {
  if (!str) str = "";
  size_t len = strlen(str);
  if (len > 255) len = 255;
  if (isStackOverflow(len + 2)) handleStackOverflow();
  memcpy(&stack[stackTop], str, len);
  stackTop += len;
  stack[stackTop++] = static_cast<uint8_t>(len);
  stack[stackTop++] = TYPE_STRING;
}

void pushBool(bool value) {
  if (isStackOverflow(1 + 2)) handleStackOverflow();
  stack[stackTop++] = value ? 1 : 0;
  stack[stackTop++] = 1;
  stack[stackTop++] = TYPE_BOOL;
}

void pushInt8(int8_t value) {
  if (isStackOverflow(1 + 2)) handleStackOverflow();
  stack[stackTop++] = static_cast<uint8_t>(value);
  stack[stackTop++] = 1;
  stack[stackTop++] = TYPE_INT8;
}

void pushUInt8(uint8_t value) {
  if (isStackOverflow(1 + 2)) handleStackOverflow();
  stack[stackTop++] = value;
  stack[stackTop++] = 1;
  stack[stackTop++] = TYPE_UINT8;
}

void pushInt16(int16_t value) {
  if (isStackOverflow(2 + 2)) handleStackOverflow();
  memcpy(&stack[stackTop], &value, 2);
  stackTop += 2;
  stack[stackTop++] = 2;
  stack[stackTop++] = TYPE_INT16;
}

void pushUInt16(uint16_t value) {
  if (isStackOverflow(2 + 2)) handleStackOverflow();
  memcpy(&stack[stackTop], &value, 2);
  stackTop += 2;
  stack[stackTop++] = 2;
  stack[stackTop++] = TYPE_UINT16;
}

// Проверяет, что на верхушке стека лежит маркер с заданным символом.
// Если да — убирает его и возвращает true.
// Иначе — возвращает false (стек не меняется).
bool popMarkerIf(char expected) {
  if (stackTop < 3) return false; // маркер = [data][len=1][type=MARKER]
  if (stack[stackTop - 1] != TYPE_MARKER) return false;
  if (stack[stackTop - 2] != 1) return false;
  if (stack[stackTop - 3] != (uint8_t)expected) return false;
  
  dropTop(0); // убирает маркер
  return true;
}
// Попытка извлечь любое целое число со стека и вернуть как int32_t
// Возвращает true при успехе, false — при ошибке
// Извлекает любое целое число со стека и возвращает как int32_t.
// ВСЕГДА удаляет элемент со стека.
// Возвращает true, если тип поддерживается, false — если нет (но стек очищен).
bool popInt32FromAny(int32_t* out) {
  if (stackTop < 2) {
    return false;
  }

  uint8_t len = stack[stackTop - 2];
  uint8_t type = stack[stackTop - 1];

  // Проверяем, что данных достаточно
  if (len > stackTop - 2) {
    stackTop = 0; // аварийная очистка
    *out = 0;
    return false;
  }

  // Сохраняем указатель на данные
  const uint8_t* data = &stack[stackTop - 2 - len];

  // Удаляем элемент со стека ДО обработки (чтобы избежать утечек)
  stackTop = stackTop - 2 - len;

  // Преобразуем в int32_t
  if (type == TYPE_INT && len == 4) {
    memcpy(out, data, 4);
    return true;
  }
  else if (type == TYPE_UINT8 && len == 1) {
    *out = data[0];
    return true;
  }
  else if (type == TYPE_INT8 && len == 1) {
    *out = (int8_t)data[0];
    return true;
  }
  else if (type == TYPE_UINT16 && len == 2) {
    uint16_t v; memcpy(&v, data, 2); *out = v;
    return true;
  }
  else if (type == TYPE_INT16 && len == 2) {
    int16_t v; memcpy(&v, data, 2); *out = v;
    return true;
  }

  // Неподдерживаемый тип — возвращаем 0
  *out = 0;
  return false;
}
// ========================
// POP функции (для будущего использования)
// ========================
int32_t popInt() {
  uint8_t len, type;
  popMetadata(len, type);
  if (type != TYPE_INT || len != 4) handleStackUnderflow();
  stackTop -= 4;
  int32_t value;
  memcpy(&value, &stack[stackTop], 4);
  return value;
}

float popFloat() {
  uint8_t len, type;
  popMetadata(len, type);
  if (type != TYPE_FLOAT || len != 4) handleStackUnderflow();
  stackTop -= 4;
  float value;
  memcpy(&value, &stack[stackTop], 4);
  return value;
}

const char* popString() {
  uint8_t len, type;
  popMetadata(len, type);
  if (type != TYPE_STRING) handleStackUnderflow();
  stackTop -= len;
  return reinterpret_cast<const char*>(&stack[stackTop]);
}

// Безопасно извлекает строку из стека как String
// Возвращает true при успехе, false — при ошибке
bool popStringFromStack(String& out) {
  if (stackTop < 2 || stack[stackTop - 1] != TYPE_STRING) {
    return false;
  }
  uint8_t len = stack[stackTop - 2];
  const uint8_t* data = &stack[stackTop - 2 - len];
  out = String((char*)data, len);
  dropTop(0);
  return true;
}

bool popBool() {
  uint8_t len, type;
  popMetadata(len, type);
  if (type != TYPE_BOOL || len != 1) handleStackUnderflow();
  stackTop -= 1;
  return stack[stackTop] != 0;
}

int8_t popInt8() {
  uint8_t len, type;
  popMetadata(len, type);
  if (type != TYPE_INT8 || len != 1) handleStackUnderflow();
  stackTop -= 1;
  return static_cast<int8_t>(stack[stackTop]);
}

// Пытается извлечь значение со стека и преобразовать его к uint8_t
// Поддерживает: uint8, int8, uint16, int16, int32
// Возвращает true при успехе, false — при ошибке или выходе за [0..255]
bool popAsUInt8(uint8_t* out) {
  if (stackTop < 2) return false;

  uint8_t type = stack[stackTop - 1];
  uint8_t len = stack[stackTop - 2];

  if (len > stackTop - 2) return false;
  const uint8_t* data = &stack[stackTop - 2 - len];

  int32_t val = 0;

  switch (type) {
    case TYPE_UINT8:
      if (len == 1) { val = data[0]; }
      else return false;
      break;
    case TYPE_INT8:
      if (len == 1) { val = (int8_t)data[0]; }
      else return false;
      break;
    case TYPE_UINT16:
      if (len == 2) { uint16_t v; memcpy(&v, data, 2); val = v; }
      else return false;
      break;
    case TYPE_INT16:
      if (len == 2) { int16_t v; memcpy(&v, data, 2); val = v; }
      else return false;
      break;
    case TYPE_INT:
      if (len == 4) { memcpy(&val, data, 4); }
      else return false;
      break;
    default:
      return false;
  }

  if (val < 0 || val > 255) return false;
  *out = (uint8_t)val;
  dropTop(0);
  return true;
}
uint8_t popUInt8() {
  uint8_t len, type;
  popMetadata(len, type);
  if (type != TYPE_UINT8 || len != 1) handleStackUnderflow();
  stackTop -= 1;
  return stack[stackTop];
}

int16_t popInt16() {
  uint8_t len, type;
  popMetadata(len, type);
  if (type != TYPE_INT16 || len != 2) handleStackUnderflow();
  stackTop -= 2;
  int16_t value;
  memcpy(&value, &stack[stackTop], 2);
  return value;
}

uint16_t popUInt16() {
  uint8_t len, type;
  popMetadata(len, type);
  if (type != TYPE_UINT16 || len != 2) handleStackUnderflow();
  stackTop -= 2;
  uint16_t value;
  memcpy(&value, &stack[stackTop], 2);
  return value;
}


// ========================
// Печать стека
// ========================

void printBytes(const uint8_t* data, size_t len) {
  for (size_t i = 0; i < len; i++) {
    if (i > 0) outputStream->print(" ");
    if (data[i] < 16) outputStream->print("0");
    outputStream->print(data[i], HEX);
  }
}

// ========================
// Парсинг и выполнение строки
// ========================
void lookupAndExecute(const String& word) {
  uint16_t ptr = 0;
  const char* target = word.c_str();
  uint8_t targetLen = word.length();

  while (ptr < dictLen) {
    if (ptr + 2 > DICT_SIZE) break;
    uint16_t nextPtr = dictionary[ptr] | (dictionary[ptr + 1] << 8);
    if (nextPtr == 0 || nextPtr <= ptr || nextPtr > DICT_SIZE) break;

    if (ptr + 3 > DICT_SIZE) break;
    uint8_t nameLen = dictionary[ptr + 2];
    if (nameLen == 0 || ptr + 3 + nameLen > DICT_SIZE) break;

    if (nameLen == targetLen) {
      bool match = true;
      for (uint8_t i = 0; i < nameLen; i++) {
        if (dictionary[ptr + 3 + i] != target[i]) {
          match = false;
          break;
        }
      }
      if (match) {
        executeAt(ptr);
        return;
      }
    }
    ptr = nextPtr;
  }

  //pushString(word.c_str());
  // Кладём как TYPE_NAME
  const char* str = word.c_str();
  size_t len = word.length();
  if (len > 255) len = 255;

  if (isStackOverflow(len + 2)) {
    handleStackOverflow();
    return;
  }

  memcpy(&stack[stackTop], str, len);
  stackTop += len;
  stack[stackTop++] = static_cast<uint8_t>(len);
  stack[stackTop++] = TYPE_NAME;
}


// ========================
// Setup и Loop
// ========================
void setup() {
  memset(dictionary, 0, DICT_SIZE);
  dictLen = 0;
  dataPoolPtr = 0; // ← критически важно!
  stackTop = 0;
  Serial.begin(115200);
    for (int i=0; i <= 255; i++){
  Serial.println();
   }


  if (!FILESYSTEM.begin()) {
    outputStream->println("FS Mount Failed");
    return;
  }

  tmpLit();
  addInternalWord("cont", contWord);
     String tmp = "cont main";
   executeLine(tmp);
  addInternalWord("goto", gotoFunc);
  addrGoto = findWordAddress("goto");
  addInternalWord("words", wordsWord);
  addInternalWord("`", printDictionary);
  addInternalWord("->", dropTop);
  addInternalWord(":", colonWord);
  addInternalWord("reset", resetFunc);
  addInternalWord("nop", nopFunc);
  varsInit();
  logicsInit();
  mathInit();
  ioInit();
  gpioInit();
  audioInit();
  ledsInit();
  timeInit();
  jsonInit();
  taskInit();  
  filesInit();
   debugInit();

   strInit();
   wifiInit();
   webInit(); 

   i2cInit();
   currentContext = 0;   
   tmp = "load startup.wrd"; // 0 = maxCont";
   executeLine(tmp);

   //outputStream->println("Words>");
   printStackCompact();


}

bool taskRunning = false; // ← добавь эту глобальную переменную в начало скетча

void loop() {
  uint32_t now = millis();
//  1. +loop слова (постоянное выполнение)
  for (uint8_t i = 0; i < loopWordCount; i++) {
    if (!taskRunning) {
      taskRunning = true;
      executeAt(loopWords[i]);
      taskRunning = false;
    }
  }

  // 2. Затем — задачи с таймером (+task)
  for (int i = 0; i < MAX_TASKS; i++) {
    if (tasks[i].active && now - tasks[i].lastRun >= tasks[i].interval) {
      if (!taskRunning) {
        taskRunning = true;
        executeAt(tasks[i].wordAddr);
        taskRunning = false;
        tasks[i].lastRun = now;
      }
    }
  }

  if (Serial.available()) {
    String line = Serial.readStringUntil('\n');
    line.trim();
    if (line.length() > 0) {
      Serial.print("→ ");
      Serial.println(line);
      executeLine(line);
      printStackCompact();
    }
  }
  while (command.indexOf('\n') >= 0) {
  int idx = command.indexOf('\n');
  String line = command.substring(0, idx);
  command = command.substring(idx + 1); // +1 для '\n'
  line.trim();
  if (line.length() > 0) {
    outputStream->print("→ ");
    outputStream->println(line);
    executeLine(line);
    printStackCompact();    
  }
}
}
