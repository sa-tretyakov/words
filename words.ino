#include <Arduino.h>
#include <map>
#include <vector>
#include <cstring>
#ifdef LittleFSM // #endif
#include <LittleFS.h>
#else
#include <SPIFFS.h>
#endif
#include <WiFi.h>  // для ESP32
// #include <ESP8266WiFi.h>  // раскомментировать, если ESP8266
// ========================
// Типы данных
// ========================
enum Type {
  UNDEFINED,
  INT,
  FLOAT,
  STRING,
  BOOL,
  INT8,
  UINT8,
  INT16,
  UINT16
};
enum OpType {
  OP_NONE,
  OP_EQ,      // ==
  OP_NE,      // !=
  OP_LT,      // <
  OP_GT,      // >
  OP_LE,      // <=
  OP_GE,      // >=
  OP_ADD,     // +
  OP_SUB,     // -
  OP_MUL,     // *
  OP_DIV      // /
};

// ========================
// Структура слова — БЕЗ self
// ========================
struct WordEntry {
  Type type = UNDEFINED;
  void* value = nullptr;
  void* address = nullptr;
  int argc = 0;
  bool readonly = false;
  bool isInternal = false;
  uint8_t contextId = 0;
};

// ========================
// Глобальные структуры
// ========================
std::map<String, WordEntry> dictionary;
//WordEntry* &dictionary["mychoice"]; = nullptr;
std::vector<WordEntry*> dataStack;
std::vector<WordEntry*> executingStack;
struct TickTask {
  String wordName;      // имя слова для вызова
  uint32_t intervalMs;  // интервал в миллисекундах
  uint32_t lastRunMs;   // время последнего запуска
  bool active;          // активна ли задача
};
std::vector<TickTask> tickTasks;

uint8_t currentContextId = 0;
const uint8_t LITERAL_CONTEXT = 255;
uint8_t nextContextId = 1;
int currentSourceLine = 0;
bool inMultilineComment = false;
String jsonBuffer = "";
bool jsonMultiline = false;
const int MAX_UDP_SOCKETS = 4;
WiFiUDP udpSockets[MAX_UDP_SOCKETS];
bool udpInUse[MAX_UDP_SOCKETS] = {false};
std::map<String, uint8_t> contextNameToId;
bool compiling = false;
String compilingName = "";
std::vector<WordEntry*> compilingBody;

// ========================
// Вспомогательные функции
// ========================
uint32_t simpleHash(const char* str) {
  uint32_t hash = 5381;
  int c;
  while ((c = *str++)) {
    hash = ((hash << 5) + hash) + c;
  }
  return hash;
}
bool isFloat(const String& s) {
  if (s.length() == 0) return false;
  size_t start = 0;
  // Поддержка знака
  if (s[0] == '-' || s[0] == '+') {
    if (s.length() < 4) return false; // минимум: +1.1 → 4 символа
    start = 1;
  }

  // Найдём точку
  int dotPos = -1;
  for (size_t i = start; i < s.length(); i++) {
    if (s[i] == '.') {
      if (dotPos != -1) return false; // две точки
      dotPos = i;
    } else if (!isdigit((unsigned char)s[i])) {
      return false; // недопустимый символ
    }
  }

  // Должна быть ровно одна точка
  if (dotPos == -1) return false;

  // Точка не должна быть первой или последней
  if (dotPos == (int)start) return false;          // например: ".5" или "+.5"
  if (dotPos == (int)s.length() - 1) return false; // например: "5." или "-5."

  return true;
}
bool isInteger(const String& s) {
  if (s.length() == 0) return false;
  size_t start = 0;
  if (s[0] == '-') {
    if (s.length() == 1) return false;
    start = 1;
  }
  for (size_t i = start; i < s.length(); i++) {
    if (!isdigit((unsigned char)s[i])) return false;
  }
  return true;
}

std::vector<String> tokenize(const String& line) {
  std::vector<String> tokens;
  String current;
  bool inQuotes = false;
  for (int i = 0; i < (int)line.length(); i++) {
    char c = line[i];
    if (c == '"' && (i == 0 || line[i - 1] != '\\')) {
      inQuotes = !inQuotes;
      current += c;
    } else if (c == ' ' && !inQuotes) {
      current.trim();
      if (current.length() > 0) {
        tokens.push_back(current);
        current = "";
      }
    } else {
      current += c;
    }
  }
  current.trim();
  if (current.length() > 0) {
    tokens.push_back(current);
  }
  return tokens;
}

WordEntry* getOrCreateLiteral(const String& token) {
  String name;
  bool isTokenFloat = false;
  bool isTokenInteger = false;

  if (token.length() >= 2 && token[0] == '"' && token[token.length() - 1] == '"') {
    String content = token.substring(1, token.length() - 1);
    uint32_t hash = simpleHash(content.c_str());
    name = "lit_" + String(hash, HEX);
  } else if (isFloat(token)) {
    name = token;
    isTokenFloat = true;
  } else if (isInteger(token)) {
    name = token;
    isTokenInteger = true;
  } else {
    return nullptr;
  }

  auto it = dictionary.find(name);
  if (it != dictionary.end()) {
    return &it->second;
  }

  WordEntry entry;
  entry.contextId = LITERAL_CONTEXT;
  entry.readonly = true;
  entry.isInternal = false;

  WordEntry** body = (WordEntry**)malloc(3 * sizeof(WordEntry*));
  body[0] = &dictionary["mychoice"];
  body[1] = &dictionary["pushSelf"]; // ← теперь отдельно
  body[2] = nullptr;
  entry.address = (void*)body;

  if (token[0] == '"') {
    String content = token.substring(1, token.length() - 1);
    entry.type = STRING;
    entry.value = strdup(content.c_str());
  } else if (isTokenFloat) { // ← ИСПОЛЬЗУЕМ СОХРАНЁННЫЙ РЕЗУЛЬТАТ
    entry.type = FLOAT;
    entry.value = new float(token.toFloat());
  } else if (isTokenInteger) { // ← ИСПОЛЬЗУЕМ СОХРАНЁННЫЙ РЕЗУЛЬТАТ
    entry.type = INT;
    entry.value = new int32_t(token.toInt());
  } else {
    // На практике сюда не должно попадать
    return nullptr;
  }

  dictionary[name] = entry;
  return &dictionary[name];
}

void parseJsonAndCreateVars(const String& jsonStr) {
  String content = jsonStr.substring(1, jsonStr.length() - 1);
  content.trim();
  if (content.length() == 0) return;

  int start = 0;
  while (start < (int)content.length()) {
    while (start < (int)content.length() && content[start] == ' ') start++;
    if (start >= (int)content.length()) break;
    if (content[start] != '"') {
      Serial.println("JSON: expected quoted key");
      return;
    }
    int keyEnd = content.indexOf('"', start + 1);
    if (keyEnd == -1) {
      Serial.println("JSON: unterminated key");
      return;
    }
    String key = content.substring(start + 1, keyEnd);
    int colon = content.indexOf(':', keyEnd + 1);
    if (colon == -1) {
      Serial.println("JSON: missing colon");
      return;
    }
    int valueStart = colon + 1;
    while (valueStart < (int)content.length() && content[valueStart] == ' ') valueStart++;
    String valueStr;
    bool isString = false;
    int valueEnd = -1;
    if (content[valueStart] == '"') {
      isString = true;
      valueEnd = valueStart;
      bool escaped = false;
      for (int i = valueStart + 1; i < (int)content.length(); i++) {
        if (content[i] == '\\' && !escaped) {
          escaped = true;
        } else if (content[i] == '"' && !escaped) {
          valueEnd = i;
          break;
        } else {
          escaped = false;
        }
      }
      if (valueEnd == -1) {
        Serial.println("JSON: unterminated string value");
        return;
      }
      valueStr = "\"" + content.substring(valueStart + 1, valueEnd) + "\"";
    } else {
      valueEnd = valueStart;
      while (valueEnd < (int)content.length()) {
        char c = content[valueEnd];
        if (c == ',' || c == '}' || c == ' ') break;
        valueEnd++;
      }
      valueStr = content.substring(valueStart, valueEnd);
    }
    WordEntry* lit = getOrCreateLiteral(valueStr);
    if (!lit) {
      Serial.println("JSON: failed to create literal for: " + valueStr);
      return;
    }
    createVariable(key, lit, false);
    int comma = content.indexOf(',', valueEnd + 1);
    if (comma == -1) break;
    start = comma + 1;
  }
  Serial.println("JSON parsed: created variables in context " + String(currentContextId));
}

OpType getOpType(WordEntry* opWord) {
  if (!opWord) return OP_NONE;
  // ← ЗАМЕНА: .self → &dictionary[...]
  if (opWord == &dictionary["=="]) return OP_EQ;
  if (opWord == &dictionary["!="]) return OP_NE;
  if (opWord == &dictionary["<"]) return OP_LT;
  if (opWord == &dictionary[">"]) return OP_GT;
  if (opWord == &dictionary["<="]) return OP_LE;
  if (opWord == &dictionary[">="]) return OP_GE;
  if (opWord == &dictionary["+"]) return OP_ADD;
  if (opWord == &dictionary["-"]) return OP_SUB;
  if (opWord == &dictionary["*"]) return OP_MUL;
  if (opWord == &dictionary["/"]) return OP_DIV;
  return OP_NONE;
}

// Возвращает true при успехе, кладёт результат ("1"/"0") на стек
bool handleComparison(WordEntry* left, WordEntry* right, OpType op) {
  if (!left || !right) return false;

  bool result = false;
  bool supported = false;

  if (left->type == STRING && right->type == STRING) {
    int cmp = strcmp((char*)left->value, (char*)right->value);
    switch (op) {
      case OP_EQ: result = (cmp == 0); break;
      case OP_NE: result = (cmp != 0); break;
      case OP_LT: result = (cmp < 0); break;
      case OP_GT: result = (cmp > 0); break;
      case OP_LE: result = (cmp <= 0); break;
      case OP_GE: result = (cmp >= 0); break;
      default: return false;
    }
    supported = true;
  }
  else if ((left->type == INT || left->type == FLOAT) &&
           (right->type == INT || right->type == FLOAT)) {
    float lv = (left->type == FLOAT) ? *(float*)left->value : *(int32_t*)left->value;
    float rv = (right->type == FLOAT) ? *(float*)right->value : *(int32_t*)right->value;
    switch (op) {
      case OP_EQ: result = (lv == rv); break;
      case OP_NE: result = (lv != rv); break;
      case OP_LT: result = (lv < rv); break;
      case OP_GT: result = (lv > rv); break;
      case OP_LE: result = (lv <= rv); break;
      case OP_GE: result = (lv >= rv); break;
      default: return false;
    }
    supported = true;
  }

  if (!supported) return false;

  WordEntry* lit = getOrCreateLiteral(result ? "1" : "0");
  if (lit) {
    dataStack.push_back(lit);
    return true;
  }
  return false;
}

// Возвращает true при успехе, кладёт литерал-результат на стек
bool handleArithmetic(WordEntry* left, WordEntry* right, OpType op) {
  if (!left || !right) return false;

  // Сложение строк
  if (op == OP_ADD && left->type == STRING && right->type == STRING) {
    String leftStr = String((char*)left->value);
    String rightStr = String((char*)right->value);
    String result = leftStr + rightStr;
    String token = "\"" + result + "\"";
    WordEntry* lit = getOrCreateLiteral(token);
    if (lit) {
      dataStack.push_back(lit);
      return true;
    }
    return false;
  }

  // Числовая арифметика (как было)
  if ((left->type != INT && left->type != FLOAT) ||
      (right->type != INT && right->type != FLOAT)) {
    return false;
  }

  float lv = (left->type == FLOAT) ? *(float*)left->value : *(int32_t*)left->value;
  float rv = (right->type == FLOAT) ? *(float*)right->value : *(int32_t*)right->value;
  float res = 0;

  switch (op) {
    case OP_ADD: res = lv + rv; break;
    case OP_SUB: res = lv - rv; break;
    case OP_MUL: res = lv * rv; break;
    case OP_DIV:
      if (rv == 0) {
        Serial.println("Arithmetic: division by zero");
        return false;
      }
      res = lv / rv;
      break;
    default: return false;
  }

  bool isInt = (res == (int32_t)res) && (left->type == INT && right->type == INT);
  String token = isInt ? String((int32_t)res) : String(res);
  WordEntry* lit = getOrCreateLiteral(token);
  if (lit) {
    dataStack.push_back(lit);
    return true;
  }
  return false;
}
String getCurrentContextName() {
  if (currentContextId == 0) {
    return "0";
  }
  if (currentContextId == LITERAL_CONTEXT) {
    return "literals";
  }
  // Ищем имя по ID
  for (const auto& pair : contextNameToId) {
    if (pair.second == currentContextId) {
      return pair.first;
    }
  }
  return String(currentContextId);
}
// ========================
// Internal-функции
// ========================
void mychoiceFunc(WordEntry* self, WordEntry** body, int* ip) {
  if (executingStack.empty()) return;
  WordEntry* currentWord = executingStack.back();

  if (!dataStack.empty()) {
    WordEntry* opWord = dataStack.back();

    // === 1. Присваивание "=" ===
    if (opWord == &dictionary["="]) {
      if (dataStack.size() < 2) {
        Serial.println("=: missing source operand");
        dataStack.push_back(currentWord);
        return;
      }
      dataStack.pop_back(); // убираем "="
      WordEntry* source = dataStack.back(); dataStack.pop_back();

      if (!source) {
        Serial.println("=: cannot assign from undefined word");
        dataStack.push_back(currentWord);
        return;
      }
      if (currentWord->readonly) {
        Serial.println("=: cannot assign to readonly word");
        dataStack.push_back(currentWord);
        return;
      }

      // Освобождаем старое значение
      if (currentWord->value) {
        if (currentWord->type == STRING) free(currentWord->value);
        else if (currentWord->type == INT) delete (int32_t*)currentWord->value;
        else if (currentWord->type == FLOAT) delete (float*)currentWord->value;
        else if (currentWord->type == UINT8) delete (uint8_t*)currentWord->value;
        else if (currentWord->type == BOOL) delete (bool*)currentWord->value;
      }

      // Копируем новое значение
      currentWord->type = source->type;
      if (source->type == STRING) {
        currentWord->value = strdup((char*)source->value);
      } else if (source->type == INT) {
        currentWord->value = new int32_t(*(int32_t*)source->value);
      } else if (source->type == FLOAT) {
        currentWord->value = new float(*(float*)source->value);
      } else if (source->type == UINT8) {
        currentWord->value = new uint8_t(*(uint8_t*)source->value);
      } else if (source->type == BOOL) {
        currentWord->value = new bool(*(bool*)source->value);
      } else {
        currentWord->value = nullptr;
      }

      // Успешное присваивание — НЕ возвращаем значение
      return;
    }

    // === 2. Логическое "not" ===
    if (opWord == &dictionary["not"]) {
      dataStack.pop_back(); // убираем "not"
      if (currentWord->type == INT || currentWord->type == BOOL) {
        int32_t val = (currentWord->type == INT)
                      ? *(int32_t*)currentWord->value
                      : (*(bool*)currentWord->value ? 1 : 0);
        val = (val == 0) ? 1 : 0;
        if (currentWord->type == INT) {
          *(int32_t*)currentWord->value = val;
        } else {
          delete (bool*)currentWord->value;
          currentWord->value = new int32_t(val);
          currentWord->type = INT;
        }
      }
      // Результат "not" — это изменённое слово, кладём его на стек
      dataStack.push_back(currentWord);
      return;
    }

    // === 3. Арифметика и сравнения ===
    OpType op = getOpType(opWord);
    if (op != OP_NONE) {
      if (dataStack.size() < 2) {
        Serial.println("Error: missing operand");
        dataStack.push_back(currentWord);
        return;
      }
      dataStack.pop_back();
      WordEntry* right = dataStack.back(); dataStack.pop_back();

      bool success = false;
      if (op == OP_EQ) {
        success = handleComparison(currentWord, right, OP_EQ);
      } else if (op >= OP_NE && op <= OP_GE) {
        success = handleComparison(currentWord, right, op);
      } else if (op >= OP_ADD && op <= OP_DIV) {
        success = handleArithmetic(currentWord, right, op);
      }

      if (success) {
        return; // результат уже на стеке
      }

      Serial.println("Operation failed: unsupported types or error");
      dataStack.push_back(currentWord);
      return;
    }
  }

  // ВАЖНО: НЕТ автоматического push currentWord!
  // Только слова с pushSelf в теле кладут себя на стек.
}
void pushSelfEntry(WordEntry* self, WordEntry** body, int* ip) {
  if (executingStack.empty()) return;
  WordEntry* currentWord = executingStack.back();
  dataStack.push_back(currentWord);
}

void printFunc(WordEntry* self, WordEntry** body, int* ip) {
  if (dataStack.empty()) {
    Serial.println(".: stack empty");
    return;
  }
  WordEntry* word = dataStack.back(); dataStack.pop_back();
  if (word == nullptr) {
    Serial.println("<undefined>");
    return;
  }
  switch (word->type) {
    case STRING: Serial.println((char*)word->value); break;
    case INT: Serial.println(*(int32_t*)word->value); break;
    case FLOAT: Serial.println(*(float*)word->value); break;
    case UINT8: Serial.println(*(uint8_t*)word->value); break;
    default: Serial.println("<unknown>");
  }
}

String stackToString() {
  if (dataStack.empty()) {
    return "[]";
  }
  String s = "[";
  for (size_t i = 0; i < dataStack.size(); i++) {
    WordEntry* w = dataStack[i];
    if (w == nullptr) {
      s += "<undef>";
    } else {
      switch (w->type) {
        case INT: s += String(*(int32_t*)w->value); break;
        case FLOAT: s += String(*(float*)w->value); break;
        case STRING: s += "\"" + String((char*)w->value) + "\""; break;
        case UINT8: s += String(*(uint8_t*)w->value); break;
        default: {
            bool found = false;
            for (const auto& pair : dictionary) {
              if (&pair.second == w) { // ← это по-прежнему работает!
                s += pair.first;
                found = true;
                break;
              }
            }
            if (!found) s += "?";
          }
      }
    }
    if (i < dataStack.size() - 1) s += " ";
  }
  s += "]";
  return s;
}

void wordsFunc(WordEntry* self, WordEntry** body, int* ip) {
  Serial.println("\nWords in context " + String(currentContextId) + ":");

  std::vector<std::pair<String, const WordEntry*> > words;
  for (const auto& pair : dictionary) {
    if (pair.second.contextId == currentContextId) {
      words.push_back(std::make_pair(pair.first, &pair.second));
    }
  }

  if (words.empty()) {
    Serial.println("  <no words>");
    Serial.println();
    return;
  }

  std::sort(words.begin(), words.end(),
  [](const std::pair<String, const WordEntry*>& a, const std::pair<String, const WordEntry*>& b) {
    return a.first < b.first;
  }
           );

  // Находим максимальную длину имени для выравнивания
  size_t maxNameLen = 4;
  for (size_t i = 0; i < words.size(); i++) {
    if (words[i].first.length() > maxNameLen) {
      maxNameLen = words[i].first.length();
    }
  }

  for (size_t i = 0; i < words.size(); i++) {
    const String& name = words[i].first;
    const WordEntry* entry = words[i].second;

    // Атрибут в самом начале: * или R или пробел
    char attr = ' ';
    if (entry->isInternal) {
      attr = '*';
    } else if (entry->readonly) {
      attr = 'R';
    }
    Serial.print("  ");
    Serial.print(attr);
    Serial.print(" ");

    // Имя
    Serial.print(name);
    for (size_t j = name.length(); j < maxNameLen; j++) {
      Serial.print(" ");
    }

    // Тип
    String typeStr;
    if (entry->type == UNDEFINED) {
      typeStr = "<>";
    } else {
      switch (entry->type) {
        case INT:       typeStr = "int"; break;
        case FLOAT:     typeStr = "float"; break;
        case STRING:    typeStr = "str"; break;
        case UINT8:     typeStr = "uint8"; break;
        case BOOL:      typeStr = "bool"; break;
        case INT8:      typeStr = "int8"; break;
        case INT16:     typeStr = "int16"; break;
        case UINT16:    typeStr = "uint16"; break;
        default:        typeStr = "<?>";
      }
    }
    Serial.print("  ");
    Serial.print(typeStr);
    for (int j = typeStr.length(); j < 7; j++) Serial.print(" ");

    // Значение в [ ] — без ограничения длины!
    Serial.print("  [");
    if (entry->value != nullptr) {
      switch (entry->type) {
        case STRING:
          Serial.print((char*)entry->value);
          break;
        case INT:
          Serial.print(*(int32_t*)entry->value);
          break;
        case FLOAT:
          Serial.print(*(float*)entry->value, 6); // 6 знаков после запятой
          break;
        case UINT8:
          Serial.print(*(uint8_t*)entry->value);
          break;
        case BOOL:
          Serial.print(*(bool*)entry->value ? "true" : "false");
          break;
        default:
          Serial.print("?");
      }
    }
    Serial.println("]");
  }
  Serial.println();
}

void pinModeFunc(WordEntry* self, WordEntry** body, int* ip) {
  if (dataStack.size() < 2) {
    Serial.println("pinMode: expected 2 arguments (pin mode)");
    return;
  }
  WordEntry* pinWord = dataStack.back(); dataStack.pop_back();
  WordEntry* modeWord = dataStack.back(); dataStack.pop_back(); // ← порядок исправлен!

  if (pinWord->type != INT || modeWord->type != INT) {
    Serial.println("pinMode: pin and mode must be INT");
    return;
  }
  pinMode(*(int32_t*)pinWord->value, *(int32_t*)modeWord->value);
}

void digitalWriteFunc(WordEntry* self, WordEntry** body, int* ip) {
  if (dataStack.size() < 2) {
    Serial.println("digitalWrite: expected 2 arguments (pin value)");
    return;
  }
  WordEntry* pinWord = dataStack.back(); dataStack.pop_back();
  WordEntry* valueWord = dataStack.back(); dataStack.pop_back(); // ← порядок исправлен!

  if (pinWord->type != INT || valueWord->type != INT) {
    Serial.println("digitalWrite: pin and value must be INT");
    return;
  }
  digitalWrite(*(int32_t*)pinWord->value, *(int32_t*)valueWord->value);
}
void analogWriteFunc(WordEntry* self, WordEntry** body, int* ip) {
  if (dataStack.size() < 2) {
    Serial.println("analogWrite: expected 2 arguments (pin value)");
    return;
  }
  WordEntry* pinWord = dataStack.back(); dataStack.pop_back();
  WordEntry* valueWord = dataStack.back(); dataStack.pop_back();

  if (pinWord->type != INT || valueWord->type != INT) {
    Serial.println("analogWrite: pin and value must be INT");
    return;
  }
  analogWrite(*(int32_t*)pinWord->value, *(int32_t*)valueWord->value);
}

void globalFunc(WordEntry* self, WordEntry** body, int* ip) {
  currentContextId = 0;
  Serial.println("Switched to global context");
}

void useFunc(WordEntry* self, WordEntry** body, int* ip) {
  if (dataStack.empty()) {
    Serial.println("use: stack empty");
    return;
  }
  WordEntry* ctxWord = dataStack.back(); dataStack.pop_back();
  if (ctxWord->type != UINT8) {
    Serial.println("use: expected context word (UINT8)");
    return;
  }
  currentContextId = *(uint8_t*)ctxWord->value;
  Serial.println("Switched to context ID " + String(currentContextId));
}

void bodyFunc(WordEntry* self, WordEntry** body, int* ip) {
  if (dataStack.empty()) {
    Serial.println("body: stack empty");
    return;
  }
  WordEntry* target = dataStack.back(); dataStack.pop_back();
  String name = "unknown";
  for (const auto& pair : dictionary) {
    if (&pair.second == target) {
      name = pair.first;
      break;
    }
  }
  if (target->isInternal) {
    Serial.println("'" + name + "' is internal");
  } else {
    Serial.print("Body of '" + name + "': [ ");
    WordEntry** bodyPtr = (WordEntry**)target->address;
    if (bodyPtr) {
      for (int i = 0; bodyPtr[i] != nullptr; i++) {
        WordEntry* w = bodyPtr[i];
        String wordName;
        bool found = false;
        for (const auto& pair : dictionary) {
          if (&pair.second == w) {
            wordName = pair.first;
            found = true;
            break;
          }
        }
        if (!found) {
          switch (w->type) {
            case STRING: wordName = "\"" + String((char*)w->value) + "\""; break;
            case INT: wordName = String(*(int32_t*)w->value); break;
            case UINT8: wordName = String(*(uint8_t*)w->value); break;
            case FLOAT: wordName = String(*(float*)w->value); break;
            default: wordName = "<lit>";
          }
        }
        if (i > 0) Serial.print(", ");
        Serial.print(wordName);
      }
    }
    Serial.println(" ]");
  }
}

void buildJsonFunc(WordEntry* self, WordEntry** body, int* ip) {
  auto it = dictionary.find("json");
  if (it == dictionary.end()) return;
  WordEntry* jsonWord = &it->second;
  String jsonStr = "{";
  bool first = true;
  for (const auto& pair : dictionary) {
    const WordEntry& entry = pair.second;
    if (entry.contextId == currentContextId &&
        entry.type != UNDEFINED &&
        entry.value != nullptr &&
        !entry.isInternal) {
      if (!first) jsonStr += ",";
      first = false;
      jsonStr += "\"" + pair.first + "\":";
      switch (entry.type) {
        case STRING: {
            String val = String((char*)entry.value);
            val.replace("\"", "\\\"");
            jsonStr += "\"" + val + "\"";
            break;
          }
        case INT: jsonStr += String(*(int32_t*)entry.value); break;
        case UINT8: jsonStr += String(*(uint8_t*)entry.value); break;
        case FLOAT: jsonStr += String(*(float*)entry.value); break;
        default: jsonStr += "null";
      }
    }
  }
  jsonStr += "}";
  if (jsonWord->value) free(jsonWord->value);
  jsonWord->value = strdup(jsonStr.c_str());
  jsonWord->type = STRING;
}

void sourceFunc(WordEntry* self, WordEntry** body, int* ip) {
  if (dataStack.empty()) {
    Serial.println("source: need filename");
    return;
  }
  WordEntry* fnameWord = dataStack.back(); dataStack.pop_back();
  if (fnameWord->type != STRING) {
    Serial.println("source: filename must be string");
    return;
  }
  String filename = String((char*)fnameWord->value);
#ifdef LittleFSM // #endif
  File file = LittleFS.open("/" + filename, "r");
#else
  File file = SPIFFS.open("/" + filename, "r");
#endif
  if (!file) {
    Serial.println("File not found: " + filename);
    return;
  }
  currentSourceLine = 1;
  while (file.available()) {
    String line = file.readStringUntil('\n');
    line.trim();
    if (line.length() > 0) {
      executeLine(line);
    }
    currentSourceLine++;
  }
  file.close();
  currentSourceLine = 0;
  Serial.println("Executed: " + filename);
}

void notInPlaceFunc(WordEntry* self, WordEntry** body, int* ip) {
  if (dataStack.empty()) {
    Serial.println("!: stack empty");
    return;
  }
  WordEntry* word = dataStack.back(); dataStack.pop_back();
  if (word->type == INT) {
    *(int32_t*)word->value = (*(int32_t*)word->value == 0) ? 1 : 0;
  }
}

void ifFunc(WordEntry* self, WordEntry** body, int* ip) {
  if (dataStack.empty()) {
    Serial.println("if: stack empty");
    return;
  }
  WordEntry* cond = dataStack.back(); dataStack.pop_back();
  if (cond->type != INT) {
    Serial.println("if: condition must be INT");
    return;
  }
  int32_t value = *(int32_t*)cond->value;
  int thenPos = -1, elsePos = -1;
  int i = *ip + 1;
  while (body[i] != nullptr) {
    if (body[i] == &dictionary["then:"] && thenPos == -1) thenPos = i;
    else if (body[i] == &dictionary["else:"] && elsePos == -1) elsePos = i;
    i++;
  }
  if (thenPos == -1) {
    Serial.println("if: missing then:");
    return;
  }
  if (value != 0) {
    *ip = thenPos;
  } else {
    if (elsePos == -1) {
      Serial.println("if: missing else:");
      return;
    }
    *ip = elsePos;
  }
}

void jumpToIfFunc(WordEntry* self, WordEntry** body, int* ip) {
  int i = *ip + 1;
  while (body[i] != nullptr) {
    if (body[i] == &dictionary[":if"]) {
      *ip = i;
      return;
    }
    i++;
  }
}

void nopFunc(WordEntry* self, WordEntry** body, int* ip) {}

// ========================
// Выполнение external-слова
// ========================
void executeExternal(WordEntry* word) {
  executingStack.push_back(word);
  WordEntry** body = (WordEntry**)word->address;
  if (body) {
    int i = 0;
    while (body[i] != nullptr) {
      WordEntry* w = body[i];
      if (w->isInternal) {
        ((void(*)(WordEntry*, WordEntry**, int*))w->address)(w, body, &i);
      } else {
        executeExternal(w);
      }
      i++;
    }
  }
  executingStack.pop_back();
}

// ========================
// Создание переменной
// ========================
void createVariable(const String& name, WordEntry* valueEntry, bool isConst) {
  auto it = dictionary.find(name);
  if (it != dictionary.end()) {
    WordEntry& old = it->second;
    if (old.value) {
      if (old.type == STRING) free(old.value);
      else if (old.type == INT) delete (int32_t*)old.value;
      else if (old.type == FLOAT) delete (float*)old.value;
      else if (old.type == UINT8) delete (uint8_t*)old.value;
    }
    if (!old.isInternal && old.address) {
      free(old.address);
    }
  }

  WordEntry** body = (WordEntry**)malloc(3 * sizeof(WordEntry*));
  body[0] = &dictionary["mychoice"];
  body[1] = &dictionary["pushSelf"]; // ← теперь отдельно
  body[2] = nullptr;

  WordEntry entry;
  entry.contextId = currentContextId;
  entry.type = valueEntry->type;
  entry.readonly = isConst;
  entry.isInternal = false;
  entry.address = (void*)body;

  if (valueEntry->type == STRING) {
    entry.value = strdup((char*)valueEntry->value);
  } else if (valueEntry->type == INT) {
    entry.value = new int32_t(*(int32_t*)valueEntry->value);
  } else if (valueEntry->type == FLOAT) {
    entry.value = new float(*(float*)valueEntry->value);
  } else if (valueEntry->type == UINT8) {
    entry.value = new uint8_t(*(uint8_t*)valueEntry->value);
  } else {
    entry.value = nullptr;
  }

  dictionary[name] = entry;
  // ← УБРАНО: .self = ...
}

// ========================
// Контексты
// ========================
void createContext(const String& name) {
  uint8_t ctxId;
  if (contextNameToId.count(name)) {
    ctxId = contextNameToId[name];
  } else {
    ctxId = nextContextId++;
    contextNameToId[name] = ctxId;
    WordEntry** body = (WordEntry**)malloc(2 * sizeof(WordEntry*));
    body[0] = &dictionary["mychoice"];
    body[1] = &dictionary["pushSelf"];
    body[2] = nullptr;
    WordEntry entry;
    entry.contextId = 0;
    entry.type = UINT8;
    entry.value = new uint8_t(ctxId);
    entry.readonly = true;
    entry.isInternal = false;
    entry.address = (void*)body;
    dictionary[name] = entry;
    // ← УБРАНО: .self = ...
  }
  currentContextId = ctxId;
  Serial.println("Context '" + name + "' activated (ID " + String(ctxId) + ")");
}

// ========================
// Парсинг var / const / cont
// ========================
bool tryParseDeclaration(const std::vector<String>& tokens) {
  if (tokens.empty()) return false;
  if (tokens[0] == "cont" && tokens.size() == 2) {
    createContext(tokens[1]);
    return true;
  }
  if ((tokens[0] == "var" || tokens[0] == "const") && tokens.size() >= 4 && tokens[2] == "=") {
    bool isConst = (tokens[0] == "const");
    String varName = tokens[1];
    String valueToken = tokens[3];
    WordEntry* lit = getOrCreateLiteral(valueToken);
    if (lit) {
      createVariable(varName, lit, isConst);
    } else {
      Serial.println(tokens[0] + ": unsupported value type: " + valueToken);
    }
    return true;
  }
  return false;
}

// ========================
// НОВЫЕ: подфункции executeLine
// ========================
static void handleMultilineJson(const String& line) {
  jsonBuffer += line;
  if (line.endsWith("}")) {
    parseJsonAndCreateVars(jsonBuffer);
    jsonBuffer = "";
    jsonMultiline = false;
  }
}

static bool handleColonDefinition(const String& line) {
  if (!line.startsWith(":")) return false;
  std::vector<String> tokens = tokenize(line);
  if (tokens.size() < 2 || tokens[0] != ":") return false;

  compilingName = tokens[1];
  compiling = true;

  if (tokens.back() == ";") {
    std::vector<WordEntry*> tempBody;
    for (int i = tokens.size() - 2; i >= 2; i--) {
      const String& token = tokens[i];
      WordEntry* wordPtr = getOrCreateLiteral(token);
      if (!wordPtr && dictionary.count(token)) {
        wordPtr = &dictionary[token]; // ← БЫЛО: .self
      }
      if (wordPtr) {
        tempBody.push_back(wordPtr);
      } else {
        Serial.println("?? " + token + " (in one-line compilation)");
        // ← УБРАНО: dataStack.push_back(nullptr);
      }
    }
    for (auto it = tempBody.rbegin(); it != tempBody.rend(); ++it) {
      compilingBody.push_back(*it);
    }
    handleCompilationEnd();
  } else {
    String rest = "";
    for (size_t i = 2; i < tokens.size(); i++) {
      if (rest.length() > 0) rest += " ";
      rest += tokens[i];
    }
    if (rest.length() > 0) {
      handleNormalLine(rest);
    }
  }
  return true;
}

static void handleCompilationEnd() {
  compiling = false;

  std::vector<WordEntry*> newBody;
  bool afterThen = false;
  for (size_t i = 0; i < compilingBody.size(); i++) {
    WordEntry* w = compilingBody[i];
    newBody.push_back(w);
    if (w == &dictionary["then:"]) {
      afterThen = true;
    } else if (afterThen && w == &dictionary["else:"]) {
      newBody.pop_back();
      newBody.push_back(&dictionary["jumpToIf"]);
      newBody.push_back(w);
      afterThen = false;
    }
  }
  compilingBody = newBody;
  compilingBody.insert(compilingBody.begin(), &dictionary["mychoice"]); // ttt

  auto oldIt = dictionary.find(compilingName);
  if (oldIt != dictionary.end()) {
    WordEntry& old = oldIt->second;
    if (!old.isInternal && old.address) {
      free(old.address);
    }
    if (old.value) {
      if (old.type == STRING) free(old.value);
      else if (old.type == INT) delete (int32_t*)old.value;
      else if (old.type == FLOAT) delete (float*)old.value;
      else if (old.type == UINT8) delete (uint8_t*)old.value;
      else if (old.type == BOOL) delete (bool*)old.value;
    }
  }

  WordEntry** body = (WordEntry**)malloc((compilingBody.size() + 1) * sizeof(WordEntry*));
  for (size_t i = 0; i < compilingBody.size(); i++) {
    body[i] = compilingBody[i];
  }
  body[compilingBody.size()] = nullptr;

  WordEntry entry;
  entry.isInternal = false;
  entry.address = (void*)body;
  entry.contextId = currentContextId;
  entry.type = UNDEFINED;
  entry.value = nullptr;
  entry.readonly = false;

  dictionary[compilingName] = entry;
  // ← УБРАНО: .self = ...

  Serial.println("Compiled: " + compilingName);
  compilingBody.clear();
}

static void handleCompilationLine(const String& line) {
  if (line == ";") {
    handleCompilationEnd();
    return;
  }
  std::vector<String> tokens = tokenize(line);
  for (int i = tokens.size() - 1; i >= 0; i--) {
    const String& token = tokens[i];
    WordEntry* wordPtr = getOrCreateLiteral(token);
    if (!wordPtr && dictionary.count(token)) {
      wordPtr = &dictionary[token]; // ← БЫЛО: .self
    }
    if (wordPtr) {
      compilingBody.push_back(wordPtr);
    } else {
      Serial.println("?? " + token + " (in compilation)");
      // ← УБРАНО: dataStack.push_back(nullptr);
    }
  }
}

static void handleNormalLine(const String& line) {
  std::vector<String> tokens = tokenize(line);
  if (tryParseDeclaration(tokens)) return;

  // Сохраняем текущее состояние стека
  size_t originalStackSize = dataStack.size();

  bool errorOccurred = false;
  for (int i = tokens.size() - 1; i >= 0; i--) {
    const String& token = tokens[i];
    WordEntry* lit = getOrCreateLiteral(token);
    if (lit) {
      executeExternal(lit);
    } else if (dictionary.count(token)) {
      WordEntry* word = &dictionary[token];
      if (word->isInternal) {
        ((void(*)(WordEntry*, WordEntry**, int*))word->address)(word, nullptr, nullptr);
      } else {
        executeExternal(word);
      }
    } else {
      // Неизвестное слово — ошибка
      if (currentSourceLine > 0) {
        Serial.print("[line ");
        Serial.print(currentSourceLine);
        Serial.print("] ?? ");
        Serial.println(token);
      } else {
        Serial.println("?? " + token);
      }
      errorOccurred = true;
      break; // прекращаем выполнение строки
    }
  }

  // Если была ошибка — откатываем стек
  if (errorOccurred) {
    while (dataStack.size() > originalStackSize) {
      dataStack.pop_back();
    }
  }
}

// ========================
// executeToken — БЕЗ nullptr в стек
// ========================
void executeToken(const String& token) {
  WordEntry* lit = getOrCreateLiteral(token);
  if (lit) {
    executeExternal(lit);
    return;
  }
  if (dictionary.count(token)) {
    WordEntry* word = &dictionary[token]; // ← БЫЛО: .self
    if (word->isInternal) {
      ((void(*)(WordEntry*, WordEntry**, int*))word->address)(word, nullptr, nullptr);
    } else {
      executeExternal(word);
    }
    return;
  }
  if (currentSourceLine > 0) {
    Serial.print("[line ");
    Serial.print(currentSourceLine);
    Serial.print("] ?? ");
    Serial.println(token);
  } else {
    Serial.println("?? " + token);
  }
  // ← УБРАНО: dataStack.push_back(nullptr);
}

// ========================
// executeLine — полностью переписан
// ========================
void executeLine(String line) {
  if (line.length() == 0) return;

  // Обработка многострочного комментария
  if (inMultilineComment) {
    if (line.indexOf("*/") != -1) {
      // Найти позицию закрывающего */
      int endPos = line.indexOf("*/");
      String after = line.substring(endPos + 2);
      inMultilineComment = false;
      if (after.length() > 0) {
        executeLine(after); // рекурсивно обработать остаток строки
      }
    }
    return; // всё остальное в строке — игнорируется
  }

  // Проверка на начало комментария в этой строке
  int startComment = line.indexOf("/*");
  if (startComment != -1) {
    // Есть начало комментария
    String before = line.substring(0, startComment);
    before.trim();
    if (before.length() > 0) {
      // Выполнить часть до /*
      executeLine(before); // рекурсивно
    }
    int endComment = line.indexOf("*/", startComment + 2);
    if (endComment != -1) {
      // Комментарий закрыт в той же строке
      String after = line.substring(endComment + 2);
      after.trim();
      if (after.length() > 0) {
        executeLine(after); // рекурсивно обработать после */
      }
    } else {
      // Комментарий не закрыт — включаем режим
      inMultilineComment = true;
    }
    return;
  }

  // Обычная обработка (без комментариев)
  line.trim();
  if (line.length() == 0) return;

  if (jsonMultiline) {
    handleMultilineJson(line);
    return;
  }

  if (line.startsWith("{")) {
    if (line.endsWith("}")) {
      parseJsonAndCreateVars(line);
    } else {
      jsonBuffer = line;
      jsonMultiline = true;
    }
    return;
  }

  if (compiling) {
    handleCompilationLine(line);
    return;
  }

  if (handleColonDefinition(line)) {
    return;
  }

  handleNormalLine(line);
}

void charFunc(WordEntry* self, WordEntry** body, int* ip) {
  if (dataStack.empty()) {
    Serial.println("char: stack empty");
    return;
  }
  WordEntry* numWord = dataStack.back(); dataStack.pop_back();

  if (numWord->type != INT) {
    Serial.println("char: expected INT");
    return;
  }

  int32_t code = *(int32_t*)numWord->value;
  if (code < 0 || code > 255) {
    Serial.println("char: value out of range (0-255)");
    return;
  }

  char str[2] = { (char)code, '\0' };
  String token = "\"" + String(str) + "\"";

  WordEntry* lit = getOrCreateLiteral(token);
  if (lit) {
    dataStack.push_back(lit);
  } else {
    Serial.println("char: failed to create literal");
  }
}
void literalsFunc(WordEntry* self, WordEntry** body, int* ip) {
  uint8_t savedContext = currentContextId;
  currentContextId = LITERAL_CONTEXT; // 255
  wordsFunc(nullptr, nullptr, nullptr); // вызываем words в контексте литералов
  currentContextId = savedContext;
}
void saveFunc(WordEntry* self, WordEntry** body, int* ip) {
  if (dataStack.size() < 2) {
    Serial.println("save: expected 2 arguments (filename data)");
    return;
  }

  // Порядок: сначала данные (левый аргумент), потом имя файла (правый)
  WordEntry* fnameWord = dataStack.back(); dataStack.pop_back(); // "wifi-cont.json"
  WordEntry* dataWord = dataStack.back(); dataStack.pop_back(); // например, результат json


  if (fnameWord->type != STRING) {
    Serial.println("save: filename must be a string");
    return;
  }

  if (dataWord->type != STRING) {
    Serial.println("save: data must be a string (e.g. result of 'json')");
    return;
  }

  String filename = String((char*)fnameWord->value);
  String content = String((char*)dataWord->value);

  // Убедимся, что путь начинается с "/"
  if (!filename.startsWith("/")) {
    filename = "/" + filename;
  }

#ifdef LittleFSM
  File file = LittleFS.open(filename, "w");
#else
  File file = SPIFFS.open(filename, "w");
#endif

  if (!file) {
    Serial.println("save: failed to open file for writing: " + filename);
    return;
  }

  if (file.print(content)) {
    Serial.println("Saved to " + filename);
  } else {
    Serial.println("save: write failed");
  }
  file.close();
}

// ========================
// Setup
// ========================
void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println();

#ifdef LittleFSM
  if (!LittleFS.begin()) {
    Serial.println("LittleFS Mount Failed");
    return;
  }
#else
  if (!SPIFFS.begin(true)) {
    Serial.println("SPIFFS Mount Failed");
    return;
  }
#endif

  // === 1. Сначала регистрируем КЛЮЧЕВЫЕ internal-слова: mychoice и pushSelf ===
  {
    WordEntry entry;
    entry.isInternal = true;
    entry.address = (void*)mychoiceFunc;
    dictionary["mychoice"] = entry;
  }
  {
    WordEntry entry;
    entry.isInternal = true;
    entry.address = (void*)pushSelfEntry; // ← ИСПРАВЛЕНО: имя функции, а не переменной!
    dictionary["pushSelf"] = entry;
  }

  // === 2. Маркеры операций (используют mychoice) ===
  auto createMarker = [](const String & name) {
    WordEntry** body = (WordEntry**)malloc(2 * sizeof(WordEntry*));
    body[0] = &dictionary["pushSelf"];
    body[1] = nullptr;
    //body[0] = &dictionary["mychoice"];
    //body[1] = nullptr;
   // body[1] = &dictionary["pushSelf"];
   // body[2] = nullptr;
    WordEntry entry;
    entry.isInternal = false;
    entry.address = (void*)body;
    entry.contextId = 0;
    entry.type = UNDEFINED;
    entry.value = nullptr;
    entry.readonly = true;
    dictionary[name] = entry;
  };
  createMarker("=");
  createMarker("+");
  createMarker("-");
  createMarker("*");
  createMarker("/");
  createMarker("==");
  createMarker("!=");
  createMarker("<");
  createMarker(">");
  createMarker("<=");
  createMarker(">=");
  createMarker("not");
  createMarker("then:");
  createMarker("else:");
  createMarker(":if");

  // === 3. jumpToIf ===
  {
    WordEntry entry;
    entry.isInternal = true;
    entry.address = (void*)jumpToIfFunc;
    dictionary["jumpToIf"] = entry;
  }

  // === 4. GPIO константы (используют mychoice, но НЕ pushSelf — константы не должны возвращать значение при использовании в выражениях?) ===
  // Но если вы хотите, чтобы HIGH возвращал 1, то нужно добавить pushSelf.
  // Для совместимости с выражениями — ДОБАВИМ pushSelf.
  auto addGpioConst = [](const String & name, int value) {
    WordEntry entry;
    entry.isInternal = false;
    entry.type = INT;
    entry.value = new int32_t(value);
    entry.readonly = true;
    WordEntry** body = (WordEntry**)malloc(3 * sizeof(WordEntry*));
    body[0] = &dictionary["mychoice"];
    body[1] = &dictionary["pushSelf"]; // ← ДОБАВЛЕНО: константы ВОЗВРАЩАЮТ значение
    body[2] = nullptr;
    entry.address = (void*)body;
    dictionary[name] = entry;
  };
  addGpioConst("INPUT", INPUT);
  addGpioConst("OUTPUT", OUTPUT);
  addGpioConst("INPUT_PULLUP", INPUT_PULLUP);
#ifdef ESP32
  addGpioConst("INPUT_PULLDOWN", INPUT_PULLDOWN);
#endif
  addGpioConst("HIGH", HIGH);
  addGpioConst("LOW", LOW);

  // === 5. Остальные internal-слова ===
  auto addInternal = [](const String & name, void (*func)(WordEntry*, WordEntry**, int*)) {
    WordEntry entry;
    entry.isInternal = true;
    entry.address = (void*)func;
    dictionary[name] = entry;
  };
  addInternal(".", printFunc);
  addInternal("`", wordsFunc);
  addInternal("body", bodyFunc);
  addInternal("pinMode", pinModeFunc);
  addInternal("digitalWrite", digitalWriteFunc);
  addInternal("analogWrite", analogWriteFunc);
  addInternal("sta", wifiFunc);
  addInternal("setAp", setApFunc);
  addInternal("apConfig", apConfigFunc);
  addInternal("ipSta", ipStaFunc);
  addInternal("modeSta", modeStaFunc);
  addInternal("modeAp", modeApFunc);
  addInternal("modeStaAp", modeStaApFunc);
  addInternal("dbm", dbmFunc);
  addInternal("use", useFunc);
  addInternal("!", notInPlaceFunc);
  addInternal("if", ifFunc);
  addInternal("nop", nopFunc);
  addInternal("load", sourceFunc);
  addInternal("save", saveFunc);
  addInternal("dir", dirFunc);
  addInternal("del", delFunc);
  addInternal("type", typeFunc);
  addInternal("global", globalFunc);
  addInternal("char", charFunc);
  addInternal("literals", literalsFunc);
  addInternal("udpOpen", udpOpenFunc);
  addInternal("udpSend", udpSendFunc);
  addInternal("udpRecv", udpRecvFunc);
  addInternal("udpClose", udpCloseFunc);
  addInternal("udpSendRaw", udpSendRawFunc);
  addInternal("addTick", addTickFunc);
  addInternal("delTick", delTickFunc);

  // === 6. buildJson ===
  {
    WordEntry entry;
    entry.isInternal = true;
    entry.address = (void*)buildJsonFunc;
    dictionary["buildJson"] = entry;
  }

  // === 7. json (должен возвращать значение) ===
  {
    WordEntry** body = (WordEntry**)malloc(4 * sizeof(WordEntry*));
    body[0] = &dictionary["buildJson"];
    body[1] = &dictionary["mychoice"];
    body[2] = &dictionary["pushSelf"]; // ← ДОБАВЛЕНО: json ВОЗВРАЩАЕТ строку
    body[3] = nullptr;
    WordEntry entry;
    entry.isInternal = false;
    entry.address = (void*)body;
    entry.type = STRING;
    entry.value = nullptr;
    entry.contextId = 0;
    dictionary["json"] = entry;
  }

  // === 8. global (константа, должна возвращать значение) ===
  {
    WordEntry** body = (WordEntry**)malloc(3 * sizeof(WordEntry*));
    body[0] = &dictionary["mychoice"];
    body[1] = &dictionary["pushSelf"]; // ← ДОБАВЛЕНО
    body[2] = nullptr;
    WordEntry entry;
    entry.contextId = 0;
    entry.type = UINT8;
    entry.value = new uint8_t(0);
    entry.readonly = true;
    entry.isInternal = false;
    entry.address = (void*)body;
    dictionary["global"] = entry;
  }

  currentContextId = 0;
  executeLine("load \"constants.words\"");
  Serial.print("[ctx:" + getCurrentContextName() + "] " + stackToString() + " words> ");
}

// ========================
// Loop
// ========================
void loop() {
  // Обработка Serial
  if (Serial.available()) {
    String line = Serial.readStringUntil('\n');
    line.trim();
    if (line.length() > 0) {
      Serial.println(line);
      executeLine(line);
    }
    Serial.print("[ctx:" + getCurrentContextName() + "] " + stackToString() + " words> ");
  }

  // Планировщик задач
  uint32_t now = millis();
  for (auto& task : tickTasks) {
    if (task.active && (now - task.lastRunMs) >= task.intervalMs) {
      task.lastRunMs = now;

      // Сохраняем текущий контекст
      uint8_t savedContext = currentContextId;

      // Выполняем слово
      if (dictionary.count(task.wordName)) {
        WordEntry* word = &dictionary[task.wordName];
        if (word->isInternal) {
          ((void(*)(WordEntry*, WordEntry**, int*))word->address)(word, nullptr, nullptr);
        } else {
          executeExternal(word);
        }
      }

      // Восстанавливаем контекст
      currentContextId = savedContext;
    }
  }

  // Небольшая задержка, чтобы не грузить CPU
  delay(1);
}
