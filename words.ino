#include <Arduino.h>
#include <cstring> // для strlen и memcpy
#include <climits> // для INT8_MIN и т.п.
#include <cctype>  // для isdigit
#define FILESYSTEM SPIFFS
#define FORMAT_FILESYSTEM false
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
// ========================
// Конфигурация стека
// ========================
Print* jsonOutput = &Serial; // по умолчанию — Serial
File jsonFile;
constexpr size_t STACK_SIZE = 2048;
uint8_t stack[STACK_SIZE];
size_t stackTop = 0; // указатель на первую свободную ячейку
uint8_t currentContext = 0; // текущий контекст (0 = global)
int32_t maxCont = 0;
bool compiling = false;        // флаг компиляции
uint16_t compileTarget = 0;    // смещение в словаре для текущего слова
String cachedScanResult = "";
bool insideMultilineComment = false;  // Комменты
// Адрес временного слова (фиксируем при старте)
uint16_t ADDR_TMP_LIT = 0;

bool shouldJump = false;
int32_t jumpOffset = 0;
// ========================
// Настройки
// ========================
#define DICT_SIZE 2048
#define DATA_POOL_SIZE 2048

// Тип хранения (зарезервировано для будущего)
#define STORAGE_EMBEDDED 0
#define STORAGE_NAMED    1
#define STORAGE_POOLED   2
#define STORAGE_CONST 3  // константа (инициализируется один раз)
#define STORAGE_CONT 4  // для cont

// ========================
// Буферы
// ========================
uint8_t dictionary[DICT_SIZE];
uint16_t dictLen = 0;

uint8_t dataPool[DATA_POOL_SIZE];
uint16_t dataPoolPtr = 0;

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
  TYPE_ARRAY = 11
};
// Арифметика
#define OP_ADD 0
#define OP_SUB 1
#define OP_MUL 2
#define OP_DIV 3

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
void printStackCompact() {
  Serial.println("");
  Serial.print("ctx:");
  Serial.print(currentContext);
  if (stackTop == 0) {
    Serial.println(" []");
    return;
  }

  const int MAX_ELEMENTS = 256;
  struct Element {
    char prefix;
    String repr;
  };
  Element elements[MAX_ELEMENTS];
  int count = 0;

  size_t tempTop = stackTop;

  while (tempTop >= 2 && count < MAX_ELEMENTS) {
    uint8_t len = stack[tempTop - 2];
    uint8_t type = stack[tempTop - 1];

    if (len > tempTop - 2) break;

    size_t dataStart = tempTop - 2 - len;
    char prefix = '?';
    String repr;

    switch (type) {
      case TYPE_INT: {
          if (len == 4) {
            int32_t val; memcpy(&val, &stack[dataStart], 4);
            repr = String(val);
            prefix = 'I';
          }
          break;
        }
      case TYPE_FLOAT: {
          if (len == 4) {
            float val; memcpy(&val, &stack[dataStart], 4);
            repr = String(val, 6);
            prefix = 'F';
          }
          break;
        }
      case TYPE_STRING: {
          repr = "";
          for (size_t i = 0; i < len; i++) {
            char c = stack[dataStart + i];
            if (c >= 32 && c <= 126) repr += c;
            else repr += '?';
          }
          prefix = 'S';
          break;
        }
      case TYPE_BOOL: {
          if (len == 1) {
            bool val = (stack[dataStart] != 0);
            repr = val ? "true" : "false";
            prefix = 'B';
          }
          break;
        }
      case TYPE_INT8: {
          int8_t val = static_cast<int8_t>(stack[dataStart]);
          repr = String(val);
          prefix = '8';
          break;
        }
      case TYPE_UINT8: {
          uint8_t val = stack[dataStart];
          repr = String(val);
          prefix = 'U';
          break;
        }
      case TYPE_INT16: {
          int16_t val; memcpy(&val, &stack[dataStart], 2);
          repr = String(val);
          prefix = 'W';
          break;
        }
      case TYPE_UINT16: {
          uint16_t val; memcpy(&val, &stack[dataStart], 2);
          repr = String(val);
          prefix = 'w';
          break;
        }
      case TYPE_NAME: {
          repr = "";
          for (size_t i = 0; i < len; i++) {
            char c = stack[dataStart + i];
            if (c >= 32 && c <= 126) repr += c;
            else repr += '?';
          }
          prefix = 'N';
          break;
        }
              case TYPE_ARRAY: {
          if (len >= 3) {
            uint8_t elemType = stack[dataStart];
            uint16_t count = stack[dataStart + 1] | (stack[dataStart + 2] << 8);
            String typeStr;
            switch (elemType) {
              case TYPE_UINT8: typeStr = "u8"; break;
              case TYPE_INT8:  typeStr = "i8"; break;
              case TYPE_UINT16: typeStr = "u16"; break;
              case TYPE_INT16:  typeStr = "i16"; break;
              case TYPE_INT:    typeStr = "i32"; break;
              default:          typeStr = "?";
            }
            repr = typeStr + "[" + String(count) + "]";
            prefix = 'A';
          } else {
            repr = "?";
            prefix = 'A';
          }
          break;
        }
      case TYPE_MARKER: {
          repr = "";
          for (size_t i = 0; i < len; i++) {
            char c = stack[dataStart + i];
            if (c >= 32 && c <= 126) repr += c;
            else repr += '?';
          }
          prefix = 'M';
          break;
        }
    }

    if (prefix != '?') {
      elements[count].prefix = prefix;
      elements[count].repr = repr;
      count++;
    } else {
      break;
    }

    tempTop = dataStart;
  }

  Serial.print(" [");
  for (int i = count - 1; i >= 0; i--) {
    if (i < count - 1) Serial.print(' ');
    Serial.print(elements[i].prefix);
    Serial.print('(');
    Serial.print(elements[i].repr);
    Serial.print(')');
  }
  Serial.println(']');
}

void printBytes(const uint8_t* data, size_t len) {
  for (size_t i = 0; i < len; i++) {
    if (i > 0) Serial.print(" ");
    if (data[i] < 16) Serial.print("0");
    Serial.print(data[i], HEX);
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
  delay(500);

  if (!FILESYSTEM.begin()) {
    Serial.println("FS Mount Failed");
    return;
  }

  tmpLit();
  // Служебные слова (storage = 0x80)
  addInternalWord(".", printTop);
  addInternalWord("print", printTop);
  addInternalWord("char", charWord);
  addInternalWord("->", dropTop);
  addInternalWord("`", printDictionary);
  addInternalWord("words", wordsWord);
  addInternalWord("var", varWord);
  addInternalWord("const", constWord);
  addInternalWord("cont", contWord);
  addInternalWord("context", contextWord);
  addInternalWord("array", arrayFunc);
  addInternalWord("ctx", contextWord);
  addInternalWord("not", notWord);
  addInternalWord("pool", dumpDataPoolWord);
  addInternalWord("delayMicroseconds", delayMicrosecondsFunc);
  addInternalWord("dir", listFilesWord);
  addInternalWord("ls", listFilesWord);
  addInternalWord("cat", catWord);
  addInternalWord("type", catWord);
  addInternalWord("load", loadWord);
  addInternalWord("json", jsonWord);
  addInternalWord(":", colonWord);
  addInternalWord(";", semicolonWord);
  addInternalWord("body", bodyWord);
  addInternalWord("+task", addTaskWord);
  addInternalWord("-task", removeTaskWord);


  // Маркеры (storage = 0x81)
  addMarkerWord("=");
  addMarkerWord("+");
  addMarkerWord("-");
  addMarkerWord("*");
  addMarkerWord("/");
  addMarkerWord("+=");
  addMarkerWord("-=");
  addMarkerWord("*=");
  addMarkerWord("/=");

  addMarkerWord("==");
  addMarkerWord("!=");
  addMarkerWord("<");
  addMarkerWord(">");
  addMarkerWord("<=");
  addMarkerWord(">=");
  addMarkerWord("[");
  addMarkerWord("]");
  addMarkerWord("if");
  addMarkerWord("end");
addMarkerWord("{");
addMarkerWord("}");
addMarkerWord("while"); // while — маркер, как if
addInternalWord("goto", gotoFunc);
  
addInternalWord("LOW", [](uint16_t) {pushUInt8(LOW);});
addInternalWord("HIGH", [](uint16_t) {pushUInt8(HIGH);});
addInternalWord("INPUT", [](uint16_t) {pushUInt8(INPUT);});
addInternalWord("OUTPUT", [](uint16_t) {pushUInt8(OUTPUT);});
addInternalWord("INPUT_PULLUP", [](uint16_t) {pushUInt8(INPUT_PULLUP);});
addInternalWord("CR", [](uint16_t) { pushString("\r"); });
addInternalWord("LF", [](uint16_t) { pushString("\n"); });
addInternalWord("CRLF", [](uint16_t) { pushString("\r\n"); });
    
pinsInit();
i2cInit();
  addInternalWord("json>serial", jsonToSerialWord);
  addInternalWord("json>file", jsonToFile);
  wifiInit();
  currentContext = 0;
  Serial.println("Words>");
  // Выполняем инициализацию одной строкой
  String tmp = "load startup.words"; // 0 = maxCont";
  executeLine(tmp);
//  tmp = ": help";
//  executeLine(tmp);
//  tmp = "type \"help.txt\"";
//  executeLine(tmp);
//  tmp = ";";
//  executeLine(tmp);
  printStackCompact();


}

bool taskRunning = false; // ← добавь эту глобальную переменную в начало скетча

void loop() {
  uint32_t now = millis();
  for (int i = 0; i < MAX_TASKS; i++) {
    if (tasks[i].active && now - tasks[i].lastRun >= tasks[i].interval) {
      if (!taskRunning) {
        taskRunning = true;
        executeAt(tasks[i].wordAddr);
        taskRunning = false;
        tasks[i].lastRun = now;
      }
      // Если задача уже выполняется — пропускаем вызов
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
}
