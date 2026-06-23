#include <Arduino.h>
#include <cstring>
// #define CHOICE_DEBUG
// === НАСТРОЙКА ТЕРМИНАЛЬНОГО СЛОЯ ===
#define ENABLE_TERM_LAYER 1  // 1 = включить, 0 = выключить (экономит память)
#if ENABLE_TERM_LAYER
bool g_termMode = false;
char term_buf[256];
uint8_t term_len = 0;
uint8_t term_cur = 0;
#endif
bool g_compile_mode = false;
uint16_t g_compile_header = 0xFFFF;
uint16_t g_loop_target = 0;   // Адрес, куда прыгает }
uint16_t g_while_addr = 0;    // Временная точка начала while
// Стек для вложенных while (макс. 8 уровней)
static uint16_t w_skip_pos[8];   // Куда писать адрес пропуска при false
static uint16_t w_loop_start[8]; // Куда прыгать goto (начало условия)
static uint8_t  w_depth = 0;     // Текущая глубина
uint16_t g_if_branch_pos   = 0; // Где лежит адрес прыжка if
uint16_t g_else_goto_pos   = 0; // Где лежит адрес пры goto из else
static uint16_t g_local_id = 0; // <-- Счетчик для локальных переменных (l1, l2, l3...)
// 🔹 НОВОЕ: Карта соответствий "исходное имя" -> "ID локальной переменной"
struct LocalVarMap {
  char original_name[32]; // Исходное имя (например, "loc")
  uint16_t local_id;      // Сгенерированный ID (например, 1 для "l1")
};
LocalVarMap g_local_map[16]; // Максимум 16 уникальных локальных переменных на одно слово
uint8_t g_local_map_count = 0; // Текущее количество записей в карте
#define FLAG_CHAIN 0x40
// Состояние токенизатора (доступно word_bead для прямого захвата из потока)
static const char* g_tok_start[64];
static uint8_t     g_tok_len[64];
static uint8_t     g_tok_count = 0;
static int8_t      g_tok_idx   = -1; // Текущий индекс в R2L-цикле
// === ЯВНЫЕ ПРОТОТИПЫ (Fix для Arduino IDE multi-tab scope) ===
void printStack();
uint16_t elem_size(const uint8_t* buf);
void printValue(const uint8_t* buf);
void printRawValue(const uint8_t* buf);

void handleTermChar(char c);
void stack_clear();
void word_prompt();
void exec_word(uint16_t addr);
bool g_in_block_comment = false; // Состояние многострочного комментария
#define FILESYSTEM SPIFFS
#define FORMAT_FILESYSTEM false
#if defined(ESP8266)
#if FILESYSTEM == SPIFFS
#include <FS.h>
#endif
#if FILESYSTEM == LittleFS
#include "FS.h"
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
#include "FS.h"
#endif
#if FILESYSTEM == SD
#include "SD.h"
#include "SPI.h"
#endif
#endif
#include <WebServer.h>
WebServer HTTP(80);
File fsUploadFile;

// web.ino — ИСПРАВЛЕННАЯ ВЕРСИЯ
#include <WebSocketsServer.h> // ⚠️ Обязательно здесь, иначе WStype_t не виден в этой вкладке

// Доступ к глобальным переменным и функциям из concWords.ino
extern Print* currentOutput;
extern void executeLine(const char* line);
extern void word_prompt();
extern void addInternalWord(const char* name, void (*func)());

WebSocketsServer wsServer(82);
WebSocketsServer prilServer(81);

class WebSocketPrint : public Print {
  public:
    WebSocketPrint(WebSocketsServer* srv) : server(srv) {}
    size_t write(uint8_t c) override {
      buf[buf_len++] = c;
      if (c == '\n' || buf_len >= 254) flush();
      return 1;
    }
    size_t write(const uint8_t* data, size_t size) override {
      size_t i = 0;
      while (i < size && buf_len < 254) buf[buf_len++] = data[i++];
      if (buf_len >= 254) flush();
      return i;
    }
    void flush() override {
      if (server && buf_len > 0) {
        server->broadcastTXT((uint8_t*)buf, buf_len);
        buf_len = 0;
      }
    }
  private:
    WebSocketsServer* server;
    char buf[256];
    uint8_t buf_len = 0;
};

WebSocketPrint wsPrint(&wsServer);
char ws_cmd[256];
uint8_t ws_len = 0;

// === ПАМЯТЬ ===
#define STACK_SIZE      4096
#define DICT_POOL_SIZE  32768
#define DATA_POOL_SIZE  32768
#define MAX_STACK_PRINT 32
uint16_t local_dict_ptr = DICT_POOL_SIZE; // Указатель на стек локалов (растет ВНИЗ)
// 🔹 Стек кадров локалов — синхронный с rstack по глубине.
// Сохраняет local_dict_ptr при входе в скомпилированное слово
// и восстанавливает при выходе. Это обеспечивает изоляцию локалов
// между вызовами слов и корректную работу рекурсии.
#define LOCAL_FRAME_STACK_SIZE 16
uint16_t local_frame_stack[LOCAL_FRAME_STACK_SIZE];
uint8_t  local_frame_ptr = 0;

#define FLAG_ALIAS 0x20 // Не пересекается с 0x01..0x08 и 0x16
static int8_t g_bead_skip = 0;
Print* currentOutput = &Serial;
uint8_t current_type = 10; // 10 = i32 по умолчанию
char line_buf[256];
char g_currentDir[64] = "/";
__attribute__((aligned(4))) uint8_t stack_mem[STACK_SIZE];
uint16_t stack_ptr = STACK_SIZE;
__attribute__((aligned(4))) uint8_t dict_pool[DICT_POOL_SIZE];
uint16_t dict_ptr = 0;
uint16_t dict_last = 0xFFFF;
__attribute__((aligned(4))) uint8_t data_pool[DATA_POOL_SIZE];
uint16_t data_ptr = 0;
uint8_t  currentContext = 0;
uint8_t  g_next_ctx  = 1;
uint16_t ip = 0;
static uint16_t g_device_counter = 0;
// СТЕК ВОЗВРАТОВ
#define RSTACK_SIZE 512
__attribute__((aligned(4))) uint8_t rstack_mem[RSTACK_SIZE];
uint16_t rstack_ptr = RSTACK_SIZE;
inline bool rstack_is_empty() {
  return rstack_ptr == RSTACK_SIZE;
}
inline void rstack_push(uint16_t addr) {
  if (rstack_ptr < 2) return;
  rstack_ptr -= 2;
  rstack_mem[rstack_ptr]     = addr & 0xFF;
  rstack_mem[rstack_ptr + 1] = addr >> 8;
}
inline uint16_t rstack_pop() {
  if (rstack_is_empty()) return 0;
  uint16_t val = rstack_mem[rstack_ptr] | (rstack_mem[rstack_ptr + 1] << 8);
  rstack_ptr += 2;
  return val;
}
// === Минимальные inline-хелперы (0 накладных расходов, экономят ~150 строк) ===
static inline bool popString(String& out) {
  if (stack_is_empty()) return false;
  uint8_t* top = &stack_mem[stack_ptr];
  if (top[0] != 0x0E) return false;
  out = String((char*)&top[2], top[1]);
  stack_ptr += elem_size(top);
  return true;
}
static inline void pushBool(bool v) {
  uint8_t b[1] = {(uint8_t)(v ? 1 : 0)}; stack_push(b, 1);
}
static inline void pushInt32(int32_t v) {
  uint8_t b[5] = {10}; memcpy(&b[1], &v, 4); stack_push(b, 5);
}
static inline void pushStringRaw(const char* s) {
  uint8_t l = strlen(s); if (l > 255) l = 255;
  uint8_t b[257] = {0x0E, l}; memcpy(&b[2], s, l); stack_push(b, 2 + l);
}
// === УНИВЕРСАЛЬНЫЕ ХЕЛПЕРЫ СТЕКА (Вставить после pushStringRaw) ===
static inline bool popUInt8(uint8_t& out) {
  if (stack_is_empty()) return false;
  uint8_t* top = &stack_mem[stack_ptr];
  if (top[0] < 4 || top[0] > 11) return false; // Только числовые теги (u8..f)
  uint16_t sz = elem_size(top);
  if (sz < 2) return false;
  out = top[1]; // LSB всегда по смещению 1 (работает для u8, u16, u32, i32)
  stack_ptr += sz;
  return true;
}

static inline bool popUInt32(uint32_t& out) {
  if (stack_is_empty()) return false;
  uint8_t* top = &stack_mem[stack_ptr];
  if (top[0] < 4 || top[0] > 11) return false;
  uint16_t sz = elem_size(top);
  if (sz < 2) return false;
  out = 0;
  uint16_t d = sz - 1;
  for (uint16_t i = 0; i < d && i < 4; i++) {
    out |= (uint32_t)top[1 + i] << (i * 8);
  }
  stack_ptr += sz;
  return true;
}

static inline bool popUInt32Optional(uint32_t& out) {
  if (stack_is_empty()) return false;
  uint8_t* top = &stack_mem[stack_ptr];
  if (top[0] < 4 || top[0] > 11) return false;
  return popUInt32(out);
}
// === РЕЕСТР ТИПОВ ===
enum { CAT_NONE, CAT_UINT, CAT_INT, CAT_FLOAT, CAT_TEXT, CAT_ADDR, CAT_LIST, CAT_STRUCT, CAT_REF };
struct TypeInfo {
  const char* name;
  const char* short_name;
  uint8_t     size;
  uint8_t     category;
  const char* desc;
};
const TypeInfo type_registry[] = {
  /*  0 */ {"BOOL",    "B",     0, CAT_NONE,   "Boolean false"},
  /*  1 */ {"BOOL",    "B",     0, CAT_NONE,   "Boolean true"},
  /*  2 */ {"NULL",    "N",     0, CAT_NONE,   "Void/marker"},
  /*  3 */ {"CONTEXT", "ctx",   1, CAT_UINT,   "Context ID"},
  /*  4 */ {"UINT8",   "u8",    1, CAT_UINT,   "Unsigned 8-bit"},
  /*  5 */ {"INT8",    "i8",    1, CAT_INT,    "Signed 8-bit"},
  /*  6 */ {"UINT16",  "u16",   2, CAT_UINT,   "Unsigned 16-bit"},
  /*  7 */ {"INT16",   "i16",   2, CAT_INT,    "Signed 16-bit"},
  /*  8 */ {"UINT24",  "u24",   3, CAT_UINT,   "Unsigned 24-bit"},
  /*  9 */ {"UINT32",  "u32",   4, CAT_UINT,   "Unsigned 32-bit"},
  /* 10 */ {"INT32",   "i32",   4, CAT_INT,    "Signed 32-bit"},
  /* 11 */ {"FLOAT",   "f",     4, CAT_FLOAT,  "IEEE754 float"},
  /* 12 */ {"MARKER",  "M",     0, CAT_TEXT,   "Text len+bytes"},
  /* 13 */ {"NAME",    "N",     0, CAT_TEXT,   "Identifier text"},
  /* 14 */ {"STRING",  "S",     0, CAT_TEXT,   "Literal string"},
  /* 15 */ {"$STRING", "$S",    3, CAT_REF,    "Pool ref len+addr"},
  /* 16 */ {"CONCAT",  "C",     0, CAT_LIST,   "Addr chain"},
  /* 17 */ {"ARRAY",   "arr",   5, CAT_STRUCT, "Array header"},
  /* 18 */ {"ADDR",    "@",     2, CAT_ADDR,   "Pool offset"},
  /* 19 */ {"FUNC",    "fn",    4, CAT_ADDR,   "C function ptr"},
  /* 20 */ {"REF_ARR", "ref", 5, CAT_STRUCT, "Array reference (base:2, len:2, type:1)"},
  {nullptr, nullptr, 0, 0, nullptr}
};
// Хелпер для получения имени чипа на этапе компиляции
static const char* getChipName() {
#if defined(CONFIG_IDF_TARGET_ESP32)
  return "esp32";
#elif defined(CONFIG_IDF_TARGET_ESP32S2)
  return "esp32-s2";
#elif defined(CONFIG_IDF_TARGET_ESP32S3)
  return "esp32-s3";
#elif defined(CONFIG_IDF_TARGET_ESP32C3)
  return "esp32-c3";
#elif defined(CONFIG_IDF_TARGET_ESP32C2)
  return "esp32-c2";
#elif defined(CONFIG_IDF_TARGET_ESP32C5)
  return "esp32-c5";
#elif defined(CONFIG_IDF_TARGET_ESP32C6)
  return "esp32-c6";
#elif defined(CONFIG_IDF_TARGET_ESP32H2)
  return "esp32-h2";
#else
  return "esp32-?";
#endif
}
// Новая перегрузка: принимает сырые строки, тегирование делает внутри
void create_internal_word_str(const char* name_raw, const char* value_raw,
                              uint8_t flags, void (*func)()) {
  uint8_t vlen = strlen(value_raw); if (vlen > 255) vlen = 255;
  uint8_t nlen = strlen(name_raw);  if (nlen > 63)  nlen = 63;

  // Размер: 9 + nlen + (2+vlen) — тот же, что и раньше
  uint16_t size = 11 + nlen + vlen;
  if (dict_ptr + size > DICT_POOL_SIZE) return;

  uint16_t a = dict_ptr; dict_ptr += size;

  // Заголовок
  dict_pool[a] = 0x00; dict_pool[a + 1] = 0x00; dict_pool[a + 2] = nlen;
  memcpy(&dict_pool[a + 3], name_raw, nlen); // ← прямое копирование, как ты и хотел

  // Флаги + тегированное тело [0x0E][len][data]
  uint16_t p = a + 3 + nlen;
  dict_pool[p] = 0x00; dict_pool[p + 1] = flags;
  dict_pool[p + 2] = 0x0E; dict_pool[p + 3] = vlen;
  memcpy(&dict_pool[p + 4], value_raw, vlen); // ← прямое копирование

  // Функция
  uint32_t fn = (uint32_t)(uintptr_t)func;
  memcpy(&dict_pool[p + 4 + vlen], &fn, 4);

  link_word(a);
}
static uint16_t tag_size(const uint8_t* buf) {
  if (!buf) return 0;
  uint8_t tag = buf[0];
  if (tag > 20) return 0; // ← Расширено до REF_ARR
  uint8_t sz = type_registry[tag].size;
  if (sz > 0) return sz;
  if (tag >= 12 && tag <= 14) return buf[1];
  if (tag == 16) return buf[1] * 2;
  return 0;
}

const char* tag_short(uint8_t tag) {
  if (tag > 20) return "??";
  const char* s = type_registry[tag].short_name;
  return s[0] ? s : type_registry[tag].name;
}

uint16_t elem_size(const uint8_t* buf) {
  if (!buf) return 0;
  uint8_t tag = buf[0];
  if (tag > 20) return 0;
  uint16_t data_len = tag_size(buf);
  return (tag >= 12 && tag <= 14) ? 2 + data_len : 1 + data_len;
}
inline bool _mem_write(uint8_t* pool, uint16_t sz, uint16_t addr, const uint8_t* src, uint16_t len) {
  if (addr + len > sz) return false;
  memcpy(&pool[addr], src, len);
  return true;
}
inline bool _mem_read(const uint8_t* pool, uint16_t sz, uint16_t addr, uint8_t* dst, uint16_t len) {
  if (addr + len > sz) return false;
  memcpy(dst, &pool[addr], len);
  return true;
}
inline bool stack_write(uint16_t addr, const uint8_t* src, uint16_t size) {
  return _mem_write(stack_mem, STACK_SIZE, addr, src, size);
}
inline bool stack_read(uint16_t addr, uint8_t* dst, uint16_t size) {
  return _mem_read(stack_mem, STACK_SIZE, addr, dst, size);
}
inline bool stack_is_empty() {
  return stack_ptr == STACK_SIZE;
}
bool stack_push(const uint8_t* src, uint16_t size) {
  if (stack_ptr < size) return false;
  stack_ptr -= size;
  return stack_write(stack_ptr, src, size);
}
uint16_t stack_pop(uint8_t* dst, uint16_t max) {
  if (stack_is_empty()) return 0;
  uint8_t tag = stack_mem[stack_ptr];
  uint16_t size = (tag >= 12 && tag <= 14) ? 2 + tag_size(&stack_mem[stack_ptr]) : 1 + tag_size(&stack_mem[stack_ptr]);
  if (size > max) return 0;
  stack_read(stack_ptr, dst, size);
  stack_ptr += size;
  return size;
}
void stack_clear() {
  stack_ptr = STACK_SIZE;
}
inline bool data_write_raw(uint16_t addr, const uint8_t* src, uint16_t size) {
  return _mem_write(data_pool, DATA_POOL_SIZE, addr, src, size);
}
inline bool data_read(uint16_t addr, uint8_t* dst, uint16_t size) {
  return _mem_read(data_pool, DATA_POOL_SIZE, addr, dst, size);
}
inline bool dict_write(uint16_t addr, const uint8_t* src, uint16_t size) {
  return _mem_write(dict_pool, DICT_POOL_SIZE, addr, src, size);
}
inline bool dict_read(uint16_t addr, uint8_t* dst, uint16_t size) {
  return _mem_read(dict_pool, DICT_POOL_SIZE, addr, dst, size);
}
uint16_t write_name(uint16_t pos, const char* name) {
  uint8_t len = strlen(name);
  dict_pool[pos] = len;
  memcpy(&dict_pool[pos + 1], name, len);
  return pos + 1 + len;
}
void link_word(uint16_t addr) {
  if (dict_last != 0xFFFF) {
    dict_pool[dict_last]     = addr & 0xFF;
    dict_pool[dict_last + 1] = addr >> 8;
  }
  dict_last = addr;
}
uint16_t dict_find(const char* name) {
  uint16_t p = 0;
  uint16_t t_len = strlen(name);
  while (p < dict_ptr) {
    uint16_t next = dict_pool[p] | (dict_pool[p + 1] << 8);
    uint8_t len = dict_pool[p + 2];
    if (len == t_len && memcmp(&dict_pool[p + 3], name, len) == 0) return p;
    if (next == 0) break;
    p = next;
  }
  return 0xFFFF;
}
// === СЛОВА И ИСПОЛНИТЕЛИ ===
void wordMarker() {
  uint8_t buf[3];
  if (stack_pop(buf, 3) != 3 || buf[0] != 0x12) return;
  uint16_t addr = buf[1] | (buf[2] << 8);
  uint8_t nlen = dict_pool[addr + 2];
  uint8_t out[257];
  out[0] = 0x0C; out[1] = nlen;
  memcpy(&out[2], &dict_pool[addr + 3], nlen);
  stack_push(out, 2 + nlen);
}
static void apply_op(const uint8_t* l_data, uint16_t l_len, uint8_t l_tag, uint16_t dict_write_addr) {
  if (stack_is_empty()) return;
  uint8_t* top = &stack_mem[stack_ptr];
  if (top[0] != 0x0C) return; // Верх должен быть маркером операции
  uint8_t op_len = top[1];
  if (op_len == 0 || op_len > 4) return;
  char op[5] = {0};
  memcpy(op, &top[2], op_len);
  uint16_t r_addr = stack_ptr + 2 + op_len;
  if (r_addr >= STACK_SIZE) return;
  uint16_t r_sz = elem_size(&stack_mem[r_addr]);
  if (r_sz == 0) return;
  uint8_t r_tag = stack_mem[r_addr];

  // 🔹 FIX: Разыменовываем ADDR (0x12) для LHS
  uint8_t actual_l_tag = l_tag;
  uint16_t actual_l_len = l_len;
  const uint8_t* actual_l_data = l_data;
  uint16_t actual_write_addr = dict_write_addr;

  if (l_tag == 0x12 && l_len == 3) {
    uint16_t var_addr = l_data[0] | (l_data[1] << 8);
    if (var_addr < dict_ptr) {
      uint8_t vn = dict_pool[var_addr + 2];
      uint16_t body = var_addr + 5 + vn;
      uint8_t vt = dict_pool[body];
      if (vt >= 4 && vt <= 11) { // Только числовые типы
        uint16_t vsz = elem_size(&dict_pool[body]);
        actual_l_tag = vt;
        actual_l_len = vsz - 1;
        actual_l_data = &dict_pool[body + 1];
        actual_write_addr = body + 1; // Пишем обратно в тело переменной
      }
    }
  }

  // 🔹 СЛОЖЕНИЕ СТРОК (Конкатенация)
  if (op_len == 1 && op[0] == '+' && (actual_l_tag >= 12 && actual_l_tag <= 14) && (r_tag >= 12 && r_tag <= 14)) {
    uint8_t l_txt_len = actual_l_data[0];
    uint8_t r_txt_len = stack_mem[r_addr + 1];
    uint16_t new_len = (uint16_t)l_txt_len + r_txt_len;
    if (new_len > 254) new_len = 254;
    uint8_t res[256];
    res[0] = 0x0E;
    res[1] = (uint8_t)new_len;
    uint16_t cl = (l_txt_len > new_len) ? new_len : l_txt_len;
    memcpy(&res[2], &actual_l_data[1], cl);
    uint16_t cr = new_len - cl;
    if (cr > r_txt_len) cr = r_txt_len;
    memcpy(&res[2 + cl], &stack_mem[r_addr + 2], cr);
    uint16_t rsz = 2 + new_len;
    if (stack_ptr < rsz) {
      currentOutput->println("stack overflow");
      return;
    }
    stack_ptr += 2 + op_len + r_sz;
    stack_ptr -= rsz;
    memcpy(&stack_mem[stack_ptr], res, rsz);
    return;
  }

  // 🔹 СРАВНЕНИЕ СТРОК (== и !=)
  if (op_len == 2 && (strcmp(op, "==") == 0 || strcmp(op, "!=") == 0) &&
      (actual_l_tag >= 12 && actual_l_tag <= 14) && (r_tag >= 12 && r_tag <= 14)) {
    uint8_t l_len = actual_l_data[0];
    uint8_t r_len = stack_mem[r_addr + 1];
    bool eq = (l_len == r_len) && (memcmp(&actual_l_data[1], &stack_mem[r_addr + 2], l_len) == 0);
    stack_ptr += 2 + op_len + r_sz;
    uint8_t res = (strcmp(op, "!=") == 0) ? !eq : eq;
    stack_push(&res, 1);
    return;
  }

  // 🔽 Декодирование LHS (Числа) — ФИКС: знаковое расширение ТОЛЬКО для signed
  int32_t l_i = 0; float l_f = 0;
  bool l_is_float = (actual_l_tag == 11);
  if (l_is_float) memcpy(&l_f, actual_l_data, 4);
  else {
    uint32_t v = 0;
    for (uint16_t i = 0; i < actual_l_len && i < 4; i++) v |= (uint32_t)actual_l_data[i] << (i * 8);
    // 🔧 FIX: знаковое расширение только для i8(5), i16(7), i32(10)
    if (actual_l_tag == 5 || actual_l_tag == 7 || actual_l_tag == 10) {
      if (actual_l_len == 1 && (actual_l_data[0] & 0x80)) v |= 0xFFFFFF00;
      if (actual_l_len == 2 && (actual_l_data[1] & 0x80)) v |= 0xFFFF0000;
      if (actual_l_len == 3 && (actual_l_data[2] & 0x80)) v |= 0xFF000000;
    }
    l_i = (int32_t)v; l_f = (float)l_i;
  }

  // 🔽 Декодирование RHS (Числа) — ФИКС: знаковое расширение ТОЛЬКО для signed
  bool r_is_float = (r_tag == 11);
  int32_t r_i = 0; float r_f = 0;
  if (r_is_float) memcpy(&r_f, &stack_mem[r_addr + 1], 4);
  else {
    uint32_t v = 0; uint16_t d = r_sz - 1;
    for (uint16_t i = 0; i < d && i < 4; i++) v |= (uint32_t)stack_mem[r_addr + 1 + i] << (i * 8);
    // 🔧 FIX: знаковое расширение только для i8(5), i16(7), i32(10)
    if (r_tag == 5 || r_tag == 7 || r_tag == 10) {
      if (d == 1 && (stack_mem[r_addr + 1] & 0x80)) v |= 0xFFFFFF00;
      if (d == 2 && (stack_mem[r_addr + 2] & 0x80)) v |= 0xFFFF0000;
      if (d == 3 && (stack_mem[r_addr + 3] & 0x80)) v |= 0xFF000000;
    }
    r_i = (int32_t)v; r_f = (float)r_i;
  }

  // 🔥 ОБЪЯВЛЕНИЯ ПЕРЕМЕННЫХ РЕЗУЛЬТАТА
  bool res_float = l_is_float || r_is_float;
  int32_t res_i = 0;
  float res_f = 0;
  float L = l_is_float ? l_f : (float)l_i;
  float R = r_is_float ? r_f : (float)r_i;
  bool is_assign = false;
  bool is_compare = false;
  bool unknown = false;

  // === ЛОГИКА ОПЕРАТОРОВ ===
  if (strcmp(op, "=") == 0) {
    is_assign = true; res_i = r_i; res_f = R;
  }
  else if (strcmp(op, "+=") == 0) {
    res_i = l_i + r_i; res_f = L + R; is_assign = true;
  }
  else if (strcmp(op, "-=") == 0) {
    res_i = l_i - r_i; res_f = L - R; is_assign = true;
  }
  else if (strcmp(op, "*=") == 0) {
    res_i = l_i * r_i; res_f = L * R; is_assign = true;
  }
  else if (strcmp(op, "/=") == 0) {
    res_i = (r_i != 0) ? (l_i / r_i) : 0;
    res_f = (R != 0.0f) ? (L / R) : 0.0f;
    is_assign = true;
  }
  else if (strcmp(op, "+") == 0)  {
    res_i = l_i + r_i;
    res_f = L + R;
  }
  else if (strcmp(op, "-") == 0)  {
    res_i = l_i - r_i;
    res_f = L - R;
  }
  else if (strcmp(op, "*") == 0)  {
    res_i = l_i * r_i;
    res_f = L * R;
  }
  else if (strcmp(op, "/") == 0)  {
    res_i = (r_i != 0) ? (l_i / r_i) : 0;
    res_f = (R != 0.0f) ? (L / R) : 0.0f;
  }
  else if (strcmp(op, "%") == 0)  {
    res_i = (r_i != 0) ? (l_i % r_i) : 0;
    res_float = false;
  }
  else if (strcmp(op, "^") == 0)  {
    res_i = l_i ^ r_i;
    res_float = false;
  }
  else if (strcmp(op, "<<") == 0) {
    res_i = l_i << r_i;
    res_float = false;
  }
  else if (strcmp(op, ">>") == 0) {
    res_i = l_i >> r_i;
    res_float = false;
  }
  else if (strcmp(op, "&") == 0)  {
    res_i = l_i & r_i;
    res_float = false;
  }
  else if (strcmp(op, "|") == 0)  {
    res_i = l_i | r_i;
    res_float = false;
  }
  else if (strcmp(op, "==") == 0) {
    res_i = (L == R);
    is_compare = true;
    res_float = false;
  }
  else if (strcmp(op, "!=") == 0) {
    res_i = (L != R);
    is_compare = true;
    res_float = false;
  }
  else if (strcmp(op, "<") == 0)  {
    res_i = (L < R);
    is_compare = true;
    res_float = false;
  }
  else if (strcmp(op, ">") == 0)  {
    res_i = (L > R);
    is_compare = true;
    res_float = false;
  }
  else if (strcmp(op, "<=") == 0) {
    res_i = (L <= R);
    is_compare = true;
    res_float = false;
  }
  else if (strcmp(op, ">=") == 0) {
    res_i = (L >= R);
    is_compare = true;
    res_float = false;
  }
  else {
    unknown = true;
  }

  // Чистим маркер и правый операнд со стека
  stack_ptr += 2 + op_len + r_sz;

  // 🟢 Неизвестный оператор → возвращаем LHS на стек
  if (unknown) {
    uint8_t tmp[8]; tmp[0] = actual_l_tag; memcpy(&tmp[1], actual_l_data, actual_l_len);
    stack_push(tmp, 1 + actual_l_len);
    return;
  }

  // 🟢 ПРИСВАИВАНИЕ
  if (is_assign) {
    uint8_t casted[8];
    if (l_is_float) {
      memcpy(casted, &res_f, 4);
    } else {
      int32_t v = res_float ? (int32_t)res_f : res_i;
      for (uint16_t i = 0; i < actual_l_len && i < 4; i++) casted[i] = (v >> (i * 8)) & 0xFF;
    }
    if (actual_write_addr) for (uint16_t i = 0; i < actual_l_len; i++) dict_pool[actual_write_addr + i] = casted[i];
    return;
  }

  // 🔵 ЛОГИЧЕСКОЕ СРАВНЕНИЕ
  if (is_compare) {
    uint8_t res_buf[1] = { (uint8_t)(res_i ? 1 : 0) };
    stack_push(res_buf, 1);
    return;
  }

  // ⚫ МАТЕМАТИКА / БИТОВЫЕ ОПЕРАЦИИ
  uint8_t res_buf[8];
  res_buf[0] = res_float ? 11 : actual_l_tag;
  if (res_float) {
    memcpy(&res_buf[1], &res_f, 4);
    stack_push(res_buf, 5);
  } else {
    int32_t v = res_i;
    for (uint16_t i = 0; i < actual_l_len && i < 4; i++) res_buf[1 + i] = (v >> (i * 8)) & 0xFF;
    stack_push(res_buf, 1 + actual_l_len);
  }
}
void choiceFunc() {
  uint8_t abuf[3];
  if (stack_pop(abuf, 3) != 3 || abuf[0] != 0x12) return;
  uint16_t addr = abuf[1] | (abuf[2] << 8);
  if (addr == 0) return; // Убрали блокировку addr >= dict_ptr
  uint8_t nlen = dict_pool[addr + 2];
  uint16_t next = dict_pool[addr] | (dict_pool[addr + 1] << 8);
  // Умное вычисление конца: для локалов (addr >= dict_ptr) конец — это DICT_POOL_SIZE
  uint16_t end = next ? next : (addr >= dict_ptr ? DICT_POOL_SIZE : dict_ptr);
  uint16_t val_start = addr + 5 + nlen;
  uint16_t val_size = (end - 4) - val_start;
  if (val_size == 0) return;
  uint8_t var_tag = dict_pool[val_start];
  uint16_t old_sp = stack_ptr;
  uint8_t flags = dict_pool[addr + 4 + nlen];

  // 🔹 ПЕРЕ-НАЗНАЧЕНИЕ МАССИВА/ССЫЛКИ
  if ((var_tag == 17 || var_tag == 20) && !stack_is_empty()) {
    uint8_t* top = &stack_mem[stack_ptr];
    if (top[0] == 0x0C && top[1] == 1 && top[2] == '=') {
      if (flags & 0x01) {
        stack_ptr = old_sp;
        return;
      }
      uint16_t src_pos = stack_ptr + 3;
      if (src_pos < STACK_SIZE) {
        uint8_t* src = &stack_mem[src_pos];
        if ((src[0] == 17 || src[0] == 20) && elem_size(src) == 6) {
          memcpy(&dict_pool[val_start], src, 6);
          dict_pool[val_start] = 20;
          stack_ptr += 9;
          mark_dirty(addr); // 🔑 1. Запись нового массива/ссылки
          return;
        }
      }
      stack_ptr = old_sp; return;
    }
  }

  // 🔍 МАРКЕР @
  if (!stack_is_empty()) {
    uint8_t* top = &stack_mem[stack_ptr];
    if (top[0] == 0x0C && top[1] == 1 && top[2] == '@') {
      stack_ptr += 3;
      uint8_t ref[4] = {15, 0, 0, 0};
      if (var_tag == 15) {
        ref[1] = dict_pool[val_start + 1];
        uint16_t d_addr = dict_pool[val_start + 2] | (dict_pool[val_start + 3] << 8);
        ref[2] = d_addr & 0xFF; ref[3] = d_addr >> 8;
      } else if (var_tag == 17 || var_tag == 20) {
        uint16_t base = dict_pool[val_start + 1] | (dict_pool[val_start + 2] << 8);
        uint16_t len  = dict_pool[val_start + 3] | (dict_pool[val_start + 4] << 8);
        uint8_t  esz  = type_registry[dict_pool[val_start + 5]].size;
        uint16_t total = len * esz;
        ref[1] = (total > 255) ? 255 : (uint8_t)total;
        ref[2] = base & 0xFF; ref[3] = base >> 8;
      } else {
        ref[1] = val_size - 1;
        ref[2] = (val_start + 1) & 0xFF; ref[3] = (val_start + 1) >> 8;
      }
      stack_push(ref, 4); return;
    }
  }

  // 🔹 $STRING
  if (var_tag == 15) {
    uint16_t slen  = dict_pool[val_start + 1];
    uint16_t d_addr = dict_pool[val_start + 2] | (dict_pool[val_start + 3] << 8);
    uint8_t tmp[257] = {0x0E, slen};
    if (d_addr + slen <= DATA_POOL_SIZE) {
      if (slen > 0) memcpy(&tmp[2], &data_pool[d_addr], slen);
    } else {
      tmp[1] = 0;
    }
    if (!stack_is_empty() && stack_mem[stack_ptr] == 0x0C) {
      uint8_t* top = &stack_mem[stack_ptr];
      if (top[1] == 1 && top[2] == '=') {
        stack_ptr += 3; if (stack_is_empty()) {
          stack_ptr = old_sp;
          return;
        }
        uint8_t* src = &stack_mem[stack_ptr]; uint16_t src_sz = elem_size(src);
        uint16_t new_len = 0; const uint8_t* new_data = nullptr;
        if (src[0] == 0x0E) {
          new_len = src[1];
          new_data = &src[2];
        }
        else if (src[0] >= 12 && src[0] <= 13) {
          new_len = src[1];
          new_data = &src[2];
        }
        else {
          stack_ptr += src_sz;
          stack_ptr = old_sp;
          return;
        }
        if (data_ptr + new_len > DATA_POOL_SIZE) {
          currentOutput->println("data_pool overflow");
          stack_ptr += src_sz;
          stack_ptr = old_sp;
          return;
        }
        uint16_t new_addr = data_ptr;
        if (new_len > 0) memcpy(&data_pool[new_addr], new_data, new_len);
        data_ptr += new_len;
        dict_pool[val_start + 1] = new_len; dict_pool[val_start + 2] = new_addr & 0xFF; dict_pool[val_start + 3] = new_addr >> 8;
        stack_ptr += src_sz;
        mark_dirty(addr); // 🔑 2. Перезапись строки
        return;
      }
      apply_op(&tmp[1], slen, 0x0E, 0); return;
    }
    stack_push(tmp, 2 + slen); return;
  }

#define RESTORE_AND_PUSH() do { stack_ptr = old_sp; stack_push(&dict_pool[val_start], val_size); return; } while(0)
  if (stack_is_empty() || stack_mem[stack_ptr] != 0x0C) RESTORE_AND_PUSH();
  uint8_t* top = &stack_mem[stack_ptr];
  if (top[1] == 1 && top[2] == ']') RESTORE_AND_PUSH();

  // 🔑 ЕДИНЫЙ ПУТЬ ДЛЯ ARRAY (17) И REF_ARR (20)
  if ((var_tag == 17 || var_tag == 20) && top[1] == 1 && top[2] == '[') {
    stack_ptr += 3;
    if (stack_is_empty()) RESTORE_AND_PUSH();
    uint8_t* nxt = &stack_mem[stack_ptr];
    if (nxt[0] == 0x0C && nxt[1] == 1 && nxt[2] == ']') {
      stack_ptr += 3;
      if (stack_is_empty() || stack_mem[stack_ptr] != 0x0C) RESTORE_AND_PUSH();
      uint8_t* eq = &stack_mem[stack_ptr];
      if (eq[1] != 1 || eq[2] != '=') RESTORE_AND_PUSH();
      stack_ptr += 3;
      uint16_t base = dict_pool[val_start + 1] | (dict_pool[val_start + 2] << 8);
      uint16_t len  = dict_pool[val_start + 3] | (dict_pool[val_start + 4] << 8);
      uint8_t  tp   = dict_pool[val_start + 5];
      uint8_t  esz  = type_registry[tp].size;
      for (uint16_t i = 0; i < len; i++) {
        if (stack_is_empty()) RESTORE_AND_PUSH();
        uint8_t* v = &stack_mem[stack_ptr];
        if (v[0] != 14 && v[0] != 15 && (v[0] < 4 || v[0] > 11)) RESTORE_AND_PUSH();
        uint16_t vsz = elem_size(v);
        uint16_t target = base + i * esz;
        if (tp == 15) {
          if (v[0] == 15) {
            memcpy(&data_pool[target], &v[1], 3);
          }
          else if (v[0] == 14 || (v[0] >= 12 && v[0] <= 13)) {
            uint8_t slen = v[1];
            if (data_ptr + slen > DATA_POOL_SIZE) {
              RESTORE_AND_PUSH();
            }
            uint16_t new_addr = data_ptr;
            if (slen > 0) memcpy(&data_pool[new_addr], &v[2], slen);
            data_ptr += slen;
            data_pool[target] = slen; data_pool[target + 1] = new_addr & 0xFF; data_pool[target + 2] = new_addr >> 8;
          } else {
            RESTORE_AND_PUSH();
          }
        } else {
          uint32_t val32 = 0; uint16_t data_bytes = vsz - 1; uint16_t max_read = (data_bytes > 4) ? 4 : data_bytes;
          for (uint16_t k = 0; k < max_read; k++) val32 |= (uint32_t)v[1 + k] << (k * 8);
          for (uint16_t k = 0; k < esz; k++) data_pool[target + k] = (val32 >> (k * 8)) & 0xFF;
        }
        stack_ptr += vsz;
      }
      mark_dirty(addr); // 🔑 3. Массовая запись в массив (var[] = [1,2,3])
      return;
    }
    if (nxt[0] != 14 && nxt[0] != 15 && (nxt[0] < 4 || nxt[0] > 11)) RESTORE_AND_PUSH();
    uint16_t idx_sz = elem_size(nxt);
    uint32_t idx_val = 0; uint16_t d = idx_sz - 1;
    for (uint16_t k = 0; k < d && k < 4; k++) idx_val |= (uint32_t)nxt[1 + k] << (k * 8);
    stack_ptr += idx_sz;
    if (stack_is_empty() || stack_mem[stack_ptr] != 0x0C) RESTORE_AND_PUSH();
    uint8_t* end_b = &stack_mem[stack_ptr];
    if (end_b[1] != 1 || end_b[2] != ']') RESTORE_AND_PUSH();
    stack_ptr += 3;
    uint16_t base = dict_pool[val_start + 1] | (dict_pool[val_start + 2] << 8);
    uint16_t arr_len = dict_pool[val_start + 3] | (dict_pool[val_start + 4] << 8);
    uint8_t  tp = dict_pool[val_start + 5];
    uint8_t  esz = type_registry[tp].size;
    if (idx_val >= arr_len) {
      currentOutput->println("idx OOB");
      RESTORE_AND_PUSH();
    }
    uint16_t data_addr = base + idx_val * esz;
    bool is_assign = false;
    if (!stack_is_empty() && stack_mem[stack_ptr] == 0x0C) {
      uint8_t* op = &stack_mem[stack_ptr];
      if (op[1] == 1 && op[2] == '=') {
        is_assign = true;
        stack_ptr += 3;
      }
    }
    if (is_assign) {
      if (stack_is_empty()) RESTORE_AND_PUSH();
      uint8_t* v = &stack_mem[stack_ptr];
      if (v[0] != 14 && v[0] != 15 && (v[0] < 4 || v[0] > 11)) RESTORE_AND_PUSH();
      uint16_t vsz = elem_size(v);
      if (tp == 15) {
        if (v[0] == 15) {
          memcpy(&data_pool[data_addr], &v[1], 3);
        }
        else if (v[0] == 14 || (v[0] >= 12 && v[0] <= 13)) {
          uint8_t slen = v[1];
          if (data_ptr + slen > DATA_POOL_SIZE) {
            RESTORE_AND_PUSH();
          }
          uint16_t new_addr = data_ptr;
          if (slen > 0) memcpy(&data_pool[new_addr], &v[2], slen);
          data_ptr += slen;
          data_pool[data_addr] = slen; data_pool[data_addr + 1] = new_addr & 0xFF; data_pool[data_addr + 2] = new_addr >> 8;
        } else {
          RESTORE_AND_PUSH();
        }
      } else {
        uint32_t val32 = 0; uint16_t data_bytes = vsz - 1; uint16_t max_read = (data_bytes > 4) ? 4 : data_bytes;
        for (uint16_t k = 0; k < max_read; k++) val32 |= (uint32_t)v[1 + k] << (k * 8);
        for (uint16_t k = 0; k < esz; k++) data_pool[data_addr + k] = (val32 >> (k * 8)) & 0xFF;
      }
      stack_ptr += vsz;
      mark_dirty(addr); // 🔑 4. Запись в один индекс (var[i] = val)
      return;
    }
    if (tp == 15) {
      uint8_t slen  = data_pool[data_addr];
      uint16_t d_addr = data_pool[data_addr + 1] | (data_pool[data_addr + 2] << 8);
      uint8_t tmp[257] = {0x0E, slen};
      if (slen > 0 && d_addr + slen <= DATA_POOL_SIZE) memcpy(&tmp[2], &data_pool[d_addr], slen);
      stack_push(tmp, 2 + slen); return;
    }
    uint8_t res[8]; res[0] = tp;
    memcpy(&res[1], &data_pool[data_addr], esz);
    stack_push(res, 1 + esz);
    return;
  }
  // 🔑 5. Скалярные операции и присваивания (=, +=, -= и т.д.)
  mark_dirty(addr);
  apply_op(&dict_pool[val_start + 1], val_size - 1, var_tag, val_start + 1);
}
void printRawValue(const uint8_t* buf) {
  if (!buf) return;
  uint8_t tag = buf[0];
  // 🔹 ОБРАБОТКА BOOL (теги 0 и 1)
  if (tag == 0 || tag == 1) {
    currentOutput->printf("%d", tag); // Печатает 0 или 1
    return;
  }
  uint16_t len = tag_size(buf);
  if (tag >= 12 && tag <= 14) {
    currentOutput->write(&buf[2], len);
    return;
  }
  uint32_t v = 0;
  for (uint16_t i = 0; i < len && i < 4; i++) {
    v |= (uint32_t)buf[1 + i] << (i * 8);
  }
  switch (tag) {
    case 10: currentOutput->printf("%ld", (long)(int32_t)v); break;
    case 11: {
        float f;
        memcpy(&f, &buf[1], 4);
        currentOutput->printf("%g", f);
      } break;
    default: currentOutput->printf("%lu", (unsigned long)v); break;
  }
}
void word_print() {
  if (stack_is_empty()) return;
  printRawValue(&stack_mem[stack_ptr]);
  //currentOutput->println();
  stack_ptr += elem_size(&stack_mem[stack_ptr]);
}
void print_word_body(uint16_t addr) {
  if (addr == 0xFFFF || addr >= dict_ptr) return;

  uint8_t name_len = dict_pool[addr + 2];
  uint16_t next = dict_pool[addr] | (dict_pool[addr + 1] << 8);
  uint8_t flags = dict_pool[addr + 4 + name_len];
  uint16_t body_start = addr + 5 + name_len;
  uint16_t scan_end = next ? next : dict_ptr;
  if (flags & 0x04) scan_end -= 4; // Вычитаем размер указателя на C-функцию

  // Печать заголовка
  currentOutput->printf("[%04X] \"", addr);
  currentOutput->write(&dict_pool[addr + 3], name_len);
  currentOutput->printf("\" flags:0x%02X ctx:%u\n", flags, dict_pool[addr + 3 + name_len]);

  if (flags & 0x04) {
    uint32_t fn;
    memcpy(&fn, &dict_pool[(next ? next : dict_ptr) - 4], 4);
    currentOutput->printf("  C-func: 0x%08lX\n", (unsigned long)fn);
  }

  currentOutput->print("  body: ");

  // Парсинг тела (с поддержкой "сырых" литералов без 00 00)
  for (uint16_t i = body_start; i < scan_end; ) {
    uint16_t wa = dict_pool[i] | (dict_pool[i + 1] << 8);

    // 1. Стандартный литерал с префиксом 00 00
    if (wa == 0x0000) {
      if (i + 3 > scan_end) break;
      uint8_t tag = dict_pool[i + 2];
      uint16_t lit_sz = elem_size(&dict_pool[i + 2]);
      uint16_t total_skip = 2 + lit_sz;
      char val_str[48] = {0};

      if (tag == 11 && i + 3 + 4 <= scan_end) {
        float f; memcpy(&f, &dict_pool[i + 3], 4);
        snprintf(val_str, sizeof(val_str), "%g", f);
      } else if (tag >= 4 && tag <= 10) {
        uint32_t v = 0; uint16_t d = lit_sz - 1;
        if (i + 3 + d <= scan_end) {
          for (uint16_t k = 0; k < d && k < 4; k++) v |= (uint32_t)dict_pool[i + 3 + k] << (k * 8);
        }
        if (tag == 5 || tag == 7 || tag == 10) snprintf(val_str, sizeof(val_str), "%ld", (long)(int32_t)v);
        else snprintf(val_str, sizeof(val_str), "%lu", (unsigned long)v);
      } else if (tag == 14 && i + 4 <= scan_end) {
        uint8_t sl = dict_pool[i + 3];
        if (sl > 20) sl = 20;
        snprintf(val_str, sizeof(val_str), "\"%.*s\"", sl, &dict_pool[i + 4]);
      } else {
        strcpy(val_str, "data");
      }
      currentOutput->printf("lit(%s:%s) ", tag_short(tag), val_str);
      i += total_skip;
    }
    // 2. "Сырой" литерал (без префикса 00 00), например, наш NAME (0x0D)
    else if (dict_pool[i] >= 4 && dict_pool[i] <= 14) {
      uint8_t tag = dict_pool[i];
      uint16_t lit_sz = elem_size(&dict_pool[i]);
      char val_str[48] = {0};

      if ((tag == 13 || tag == 14) && i + 2 <= scan_end) {
        uint8_t sl = dict_pool[i + 1];
        if (sl > 20) sl = 20;
        snprintf(val_str, sizeof(val_str), "\"%.*s\"", sl, &dict_pool[i + 2]);
      } else {
        strcpy(val_str, "raw_data");
      }
      currentOutput->printf("%s:%s ", tag_short(tag), val_str);
      i += lit_sz;
    }
    // 3. Стандартный адрес слова
    else {
      const char* wname = nullptr;
      char buf[64];
      uint16_t p = 0;
      while (p < dict_ptr) {
        if (p == wa) {
          uint8_t l = dict_pool[p + 2];
          if (l > 63) l = 63;
          memcpy(buf, &dict_pool[p + 3], l);
          buf[l] = '\0';
          wname = buf;
          break;
        }
        uint16_t nx = dict_pool[p] | (dict_pool[p + 1] << 8);
        if (nx == 0) break;
        p = nx;
      }

      uint16_t skip = 2;
      if (wname) {
        currentOutput->print(wname);
        if ((strcmp(wname, "if") == 0 || strcmp(wname, "goto") == 0) && i + 4 <= scan_end) {
          uint16_t target = dict_pool[i + 2] | (dict_pool[i + 3] << 8);
          currentOutput->printf("@%04X", target);
          skip += 2;
        }
        currentOutput->print(" ");
      } else {
        if (wa >= 5 && wa + 10 < dict_ptr) {
          uint8_t nl = dict_pool[wa + 2];
          if (nl <= 32 && wa + 5 + nl < dict_ptr) {
            uint8_t fl = dict_pool[wa + 4 + nl];
            if (fl == 0x16) {
              char mbuf[33];
              memcpy(mbuf, &dict_pool[wa + 3], nl);
              mbuf[nl] = '\0';
              currentOutput->print(mbuf);
              currentOutput->print(" ");
              i += 2;
              continue;
            }
          }
        }
        if (wa >= body_start && wa < scan_end) {
          currentOutput->printf("@%04X", wa);
        } else {
          currentOutput->print("?");
        }
        currentOutput->print(" ");
      }
      i += skip;
    }
  }
  currentOutput->println();
}

void exec_word(uint16_t addr) {
uint8_t nlen  = dict_pool[addr + 2];
uint8_t flags = dict_pool[addr + 4 + nlen];

// === 3. CHAIN ===
if (flags & FLAG_CHAIN) {
uint16_t body_start = addr + 5 + nlen;
uint16_t head = dict_pool[body_start] | (dict_pool[body_start + 1] << 8);
// 🔑 Запрет присваивания к нити
if (!stack_is_empty()) {
uint8_t* top = &stack_mem[stack_ptr];
if (top[0] == 0x0C) {
uint8_t op_len = top[1];
if (op_len == 1 || op_len == 2) {
char op[3] = {0};
memcpy(op, &top[2], op_len);
if (strcmp(op, "=") == 0 || strcmp(op, "+=") == 0 ||
strcmp(op, "-=") == 0 || strcmp(op, "*=") == 0 ||
strcmp(op, "/=") == 0) {
currentOutput->print("⚠️ error: cannot assign to chain '");
currentOutput->write(&dict_pool[addr + 3], nlen);
currentOutput->println("'");
return;
}
}
}
}
if (!stack_is_empty()) {
uint8_t* top = &stack_mem[stack_ptr];
if (top[0] == 0x0C && top[1] == 4 && memcmp(&top[2], "bead", 4) == 0) {
uint16_t bead_sz = 6;
uint8_t* action_ptr = &stack_mem[stack_ptr + bead_sz];
if (action_ptr[0] == 0x0D) {
uint8_t alen = action_ptr[1]; if (alen > 63) alen = 63;
char act_name[64]; memcpy(act_name, &action_ptr[2], alen); act_name[alen] = '\0';
uint16_t action_addr = dict_find(act_name);
if (action_addr != 0xFFFF && data_ptr + 4 <= DATA_POOL_SIZE) {
uint16_t new_node = data_ptr; data_ptr += 4;
data_pool[new_node] = 0; data_pool[new_node + 1] = 0;
data_pool[new_node + 2] = action_addr & 0xFF; data_pool[new_node + 3] = action_addr >> 8;
if (head == 0) {
dict_pool[body_start] = new_node & 0xFF;
dict_pool[body_start + 1] = new_node >> 8;
}
else {
uint16_t curr = head;
while (true) {
uint16_t nxt = data_pool[curr] | (data_pool[curr + 1] << 8);
if (nxt == 0)break;
curr = nxt;
} data_pool[curr] = new_node & 0xFF;
data_pool[curr + 1] = new_node >> 8;
}
}
stack_ptr += bead_sz + (2 + alen); return;
}
}
}
uint16_t node = head; uint16_t saved_sp = stack_ptr;
while (node != 0) {
if (node + 3 >= DATA_POOL_SIZE) break;
uint16_t next = data_pool[node] | (data_pool[node + 1] << 8);
uint16_t action = data_pool[node + 2] | (data_pool[node + 3] << 8);
stack_ptr = saved_sp;
if (action != 0) exec_word(action);
node = next;
}
return;
}

// === 4. Чистый алиас ===
if (flags == FLAG_ALIAS) {
uint16_t target = dict_pool[addr + 5 + nlen] | ((uint16_t)dict_pool[addr + 6 + nlen] << 8);
exec_word(target); return;
}

// === 5. Контекст ===
if (flags == 0x0C) {
uint16_t body_start = addr + 5 + nlen;
if (body_start < dict_ptr && dict_pool[body_start] == 0x03) {
currentContext = dict_pool[body_start + 1];
return;
}
}

// === 6. Скомпилированное слово ===
if (flags & 0x08) {
uint16_t next = dict_pool[addr] | (dict_pool[addr + 1] << 8);
uint16_t body_end = next ? next : dict_ptr;

// 🔹 СОХРАНЯЕМ кадр локалов перед входом в слово
uint16_t saved_local_dict_ptr = local_dict_ptr;
if (local_frame_ptr < LOCAL_FRAME_STACK_SIZE) {
local_frame_stack[local_frame_ptr++] = local_dict_ptr;
}

rstack_push_frame(ip, body_end);
vm_run(addr + 5 + nlen, body_end);
ip = rstack_pop_ret();

// 🔹 ВОССТАНАВЛИВАЕМ кадр локалов после выхода из слова
if (local_frame_ptr > 0) {
local_dict_ptr = local_frame_stack[--local_frame_ptr];
} else {
// Страховка: если стек кадров почему-то пуст — восстанавливаем из локальной переменной
local_dict_ptr = saved_local_dict_ptr;
}
return;
}

// === 7. Маркер/Встроенная функция ===
if (flags == 0x16) {
uint8_t a[3] = {0x12, (uint8_t)(addr & 0xFF), (uint8_t)(addr >> 8)};
stack_push(a, 3);
uint32_t fn; uint16_t next = dict_pool[addr] | (dict_pool[addr + 1] << 8);
uint16_t end = next ? next : dict_ptr;
memcpy(&fn, &dict_pool[end - 4], 4);
((void(*)())fn)();
return;
}

// === 8. Переменная / Внутреннее слово ===
if ((flags & 0x02) || (flags & 0x08)) {
uint8_t a[3] = {0x12, (uint8_t)(addr & 0xFF), (uint8_t)(addr >> 8)};
stack_push(a, 3);
}
if (flags & 0x04) {
uint16_t next = dict_pool[addr] | (dict_pool[addr + 1] << 8);
uint16_t end = next ? next : (addr >= dict_ptr ? DICT_POOL_SIZE : dict_ptr);
uint32_t fn; memcpy(&fn, &dict_pool[end - 4], 4);
((void(*)())fn)();
}
}

void word_dict_dump() {
  for (uint16_t p = 0; p < dict_ptr; ) {
    uint16_t next = dict_pool[p] | (dict_pool[p + 1] << 8);
    uint16_t end = next ? next : dict_ptr;
    uint8_t nlen = dict_pool[p + 2];
    char name[33];
    if (nlen > 32) nlen = 32;
    memcpy(name, &dict_pool[p + 3], nlen);
    name[nlen] = '\0';
    uint8_t ctx = dict_pool[p + 3 + nlen];
    uint8_t flags = dict_pool[p + 4 + nlen];
    currentOutput->printf("[%04X->%04X] %02X \"%s\" '%02X' {%02X} [", p, end, nlen, name, ctx, flags);
    uint16_t b = p + 5 + nlen;
    if (flags & 0x04) {
      uint16_t ptr_pos = end - 4;
      for (uint16_t i = b; i < ptr_pos; i++) currentOutput->printf("%02X ", dict_pool[i]);
      currentOutput->print("| ");
      for (uint16_t i = ptr_pos; i < end; i++) currentOutput->printf("%02X ", dict_pool[i]);
    } else {
      for (uint16_t i = b; i < end; i++) currentOutput->printf("%02X ", dict_pool[i]);
    }
    currentOutput->println("]");
    if (next == 0) break;
    p = next;
  }
}
void word_pool_dump() {
  uint16_t start = 0;
  uint16_t end   = data_ptr; // По умолчанию: от 0 до текущего конца

  // 1. Снимаем начальный адрес (если есть)
  if (!stack_is_empty()) {
    uint8_t buf[4]; uint16_t sz = stack_pop(buf, sizeof(buf));
    if (sz >= 3 && (buf[0] == 0x12 || buf[0] == 0x06)) {
      start = buf[1] | (buf[2] << 8);
    } else if (sz >= 2 && buf[0] >= 4 && buf[0] <= 11) {
      uint32_t v = 0;
      for (uint16_t k = 0; k < sz - 1 && k < 4; k++) v |= (uint32_t)buf[1 + k] << (k * 8);
      if (v < DATA_POOL_SIZE) start = (uint16_t)v;
    }
  }

  // 2. Снимаем длину (если есть второй аргумент)
  if (!stack_is_empty()) {
    uint8_t buf[4]; uint16_t sz = stack_pop(buf, sizeof(buf));
    if (sz >= 2 && buf[0] >= 4 && buf[0] <= 11) {
      uint32_t l = 0;
      for (uint16_t k = 0; k < sz - 1 && k < 4; k++) l |= (uint32_t)buf[1 + k] << (k * 8);
      if (start + l <= DATA_POOL_SIZE) end = start + (uint16_t)l;
    }
  }

  if (start >= data_ptr && start >= end) {
    currentOutput->println("pool empty or address out of range");
    return;
  }
  if (end > data_ptr) end = data_ptr;

  currentOutput->println("data_pool dump:");
  for (uint16_t i = start; i < end; i += 16) {
    currentOutput->printf("%04X: ", i);
    for (int j = 0; j < 16; j++) {
      if (i + j < end) currentOutput->printf("%02X ", data_pool[i + j]);
      else currentOutput->print("   ");
    }
    currentOutput->print("| ");
    for (int j = 0; j < 16; j++) {
      if (i + j < end) {
        char c = data_pool[i + j];
        currentOutput->write((c >= 32 && c < 127) ? c : '.');
      }
    }
    currentOutput->println();
  }
}
void addInternalWord(const char* name, void (*func)()) {
  uint16_t len = strlen(name);
  uint16_t size = 9 + len;
  if (dict_ptr + size > DICT_POOL_SIZE) return;
  uint16_t addr = dict_ptr; dict_ptr += size;
  uint16_t pos = write_name(addr + 2, name);
  dict_pool[pos] = currentContext; dict_pool[pos + 1] = 0x04;
  uint32_t ptr = (uint32_t)(uintptr_t)func;
  memcpy(&dict_pool[pos + 2], &ptr, 4);
  link_word(addr);
}
void addMarkerWord(const char* name) {
  uint16_t len = strlen(name);
  uint16_t size = 10 + len;
  if (dict_ptr + size > DICT_POOL_SIZE) return;
  uint16_t addr = dict_ptr; dict_ptr += size;
  uint16_t pos = write_name(addr + 2, name);
  dict_pool[pos] = currentContext;
  dict_pool[pos + 1] = 0x16;
  dict_pool[pos + 2] = 0x0C;
  uint32_t fn = (uint32_t)(uintptr_t)wordMarker;
  memcpy(&dict_pool[pos + 3], &fn, 4);
  link_word(addr);
}
void word_exit() {
  ip = rstack_peek_end(); // Прыгаем на конец ТЕКУЩЕГО слова (из верхнего кадра)
}

void processToken(const char* token) {
  if (!token || !*token) return;
  size_t t_len = strlen(token);
  if (token[0] == '"' && token[t_len - 1] == '"' && t_len >= 2) {
    size_t str_len = t_len - 2;
    if (str_len > 255) str_len = 255;
    uint8_t buf[257]; buf[0] = 0x0E; buf[1] = (uint8_t)str_len;
    memcpy(&buf[2], &token[1], str_len);
    if (!stack_is_empty() && stack_mem[stack_ptr] == 0x0C) apply_op(&buf[1], str_len, 0x0E, 0);
    else stack_push(buf, 2 + str_len); return;
  }
  uint16_t addr = dict_find(token);
  if (addr != 0xFFFF) {
    exec_word(addr);
    return;
  }
  uint8_t tag = 0; uint32_t val_u = 0; int32_t val_s = 0; bool is_num = false; char* endptr;
  const char* suffixes[]  = {"u8", "i8", "u16", "i16", "u24", "u32", "i32", "f"};
  const uint8_t suf_tags[] = {4,  5,  6,   7,   8,   9,  10,  11};
  bool suffix_found = false; int matched_idx = -1;
  for (int i = 0; i < 8; i++) {
    size_t s_len = strlen(suffixes[i]);
    if (t_len > s_len && strcmp(token + t_len - s_len, suffixes[i]) == 0) {
      tag = suf_tags[i];
      suffix_found = true;
      matched_idx = i;
      break;
    }
  }
  if (suffix_found) {
    size_t num_len = t_len - strlen(suffixes[matched_idx]);
    char num_buf[64]; if (num_len >= sizeof(num_buf)) num_len = sizeof(num_buf) - 1;
    strncpy(num_buf, token, num_len); num_buf[num_len] = '\0';
    if (tag == 11) {
      strtof(num_buf, &endptr);
      if (endptr == num_buf + num_len) is_num = true;
    }
    else {
      val_u = strtoul(num_buf, &endptr, 0);
      if (endptr == num_buf + num_len) {
        is_num = true;
        val_s = (int32_t)val_u;
      }
    }
  } else {
if (t_len > 2 && token[0] == '0' && (token[1] == 'x' || token[1] == 'X')) { // исправлено
    val_u = strtoul(token + 2, &endptr, 16);
    if (endptr == token + t_len) {
        is_num = true;
        // 🔹 Определяем тип по количеству hex-цифр, а не по значению
        size_t hex_digits = t_len - 2;
        if (hex_digits > 8) hex_digits = 8;
        if (hex_digits <= 2)      tag = 4;   // u8:  0x0 .. 0xFF
        else if (hex_digits <= 4) tag = 6;   // u16: 0x000 .. 0xFFFF
        else if (hex_digits <= 6) tag = 8;   // u24: 0x00000 .. 0xFFFFFF
        else                      tag = 9;   // u32: 0x0000000 .. 0xFFFFFFFF
    }
}
    if (!is_num) {
      bool is_float_fmt = false;
      for (size_t k = 0; k < t_len; k++) {
        if (token[k] == '.' || token[k] == 'e' || token[k] == 'E') {
          is_float_fmt = true;
          break;
        }
      }
      if (is_float_fmt) {
        strtof(token, &endptr);
        if (endptr == token + t_len) {
          is_num = true;
          tag = 11;
        }
      }
      if (!is_num) {
        val_s = strtol(token, &endptr, 10);
        if (endptr == token + t_len) {
          is_num = true;
          val_u = (uint32_t)val_s;
          tag = current_type;
        }
      }
    }
  }
  if (is_num) {
    uint8_t buf[8]; size_t sz = 0; buf[0] = tag;
    switch (tag) {
      case 4:  buf[1] = (uint8_t)val_u; sz = 2; break; case 5:  buf[1] = (int8_t)val_s;  sz = 2; break;
      case 6:  *(uint16_t*)&buf[1] = (uint16_t)val_u; sz = 3; break; case 7:  *(int16_t*)&buf[1]  = (int16_t)val_s;  sz = 3; break;
      case 8:  buf[1] = (uint8_t)val_u; buf[2] = (uint8_t)(val_u >> 8); buf[3] = (uint8_t)(val_u >> 16); sz = 4; break;
      case 9:  *(uint32_t*)&buf[1] = val_u; sz = 5; break; case 10: *(int32_t*)&buf[1]  = val_s; sz = 5; break;
      case 11: {
          float f = strtof(token, NULL);
          memcpy(&buf[1], &f, 4);
          sz = 5;
          break;
        }
    }
    bool call_apply = false;
    if (!stack_is_empty() && stack_mem[stack_ptr] == 0x0C) {
      uint8_t op_len = stack_mem[stack_ptr + 1];
      if (op_len == 1) {
        char c = stack_mem[stack_ptr + 2];
        if (c != '[' && c != ']' && c != '=') call_apply = true;
      } else {
        call_apply = true;
      }
    }
    if (call_apply) apply_op(&buf[1], sz - 1, tag, 0); else stack_push(buf, sz); return;
  }
  if (!stack_is_empty()) {
    uint8_t* top = &stack_mem[stack_ptr];
    if (top[0] == 0x0C && top[1] == 1 && top[2] == '=') {
      uint16_t cur = stack_ptr; uint16_t eq_sz = elem_size(&stack_mem[cur]); cur += eq_sz;
      bool is_array = false; uint16_t arr_m_sz = 0;
      if (cur < STACK_SIZE) {
        uint8_t* p = &stack_mem[cur];
        if (p[0] == 0x0C && p[1] == 5 && memcmp(&p[2], "array", 5) == 0) {
          is_array = true;
          arr_m_sz = elem_size(p);
          cur += arr_m_sz;
        }
      }
      uint8_t type_tag = 0; uint16_t type_m_sz = 0;
      if (cur < STACK_SIZE) {
        uint8_t* p = &stack_mem[cur];
        if (p[0] == 0x0C && p[1] >= 2 && p[1] <= 4) {
          const char* tnames[] = {"u8", "i8", "u16", "i16", "u24", "u32", "i32", "f", "$S"};
          const uint8_t ttags[] = {4, 5, 6, 7, 8, 9, 10, 11, 15};
          for (int i = 0; i < 9; i++) {
            if (strncmp((char*)&p[2], tnames[i], p[1]) == 0) {
              type_tag = ttags[i];
              type_m_sz = elem_size(p);
              cur += type_m_sz;
              break;
            }
          }
        }
      }
      if (type_tag == 0) {
        uint8_t* sv = &stack_mem[cur];
        if (sv[0] >= 4 && sv[0] <= 11) type_tag = sv[0];
        else type_tag = current_type;
      }
      if (is_array) {
        uint16_t sz_sz = 0; uint16_t arr_len = 0;
        if (cur < STACK_SIZE) {
          uint8_t* p = &stack_mem[cur];
          if (p[0] >= 4 && p[0] <= 11) {
            uint32_t v = 0;
            uint16_t d = elem_size(p) - 1;
            for (uint16_t k = 0; k < d && k < 4; k++) v |= (uint32_t)p[1 + k] << (k * 8);
            arr_len = (uint16_t)v;
            sz_sz = elem_size(p);
          }
        }
        if (arr_len > 0) {
          uint8_t el_sz = type_registry[type_tag].size; uint32_t total = (uint32_t)arr_len * el_sz;
          if (data_ptr + total <= DATA_POOL_SIZE) {
            uint16_t a = data_ptr; data_ptr += total; memset(&data_pool[a], 0, total);
            uint8_t arr_body[6]; arr_body[0] = 17; arr_body[1] = a & 0xFF; arr_body[2] = a >> 8; arr_body[3] = arr_len & 0xFF; arr_body[4] = arr_len >> 8; arr_body[5] = type_tag;
            uint16_t ws = 9 + t_len + 6;
            if (dict_ptr + ws <= DICT_POOL_SIZE) {
              uint16_t d = dict_ptr;
              dict_ptr += ws;
              dict_pool[d + 2] = (uint8_t)t_len;
              memcpy(&dict_pool[d + 3], token, t_len);
              uint16_t p = d + 3 + t_len;
              dict_pool[p] = currentContext;
              dict_pool[p + 1] = 0x06;
              memcpy(&dict_pool[p + 2], arr_body, 6);
              uint32_t fn = (uint32_t)(uintptr_t)choiceFunc;
              memcpy(&dict_pool[p + 8], &fn, 4);
              link_word(d);
            }
          }
          stack_ptr += eq_sz + arr_m_sz + type_m_sz + sz_sz; return;
        }
      } else {
        uint16_t val_ptr = cur; uint8_t* val = &stack_mem[val_ptr]; uint16_t val_sz = elem_size(val);
        if (val_sz > 0 && (val[0] >= 0x0C && val[0] <= 0x0E)) {
          uint8_t slen = (val[0] == 0x0E) ? val[1] : val[0]; const uint8_t* src = (val[0] == 0x0E) ? &val[2] : &val[1];
          if (data_ptr + slen > DATA_POOL_SIZE) {
            currentOutput->println("data_pool overflow");
            return;
          }
          uint16_t d_addr = data_ptr; memcpy(&data_pool[d_addr], src, slen); data_ptr += slen;
          uint8_t ref_body[4] = {15, slen, (uint8_t)(d_addr & 0xFF), (uint8_t)(d_addr >> 8)};
          uint16_t ws = 9 + t_len + 4; if (dict_ptr + ws > DICT_POOL_SIZE) {
            currentOutput->println("dict overflow");
            return;
          }
          uint16_t a = dict_ptr; dict_ptr += ws; dict_pool[a + 2] = (uint8_t)t_len; memcpy(&dict_pool[a + 3], token, t_len); uint16_t p = a + 3 + t_len; dict_pool[p] = currentContext; dict_pool[p + 1] = 0x06; memcpy(&dict_pool[p + 2], ref_body, 4); uint32_t fn = (uint32_t)(uintptr_t)choiceFunc; memcpy(&dict_pool[p + 6], &fn, 4); link_word(a);
          stack_ptr += eq_sz + val_sz; return;
        }
        // 🔹 ССЫЛКА НА МАССИВ
        uint8_t* src_header = nullptr;
        if (val_sz == 6 && (val[0] == 17 || val[0] == 20)) {
          src_header = val;
        }
        else if (val_sz == 3 && val[0] == 0x12) {
          uint16_t v_addr = val[1] | (val[2] << 8);
          if (v_addr < dict_ptr) {
            uint8_t vn = dict_pool[v_addr + 2]; uint16_t body = v_addr + 5 + vn;
            uint8_t t = dict_pool[body];
            if ((t == 17 || t == 20) && body + 6 <= dict_ptr) {
              src_header = &dict_pool[body];
            }
          }
        }
        if (src_header) {
          uint16_t ws = 9 + t_len + 6;
          if (dict_ptr + ws > DICT_POOL_SIZE) {
            currentOutput->println("dict overflow");
            return;
          }
          uint16_t a = dict_ptr; dict_ptr += ws; dict_pool[a] = 0; dict_pool[a + 1] = 0; dict_pool[a + 2] = t_len;
          memcpy(&dict_pool[a + 3], token, t_len); uint16_t p = a + 3 + t_len;
          dict_pool[p] = currentContext; dict_pool[p + 1] = 0x06; memcpy(&dict_pool[p + 2], src_header, 6); dict_pool[p + 2] = 20;
          uint32_t fn = (uint32_t)(uintptr_t)choiceFunc; memcpy(&dict_pool[p + 8], &fn, 4); link_word(a);
          stack_ptr += eq_sz + val_sz; return;
        }
        if (val_sz > 0 && val[0] <= 20) {
          uint8_t final_tag = type_tag; uint8_t data_sz = type_registry[final_tag].size; uint16_t final_sz = 1 + data_sz;
          uint8_t new_body[8]; new_body[0] = final_tag; int32_t src_i = 0; float src_f = 0.0;
          if (val[0] == 11) {
            memcpy(&src_f, &val[1], 4);
            src_i = (int32_t)src_f;
          }
          else {
            uint32_t u = 0;
            uint16_t d = val_sz - 1;
            for (uint16_t k = 0; k < d && k < 4; k++) u |= (uint32_t)val[1 + k] << (k * 8);
            if (d == 1 && (val[1] & 0x80)) u |= 0xFFFFFF00;
            if (d == 2 && (val[2] & 0x80)) u |= 0xFFFF0000;
            if (d == 3 && (val[3] & 0x80)) u |= 0xFF000000;
            src_i = (int32_t)u;
            src_f = (float)src_i;
          }
          if (final_tag == 11) {
            float v = (val[0] == 11 || type_tag != current_type) ? src_f : (float)src_i;
            memcpy(&new_body[1], &v, 4);
            final_sz = 5;
          }
          else {
            int32_t v = (val[0] == 11) ? (int32_t)src_f : src_i;
            uint16_t d = data_sz;
            for (uint16_t k = 0; k < d && k < 4; k++) new_body[1 + k] = (v >> (k * 8)) & 0xFF;
            final_sz = 1 + d;
          }
          uint16_t ws = 9 + t_len + final_sz;
          if (dict_ptr + ws <= DICT_POOL_SIZE) {
            uint16_t a = dict_ptr;
            dict_ptr += ws;
            dict_pool[a + 2] = (uint8_t)t_len;
            memcpy(&dict_pool[a + 3], token, t_len);
            uint16_t p = a + 3 + t_len;
            dict_pool[p] = currentContext;
            dict_pool[p + 1] = 0x06;
            memcpy(&dict_pool[p + 2], new_body, final_sz);
            uint32_t fn = (uint32_t)(uintptr_t)choiceFunc;
            memcpy(&dict_pool[p + 2 + final_sz], &fn, 4);
            link_word(a);
          }
          stack_ptr += eq_sz + type_m_sz + val_sz; return;
        }
      }
    }
  }
  uint8_t buf[257]; buf[0] = 0x0D; buf[1] = (uint8_t)t_len; memcpy(&buf[2], token, t_len); stack_push(buf, 2 + t_len);
}
void executeLine(const char* line) {
  // 🔹 JSON IN-PLACE: правим line напрямую, печатаем line, проваливаемся в парсер
  char* p = (char*)line; // line всегда указывает на line_buf[256] — запись безопасна
  size_t n = strlen(p);
  if (n > 3 && p[0] == '{' && p[n - 1] == '}') {
    char* out = p;
    char* in  = p + 1;
    bool is_key = true;
    while (in < p + n - 1) {
      char c = *in++;
      if (c == '{' || c == '}' || c == ',') {
        *out++ = ' ';
        is_key = true;
        continue;
      }
      if (c == '"') {
        if (is_key) continue;  // кавычки ключей → игнор
      }
      else if (c == ':') {
        *out++ = ' ';  // : →  =  (с пробелами!)
        *out++ = '=';
        *out++ = ' ';
        is_key = false;
        continue;
      }
      *out++ = c;
    }
    *out = '\0'; // обрезаем строку прямо в буфере
    // currentOutput->print("JSON→HDL: "); currentOutput->println(line); // ← ПЕЧАТАЕМ line — именно она пойдёт дальше
  }


  // 🔹 1. ПРЕ-ОБРАБОТКА: фильтруем /* ... */ и // (с защитой от кавычек)
  char clean_line[256];
  uint16_t src = 0, dst = 0;
  uint16_t len = strlen(line);
  bool in_quotes = false;
  while (src < len && dst < 255) {
    char c = line[src];
    if (g_in_block_comment) {
      if (c == '*' && src + 1 < len && line[src + 1] == '/') {
        g_in_block_comment = false;
        src += 2;
      } else {
        src++;
      }
      continue;
    }
    if (c == '"') {
      in_quotes = !in_quotes;
      clean_line[dst++] = c;
      src++;
      continue;
    }
    if (in_quotes) {
      clean_line[dst++] = c;
      src++;
      continue;
    }
    if (c == '/' && src + 1 < len && line[src + 1] == '/') {
      break;
    }
    if (c == '/' && src + 1 < len && line[src + 1] == '*') {
      g_in_block_comment = true;
      src += 2;
      continue;
    }
    clean_line[dst++] = c;
    src++;
  }
  clean_line[dst] = '\0';
  len = dst;

  // 🔹 2. ТОКЕНИЗАЦИЯ (пишем в глобальный массив для прямого доступа bead)
  g_tok_count = 0;
  uint16_t pos = 0;
  in_quotes = false;
  while (pos < len && g_tok_count < 64) {
    while (pos < len && clean_line[pos] == ' ' && !in_quotes) pos++;
    if (pos >= len) break;
    g_tok_start[g_tok_count] = &clean_line[pos];
    uint16_t start = pos;
    while (pos < len) {
      char c = clean_line[pos];
      if (c == '"') in_quotes = !in_quotes;
      else if (c == ' ' && !in_quotes) break;
      pos++;
    }
    g_tok_len[g_tok_count] = pos - start;
    if (g_tok_len[g_tok_count] > 0) g_tok_count++;
  }

  // 🔹 3. ИСПОЛНЕНИЕ: R2L-цикл с управлением индексом для bead
  for (g_tok_idx = g_tok_count - 1; g_tok_idx >= 0; g_tok_idx--) {
    char token[257];
    uint8_t l = g_tok_len[g_tok_idx];
    if (l > 256) l = 256;
    memcpy(token, g_tok_start[g_tok_idx], l);
    token[l] = '\0';

    if (g_compile_mode) {
      compile_token(token);
      if (!g_compile_mode) break;
    } else {
      processToken(token);
    }
  }
  g_tok_idx = -1; // Сброс после завершения строки
}

void word_const() {
  uint8_t nlen = dict_pool[dict_last + 2];
  uint16_t fpos = dict_last + 4 + nlen;
  uint8_t flags = dict_pool[fpos];
  if (flags & 0x01) {
    currentOutput->println("already a constant !");
    return;
  }
  if (!(flags & 0x02)) {
    currentOutput->println("constant not created !");
    return;
  }
  dict_pool[fpos] |= 0x01;
}
void word_ls() {
  File root = FILESYSTEM.open("/");
  if (!root) {
    currentOutput->println("fs mount failed");
    return;
  }
  size_t p_len = strlen(g_currentDir);
  bool is_root = (p_len == 1 && g_currentDir[0] == '/');
  char dirs[32][32]; uint8_t d_cnt = 0;
  struct {
    char name[32];
    uint32_t size;
  } files[64]; uint8_t f_cnt = 0;
  uint32_t total_size = 0;
  while (File f = root.openNextFile()) {
    const char* full = f.path();
    if (!full) continue;
    bool in_scope = false;
    if (is_root) {
      in_scope = (full[0] == '/');
    }
    else {
      if (strncmp(full, g_currentDir, p_len) == 0 && full[p_len] == '/') in_scope = true;
    }
    if (!in_scope) continue;
    const char* rel = is_root ? (full + 1) : (full + p_len + 1);
    if (rel[0] == '\0') continue;
    const char* slash = strchr(rel, '/');
    bool is_dir = f.isDirectory() || (slash != nullptr);
    if (is_dir) {
      size_t len = slash ? (slash - rel) : strlen(rel);
      if (len > 0 && len < 32) {
        char nm[32]; memcpy(nm, rel, len); nm[len] = '\0';
        bool dup = false;
        for (uint8_t i = 0; i < d_cnt; i++) if (strcmp(dirs[i], nm) == 0) {
            dup = true;
            break;
          }
        if (!dup && d_cnt < 32) strcpy(dirs[d_cnt++], nm);
      }
    } else {
      if (f_cnt < 64) {
        strcpy(files[f_cnt].name, rel);
        files[f_cnt].size = f.size();
        total_size += files[f_cnt].size;
        f_cnt++;
      }
    }
  }
  root.close();
  currentOutput->println("<DIR>            .");
  currentOutput->println("<DIR>            ..");
  for (uint8_t i = 0; i < d_cnt; i++) {
    currentOutput->print("<DIR>            ");
    currentOutput->println(dirs[i]);
  }
  for (uint8_t i = 0; i < f_cnt; i++) {
    currentOutput->print("      ");
    currentOutput->printf("%10lu ", (unsigned long)files[i].size);
    currentOutput->println(files[i].name);
  }
  uint32_t free_bytes = FILESYSTEM.totalBytes() - FILESYSTEM.usedBytes();
  currentOutput->printf("%6lu files        %lu bytes\n", (unsigned long)f_cnt, (unsigned long)total_size);
  currentOutput->printf("%6lu dirs     %lu bytes free\n", (unsigned long)(d_cnt + 2), (unsigned long)free_bytes);
}
void word_cd() {
  if (stack_is_empty()) {
    currentOutput->println("stack empty");
    return;
  }
  uint8_t tag = stack_mem[stack_ptr];
  if (tag != 0x0C && tag != 0x0D && tag != 0x0E) {
    currentOutput->println("invalid type");
    return;
  }
  uint8_t len = stack_mem[stack_ptr + 1];
  if (len > 63) len = 63;
  char txt[64];
  memcpy(txt, &stack_mem[stack_ptr + 2], len);
  txt[len] = '\0';
  stack_ptr += 2 + len;
  if (len == 1 && txt[0] == '/') {
    strcpy(g_currentDir, "/");
    currentOutput->println("changed to root");
    return;
  }
  if (strcmp(txt, "..") == 0) {
    if (strlen(g_currentDir) <= 1) {
      currentOutput->println("already at root");
      return;
    }
    char* last_slash = strrchr(g_currentDir + 1, '/');
    if (last_slash) *last_slash = '\0'; else strcpy(g_currentDir, "/");
    currentOutput->println("moved up");
    return;
  }
  char new_path[128];
  if (strlen(g_currentDir) > 1) snprintf(new_path, sizeof(new_path), "%s/%s", g_currentDir, txt);
  else snprintf(new_path, sizeof(new_path), "/%s", txt);
  bool found = false;
  File root = FILESYSTEM.open("/");
  if (root) {
    size_t p_len = strlen(new_path);
    while (File f = root.openNextFile()) {
      const char* fp = f.path();
      if (strncmp(fp, new_path, p_len) == 0 && fp[p_len] == '/') {
        found = true;
        break;
      }
    }
    root.close();
  }
  if (found) {
    strcpy(g_currentDir, new_path);
    currentOutput->println("directory changed");
  }
  else {
    currentOutput->println("directory not found");
  }
}
void word_prompt() {
  currentOutput->print(g_currentDir);
  currentOutput->print(" ok>");
}
void exec_context_word() {
  uint8_t abuf[3];
  if (stack_pop(abuf, 3) != 3 || abuf[0] != 0x12) return;
  uint16_t addr = abuf[1] | (abuf[2] << 8);
  if (addr == 0 || addr >= dict_ptr) return;
  uint8_t nlen = dict_pool[addr + 2];
  currentContext = dict_pool[addr + nlen + 6];
}
void create_internal_word(uint8_t* name_ptr, const uint8_t* body_data, uint16_t body_size, uint8_t flags, void (*func)()) {
  uint8_t nlen = name_ptr[1];
  if (nlen == 0 || nlen > 63) return;
  uint16_t size = 9 + nlen + body_size;
  if (dict_ptr + size > DICT_POOL_SIZE) return;
  uint16_t a = dict_ptr;
  dict_ptr += size;
  dict_pool[a] = 0x00; dict_pool[a + 1] = 0x00;
  dict_pool[a + 2] = nlen;
  memcpy(&dict_pool[a + 3], &name_ptr[2], nlen);
  uint16_t p = a + 3 + nlen;
  dict_pool[p] = 0x00;
  dict_pool[p + 1] = flags;
  memcpy(&dict_pool[p + 2], body_data, body_size);
  uint32_t fn = (uint32_t)(uintptr_t)func;
  memcpy(&dict_pool[p + 2 + body_size], &fn, 4);
  link_word(a);
}
void word_cont() {
  if (stack_is_empty() || stack_mem[stack_ptr] != 0x0D) return;
  uint8_t body[2] = {0x03, g_next_ctx};
  create_internal_word(&stack_mem[stack_ptr], body, 2, 0x0C, exec_context_word);
  stack_ptr += 2 + stack_mem[stack_ptr + 1];
  currentContext = g_next_ctx;
  g_next_ctx++;
  // print_context();
  // currentOutput->println(); // ← Вывод при создании
}
void print_context() {
  if (currentContext == 0) {
    currentOutput->print("main");
    return;
  }
  uint16_t p = 0;
  bool found = false;
  while (p < dict_ptr) {
    uint16_t next = dict_pool[p] | (dict_pool[p + 1] << 8);
    uint8_t nlen = dict_pool[p + 2];
    uint8_t flags = dict_pool[p + 4 + nlen];
    if (flags == 0x0C) {
      uint16_t body_start = p + 5 + nlen;
      if (body_start + 2 <= dict_ptr && dict_pool[body_start] == 0x03) {
        uint8_t stored_ctx_id = dict_pool[body_start + 1];
        if (stored_ctx_id == currentContext) {
          char name[64];
          if (nlen > 63) nlen = 63;
          memcpy(name, &dict_pool[p + 3], nlen);
          name[nlen] = '\0';
          currentOutput->print(name);
          found = true;
          break;
        }
      }
    }
    if (next == 0) break;
    p = next;
  }
  if (!found) {
    currentOutput->print("unknown_ctx_");
    currentOutput->print(currentContext);
  }
}
void word_main() {
  currentContext = 0;
  // print_context(); currentOutput->println(); // ← Вывод при возврате в main
}


void printValue(const uint8_t* buf) {
  if (!buf) return;
  uint8_t tag = buf[0];
  if (tag > 20) {
    currentOutput->print("??(?)");
    return;
  }
  if (tag == 0) {
    currentOutput->print("B(0)");
    return;
  }
  if (tag == 1) {
    currentOutput->print("B(1)");
    return;
  }
  if (tag == 2) {
    currentOutput->print("N()");
    return;
  }

  const TypeInfo* t = &type_registry[tag];
  const char* name = t->short_name[0] ? t->short_name : t->name;

  switch (t->category) {
    case CAT_UINT: {
        uint32_t v = 0;
        uint8_t dlen = t->size;
        for (uint8_t i = 0; i < dlen && i < 4; i++) v |= (uint32_t)buf[1 + i] << (i * 8);
        currentOutput->printf("%s(%lu)", name, (unsigned long)v);
        return;
      }
    case CAT_INT: {
        uint32_t v = 0;
        uint8_t dlen = t->size;
        for (uint8_t i = 0; i < dlen && i < 4; i++) v |= (uint32_t)buf[1 + i] << (i * 8);
        if (dlen == 1 && (buf[1] & 0x80)) v |= 0xFFFFFF00;
        if (dlen == 2 && (buf[2] & 0x80)) v |= 0xFFFF0000;
        if (dlen == 3 && (buf[3] & 0x80)) v |= 0xFF000000;
        currentOutput->printf("%s(%ld)", name, (long)(int32_t)v);
        return;
      }
    case CAT_FLOAT: {
        float f; memcpy(&f, &buf[1], 4);
        currentOutput->printf("f(%g)", f);
        return;
      }
    case CAT_TEXT: {
        uint8_t len = buf[1];
        currentOutput->printf("%s(\"", name);
        if (len > 0) {
          uint16_t pl = (len > 32) ? 32 : len;
          currentOutput->write(&buf[2], pl);
          if (len > 32) currentOutput->print("...");
        }
        currentOutput->print("\")");
        return;
      }
    case CAT_REF: { // $STRING (15)
        uint8_t len = buf[1];
        uint16_t addr = buf[2] | (buf[3] << 8);
        currentOutput->printf("$S(%u@%04X\"", len, addr);
        if (addr + len <= DATA_POOL_SIZE && len > 0) {
          uint16_t pl = (len > 24) ? 24 : len;
          currentOutput->write(&data_pool[addr], pl);
          if (len > 24) currentOutput->print("...");
        }
        currentOutput->print("\")");
        return;
      }
    case CAT_STRUCT: {
        if (tag == 17 || tag == 20) {
          uint16_t base = buf[1] | (buf[2] << 8);
          uint16_t len  = buf[3] | (buf[4] << 8);
          uint8_t  tp   = buf[5];
          const char* tname = (tp <= 20 && tp > 0) ? tag_short(tp) : "??";
          if (tag == 20) currentOutput->printf("ref(@%04X,len:%u,%s)", base, len, tname);
          else currentOutput->printf("arr(@%04X,len:%u,%s)", base, len, tname);
        } else {
          currentOutput->printf("%s(?)", name);
        }
        return;
      }
    case CAT_ADDR:
    case CAT_LIST: {
        currentOutput->printf("%s(0x", name);
        uint8_t dlen = t->size > 0 ? t->size : 4;
        for (int i = 0; i < dlen; i++) currentOutput->printf("%02X", buf[1 + i]);
        currentOutput->print(")");
        return;
      }
    default:
      currentOutput->printf("%s(?)", name);
  }
}
void printStack() {
  if (stack_is_empty()) {
    print_context();
    currentOutput->println(" [ ]");
    return;
  }
  uint16_t addrs[MAX_STACK_PRINT]; int n = 0;
  for (uint16_t p = stack_ptr; n < MAX_STACK_PRINT; ) {
    uint16_t sz = elem_size(&stack_mem[p]);
    if (sz == 0 || p + sz > STACK_SIZE) break;
    addrs[n++] = p; p += sz;
  }
  print_context();
  currentOutput->print(" [ ");
  for (int i = n - 1; i >= 0; i--) {
    printValue(&stack_mem[addrs[i]]);
    if (i > 0) currentOutput->print(" ");
  }
  currentOutput->println(" ]");
}
void printWordByAddr(uint16_t addr) {
  if (addr >= dict_ptr) return;
  uint16_t next = dict_pool[addr] | ((uint16_t)dict_pool[addr + 1] << 8);
  uint8_t nlen = dict_pool[addr + 2];
  uint8_t flags = dict_pool[addr + 4 + nlen];
  uint16_t end = next ? next : dict_ptr;
  char name[32];
  memcpy(name, &dict_pool[addr + 3], nlen);
  name[nlen] = 0;
  currentOutput->printf("[%04X->%04X] \"%-12s\" | ", addr, end, name);
  if (flags == 0) currentOutput->print("SYS");
  else {
    bool f = true;
    if (flags & 0x01) {
      if (!f) currentOutput->print(", ");
      currentOutput->print("CONST");
      f = false;
    }
    if (flags & 0x02) {
      if (!f) currentOutput->print(", ");
      currentOutput->print("VAR");
      f = false;
    }
    if (flags & 0x04) {
      if (!f) currentOutput->print(", ");
      currentOutput->print("INTERNAL");
      f = false;
    }
    if (flags & 0x08) {
      if (!f) currentOutput->print(", ");
      currentOutput->print("COMPILED");
      f = false;
    }
  }
  currentOutput->print(" | BODY: ");
  uint16_t b = addr + 5 + nlen;
  uint16_t body_end = (flags & 0x04) ? (end - 4) : end;
  while (b < body_end) {
    uint8_t tag = dict_pool[b];
    uint16_t sz = tag_size(&dict_pool[b]);
    if (sz == 0 || b + 1 + sz > body_end) break;
    currentOutput->printf("[%s] ", tag_short(tag));
    for (int i = 0; i < sz; i++) {
      uint8_t v = dict_pool[b + 1 + i];
      if ((tag >= 12 && tag <= 14) && i > 0) currentOutput->write(v);
      else currentOutput->printf("%02X ", v);
    }
    b += 1 + sz;
  }
  if (flags & 0x04) {
    uint32_t fn;
    memcpy(&fn, &dict_pool[end - 4], 4);
    currentOutput->printf("| FN: 0x%08X", fn);
  }
  currentOutput->println();
}
void word_hexdump() {
  uint16_t start = 0;
  uint16_t len = dict_ptr;
  if (!stack_is_empty()) {
    uint8_t buf[4];
    uint16_t sz = stack_pop(buf, sizeof(buf));
    if (sz >= 3 && buf[0] == 0x12) {
      start = buf[1] | (buf[2] << 8);
    }
    else if (sz >= 3 && buf[0] == 0x06) {
      start = buf[1] | (buf[2] << 8);
    }
  }
  if (start >= dict_ptr) {
    currentOutput->println("address out of range");
    return;
  }
  uint16_t end = dict_ptr;
  for (uint16_t i = start; i < end; i += 16) {
    currentOutput->printf("%04X: ", i);
    for (int j = 0; j < 16; j++) {
      if (i + j < end) currentOutput->printf("%02X ", dict_pool[i + j]);
      else currentOutput->print("   ");
    }
    currentOutput->print("| ");
    for (int j = 0; j < 16; j++) {
      if (i + j < end) {
        char c = dict_pool[i + j];
        currentOutput->write((c >= 32 && c < 127) ? c : '.');
      }
    }
    currentOutput->println();
  }
}


void word_device() {
    // 1. Проверка: стек не пустой
    if (stack_is_empty()) {
        currentOutput->println("device: укажите имя устройства");
        return;
    }
    
    // 2. Проверка: на стеке должно быть NAME (тег 0x0D)
    uint8_t* top = &stack_mem[stack_ptr];
    if (top[0] != 0x0D) {
        // 🔑 ИСПРАВЛЕНО: правильное сообщение об ошибке
        currentOutput->print("device: ожидалось имя слова, получено ");
        currentOutput->println(tag_short(top[0]));
        return;
    }
    
    // 3. Проверка длины имени
    uint8_t nlen = top[1];
    if (nlen == 0 || nlen > 63) {
        currentOutput->println("device: недопустимая длина имени (1-63 символа)");
        return;
    }
    
    // 4. Извлекаем имя
    char tmp[64];
    memcpy(tmp, &top[2], nlen);
    tmp[nlen] = '\0';
    
    // 5. Проверка: слово не должно уже существовать
    if (dict_find(tmp) != 0xFFFF) {
        currentOutput->print("device: слово '");
        currentOutput->print(tmp);
        currentOutput->println("' уже существует!");
        return;
    }
    
    // 6. Создаём устройство (слово с флагом VAR | INTERNAL)
    uint8_t body[3] = {6, (uint8_t)(g_device_counter & 0xFF), (uint8_t)(g_device_counter >> 8)};
    create_internal_word(&stack_mem[stack_ptr], body, 3, 0x06, choiceFunc);
    
    // 7. Устанавливаем контекст
    uint16_t ctx_pos = dict_last + 3 + dict_pool[dict_last + 2];
    dict_pool[ctx_pos] = currentContext;
    
    // 8. Снимаем NAME со стека и увеличиваем счётчик
    stack_ptr += 2 + nlen;
    g_device_counter++;
}

void wordNop() {}
// --- Вспомогательная: измеряет и кладёт размер на стек ---
static void _lenCalc(bool dropOriginal) {
  if (stack_is_empty()) {
    currentOutput->println("⚠️ len: стек пуст");
    return;
  }

  uint8_t* top = &stack_mem[stack_ptr];
  uint8_t tag = top[0];
  uint16_t sz = elem_size(top);

  // 🔹 Обработка массива/структуры (тег 17 = ARRAY)
  if (tag == 17) {
    uint16_t arr_len = top[3] | (top[4] << 8);  // длина массива
    if (dropOriginal) stack_ptr += sz;
    pushInt32((int32_t)arr_len);  // возвращаем как i32
    return;
  }

  // 🔹 Обработка строки (тег 14 = STRING)
  if (tag == 14) {
    uint8_t str_len = top[1];
    if (dropOriginal) stack_ptr += sz;
    pushInt32((int32_t)str_len);
    return;
  }

  // 🔹 Скаляры: возвращаем размер элемента в байтах
  uint8_t elem_sz = type_registry[tag].size;
  if (elem_sz == 0 && tag >= 12 && tag <= 14) elem_sz = top[1]; // для текстовых

  if (dropOriginal) stack_ptr += sz;
  pushInt32((int32_t)elem_sz);
}

// --- len: измерить и УДАЛИТЬ исходное значение ---
void lenWord() {
  _lenCalc(true);
}

// --- len?: измерить и ОСТАВИТЬ исходное значение ---
void lenPeekWord() {
  _lenCalc(false);
}

void wordLit() {
  if (ip >= dict_ptr) return;
  uint8_t* lit_ptr = &dict_pool[ip];
  uint16_t sz = elem_size(lit_ptr);
  if (sz == 0 || ip + sz > dict_ptr) return;

  uint8_t tag = lit_ptr[0];
  bool call_apply = false;

  // 🔍 Смотрим на маркер над литералом (точная копия логики REPL)
  if (!stack_is_empty() && stack_mem[stack_ptr] == 0x0C) {
    uint8_t op_len = stack_mem[stack_ptr + 1];
    if (op_len == 1) {
      char c = stack_mem[stack_ptr + 2];
      // Структурные маркеры и = обрабатываются отдельно (choiceFunc / массивы)
      if (c != '[' && c != ']' && c != '=') call_apply = true;
    } else {
      call_apply = true; // Операторы: ==, !=, +, -, <, >, += и т.д.
    }
  }

  ip += sz; // Сдвигаем ip до передачи управления

  if (call_apply) {
    // Маркер есть → вычисляем операцию сразу, как в терминале
    apply_op(&lit_ptr[1], sz - 1, tag, 0);
  } else {
    // Маркера нет → просто кладём на стек
    stack_push(lit_ptr, sz);
  }
}
void word_words() {
  uint16_t p = 0;
  uint8_t line_pos = 0;
  bool any_printed = false;
  while (p < dict_ptr) {
    uint16_t next = dict_pool[p] | (dict_pool[p + 1] << 8);
    uint8_t nlen = dict_pool[p + 2];
    uint8_t ctx = dict_pool[p + 3 + nlen];
    uint8_t flags = dict_pool[p + 4 + nlen];
    // Фильтр по текущему контексту
    if (ctx == currentContext) {
      // Проверка: является ли слово определением контекста
      bool is_ctx_def = false;
      if (flags == 0x0C) {
        uint16_t body = p + 5 + nlen;
        if (body < dict_ptr && dict_pool[body] == 0x03) is_ctx_def = true;
      }
      uint8_t word_len = nlen + 1 + (is_ctx_def ? 1 : 0); // +1 за символ ▸
      // Перенос строки при лимите 64 символа
      if (line_pos > 0 && line_pos + word_len > 64) {
        currentOutput->println();
        line_pos = 0;
      }
      if (is_ctx_def) currentOutput->print("▸");
      currentOutput->write(&dict_pool[p + 3], nlen);
      currentOutput->print(' ');
      line_pos += word_len;
      any_printed = true;
    }
    if (next == 0) break;
    p = next;
  }
  if (any_printed) currentOutput->println();
}
// === ОБРАБОТЧИК ЦЕПОЧЕК (bead + исполнение) ===
void exec_chain() {
  // 1. Получаем адрес самого слова-цепочки из стека (ADDR 0x12)
  uint8_t abuf[3];
  if (stack_pop(abuf, 3) != 3 || abuf[0] != 0x12) return;
  uint16_t addr = abuf[1] | (abuf[2] << 8);
  if (addr == 0 || addr >= dict_ptr) return;

  uint8_t nlen = dict_pool[addr + 2];
  uint16_t body_start = addr + 5 + nlen;

  // 2. Проверяем наличие маркера bead на стеке
  if (!stack_is_empty()) {
    uint8_t* top = &stack_mem[stack_ptr];
    if (top[0] == 0x0C && top[1] == 4 && memcmp(&top[2], "bead", 4) == 0) {
      uint16_t bead_sz = 6;
      uint8_t* action_ptr = &stack_mem[stack_ptr + bead_sz];

      if (action_ptr[0] == 0x0D) { // Ожидаем NAME действия
        uint8_t alen = action_ptr[1];
        if (alen > 63) alen = 63;
        char act_name[64];
        memcpy(act_name, &action_ptr[2], alen);
        act_name[alen] = '\0';

        uint16_t action_addr = dict_find(act_name);
        if (action_addr != 0xFFFF && data_ptr + 4 <= DATA_POOL_SIZE) {
          uint16_t new_node = data_ptr;
          data_pool[new_node]   = 0; data_pool[new_node + 1] = 0; // next = 0
          data_pool[new_node + 2] = action_addr & 0xFF;
          data_pool[new_node + 3] = action_addr >> 8;
          data_ptr += 4;

          uint16_t head = dict_pool[body_start] | (dict_pool[body_start + 1] << 8);
          if (head == 0) {
            dict_pool[body_start]   = new_node & 0xFF;
            dict_pool[body_start + 1] = new_node >> 8;
          } else {
            uint16_t curr = head;
            while (true) {
              uint16_t nxt = data_pool[curr] | (data_pool[curr + 1] << 8);
              if (nxt == 0) break;
              curr = nxt;
            }
            data_pool[curr]   = new_node & 0xFF;
            data_pool[curr + 1] = new_node >> 8;
          }
        }
        // Потребляем bead и NAME. Тело слова НЕ пушится обратно.
        stack_ptr += bead_sz + (2 + alen);
        return;
      }
    }
  }

  // 3. Исполнение цепи (FIFO)
  uint16_t node = dict_pool[body_start] | (dict_pool[body_start + 1] << 8);
  uint16_t saved_sp = stack_ptr;
  while (node != 0) {
    if (node + 3 >= DATA_POOL_SIZE) break;
    uint16_t next   = data_pool[node]   | (data_pool[node + 1]   << 8);
    uint16_t action = data_pool[node + 2] | (data_pool[node + 3] << 8);

    stack_ptr = saved_sp; // Изоляция стека для каждого звена
    if (action != 0) exec_word(action); // action=0 → безопасный nop
    node = next;
  }
}

void word_cord() {
    if (stack_is_empty()) {
        currentOutput->println("cord: stack empty");
        return;
    }
    uint8_t* top = &stack_mem[stack_ptr];
    if (top[0] != 0x0D) {
        currentOutput->println("cord: NAME expected");
        return;
    }
    uint8_t nlen = top[1];
    if (nlen == 0 || nlen > 63) {
        currentOutput->println("cord: invalid name");
        return;
    }
    char tmp[64]; memcpy(tmp, &top[2], nlen); tmp[nlen] = '\0';
    if (dict_find(tmp) != 0xFFFF) {
        currentOutput->print("cord: exists: "); currentOutput->println(tmp); return;
    }

    // 🔑 УБРАЛИ создание узла-заглушки в data_pool
    // Голова цепи = 0 (пустая цепь) — экономим 4 байта data_pool

    uint16_t rec_sz = 11 + nlen;
    if (dict_ptr + rec_sz > DICT_POOL_SIZE) {
        currentOutput->println("cord: dict_pool full");
        return;
    }
    uint16_t a = dict_ptr; dict_ptr += rec_sz;
    dict_pool[a] = 0; dict_pool[a + 1] = 0;
    dict_pool[a + 2] = nlen;
    memcpy(&dict_pool[a + 3], &top[2], nlen);
    uint16_t p = a + 3 + nlen;
    dict_pool[p]     = currentContext;
    dict_pool[p + 1] = 0x06 | FLAG_CHAIN; // VAR | INTERNAL | CHAIN
    dict_pool[p + 2] = 0;  // 🔑 head = 0 (пустая цепь)
    dict_pool[p + 3] = 0;
    uint32_t fn = (uint32_t)(uintptr_t)choiceFunc;
    memcpy(&dict_pool[p + 4], &fn, 4);
    link_word(a);
    stack_ptr += 2 + nlen;
}


void word_drop()  {
  if (!stack_is_empty()) stack_ptr += 1 + tag_size(&stack_mem[stack_ptr]);
}
void vm_run(uint16_t start, uint16_t end) {
  ip = start;
  while (ip < end) {
    uint16_t wa = dict_pool[ip] | (dict_pool[ip + 1] << 8);
    ip += 2;
    exec_word(wa);
  }
}
inline void rstack_push_frame(uint16_t ret_addr, uint16_t word_end) {
  if (rstack_ptr < 4) return;
  rstack_ptr -= 4;
  rstack_mem[rstack_ptr]     = ret_addr & 0xFF;
  rstack_mem[rstack_ptr + 1] = ret_addr >> 8;
  rstack_mem[rstack_ptr + 2] = word_end & 0xFF;
  rstack_mem[rstack_ptr + 3] = word_end >> 8;
}
inline uint16_t rstack_pop_ret() {
  if (rstack_ptr + 4 > RSTACK_SIZE) return 0;
  uint16_t val = rstack_mem[rstack_ptr] | (rstack_mem[rstack_ptr + 1] << 8);
  rstack_ptr += 4;
  return val;
}
inline uint16_t rstack_peek_end() {
  if (rstack_ptr + 4 > RSTACK_SIZE) return 0;
  return rstack_mem[rstack_ptr + 2] | (rstack_mem[rstack_ptr + 3] << 8);
}
void word_type() {
  if (stack_is_empty()) return;
  uint8_t* top = &stack_mem[stack_ptr];
  uint8_t tag = top[0];
  // Принимаем только NAME (0x0D) или STRING (0x0E)
  if (tag != 0x0D && tag != 0x0E) return;
  uint16_t sz = elem_size(top);
  uint8_t len = top[1];
  if (len > 255) len = 255; // защита от переполнения буфера
  char fname[257];
  memcpy(fname, &top[2], len);
  fname[len] = '\0';
  // Снимаем элемент со стека сразу (семантика stack-языков)
  stack_ptr += sz;
  // Собираем полный путь с учётом текущей директории
  char full_path[256];
  if (strlen(g_currentDir) > 1) {
    snprintf(full_path, sizeof(full_path), "%s/%s", g_currentDir, fname);
  } else {
    snprintf(full_path, sizeof(full_path), "/%s", fname);
  }
  File f = FILESYSTEM.open(full_path, "r");
  if (!f) {
    currentOutput->print("error: cannot open ");
    currentOutput->println(full_path);
    return;
  }
  // Чтение и вывод блоками по 64 байта (безопасно для стека ESP)
  uint8_t buf[64];
  size_t n;
  while ((n = f.read(buf, sizeof(buf))) > 0) {
    currentOutput->write(buf, n);
  }
  f.close();
}

void word_load() {
if (stack_is_empty()) {
currentOutput->println("load error: stack empty");
return;
}
uint8_t* top = &stack_mem[stack_ptr];
uint8_t tag = top[0];
if (tag != 0x0D && tag != 0x0E) {
currentOutput->println("load error: expected filename");
return;
}
uint16_t sz = elem_size(top);
uint8_t len = top[1];
if (len > 255) len = 255;
char fname[257];
memcpy(fname, &top[2], len);
fname[len] = '\0';
stack_ptr += sz;

char full_path[64];
size_t dlen = strlen(g_currentDir);
size_t flen = strlen(fname);
if (dlen + 1 + flen + 1 > sizeof(full_path)) {
currentOutput->println("load error: path too long (>63 chars)");
return;
}
if (dlen > 1) snprintf(full_path, sizeof(full_path), "%s/%s", g_currentDir, fname);
else snprintf(full_path, sizeof(full_path), "/%s", fname);

File f = FILESYSTEM.open(full_path, "r");
if (!f || f.isDirectory()) {
currentOutput->print("load error: file not found or is directory: ");
currentOutput->println(full_path);
if (f) f.close();
return;
}

String line;
uint16_t lines_executed = 0;
while (f.available()) {
line = f.readStringUntil('\n');
if (line.length() == 0) continue;
if (line.endsWith("\r")) line.remove(line.length() - 1);
executeLine(line.c_str());
lines_executed++;
}
f.close();
}
void word_save() {
  char full_path[256];
  char* name_ptr = nullptr;
  uint8_t name_len = 0;
  // 1. Проверяем имя на стеке
  if (stack_is_empty() || (stack_mem[stack_ptr] != 0x0D && stack_mem[stack_ptr] != 0x0E)) {
    // Имени нет → дефолт
    strcpy(full_path, "/start.hdl");
  } else {
    uint8_t* top = &stack_mem[stack_ptr];
    name_len = top[1];
    name_ptr = (char*)&top[2]; // ← Прямой указатель на имя в памяти стека
    // 2. Собираем путь сразу в full_path за один проход
    if (strlen(g_currentDir) > 1) {
      uint8_t d_len = strlen(g_currentDir);
      memcpy(full_path, g_currentDir, d_len);
      full_path[d_len] = '/';
      memcpy(&full_path[d_len + 1], name_ptr, name_len);
      full_path[d_len + 1 + name_len] = '\0';
    } else {
      full_path[0] = '/';
      memcpy(&full_path[1], name_ptr, name_len);
      full_path[1 + name_len] = '\0';
    }
    // Имя потреблено
    stack_ptr += elem_size(top);
  }
  // 3. Открытие и дамп
  File f = FILESYSTEM.open(full_path, "w");
  if (!f) {
    currentOutput->print("save error: ");
    currentOutput->println(full_path);
    return;
  }
  uint8_t hdr[16] = {0};
  hdr[0] = 0x48; hdr[1] = 0x44; hdr[2] = 0x4C; hdr[3] = 0x53;
  *(uint16_t*)&hdr[4] = stack_ptr;
  *(uint16_t*)&hdr[6] = dict_ptr;
  *(uint16_t*)&hdr[8] = data_ptr;
  hdr[10] = currentContext;
  hdr[11] = g_next_ctx;
  hdr[12] = current_type;
  f.write(hdr, sizeof(hdr));
  f.write(stack_mem, STACK_SIZE);
  f.write(dict_pool, dict_ptr);
  f.write(data_pool, data_ptr);
  f.flush();
  f.close();
  currentOutput->println("saved");
}
void word_restore() {
  char full_path[256];
  char* name_ptr = nullptr;
  uint8_t name_len = 0;
  // 1. Определяем имя файла (аналогично save)
  if (stack_is_empty() || (stack_mem[stack_ptr] != 0x0D && stack_mem[stack_ptr] != 0x0E)) {
    if (strlen(g_currentDir) > 1) snprintf(full_path, sizeof(full_path), "%s/start.words", g_currentDir);
    else strcpy(full_path, "/start.hdl");
  } else {
    uint8_t* top = &stack_mem[stack_ptr];
    name_len = top[1];
    name_ptr = (char*)&top[2];
    if (strlen(g_currentDir) > 1) {
      uint8_t d_len = strlen(g_currentDir);
      memcpy(full_path, g_currentDir, d_len);
      full_path[d_len] = '/';
      memcpy(&full_path[d_len + 1], name_ptr, name_len);
      full_path[d_len + 1 + name_len] = '\0';
    } else {
      full_path[0] = '/';
      memcpy(&full_path[1], name_ptr, name_len);
      full_path[1 + name_len] = '\0';
    }
    stack_ptr += elem_size(top); // Потребляем имя
  }
  // 2. Открываем файл
  File f = FILESYSTEM.open(full_path, "r");
  if (!f) {
    currentOutput->print("restore error: ");
    currentOutput->println(full_path);
    return;
  }
  // 3. Читаем заголовок
  uint8_t hdr[16];
  if (f.read(hdr, 16) != 16) {
    f.close();
    currentOutput->println("restore error: invalid header");
    return;
  }
  // 4. Проверка магии "HDLS"
  if (hdr[0] != 0x48 || hdr[1] != 0x44 || hdr[2] != 0x4C || hdr[3] != 0x53) {
    f.close(); currentOutput->println("restore error: bad magic"); return;
  }
  // 5. Извлекаем указатели и метаданные
  uint16_t new_stack_ptr = *(uint16_t*)&hdr[4];
  uint16_t new_dict_ptr  = *(uint16_t*)&hdr[6];
  uint16_t new_data_ptr  = *(uint16_t*)&hdr[8];
  uint8_t  new_ctx       = hdr[10];
  uint8_t  new_next_ctx  = hdr[11];
  uint8_t  new_type      = hdr[12];
  // 6. Защита от повреждения памяти
  if (new_stack_ptr > STACK_SIZE || new_dict_ptr > DICT_POOL_SIZE || new_data_ptr > DATA_POOL_SIZE) {
    f.close(); currentOutput->println("restore error: pointers out of range"); return;
  }
  // 7. Мгновенное чтение пулов
  f.read(stack_mem, STACK_SIZE);
  f.read(dict_pool, new_dict_ptr);
  f.read(data_pool, new_data_ptr);
  f.close();
  // 8. Применяем состояние
  stack_ptr      = new_stack_ptr;
  dict_ptr       = new_dict_ptr;
  data_ptr       = new_data_ptr;
  currentContext = new_ctx;
  g_next_ctx     = new_next_ctx;
  current_type   = new_type;
  currentOutput->println("restored");
}

void compile_token(const char* token) {
  static uint16_t w_skip_stack[8];
  static uint8_t  w_depth = 0;
  
  size_t t_len = strlen(token);
  if (t_len == 0) return;

  // === ПЕРЕХВАТ +task и -task ===
  if (strcmp(token, "+task") == 0 || strcmp(token, "-task") == 0) {
    bool is_add = (strcmp(token, "+task") == 0);
    int idx1 = g_tok_idx - 1;
    int idx2 = g_tok_idx - 2;
    
    if (idx1 >= 0 && idx2 >= 0) {
      uint8_t len1 = g_tok_len[idx1]; if (len1 > 31) len1 = 31;
      char str1[32]; memcpy(str1, g_tok_start[idx1], len1); str1[len1] = '\0';
      
      uint8_t len2 = g_tok_len[idx2]; if (len2 > 31) len2 = 31;
      char str2[32]; memcpy(str2, g_tok_start[idx2], len2); str2[len2] = '\0';
      
      char* endptr;
      uint32_t val1 = (uint32_t)strtoul(str1, &endptr, 10);
      bool is_num1 = (endptr != str1 && *endptr == '\0');
      
      char tname[64] = {0}; uint32_t interval = 0; bool valid = false;
      
      if (is_num1 && val1 > 0) {
        interval = val1; memcpy(tname, str2, len2); tname[len2] = '\0'; valid = true;
      } else {
        uint32_t val2 = (uint32_t)strtoul(str2, &endptr, 10);
        if ((endptr != str2 && *endptr == '\0') && val2 > 0) {
          interval = val2; memcpy(tname, str1, len1); tname[len1] = '\0'; valid = true;
        }
      }
      
      if (valid) {
        uint16_t addr = dict_find(tname);
        if (addr != 0xFFFF) {
          dict_pool[dict_ptr++] = 0x00; dict_pool[dict_ptr++] = 0x00;
          dict_pool[dict_ptr++] = 9;
          memcpy(&dict_pool[dict_ptr], &interval, 4); dict_ptr += 4;
          
          dict_pool[dict_ptr++] = 0x00; dict_pool[dict_ptr++] = 0x00;
          dict_pool[dict_ptr++] = 6;
          dict_pool[dict_ptr++] = addr & 0xFF;
          dict_pool[dict_ptr++] = addr >> 8;
          
          uint16_t sched_addr = dict_find(is_add ? "__schedule_task__" : "__remove_task__");
          if (sched_addr != 0xFFFF) {
            dict_pool[dict_ptr++] = sched_addr & 0xFF;
            dict_pool[dict_ptr++] = sched_addr >> 8;
          }
          
          g_tok_idx -= 2;
          return;
        }
      }
    }
  }

  // 1. Завершение компиляции
  if (token[0] == ';' && t_len == 1) {
    link_word(g_compile_header);
    g_compile_mode = false;
    g_compile_header = 0xFFFF;
    return;
  }

  // === ЦИКЛЫ ===
  if (strcmp(token, "{") == 0) {
    g_loop_target = dict_ptr;
    return;
  }
  
  if (strcmp(token, "while") == 0) {
    uint16_t addr = dict_find(token);
    if (addr != 0xFFFF) {
      dict_pool[dict_ptr++] = addr & 0xFF;
      dict_pool[dict_ptr++] = addr >> 8;
    }
    
    if (w_depth < 8) {
      w_skip_stack[w_depth] = dict_ptr;
      w_depth++;
    }
    dict_pool[dict_ptr++] = 0;
    dict_pool[dict_ptr++] = 0;
    return;
  }

  // === ЗАКРЫТИЕ БЛОКА } ===
  if (strcmp(token, "}") == 0) {
    uint16_t end_pos = dict_ptr;
    
    if (g_if_branch_pos) {
      dict_pool[g_if_branch_pos]     = end_pos & 0xFF;
      dict_pool[g_if_branch_pos + 1] = end_pos >> 8;
      g_if_branch_pos = 0;
    } else if (g_loop_target != 0) {
      uint16_t addr_goto = dict_find("goto");
      if (addr_goto != 0xFFFF) {
        dict_pool[dict_ptr++] = addr_goto & 0xFF;
        dict_pool[dict_ptr++] = addr_goto >> 8;
      }
      dict_pool[dict_ptr++] = g_loop_target & 0xFF;
      dict_pool[dict_ptr++] = g_loop_target >> 8;
      g_loop_target = 0;
      
      if (w_depth > 0) {
        w_depth--;
        uint16_t skip_pos = w_skip_stack[w_depth];
        uint16_t skip_target = dict_ptr;
        dict_pool[skip_pos]     = skip_target & 0xFF;
        dict_pool[skip_pos + 1] = skip_target >> 8;
      }
    }
    return;
  }

  // === ВЕТВЛЕНИЯ ===
  if (strcmp(token, "if") == 0) {
    uint16_t addr = dict_find("if");
    if (addr != 0xFFFF) {
      dict_pool[dict_ptr++] = addr & 0xFF;
      dict_pool[dict_ptr++] = addr >> 8;
    }
    
    g_if_branch_pos = dict_ptr;
    dict_pool[dict_ptr++] = 0;
    dict_pool[dict_ptr++] = 0;
    return;
  }

  // 2. Строка "..."
  if (token[0] == '"' && token[t_len - 1] == '"' && t_len >= 2) {
    size_t l = t_len - 2; if (l > 255) l = 255;
    dict_pool[dict_ptr++] = 0x00; dict_pool[dict_ptr++] = 0x00;
    dict_pool[dict_ptr++] = 0x0E;
    dict_pool[dict_ptr++] = (uint8_t)l;
    for (size_t i = 0; i < l; i++) dict_pool[dict_ptr++] = token[1 + i];
    return;
  }

  // =========================================================================
  // 🔑 ПРАВИЛЬНЫЙ ПОРЯДОК:
  // 1. Проверяем, является ли это попыткой присваивания
  // 2. Ищем в словаре
  // 3. Если присваивание и НЕ найдено → создаём локальную
  // 4. Если найдено → компилируем адрес (работает для глобальных переменных!)
  // =========================================================================
  
  // Проверяем, является ли это попыткой присваивания (x =)
  bool is_assign_attempt = false;
  if (g_tok_idx + 1 < g_tok_count) {
    uint8_t prev_len = g_tok_len[g_tok_idx + 1];
    if (prev_len == 1 && g_tok_start[g_tok_idx + 1][0] == '=') {
      is_assign_attempt = true;
    }
  }
  
  // Ищем слово в словаре (глобальные переменные и функции)
  uint16_t addr = dict_find(token);
  
  // Если это присваивание и слово НЕ найдено — создаём локальную переменную
  if (is_assign_attempt && addr == 0xFFFF) {
    uint16_t found_id = 0;
    
    for (uint8_t i = 0; i < g_local_map_count; i++) {
      if (strcmp(g_local_map[i].original_name, token) == 0) {
        found_id = g_local_map[i].local_id;
        break;
      }
    }
    
    if (found_id == 0) {
      g_local_id++;
      found_id = g_local_id;
      
      if (g_local_map_count < 16) {
        strncpy(g_local_map[g_local_map_count].original_name, token, 31);
        g_local_map[g_local_map_count].original_name[31] = '\0';
        g_local_map[g_local_map_count].local_id = found_id;
        g_local_map_count++;
      }
    }
    
    uint16_t resolver_addr = dict_find("__local__");
    if (resolver_addr != 0xFFFF) {
      dict_pool[dict_ptr++] = resolver_addr & 0xFF;
      dict_pool[dict_ptr++] = resolver_addr >> 8;
    }
    
    char var_name[32];
    snprintf(var_name, sizeof(var_name), "l%u", found_id);
    uint8_t nlen = strlen(var_name);
    if (nlen > 63) nlen = 63;
    
    dict_pool[dict_ptr++] = 0x0D;
    dict_pool[dict_ptr++] = nlen;
    for (uint8_t i = 0; i < nlen; i++) {
      dict_pool[dict_ptr++] = var_name[i];
    }
    return;
  }
  
  // Если слово найдено в словаре — компилируем его адрес
  // Это работает и для глобальных переменных, и для функций!
  if (addr != 0xFFFF) {
    uint8_t nlen  = dict_pool[addr + 2];
    uint8_t flags = dict_pool[addr + 4 + nlen];
    
    if (flags == FLAG_ALIAS) {
      addr = dict_pool[addr + 5 + nlen] | ((uint16_t)dict_pool[addr + 6 + nlen] << 8);
    }
    
    dict_pool[dict_ptr++] = addr & 0xFF;
    dict_pool[dict_ptr++] = addr >> 8;
    return;
  }

  // 4. Число → литерал
  uint8_t tag = 0;
  uint32_t val_u = 0;
  float val_f = 0.0f;
  bool is_num = false;
  char* endptr;
  
  const char* suffixes[]  = {"u8", "i8", "u16", "i16", "u24", "u32", "i32", "f"};
  const uint8_t suf_tags[] = {4,  5,  6,   7,   8,   9,  10,  11};
  bool suffix_found = false;
  
  for (int i = 0; i < 8; i++) {
    size_t sl = strlen(suffixes[i]);
    if (t_len > sl && strcmp(token + t_len - sl, suffixes[i]) == 0) {
      tag = suf_tags[i];
      suffix_found = true;
      
      char num_buf[32];
      size_t num_len = t_len - sl;
      if (num_len >= sizeof(num_buf)) num_len = sizeof(num_buf) - 1;
      strncpy(num_buf, token, num_len);
      num_buf[num_len] = '\0';
      
      if (tag == 11) {
        val_f = strtof(num_buf, &endptr);
        if (endptr == num_buf + num_len) is_num = true;
      } else {
        val_u = strtoul(num_buf, &endptr, 0);
        if (endptr == num_buf + num_len) is_num = true;
      }
      break;
    }
  }
  
  if (!suffix_found) {
    if (t_len > 2 && token[0] == '0' && (token[1] == 'x' || token[1] == 'X')) {
      val_u = strtoul(token + 2, &endptr, 16);
      if (endptr == token + t_len) {
        is_num = true;
        // 🔹 ИСПРАВЛЕНО: определяем тип по количеству hex-цифр, а не по значению
        size_t hex_digits = t_len - 2;
        if (hex_digits > 8) hex_digits = 8;
        if (hex_digits <= 2)      tag = 4;   // u8:  0x0 .. 0xFF
        else if (hex_digits <= 4) tag = 6;   // u16: 0x000 .. 0xFFFF
        else if (hex_digits <= 6) tag = 8;   // u24: 0x00000 .. 0xFFFFFF
        else                      tag = 9;   // u32: 0x0000000 .. 0xFFFFFFFF
      }
    } else {
      bool is_float_fmt = false;
      for (size_t k = 0; k < t_len; k++) {
        if (token[k] == '.' || token[k] == 'e' || token[k] == 'E') {
          is_float_fmt = true;
          break;
        }
      }
      
      if (is_float_fmt) {
        val_f = strtof(token, &endptr);
        if (endptr == token + t_len) {
          is_num = true;
          tag = 11;
        }
      }
      
      if (!is_num) {
        val_u = strtoul(token, &endptr, 10);
        if (endptr == token + t_len) {
          is_num = true;
          tag = current_type;
        }
      }
    }
  }
  
  if (is_num) {
    dict_pool[dict_ptr++] = 0x00; dict_pool[dict_ptr++] = 0x00;
    dict_pool[dict_ptr++] = tag;
    
    switch (tag) {
      case 4: case 5:
        dict_pool[dict_ptr++] = (uint8_t)val_u;
        break;
      case 6: case 7:
        dict_pool[dict_ptr++] = val_u & 0xFF;
        dict_pool[dict_ptr++] = (val_u >> 8) & 0xFF;
        break;
      case 8:
        dict_pool[dict_ptr++] = val_u & 0xFF;
        dict_pool[dict_ptr++] = (val_u >> 8) & 0xFF;
        dict_pool[dict_ptr++] = (val_u >> 16) & 0xFF;
        break;
      case 11:
        memcpy(&dict_pool[dict_ptr], &val_f, 4);
        dict_ptr += 4;
        break;
      default:
        memcpy(&dict_pool[dict_ptr], &val_u, 4);
        dict_ptr += 4;
        break;
    }
    return;
  }

  // 5. Проверяем, является ли это уже известной локальной переменной (чтение)
  uint16_t found_id = 0;
  for (uint8_t i = 0; i < g_local_map_count; i++) {
    if (strcmp(g_local_map[i].original_name, token) == 0) {
      found_id = g_local_map[i].local_id;
      break;
    }
  }

  if (found_id > 0) {
    uint16_t resolver_addr = dict_find("__local__");
    if (resolver_addr != 0xFFFF) {
      dict_pool[dict_ptr++] = resolver_addr & 0xFF;
      dict_pool[dict_ptr++] = resolver_addr >> 8;
    }
    
    char var_name[32];
    snprintf(var_name, sizeof(var_name), "l%u", found_id);
    uint8_t nlen = strlen(var_name);
    if (nlen > 63) nlen = 63;
    
    dict_pool[dict_ptr++] = 0x0D;
    dict_pool[dict_ptr++] = nlen;
    for (uint8_t i = 0; i < nlen; i++) {
      dict_pool[dict_ptr++] = var_name[i];
    }
    return;
  }

  // 6. Неизвестное слово — предупреждение и компиляция как NAME
  currentOutput->print("Warning: unknown word '");
  currentOutput->print(token);
  currentOutput->println("', compiling as NAME");
  
  uint8_t nlen = t_len;
  if (nlen > 63) nlen = 63;
  
  dict_pool[dict_ptr++] = 0x0D;
  dict_pool[dict_ptr++] = nlen;
  for (uint8_t i = 0; i < nlen; i++) {
    dict_pool[dict_ptr++] = token[i];
  }
}

// === ЕДИНОЕ ЯДРО для copy и rgb2grb ===
// copy приёмник начало конец источник
// rgb2grb приёмник начало конец источник
//
// ВСЕГДА циклически заполняет диапазон [начало, конец)
// Источник — скаляр: повторяет одно значение
// Источник — массив: циклически повторяет элементы


// === ЕДИНОЕ ЯДРО для copy, rgb2grb, rgb2wrgb ===
// copy     приёмник начало конец источник   — обычное копирование
// rgb2grb  приёмник начало конец источник   — RGB → GRB (3→3 байта)
// rgb2wrgb приёмник начало конец источник   — RGB → WRGB (3→4 байта, W=min(R,G,B))
//
// Источник — скаляр: заполняет весь диапазон [начало, конец)
// Источник — массив: циклически повторяет элементы

static void copy_convert_core(uint8_t convert_mode) {
    // convert_mode: 0 = copy, 1 = rgb2grb, 2 = rgb2wrgb
    const char* name;
    switch (convert_mode) {
        case 1: name = "rgb2grb"; break;
        case 2: name = "rgb2wrgb"; break;
        default: name = "copy"; break;
    }
    
    // 1. Снимаем приёмник
    if (stack_is_empty()) {
        currentOutput->printf("%s: ожидается приёмник\n", name);
        return;
    }
    uint8_t* dst_ptr = &stack_mem[stack_ptr];
    if (dst_ptr[0] != 17 && dst_ptr[0] != 20) {
        currentOutput->printf("%s: приёмник должен быть массивом\n", name);
        return;
    }
    uint16_t dst_base = dst_ptr[1] | (dst_ptr[2] << 8);
    uint16_t dst_len  = dst_ptr[3] | (dst_ptr[4] << 8);
    uint8_t  dst_type = dst_ptr[5];
    uint16_t dst_esz  = type_registry[dst_type].size;
    uint32_t dst_bytes = (uint32_t)dst_len * dst_esz;
    stack_ptr += elem_size(dst_ptr);
    
    // 2. Начало
    if (stack_is_empty()) {
        currentOutput->printf("%s: ожидается начало\n", name);
        return;
    }
    uint8_t* start_ptr = &stack_mem[stack_ptr];
    if (start_ptr[0] < 4 || start_ptr[0] > 11) {
        currentOutput->printf("%s: начало должно быть числом\n", name);
        return;
    }
    uint32_t start = 0;
    uint16_t d = elem_size(start_ptr) - 1;
    for (uint16_t k = 0; k < d && k < 4; k++)
        start |= (uint32_t)start_ptr[1 + k] << (k * 8);
    stack_ptr += elem_size(start_ptr);
    
    // 3. Конец
    if (stack_is_empty()) {
        currentOutput->printf("%s: ожидается конец\n", name);
        return;
    }
    uint8_t* end_ptr = &stack_mem[stack_ptr];
    if (end_ptr[0] < 4 || end_ptr[0] > 11) {
        currentOutput->printf("%s: конец должен быть числом\n", name);
        return;
    }
    uint32_t end = 0;
    d = elem_size(end_ptr) - 1;
    for (uint16_t k = 0; k < d && k < 4; k++)
        end |= (uint32_t)end_ptr[1 + k] << (k * 8);
    stack_ptr += elem_size(end_ptr);
    
    // 4. Источник
    if (stack_is_empty()) {
        currentOutput->printf("%s: ожидается источник\n", name);
        return;
    }
    uint8_t* src_ptr = &stack_mem[stack_ptr];
    uint16_t src_base = 0, src_len = 0, src_esz = 0;
    uint8_t temp_buf[4];
    bool use_temp = false;
    
    if (src_ptr[0] == 17 || src_ptr[0] == 20) {
        src_base = src_ptr[1] | (src_ptr[2] << 8);
        src_len  = src_ptr[3] | (src_ptr[4] << 8);
        src_esz  = type_registry[src_ptr[5]].size;
        stack_ptr += elem_size(src_ptr);
    }
    else if (src_ptr[0] >= 4 && src_ptr[0] <= 11) {
        uint32_t val = 0;
        d = elem_size(src_ptr) - 1;
        for (uint16_t k = 0; k < d && k < 4; k++)
            val |= (uint32_t)src_ptr[1 + k] << (k * 8);
        src_esz = type_registry[src_ptr[0]].size;
        for (uint16_t k = 0; k < src_esz; k++)
            temp_buf[k] = (val >> (k * 8)) & 0xFF;
        src_len = 1;
        use_temp = true;
        stack_ptr += elem_size(src_ptr);
    }
    else {
        currentOutput->printf("%s: источник должен быть массивом или числом\n", name);
        return;
    }
    
    // 5. Границы
    if (end > dst_len) end = dst_len;
    if (end <= start) {
        currentOutput->printf("%s: конец должен быть больше начала\n", name);
        return;
    }
    
    uint32_t count = end - start;
    uint16_t dst_addr = dst_base + start * dst_esz;
    
    // 6. ЗАПИСЬ
    if (convert_mode == 1) {
        // === RGB → GRB (3 → 3) ===
        for (uint32_t i = 0; i < count; i++) {
            uint8_t r, g, b;
            if (use_temp) {
                r = temp_buf[2]; g = temp_buf[1]; b = temp_buf[0];
            } else {
                uint32_t si = i % src_len;
                uint16_t so = src_base + si * src_esz;
                b = data_pool[so + 0];
                g = data_pool[so + 1];
                r = data_pool[so + 2];
            }
            data_pool[dst_addr + i * 3 + 0] = g;
            data_pool[dst_addr + i * 3 + 1] = r;
            data_pool[dst_addr + i * 3 + 2] = b;
        }
    }
    else if (convert_mode == 2) {
        // === RGB → WRGB (3 → 4) ===
        // Порядок в памяти: W, R, G, B (стандарт SK6812)
        // W = min(R, G, B) — извлекаем "чистый белый"
        for (uint32_t i = 0; i < count; i++) {
            uint8_t r, g, b;
            if (use_temp) {
                r = temp_buf[2]; g = temp_buf[1]; b = temp_buf[0];
            } else {
                uint32_t si = i % src_len;
                uint16_t so = src_base + si * src_esz;
                b = data_pool[so + 0];
                g = data_pool[so + 1];
                r = data_pool[so + 2];
            }
            uint8_t w = (r < g) ? ((r < b) ? r : b) : ((g < b) ? g : b);
            data_pool[dst_addr + i * 4 + 0] = w;
            data_pool[dst_addr + i * 4 + 1] = r;
            data_pool[dst_addr + i * 4 + 2] = g;
            data_pool[dst_addr + i * 4 + 3] = b;
        }
    }
    else {
        // === Обычное копирование (циклическое) ===
        for (uint32_t i = 0; i < count; i++) {
            if (use_temp) {
                memcpy(&data_pool[dst_addr + i * dst_esz], temp_buf, dst_esz);
            } else {
                uint32_t si = i % src_len;
                uint16_t so = src_base + si * src_esz;
                memmove(&data_pool[dst_addr + i * dst_esz], &data_pool[so], dst_esz);
            }
        }
    }
}

void word_copy()    { copy_convert_core(0); }
void word_rgb2grb() { copy_convert_core(1); }
void word_rgb2wrgb(){ copy_convert_core(2); }




void word_if() {
  if (stack_is_empty()) return;
  uint8_t* top = &stack_mem[stack_ptr];
  uint8_t tag = top[0];
  uint16_t sz = elem_size(top);
  bool is_true = false;
  // 🔹 Декодирование условия
  if (tag == 1) is_true = true;       // BOOL true
  else if (tag == 0) is_true = false; // BOOL false
  else if (tag >= 4 && tag <= 11) {   // Числовые типы
    for (uint16_t i = 1; i < sz && i < 4; i++) {
      if (top[i] != 0) {
        is_true = true;
        break;
      }
    }
  }
  stack_ptr += sz; // Снимаем условие со стека
  // 🔹 Работа с потоком исполнения
  if (ip + 2 > dict_ptr) return; // Защита от выхода за пределы
  uint16_t target = dict_pool[ip] | (dict_pool[ip + 1] << 8);
  ip += 2; // ⚠️ КРИТИЧНО: всегда пропускаем 2 байта служебных данных
  if (!is_true) {
    ip = target; // Ложь → прыжок за тело блока
  }
  // Истина → ip уже сдвинут на начало тела, vm_run продолжит исполнение
}
void word_colon() {
  if (stack_is_empty()) return;
  uint8_t* top = &stack_mem[stack_ptr];
  if (top[0] != 0x0D) return; // Ожидается NAME
  uint8_t nlen = top[1];
  if (nlen == 0 || nlen > 63) return;
  if (dict_ptr + 9 + nlen > DICT_POOL_SIZE) return;
  g_compile_header = dict_ptr;
  // Записываем заголовок
  dict_pool[dict_ptr] = 0; dict_pool[dict_ptr + 1] = 0;
  dict_pool[dict_ptr + 2] = nlen;
  memcpy(&dict_pool[dict_ptr + 3], &top[2], nlen);
  dict_pool[dict_ptr + 3 + nlen] = currentContext;
  dict_pool[dict_ptr + 4 + nlen] = 0x08; // FLAG_COMPILED
  dict_ptr += 5 + nlen; // Сдвигаем dict_ptr к началу тела
  g_compile_mode = true;
  stack_ptr += 2 + nlen; // Снимаем NAME со стека
  g_local_map_count = 0;
}
void word_semicolon() {
  // Вызывается только из compile_token, но оставляем заглушку для консистентности
  if (!g_compile_mode) return;
}

void word_while() {
  if (stack_is_empty()) return;
  uint8_t* top = &stack_mem[stack_ptr];
  uint8_t tag = top[0];

  // 🔹 АВТО-РАЗРЕШЕНИЕ МАРКЕРОВ СРАВНЕНИЯ (фикс разрыва компилятор/рантайм)
  if (tag == 0x0C && top[1] >= 1 && top[1] <= 2) {
    char op[3] = {0};
    memcpy(op, &top[2], top[1]);
    stack_ptr += 2 + top[1]; // Снимаем маркер

    if (stack_is_empty()) return;
    uint8_t* r_ptr = &stack_mem[stack_ptr];
    uint16_t r_sz = elem_size(r_ptr);
    stack_ptr += r_sz; // Снимаем правый операнд

    if (stack_is_empty()) return;
    uint8_t* l_ptr = &stack_mem[stack_ptr];
    uint16_t l_sz = elem_size(l_ptr);
    stack_ptr += l_sz; // Снимаем левый операнд

    // Декодирование L
    int32_t L = 0; uint16_t dL = l_sz - 1;
    for (uint16_t k = 0; k < dL && k < 4; k++) L |= (int32_t)l_ptr[1 + k] << (k * 8);
    if (l_ptr[0] == 5 || l_ptr[0] == 7 || l_ptr[0] == 10) {
      if (dL == 1 && (l_ptr[1] & 0x80)) L |= 0xFFFFFF00;
      if (dL == 2 && (l_ptr[2] & 0x80)) L |= 0xFFFF0000;
      if (dL == 3 && (l_ptr[3] & 0x80)) L |= 0xFF000000;
    }

    // Декодирование R
    int32_t R = 0; uint16_t dR = r_sz - 1;
    for (uint16_t k = 0; k < dR && k < 4; k++) R |= (int32_t)r_ptr[1 + k] << (k * 8);
    if (r_ptr[0] == 5 || r_ptr[0] == 7 || r_ptr[0] == 10) {
      if (dR == 1 && (r_ptr[1] & 0x80)) R |= 0xFFFFFF00;
      if (dR == 2 && (r_ptr[2] & 0x80)) R |= 0xFFFF0000;
      if (dR == 3 && (r_ptr[3] & 0x80)) R |= 0xFF000000;
    }

    bool res = false;
    if (strcmp(op, "<") == 0) res = L < R;
    else if (strcmp(op, ">") == 0) res = L > R;
    else if (strcmp(op, "<=") == 0) res = L <= R;
    else if (strcmp(op, ">=") == 0) res = L >= R;
    else if (strcmp(op, "==") == 0) res = L == R;
    else if (strcmp(op, "!=") == 0) res = L != R;

    uint8_t b[1] = { (uint8_t)(res ? 1 : 0) };
    stack_push(b, 1);
    top = &stack_mem[stack_ptr];
    tag = top[0];
  }

  // Стандартная проверка условия
  bool is_true = false;
  if (tag == 1) is_true = true;
  else if (tag == 0) is_true = false;
  else {
    uint16_t sz = elem_size(top);
    for (uint16_t i = 1; i < sz && i < 5; i++) {
      if (top[i] != 0) {
        is_true = true;
        break;
      }
    }
  }
  stack_ptr += elem_size(top);

  if (ip + 2 <= dict_ptr) {
    uint16_t skip = dict_pool[ip] | (dict_pool[ip + 1] << 8);
    ip += 2;
    if (!is_true) {
      ip = skip;
      return;
    }
  }
}

void word_goto() {
  if (ip + 2 <= dict_ptr) {
    uint16_t target = dict_pool[ip] | (dict_pool[ip + 1] << 8);
    ip = target; // Переход. vm_run продолжит исполнение с нового ip.
  }
}
void wordNot() {
  if (stack_is_empty()) return;
  uint8_t* top = &stack_mem[stack_ptr];
  uint8_t tag = top[0];
  // Работаем только с числовыми типами (u8..f, теги 4..11)
  if (tag < 4 || tag > 11) return;
  uint16_t sz = elem_size(top);
  bool is_zero = true;
  for (uint16_t i = 1; i < sz; i++) {
    if (top[i] != 0) {
      is_zero = false;
      break;
    }
  }
  stack_ptr += sz; // Потребляем значение со стека
  // Результат: 1 если было 0, иначе 0. Возвращаем как u8 (тег 4) для совместимости с blink_state
  uint8_t res = is_zero ? 1 : 0;
  uint8_t out[2] = {4, res};
  stack_push(out, 2);
}
// === ПЛАНИРОВЩИК ЗАДАЧ ===
#define MAX_TASKS 8
struct TaskEntry {
  uint16_t addr;       // Адрес слова в dict_pool
  uint32_t interval;   // Период в мс
  uint32_t last;       // Время последнего запуска
};
TaskEntry g_tasks[MAX_TASKS];
uint8_t   g_task_count = 0;


void printActiveTasks() {
  if (g_task_count == 0) return;
  currentOutput->print("tasks: ");
  for (uint8_t i = 0; i < g_task_count; i++) {
    uint16_t t_addr = g_tasks[i].addr;
    const char* t_name = "?"; char buf[32];
    for (uint16_t p = 0; ; ) {
      if (p == t_addr) {
        uint8_t nlen = dict_pool[p + 2];
        if (nlen < sizeof(buf)) {
          memcpy(buf, &dict_pool[p + 3], nlen);
          buf[nlen] = '\0';
          t_name = buf;
        }
        break;
      }
      uint16_t next = dict_pool[p] | (dict_pool[p + 1] << 8);
      if (next == 0) break;
      p = next;
    }
    if (g_tasks[i].interval == 0) {
      currentOutput->printf("[%s:loop] ", t_name);
    } else {
      currentOutput->printf("[%s:%lu] ", t_name, (unsigned long)g_tasks[i].interval);
    }
  }
  currentOutput->println();
}
void delayMicrosecondsWord() {
  uint8_t buf[8];
  uint16_t sz = stack_pop(buf, sizeof(buf));
  if (sz < 2) return; // Защита: стек пуст или недопустимый элемент
  uint32_t us = 0;
  uint16_t d = sz - 1; // количество байт данных
  for (uint16_t k = 0; k < d && k < 4; k++) {
    us |= (uint32_t)buf[1 + k] << (k * 8);
  }
  ::delayMicroseconds(us);
}


// 🔹 ЕДИНАЯ ЯДРЕНАЯ ФУНКЦИЯ ЭКСПОРТА
static void json_export_core(bool delta_mode) {
  currentOutput->print('{');
  bool first = true;
  uint16_t p = 0;
  while (p < dict_ptr) {
    uint16_t next = dict_pool[p] | (dict_pool[p + 1] << 8);
    uint8_t nlen  = dict_pool[p + 2];
    uint8_t ctx   = dict_pool[p + 3 + nlen];
    uint8_t flags = dict_pool[p + 4 + nlen];

    if (ctx != currentContext || (flags & 0x10)) {
      if (next == 0) break; p = next; continue;
    }

    bool is_var   = (flags & 0x02) != 0;
    bool is_alias = (flags & 0x20) != 0;
    bool is_chain = (flags & 0x40) != 0;
    bool is_dirty = (flags & 0x80) != 0;

    // 🔹 ЕДИНЫЙ ФИЛЬТР: пропускаем всё, что не проходит по критериям
    if (!is_var || is_alias || is_chain || (delta_mode && !is_dirty)) {
      if (next == 0) break; p = next; continue;
    }

    uint16_t body_start = p + 5 + nlen;
    uint16_t end = next ? next : dict_ptr;
    uint16_t val_size = (end - 4) - body_start;
    if (val_size == 0) {
      if (next == 0) break;
      p = next;
      continue;
    }

    uint8_t tag = dict_pool[body_start];
    uint16_t data_ptr_addr = body_start;

    // 🔹 Алиасы на переменные уже отфильтрованы выше, но если цель нужна — берём её тело
    if (is_alias && !is_var) {
      uint16_t target = dict_pool[body_start] | (dict_pool[body_start + 1] << 8);
      if (target < dict_ptr) {
        uint8_t t_nlen = dict_pool[target + 2];
        uint8_t t_flags = dict_pool[target + 4 + t_nlen];
        if ((t_flags & 0x02) != 0) {
          tag = dict_pool[target + 5 + t_nlen];
          data_ptr_addr = target + 5 + t_nlen;
        }
      }
    }

    if (!first) currentOutput->print(',');
    first = false;

    // Печать имени
    currentOutput->print('"');
    for (uint8_t i = 0; i < nlen; i++) currentOutput->write(dict_pool[p + 3 + i]);
    currentOutput->print("\":");

    // 🔹 ВЫВОД ЗНАЧЕНИЯ (идентично для обоих режимов)
    if (tag == 17 || tag == 20) {
      uint16_t base = dict_pool[data_ptr_addr + 1] | (dict_pool[data_ptr_addr + 2] << 8);
      uint16_t len  = dict_pool[data_ptr_addr + 3] | (dict_pool[data_ptr_addr + 4] << 8);
      uint8_t  el_tp = dict_pool[data_ptr_addr + 5];
      if (tag == 20) {
        currentOutput->printf("[%u, %u, %u]", base, len, el_tp);
      } else {
        uint8_t esz = type_registry[el_tp].size;
        currentOutput->print('[');
        for (uint16_t i = 0; i < len; i++) {
          if (i > 0) currentOutput->print(',');
          uint16_t addr = base + i * esz;
          if (addr + esz > DATA_POOL_SIZE) {
            currentOutput->print("null");
            continue;
          }
          if (el_tp == 15) {
            uint8_t slen = data_pool[addr]; uint16_t d_addr = data_pool[addr + 1] | (data_pool[addr + 2] << 8);
            if (slen == 0 || d_addr == 0 || d_addr + slen > DATA_POOL_SIZE) {
              currentOutput->print("null");
            }
            else {
              currentOutput->print('"');
              for (uint8_t j = 0; j < slen; j++) {
                char c = data_pool[d_addr + j];
                if (c == '"' || c == '\\') {
                  currentOutput->print('\\');
                  currentOutput->write(c);
                }
                else if (c == '\n') currentOutput->print("\\n");
                else if (c == '\r') currentOutput->print("\\r");
                else if (c == '\t') currentOutput->print("\\t");
                else currentOutput->write(c);
              }
              currentOutput->print('"');
            }
            continue;
          }
          if (el_tp == 11) {
            float f; memcpy(&f, &data_pool[addr], 4);
            currentOutput->printf("%.4g", f);
          } else {
            uint32_t u = 0;
            for (uint8_t k = 0; k < esz && k < 4; k++) u |= (uint32_t)data_pool[addr + k] << (k * 8);
            bool is_signed = (el_tp == 5 || el_tp == 7 || el_tp == 10);
            if (is_signed) {
              if (esz == 1 && (data_pool[addr] & 0x80)) u |= 0xFFFFFF00;
              if (esz == 2 && (data_pool[addr + 1] & 0x80)) u |= 0xFFFF0000;
              if (esz == 3 && (data_pool[addr + 2] & 0x80)) u |= 0xFF000000;
            }
            if (is_signed) currentOutput->printf("%ld", (long)(int32_t)u);
            else currentOutput->printf("%lu", (unsigned long)u);
          }
        }
        currentOutput->print(']');
      }
    }
    else if (tag == 0 || tag == 1) {
      currentOutput->print(tag == 1 ? "true" : "false");
    }
    else if (tag >= 4 && tag <= 11) {
      if (tag == 11) {
        float f;
        memcpy(&f, &dict_pool[data_ptr_addr + 1], 4);
        currentOutput->printf("%.4g", f);
      }
      else {
        uint32_t u = 0; uint16_t d = val_size - 1;
        for (uint16_t k = 0; k < d && k < 4; k++) u |= (uint32_t)dict_pool[data_ptr_addr + 1 + k] << (k * 8);
        bool is_signed = (tag == 5 || tag == 7 || tag == 10);
        if (is_signed) {
          if (d == 1 && (dict_pool[data_ptr_addr + 1] & 0x80)) u |= 0xFFFFFF00;
          if (d == 2 && (dict_pool[data_ptr_addr + 2] & 0x80)) u |= 0xFFFF0000;
          if (d == 3 && (dict_pool[data_ptr_addr + 3] & 0x80)) u |= 0xFF000000;
        }
        if (is_signed) currentOutput->printf("%ld", (long)(int32_t)u); else currentOutput->printf("%lu", (unsigned long)u);
      }
    }
    else if (tag == 0x0E || tag == 15) {
      uint16_t slen; const uint8_t* src;
      if (tag == 0x0E) {
        slen = dict_pool[data_ptr_addr + 1];
        src = &dict_pool[data_ptr_addr + 2];
      }
      else {
        slen = dict_pool[data_ptr_addr + 1];
        uint16_t a = dict_pool[data_ptr_addr + 2] | (dict_pool[data_ptr_addr + 3] << 8);
        src = &data_pool[a];
      }
      currentOutput->print('"');
      for (uint8_t i = 0; i < slen; i++) {
        char c = src[i];
        if (c == '"' || c == '\\') {
          currentOutput->print('\\');
          currentOutput->write(c);
        }
        else if (c == '\n') currentOutput->print("\\n");
        else if (c == '\r') currentOutput->print("\\r");
        else if (c == '\t') currentOutput->print("\\t");
        else currentOutput->write(c);
      }
      currentOutput->print('"');
    }
    else {
      currentOutput->print("null");
    }

    // 🔹 В DELTA-РЕЖИМЕ сбрасываем флаг после успешной отправки
    if (delta_mode) dict_pool[p + 4 + nlen] &= ~0x80;

    if (next == 0) break; p = next;
  }
  currentOutput->print('}');
}

// 🔹 ТОНКИЕ ОБЁРТКИ (интерфейсы намерения)
void word_json_export() {
  json_export_core(false);
}
void word_json_delta()  {
  json_export_core(true);
}
inline void mark_dirty(uint16_t var_addr) {
  if (var_addr < dict_ptr) {
    dict_pool[var_addr + 4 + dict_pool[var_addr + 2]] |= 0x80;
  }
}

void word_view() {
    // 1. Захватываем предыдущий токен из потока (имя исходного массива)
    // В тексте это слово-источник (стоит ДО view)
    // В R2L-потоке оно будет прочитано ПОСЛЕ view
    int idx = g_tok_idx - 1;
    if (idx < 0) {
        currentOutput->println("view: no source array before");
        return;
    }
    
    // 2. Извлекаем имя исходного массива
    uint8_t src_len = g_tok_len[idx];
    if (src_len > 63) src_len = 63;
    char src_name[64];
    memcpy(src_name, g_tok_start[idx], src_len);
    src_name[src_len] = '\0';
    
    // 3. Ищем исходный массив в словаре
    uint16_t src_addr = dict_find(src_name);
    if (src_addr == 0xFFFF) {
        currentOutput->print("view: not found: ");
        currentOutput->println(src_name);
        return;
    }
    
    // 4. Проверяем, что исходное слово — массив или ссылка
    uint8_t src_nlen = dict_pool[src_addr + 2];
    uint16_t src_body = src_addr + 5 + src_nlen;
    uint8_t src_tag = dict_pool[src_body];
    if (src_tag != 17 && src_tag != 20) {
        currentOutput->println("view: source is not an array");
        return;
    }
    
    // 5. Снимаем со стека: NAME нового имени, тип, длину
    // (в R2L-потоке они были прочитаны раньше, поэтому на стеке в обратном порядке)
    
    // 5.1. Снимаем NAME нового имени (сверху стека)
    if (stack_is_empty()) {
        currentOutput->println("view: new name expected");
        return;
    }
    uint8_t* name_ptr = &stack_mem[stack_ptr];
    if (name_ptr[0] != 0x0D) {
        currentOutput->println("view: NAME expected");
        return;
    }
    uint8_t new_nlen = name_ptr[1];
    if (new_nlen == 0 || new_nlen > 63) {
        currentOutput->println("view: invalid name length");
        return;
    }
    char new_name[64];
    memcpy(new_name, &name_ptr[2], new_nlen);
    new_name[new_nlen] = '\0';
    stack_ptr += elem_size(name_ptr);
    
    // 5.2. Снимаем тип (маркер или число)
    if (stack_is_empty()) {
        currentOutput->println("view: type expected");
        return;
    }
    uint8_t* type_ptr = &stack_mem[stack_ptr];
    uint8_t new_type = 0;
    
    if (type_ptr[0] == 0x0C) {
        // Маркер типа (u8, u16, ...)
        uint8_t tlen = type_ptr[1];
        const char* tnames[] = {"u8", "i8", "u16", "i16", "u24", "u32", "i32", "f", "$S"};
        const uint8_t ttags[]  = {4, 5, 6, 7, 8, 9, 10, 11, 15};
        for (int i = 0; i < 9; i++) {
            if (tlen == strlen(tnames[i]) && memcmp(&type_ptr[2], tnames[i], tlen) == 0) {
                new_type = ttags[i];
                break;
            }
        }
    } else if (type_ptr[0] >= 4 && type_ptr[0] <= 11) {
        // Числовой тег
        new_type = type_ptr[0];
    }
    
    if (new_type == 0) {
        currentOutput->println("view: invalid type");
        return;
    }
    stack_ptr += elem_size(type_ptr);
    
    // 5.3. Снимаем длину
    if (stack_is_empty()) {
        currentOutput->println("view: length expected");
        return;
    }
    uint8_t* len_ptr = &stack_mem[stack_ptr];
    if (len_ptr[0] < 4 || len_ptr[0] > 11) {
        currentOutput->println("view: length expected");
        return;
    }
    uint32_t new_len = 0;
    uint16_t d = elem_size(len_ptr) - 1;
    for (uint16_t k = 0; k < d && k < 4; k++) {
        new_len |= (uint32_t)len_ptr[1 + k] << (k * 8);
    }
    stack_ptr += elem_size(len_ptr);
    
    // 6. Проверяем, не превышает ли новая ссылка буфер исходного массива
    uint8_t old_type = dict_pool[src_body + 5];
    uint16_t old_len = dict_pool[src_body + 3] | (dict_pool[src_body + 4] << 8);
    uint32_t old_bytes = (uint32_t)old_len * type_registry[old_type].size;
    if (new_len * type_registry[new_type].size > old_bytes) {
        currentOutput->println("view: exceeds buffer");
        return;
    }
    
    // 7. Проверяем, не существует ли уже слово с таким именем
    uint16_t existing = dict_find(new_name);
    if (existing != 0xFFFF) {
        uint8_t ex_nlen = dict_pool[existing + 2];
        uint8_t ex_flags = dict_pool[existing + 4 + ex_nlen];
        if (ex_flags != FLAG_ALIAS) {
            currentOutput->println("view: name exists");
            return;
        }
    }
    
    // 8. Создаём ссылку на массив (REF_ARR, тег 20)
    uint16_t ws = 9 + new_nlen + 6;
    if (dict_ptr + ws > DICT_POOL_SIZE) {
        currentOutput->println("view: dict overflow");
        return;
    }
    
    uint16_t a = dict_ptr;
    dict_ptr += ws;
    dict_pool[a] = 0;
    dict_pool[a + 1] = 0;
    dict_pool[a + 2] = new_nlen;
    memcpy(&dict_pool[a + 3], new_name, new_nlen);
    
    uint16_t p = a + 3 + new_nlen;
    dict_pool[p] = currentContext;
    dict_pool[p + 1] = 0x06 | 0x01; // VAR | INTERNAL | CONST
    dict_pool[p + 2] = 20; // REF_ARR
    dict_pool[p + 3] = dict_pool[src_body + 1]; // base_lo
    dict_pool[p + 4] = dict_pool[src_body + 2]; // base_hi
    dict_pool[p + 5] = new_len & 0xFF;
    dict_pool[p + 6] = new_len >> 8;
    dict_pool[p + 7] = new_type;
    
    uint32_t fn = (uint32_t)(uintptr_t)choiceFunc;
    memcpy(&dict_pool[p + 8], &fn, 4);
    link_word(a);
    
    // 9. 🔑 КЛЮЧЕВОЙ МОМЕНТ: пропускаем токен исходного массива в R2L-цикле
    g_tok_idx -= 1;
}

void word_as() {
    // 1. Захватываем предыдущий токен из потока
    // В тексте это слово-источник (стоит ДО as)
    // В R2L-потоке оно будет прочитано ПОСЛЕ as
    int idx = g_tok_idx - 1;
    if (idx < 0) {
        currentOutput->println("as: no word before");
        return;
    }
    
    // 2. Извлекаем имя существующего слова
    uint8_t src_len = g_tok_len[idx];
    if (src_len > 63) src_len = 63;
    char src_name[64];
    memcpy(src_name, g_tok_start[idx], src_len);
    src_name[src_len] = '\0';
    
    // 3. Ищем существующее слово в словаре
    uint16_t src_addr = dict_find(src_name);
    if (src_addr == 0xFFFF) {
        currentOutput->print("as: not found: ");
        currentOutput->println(src_name);
        return;
    }
    
    // 4. Снимаем NAME нового имени со стека
    // (оно было прочитано раньше в R2L-потоке)
    if (stack_is_empty()) {
        currentOutput->println("as: new name expected");
        return;
    }
    uint8_t* top = &stack_mem[stack_ptr];
    if (top[0] != 0x0D) {
        currentOutput->println("as: NAME expected on stack");
        return;
    }
    uint8_t new_nlen = top[1];
    if (new_nlen == 0 || new_nlen > 63) {
        currentOutput->println("as: invalid name length");
        return;
    }
    char new_name[64];
    memcpy(new_name, &top[2], new_nlen);
    new_name[new_nlen] = '\0';
    stack_ptr += elem_size(top);  // Снимаем NAME со стека
    
    // 5. Проверяем, не существует ли уже слово с таким именем
    uint16_t existing = dict_find(new_name);
    if (existing != 0xFFFF) {
        uint8_t ex_flags = dict_pool[existing + 4 + dict_pool[existing + 2]];
        if (ex_flags != FLAG_ALIAS) {
            currentOutput->println("as: name exists");
            return;
        }
    }
    
    // 6. Создаём алиас (копия оригинальной логики)
    uint16_t a_size = 9 + new_nlen;
    if (dict_ptr + a_size > DICT_POOL_SIZE) {
        currentOutput->println("as: dict overflow");
        return;
    }
    uint16_t a = dict_ptr; dict_ptr += a_size;
    dict_pool[a] = 0; dict_pool[a + 1] = 0;
    dict_pool[a + 2] = new_nlen;
    memcpy(&dict_pool[a + 3], new_name, new_nlen);
    dict_pool[a + 3 + new_nlen] = currentContext;
    dict_pool[a + 4 + new_nlen] = FLAG_ALIAS;
    dict_pool[a + 5 + new_nlen] = src_addr & 0xFF;
    dict_pool[a + 6 + new_nlen] = src_addr >> 8;
    link_word(a);
    
    // 7. 🔑 КЛЮЧЕВОЙ МОМЕНТ: пропускаем токен-источник в R2L-цикле
    g_tok_idx -= 1;
}

void word_body() {
    // 1. Захватываем предыдущий токен из потока (имя слова)
    int idx = g_tok_idx - 1;
    if (idx < 0) {
        currentOutput->println("body: no word before in text");
        return;
    }

    // 2. Извлекаем имя слова
    uint8_t len = g_tok_len[idx];
    if (len > 63) len = 63;
    char tname[64];
    memcpy(tname, g_tok_start[idx], len);
    tname[len] = '\0';

    // 3. Ищем слово в словаре
    uint16_t addr = dict_find(tname);
    if (addr == 0xFFFF) {
        currentOutput->print("body: not found: ");
        currentOutput->println(tname);
        return;
    }

    uint8_t nlen  = dict_pool[addr + 2];
    uint8_t flags = dict_pool[addr + 4 + nlen];

    // 🔑 НОВОЕ: если это цепочка — печатаем её содержимое красиво
    if (flags & FLAG_CHAIN) {
        uint16_t body_start = addr + 5 + nlen;
        uint16_t node = dict_pool[body_start] | (dict_pool[body_start + 1] << 8);

        // Заголовок (как у обычного body)
        currentOutput->printf("[%04X] \"%s\" flags:0x%02X ctx:%u\n",
                              addr, tname, flags, dict_pool[addr + 3 + nlen]);
        currentOutput->print("  chain: ");

        if (node == 0) {
            currentOutput->println("(empty)");
        } else {
            bool first = true;
            uint16_t curr = node;
            uint16_t safety = 0;
            while (curr != 0 && safety++ < 256) {
                if (curr + 3 >= DATA_POOL_SIZE) {
                    currentOutput->print(" [broken]");
                    break;
                }
                uint16_t next   = data_pool[curr]   | (data_pool[curr + 1] << 8);
                uint16_t action = data_pool[curr + 2] | (data_pool[curr + 3] << 8);

                if (!first) currentOutput->print(" → ");
                first = false;

                // Имя действия по его адресу в dict_pool
                if (action != 0 && action < dict_ptr) {
                    uint8_t an = dict_pool[action + 2];
                    if (an > 0 && an <= 63 && action + 3 + an <= dict_ptr) {
                        currentOutput->write(&dict_pool[action + 3], an);
                    } else {
                        currentOutput->printf("@%04X", action);
                    }
                } else {
                    currentOutput->print("(null)");
                }

                curr = next;
            }
            currentOutput->println();
        }

        g_tok_idx -= 1; // пропускаем токен-источник в R2L-цикле
        return;
    }

    // 4. Обычное слово — печатаем тело как раньше
    print_word_body(addr);

    // 5. Пропускаем токен в R2L-цикле
    g_tok_idx -= 1;
}


// === ГЛОБАЛЬНЫЙ ДЕСКРИПТОР (добавить в начало concWords.ino, после объявления data_pool) ===
static File g_outFile;
void word_out_file() {
  if (stack_is_empty()) {
    currentOutput->println("out>file: filename expected");
    return;
  }
  uint8_t* top = &stack_mem[stack_ptr];
  uint8_t tag = top[0];
  // 🔹 Принимаем и NAME (0x0D), и STRING (0x0E)
  if (tag != 0x0D && tag != 0x0E) {
    currentOutput->println("out>file: NAME or STRING expected");
    return;
  }
  uint8_t len = top[1];
  if (len > 255) len = 255;
  char fname[257];
  memcpy(fname, &top[2], len);
  fname[len] = '\0';
  stack_ptr += elem_size(top); // Снимаем со стека
  if (g_outFile) g_outFile.close(); // Закрываем предыдущий файл
  char full_path[256];
  if (strlen(g_currentDir) > 1) snprintf(full_path, sizeof(full_path), "%s/%s", g_currentDir, fname);
  else snprintf(full_path, sizeof(full_path), "/%s", fname);
  g_outFile = FILESYSTEM.open(full_path, "w");
  if (g_outFile) {
    currentOutput = &g_outFile; // 🔀 Переключаем весь вывод в файл
    currentOutput->print("out>file: OK -> ");
    currentOutput->println(full_path);
  } else {
    currentOutput = &Serial; // Фолбэк при ошибке открытия
    Serial.print("out>file: FAILED -> ");
    Serial.println(full_path);
  }
}
void word_out_serial() {
  if (g_outFile) g_outFile.close();
  currentOutput = &Serial; // 🔀 Возвращаем вывод на UART
  Serial.println("out>serial: OK");
}
void word_add_loop() { // === +loop / -loop (фоновое исполнение без задержки) ===
  if (stack_is_empty()) return;
  uint8_t* top = &stack_mem[stack_ptr];
  if (top[0] != 0x0E) {
    currentOutput->println("loop name expected");
    return;
  }
  uint8_t nlen = top[1]; if (nlen >= 64) nlen = 63;
  char tname[64]; memcpy(tname, &top[2], nlen); tname[nlen] = '\0';
  stack_ptr += elem_size(top);

  uint16_t addr = dict_find(tname);
  if (addr == 0xFFFF) {
    currentOutput->print("word not found: ");
    currentOutput->println(tname);
    return;
  }

  // Проверка на дубликат (только для loop)
  for (uint8_t i = 0; i < g_task_count; i++) {
    if (g_tasks[i].addr == addr && g_tasks[i].interval == 0) {
      currentOutput->println("loop already scheduled"); return;
    }
  }
  if (g_task_count >= MAX_TASKS) {
    currentOutput->println("task limit reached");
    return;
  }

  g_tasks[g_task_count].addr = addr;
  g_tasks[g_task_count].interval = 0; // 0 = непрерывный цикл
  g_tasks[g_task_count].last = millis();
  g_task_count++;
  //currentOutput->println("loop scheduled");
}
void word_remove_loop() {
  if (stack_is_empty()) return;
  uint8_t* top = &stack_mem[stack_ptr];
  if (top[0] != 0x0E) {
    currentOutput->println("loop name expected");
    return;
  }
  uint8_t nlen = top[1]; if (nlen >= 64) nlen = 63;
  char tname[64]; memcpy(tname, &top[2], nlen); tname[nlen] = '\0';
  stack_ptr += elem_size(top);

  uint16_t addr = dict_find(tname);
  if (addr == 0xFFFF) {
    currentOutput->print("word not found: ");
    currentOutput->println(tname);
    return;
  }

  int found_idx = -1;
  for (uint8_t i = 0; i < g_task_count; i++) {
    if (g_tasks[i].addr == addr && g_tasks[i].interval == 0) {
      found_idx = i; break;
    }
  }
  if (found_idx == -1) {
    currentOutput->print("loop not found: ");
    currentOutput->println(tname);
    return;
  }

  for (uint8_t i = found_idx; i < g_task_count - 1; i++) g_tasks[i] = g_tasks[i + 1];
  g_task_count--;
  //currentOutput->println("loop removed");
}
void word_json_export_file() {
  if (stack_is_empty()) {
    currentOutput->println("json>file: filename expected");
    return;
  }
  uint8_t* top = &stack_mem[stack_ptr];
  if (top[0] != 0x0D && top[0] != 0x0E) {
    currentOutput->println("json>file: NAME or STRING expected");
    return;
  }
  uint8_t len = top[1];
  if (len > 255) len = 255;
  char fname[257];
  memcpy(fname, &top[2], len);
  fname[len] = '\0';
  stack_ptr += elem_size(top); // Снимаем имя со стека

  // Собираем полный путь с учётом текущей директории
  char full_path[256];
  if (strlen(g_currentDir) > 1) snprintf(full_path, sizeof(full_path), "%s/%s", g_currentDir, fname);
  else snprintf(full_path, sizeof(full_path), "/%s", fname);

  File f = FILESYSTEM.open(full_path, "w");
  if (!f) {
    currentOutput->print("json>file: cannot open ");
    currentOutput->println(full_path);
    return;
  }

  // 🔀 Временно переключаем весь вывод в файл
  Print* saved_output = currentOutput;
  currentOutput = &f;

  word_json_export();          // Генерация JSON
  currentOutput->println();    // Завершающий перенос строки для чистоты файла

  // 🔙 Восстанавливаем вывод и закрываем файл
  f.close();
  currentOutput = saved_output;

  currentOutput->print("json>file: saved to ");
  currentOutput->println(full_path);
}
void word_reset() {
  currentOutput->println("system reset...");
  currentOutput->println("/ ok>");
  currentOutput->flush();  // 🔑 Универсально: ждёт очистки буфера Serial / WS / File
  delay(200);              // Даём физическим буферам время на полную передачу
  ESP.restart();
}
// === ЧЕКПОИНТ И ОТКАТ (MARKER / FORGET) ===
struct CheckpointState {
  uint16_t dict_ptr;
  uint16_t data_ptr;
  uint16_t stack_ptr;
  uint16_t rstack_ptr;
  uint8_t  currentContext;
  uint8_t  g_next_ctx;
  uint8_t  g_task_count;
  bool     valid;
} g_checkpoint = {0, 0, STACK_SIZE, RSTACK_SIZE, 0, 1, 0, false};

void word_checkpoint() { // <--
  g_checkpoint.dict_ptr       = dict_ptr;
  g_checkpoint.data_ptr       = data_ptr;
  g_checkpoint.stack_ptr      = stack_ptr;
  g_checkpoint.rstack_ptr     = rstack_ptr;
  g_checkpoint.currentContext = currentContext;
  g_checkpoint.g_next_ctx     = g_next_ctx;
  g_checkpoint.g_task_count   = g_task_count;
  g_checkpoint.valid          = true;

  // Сброс состояний парсера/компилятора
  g_compile_mode = false;
  g_compile_header = 0xFFFF;
  g_loop_target = 0;
  g_if_branch_pos = 0;
  g_in_block_comment = false;
}
void word_forget() {
  if (!g_checkpoint.valid) {
    currentOutput->println("forget: no <-- checkpoint");
    return;
  }

  // 🔹 1. Восстанавливаем состояние памяти
  dict_ptr       = g_checkpoint.dict_ptr;
  data_ptr       = g_checkpoint.data_ptr;
  stack_ptr      = g_checkpoint.stack_ptr;
  rstack_ptr     = g_checkpoint.rstack_ptr;
  currentContext = g_checkpoint.currentContext;
  g_next_ctx     = g_checkpoint.g_next_ctx;

  // 🔹 2. Безопасный откат задач
  uint8_t kept = 0;
  for (uint8_t i = 0; i < g_checkpoint.g_task_count; i++) {
    if (g_tasks[i].addr < dict_ptr) {
      if (i != kept) g_tasks[kept] = g_tasks[i];
      kept++;
    }
  }
  g_task_count = kept;

  // 🔹 3. Сброс состояний парсера/компилятора
  g_compile_mode = false;
  g_compile_header = 0xFFFF;
  g_loop_target = 0;
  g_if_branch_pos = 0;
  g_in_block_comment = false;

  // 🔹 4. Восстанавливаем хвост связного списка
  dict_last = 0xFFFF;
  uint16_t p = 0;
  while (p < dict_ptr) {
    uint16_t next = dict_pool[p] | (dict_pool[p + 1] << 8);
    if (next == 0 || next >= dict_ptr) {
      dict_last = p;
      break;
    }
    p = next;
  }

  // 🔹 5. Финальное сообщение — через currentOutput, без смены канала!
  currentOutput->println("forget: rolled back to <--");
  word_prompt();  // ← Перерисовываем приглашение в том же канале
}
void word_bead() {
  // 1. Захват имён из потока (R2L: сначала action, потом chain)
  int idx_action = g_tok_idx - 1;
  int idx_chain  = g_tok_idx - 2;
  if (idx_action < 0 || idx_chain < 0) {
    currentOutput->println("bead: expected 2 words in stream");
    return;
  }

  char action_name[64] = {0};
  char chain_name[64]  = {0};
  uint8_t l1 = g_tok_len[idx_action]; if (l1 > 63) l1 = 63;
  memcpy(action_name, g_tok_start[idx_action], l1);
  uint8_t l2 = g_tok_len[idx_chain];  if (l2 > 63) l2 = 63;
  memcpy(chain_name, g_tok_start[idx_chain], l2);

  // Пропускаем исполнение этих токенов
  g_tok_idx -= 2;

  // 2. Ищем ACTION слово в словаре
  uint16_t action_addr = dict_find(action_name);
  if (action_addr == 0xFFFF) {
    currentOutput->print("bead: action '"); currentOutput->print(action_name); currentOutput->println("' not found");
    return;
  }

  // 3. Ищем CHAIN слово в словаре
  uint16_t chain_addr = dict_find(chain_name);
  if (chain_addr == 0xFFFF) {
    currentOutput->print("bead: chain '"); currentOutput->print(chain_name); currentOutput->println("' not found");
    return;
  }

  // 4. Проверяем, что цепочка действительно имеет флаг CHAIN (опционально, но надёжно)
  uint8_t c_nlen  = dict_pool[chain_addr + 2];
  uint8_t c_flags = dict_pool[chain_addr + 4 + c_nlen];
  if (!(c_flags & FLAG_CHAIN)) {
    currentOutput->print("bead: '"); currentOutput->print(chain_name); currentOutput->println("' is not a chain (use cord first)");
    return;
  }

  // 5. Выделяем узел в data_pool: 4 байта [next_lo, next_hi, action_lo, action_hi]
  if (data_ptr + 4 > DATA_POOL_SIZE) {
    currentOutput->println("bead: data_pool full");
    return;
  }
  uint16_t new_node = data_ptr;
  data_ptr += 4;

  // 6. Записываем узел (next=0, action=адрес слова)
  data_pool[new_node]   = 0;
  data_pool[new_node + 1] = 0;
  data_pool[new_node + 2] = action_addr & 0xFF;
  data_pool[new_node + 3] = action_addr >> 8;

  // 7. Линкуем к цепи (FIFO: добавляем в хвост)
  uint16_t body_start = chain_addr + 5 + c_nlen;
  uint16_t head = dict_pool[body_start] | (dict_pool[body_start + 1] << 8);

  if (head == 0) {
    // Цепь пуста → новое звено становится головой
    dict_pool[body_start]   = new_node & 0xFF;
    dict_pool[body_start + 1] = new_node >> 8;
  } else {
    // Цепь есть → идём до последнего звена (next == 0)
    uint16_t curr = head;
    while (true) {
      uint16_t nxt = data_pool[curr] | (data_pool[curr + 1] << 8);
      if (nxt == 0) break;
      curr = nxt;
    }
    // Подцепляем новое звено
    data_pool[curr]   = new_node & 0xFF;
    data_pool[curr + 1] = new_node >> 8;
  }

  //currentOutput->print("bead: '"); currentOutput->print(action_name);
  //currentOutput->print("' -> '"); currentOutput->print(chain_name);
  //currentOutput->println("' linked");
}
// === ПРОВЕРКА СТЕКА (возвращает BOOL на стек) ===
void word_check() {
  pushBool(stack_is_empty()); // 1 = стек пуст, 0 = остались данные
}
void word_heap() {
  uint32_t free = (uint32_t)ESP.getFreeHeap();
  uint8_t buf[5];
  buf[0] = 9;                // тег UINT32
  memcpy(&buf[1], &free, 4); // little-endian
  stack_push(buf, 5);
}
void word_json_export_serial() {
  // 1. Сохраняем текущий канал вывода
  Print* saved_output = currentOutput;

  // 2. Принудительно переключаем вывод на Serial
  currentOutput = &Serial;

  // 3. Генерируем JSON
  word_json_export();
  currentOutput->println(); // Завершающий перенос строки для чистоты вывода

  // 4. Восстанавливаем предыдущий канал вывода
  currentOutput = saved_output;
}
void word_local_stub() {
  // 1. Читаем имя переменной прямо из потока инструкций
  if (ip >= dict_ptr) return;
  if (dict_pool[ip] != 0x0D) return; // Ожидаем тег NAME

  uint8_t len = dict_pool[ip + 1];
  if (len == 0 || len > 63) return;

  char var_name[64];
  memcpy(var_name, &dict_pool[ip + 2], len);
  var_name[len] = '\0';

  // КРИТИЧНО: сдвигаем ip, чтобы VM не пыталась исполнить имя как код
  ip += 2 + len;

  // 2. Ищем слово ТОЛЬКО в нижнем стеке (local_dict_ptr)
  uint16_t addr = 0xFFFF;
  uint16_t p = local_dict_ptr;
  while (p < DICT_POOL_SIZE) {
    uint16_t next = dict_pool[p] | (dict_pool[p + 1] << 8);
    uint8_t nlen = dict_pool[p + 2];
    if (nlen == len && memcmp(&dict_pool[p + 3], var_name, len) == 0) {
      addr = p;
      break;
    }
    if (next == 0) break;
    p = next;
  }

  // 3. Проверяем, есть ли на стеке маркер '=' (присваивание)
  bool is_assign = false;
  uint16_t val_sz = 0;
  uint8_t* val_ptr = nullptr;

  if (!stack_is_empty()) {
    uint8_t* top = &stack_mem[stack_ptr];
    if (top[0] == 0x0C && top[1] == 1 && top[2] == '=') {
      is_assign = true;
      uint16_t under_marker = stack_ptr + 3;
      if (under_marker < STACK_SIZE) {
        val_ptr = &stack_mem[under_marker];
        val_sz = elem_size(val_ptr);
      }
    }
  }

  // 4. Если слова НЕТ, создаем его
  if (addr == 0xFFFF) {
    if (!is_assign || !val_ptr || val_sz == 0) {
      currentOutput->print("[ERR] __local__: variable '");
      currentOutput->print(var_name);
      currentOutput->println("' not found and no data to create it!");
      return;
    }

    uint8_t v_tag = val_ptr[0];
    uint16_t v_sz = val_sz;
    uint16_t word_size = 9 + len + v_sz;

    if (local_dict_ptr < word_size) {
      currentOutput->println("[ERR] local_dict overflow");
      return;
    }

    local_dict_ptr -= word_size;
    addr = local_dict_ptr;

    uint16_t prev_top = local_dict_ptr + word_size;
    if (prev_top >= DICT_POOL_SIZE) {
      dict_pool[addr] = 0; dict_pool[addr + 1] = 0;
    } else {
      dict_pool[addr] = prev_top & 0xFF;
      dict_pool[addr + 1] = prev_top >> 8;
    }

    dict_pool[addr + 2] = len;
    memcpy(&dict_pool[addr + 3], var_name, len);
    uint16_t p_hdr = addr + 3 + len;
    dict_pool[p_hdr] = currentContext;
    dict_pool[p_hdr + 1] = 0x06; // VAR | INTERNAL

    uint16_t body = p_hdr + 2;
    dict_pool[body] = v_tag;
    memcpy(&dict_pool[body + 1], &val_ptr[1], v_sz - 1);

    uint32_t fn = (uint32_t)(uintptr_t)choiceFunc;
    memcpy(&dict_pool[p_hdr + 2 + v_sz], &fn, 4);

    //link_word(addr);
    stack_ptr += 3 + val_sz;
    return;
  }

  // 5. Если слово УЖЕ ЕСТЬ, исполняем его стандартным способом
  exec_word(addr);
}

void word_lwords() {
  // Если указатель не сдвигался, значит локалов нет
  if (local_dict_ptr == DICT_POOL_SIZE) {
    currentOutput->println("locals: empty");
    return;
  }

  currentOutput->println("locals:");
  uint16_t p = local_dict_ptr;

  // Обходим связный список локалов
  while (p < DICT_POOL_SIZE) {
    uint16_t next = dict_pool[p] | (dict_pool[p + 1] << 8);
    uint8_t nlen = dict_pool[p + 2];
    if (nlen > 63) nlen = 63;

    char name[65];
    memcpy(name, &dict_pool[p + 3], nlen);
    name[nlen] = '\0';

    uint16_t body_start = p + 5 + nlen;

    // Печатаем имя
    currentOutput->printf("  %s = ", name);

    // Печатаем значение (тег и данные лежат прямо в body_start)
    if (body_start + 1 < DICT_POOL_SIZE) {
      printValue(&dict_pool[body_start]);
    } else {
      currentOutput->print("?");
    }
    currentOutput->println();

    // Переход к следующему слову в списке
    if (next == 0) break;
    p = next;
  }
}
void word_add_task() {
  // === РЕЖИМ REPL: Заглядываем в поток токенов (R2L) ===
  int idx1 = g_tok_idx - 1;
  int idx2 = g_tok_idx - 2;
  if (idx1 >= 0 && idx2 >= 0) {
    uint8_t len1 = g_tok_len[idx1]; if (len1 > 31) len1 = 31;
    char str1[32]; memcpy(str1, g_tok_start[idx1], len1); str1[len1] = '\0';

    uint8_t len2 = g_tok_len[idx2]; if (len2 > 31) len2 = 31;
    char str2[32]; memcpy(str2, g_tok_start[idx2], len2); str2[len2] = '\0';

    char* endptr;
    uint32_t val1 = (uint32_t)strtoul(str1, &endptr, 10);
    bool is_num1 = (endptr != str1 && *endptr == '\0');

    char tname[64] = {0}; uint32_t interval = 0; bool valid = false;
    if (is_num1 && val1 > 0) {
      interval = val1; memcpy(tname, str2, len2); tname[len2] = '\0'; valid = true;
    } else {
      uint32_t val2 = (uint32_t)strtoul(str2, &endptr, 10);
      if ((endptr != str2 && *endptr == '\0') && val2 > 0) {
        interval = val2; memcpy(tname, str1, len1); tname[len1] = '\0'; valid = true;
      }
    }

    if (valid) {
      uint16_t addr = dict_find(tname);
      if (addr != 0xFFFF) {
        for (uint8_t i = 0; i < g_task_count; i++) {
          if (g_tasks[i].addr == addr && g_tasks[i].interval == interval) {
            currentOutput->println("+task: already scheduled"); return;
          }
        }
        if (g_task_count >= MAX_TASKS) {
          currentOutput->println("+task: limit");
          return;
        }
        g_tasks[g_task_count].addr = addr;
        g_tasks[g_task_count].interval = interval;
        g_tasks[g_task_count].last = millis();
        g_task_count++;
        currentOutput->print("scheduled: "); currentOutput->print(tname);
        currentOutput->print(" every "); currentOutput->print(interval); currentOutput->println(" ms");

        // 🔑 Ключевой момент: пропускаем эти токены в цикле парсера!
        g_tok_idx -= 2;
        return;
      }
    }
  }

  // === Фоллбэк: работа со стеком (если вызвана из скомпилированного кода без компиляторного перехвата) ===
  if (stack_is_empty()) return;
  uint8_t* top = &stack_mem[stack_ptr];
  if (top[0] != 0x0E) {
    currentOutput->println("task name expected");
    return;
  }
  uint8_t nlen = top[1]; if (nlen >= 64) nlen = 63;
  char tname[64]; memcpy(tname, &top[2], nlen); tname[nlen] = '\0';
  stack_ptr += elem_size(top);

  if (stack_is_empty()) return;
  uint8_t* ie = &stack_mem[stack_ptr];
  if (ie[0] < 4 || ie[0] > 11) {
    currentOutput->println("interval expected");
    return;
  }
  uint32_t interval = 0; uint16_t d = elem_size(ie) - 1;
  for (uint16_t k = 0; k < d && k < 4; k++) interval |= (uint32_t)ie[1 + k] << (k * 8);
  stack_ptr += elem_size(ie);

  if (interval == 0) return;
  uint16_t addr = dict_find(tname);
  if (addr == 0xFFFF) return;
  if (g_task_count >= MAX_TASKS) return;

  g_tasks[g_task_count].addr = addr;
  g_tasks[g_task_count].interval = interval;
  g_tasks[g_task_count].last = millis();
  g_task_count++;
}

void word_remove_task() {
  // === РЕЖИМ REPL ===
  int idx1 = g_tok_idx - 1;
  if (idx1 >= 0) {
    uint8_t len1 = g_tok_len[idx1]; if (len1 > 63) len1 = 63;
    char tname[64]; memcpy(tname, g_tok_start[idx1], len1); tname[len1] = '\0';

    uint16_t addr = dict_find(tname);
    if (addr != 0xFFFF) {
      int found_idx = -1;
      for (uint8_t i = 0; i < g_task_count; i++) {
        if (g_tasks[i].addr == addr) {
          found_idx = i;
          break;
        }
      }
      if (found_idx != -1) {
        for (uint8_t i = found_idx; i < g_task_count - 1; i++) g_tasks[i] = g_tasks[i + 1];
        g_task_count--;
        currentOutput->print("-task: removed '"); currentOutput->print(tname); currentOutput->println("'");
        g_tok_idx -= 1; // Пропускаем токен
        return;
      }
    }
  }

  // === Фоллбэк: стек ===
  if (stack_is_empty()) return;
  uint8_t* top = &stack_mem[stack_ptr];
  if (top[0] != 0x0E && top[0] != 0x0D) return;
  uint8_t nlen = top[1]; if (nlen >= 64) nlen = 63;
  char tname[64]; memcpy(tname, &top[2], nlen); tname[nlen] = '\0';
  stack_ptr += elem_size(top);

  uint16_t addr = dict_find(tname);
  if (addr == 0xFFFF) return;
  int found_idx = -1;
  for (uint8_t i = 0; i < g_task_count; i++) {
    if (g_tasks[i].addr == addr) {
      found_idx = i;
      break;
    }
  }
  if (found_idx != -1) {
    for (uint8_t i = found_idx; i < g_task_count - 1; i++) g_tasks[i] = g_tasks[i + 1];
    g_task_count--;
  }
}
// === СЛУЖЕБНЫЕ ФУНКЦИИ ДЛЯ ПЛАНИРОВЩИКА (вызываются из скомпилированного кода) ===
void word_schedule_task_runtime() {
  if (stack_is_empty()) return;
  // 1. Снимаем адрес слова (u16 или u32)
  uint8_t buf[8];
  uint16_t sz = stack_pop(buf, sizeof(buf));
  if (sz < 3 || (buf[0] != 6 && buf[0] != 9)) {
    currentOutput->println("runtime err: expected word addr");
    return;
  }
  uint16_t addr = buf[1] | (buf[2] << 8);

  // 2. Снимаем интервал (число)
  if (stack_is_empty()) return;
  sz = stack_pop(buf, sizeof(buf));
  if (sz < 2 || buf[0] < 4 || buf[0] > 11) {
    currentOutput->println("runtime err: expected interval");
    return;
  }
  uint32_t interval = 0;
  uint16_t d = sz - 1;
  for (uint16_t k = 0; k < d && k < 4; k++) {
    interval |= (uint32_t)buf[1 + k] << (k * 8);
  }

  if (interval == 0) return;

  // 3. Регистрация (проверка на дубликат)
  for (uint8_t i = 0; i < g_task_count; i++) {
    if (g_tasks[i].addr == addr && g_tasks[i].interval == interval) return;
  }
  if (g_task_count >= MAX_TASKS) {
    currentOutput->println("runtime err: task limit");
    return;
  }
  g_tasks[g_task_count].addr = addr;
  g_tasks[g_task_count].interval = interval;
  g_tasks[g_task_count].last = millis();
  g_task_count++;
}

void word_remove_task_runtime() {
  if (stack_is_empty()) return;
  uint8_t buf[8];
  uint16_t sz = stack_pop(buf, sizeof(buf));
  if (sz < 3 || (buf[0] != 6 && buf[0] != 9)) return;
  uint16_t addr = buf[1] | (buf[2] << 8);

  int found_idx = -1;
  for (uint8_t i = 0; i < g_task_count; i++) {
    if (g_tasks[i].addr == addr) {
      found_idx = i;
      break;
    }
  }
  if (found_idx != -1) {
    for (uint8_t i = found_idx; i < g_task_count - 1; i++) {
      g_tasks[i] = g_tasks[i + 1];
    }
    g_task_count--;
  }
}



void word_randRange() {
    // 1. Снимаем min (ВЕРХ стека — был прочитан вторым в R2L)
    if (stack_is_empty()) {
        currentOutput->println("randRange: min expected");
        return;
    }
    uint8_t* min_ptr = &stack_mem[stack_ptr];
    if (min_ptr[0] < 4 || min_ptr[0] > 11) {
        currentOutput->println("randRange: min must be a number");
        return;
    }
    uint16_t min_sz = elem_size(min_ptr);
    uint32_t min_val = 0;
    uint16_t d = min_sz - 1;
    for (uint16_t k = 0; k < d && k < 4; k++) {
        min_val |= (uint32_t)min_ptr[1 + k] << (k * 8);
    }
    stack_ptr += min_sz;
    
    // 2. Снимаем max (НИЗ стека — был прочитан первым в R2L)
    if (stack_is_empty()) {
        currentOutput->println("randRange: max expected");
        return;
    }
    uint8_t* max_ptr = &stack_mem[stack_ptr];
    if (max_ptr[0] < 4 || max_ptr[0] > 11) {
        currentOutput->println("randRange: max must be a number");
        return;
    }
    uint8_t res_tag = max_ptr[0];  // 🔑 Тег результата = тегу max
    uint16_t max_sz = elem_size(max_ptr);
    uint32_t max_val = 0;
    d = max_sz - 1;
    for (uint16_t k = 0; k < d && k < 4; k++) {
        max_val |= (uint32_t)max_ptr[1 + k] << (k * 8);
    }
    stack_ptr += max_sz;
    
    // 3. Проверяем min <= max
    if (min_val > max_val) {
        currentOutput->println("randRange: min > max");
        return;
    }
    
    // 4. Генерируем случайное число в диапазоне [min..max]
    uint32_t range = max_val - min_val + 1;
    uint32_t r = min_val + (esp_random() % range);
    
    // 5. 🔑 Возвращаем результат С ТЕГОМ max
    uint8_t out[8];
    out[0] = res_tag;
    
    switch (res_tag) {
        case 4:  // u8
            out[1] = (uint8_t)r;
            stack_push(out, 2);
            break;
        case 5:  // i8
            out[1] = (int8_t)r;
            stack_push(out, 2);
            break;
        case 6:  // u16
            *(uint16_t*)&out[1] = (uint16_t)r;
            stack_push(out, 3);
            break;
        case 7:  // i16
            *(int16_t*)&out[1] = (int16_t)r;
            stack_push(out, 3);
            break;
        case 8:  // u24
            out[1] = (uint8_t)r;
            out[2] = (uint8_t)(r >> 8);
            out[3] = (uint8_t)(r >> 16);
            stack_push(out, 4);
            break;
        case 9:  // u32
            *(uint32_t*)&out[1] = r;
            stack_push(out, 5);
            break;
        case 10: // i32
            *(int32_t*)&out[1] = (int32_t)r;
            stack_push(out, 5);
            break;
        case 11: // float
            {
                float f = (float)r;
                memcpy(&out[1], &f, 4);
                stack_push(out, 5);
            }
            break;
        default:
            // Фолбэк — u32
            out[0] = 9;
            *(uint32_t*)&out[1] = r;
            stack_push(out, 5);
            break;
    }
}

// === JSON ACCUMULATOR FOR json-set / json>var ===
char g_json_acc[256];
uint16_t g_json_acc_len = 0;
bool g_json_acc_first = true;

void jsonSetWord() {
    // 1. Снимаем ключ (NAME или STRING)
    if (stack_is_empty()) {
        currentOutput->println("json-set: key expected");
        return;
    }
    uint8_t* top = &stack_mem[stack_ptr];
    if (top[0] != 0x0E && top[0] != 0x0D) {
        currentOutput->println("json-set: key must be string/name");
        return;
    }
    uint8_t key_len = top[1];
    char key[130];
    uint16_t k_out = 0;
    // Копируем ключ с экранированием кавычек
    for (uint8_t i = 0; i < key_len && k_out < 125; i++) {
        char c = top[2 + i];
        if (c == '"' || c == '\\') key[k_out++] = '\\';
        key[k_out++] = c;
    }
    key[k_out] = '\0';
    stack_ptr += elem_size(top);

    // 2. Смотрим на значение (не снимая со стека)
    if (stack_is_empty()) {
        currentOutput->println("json-set: value expected");
        return;
    }
    top = &stack_mem[stack_ptr];
    
    // 3. Сериализуем значение в JSON-литерал
    char val_str[128];
    uint16_t val_len = 0;
    uint8_t tag = top[0];
    
    if (tag == 0 || tag == 1) { // BOOL
        val_len = sprintf(val_str, tag == 1 ? "true" : "false");
    } else if (tag >= 4 && tag <= 11) { // Числа
        if (tag == 11) { // FLOAT
            float f; memcpy(&f, &top[1], 4);
            val_len = sprintf(val_str, "%g", f);
        } else {
            uint32_t u = 0;
            uint16_t d = elem_size(top) - 1;
            for (uint16_t k = 0; k < d && k < 4; k++) u |= (uint32_t)top[1 + k] << (k * 8);
            bool is_signed = (tag == 5 || tag == 7 || tag == 10);
            if (is_signed) {
                if (d == 1 && (top[1] & 0x80)) u |= 0xFFFFFF00;
                if (d == 2 && (top[2] & 0x80)) u |= 0xFFFF0000;
                if (d == 3 && (top[3] & 0x80)) u |= 0xFF000000;
            }
            if (is_signed) val_len = sprintf(val_str, "%ld", (long)(int32_t)u);
            else val_len = sprintf(val_str, "%lu", (unsigned long)u);
        }
    } else if (tag == 0x0E || tag == 15) { // STRING или $STRING
        val_str[val_len++] = '"';
        uint16_t slen;
        const uint8_t* src;
        if (tag == 0x0E) {
            slen = top[1]; src = &top[2];
        } else {
            slen = top[1];
            uint16_t a = top[2] | (top[3] << 8);
            src = &data_pool[a];
        }
        for (uint16_t i = 0; i < slen && val_len < 120; i++) {
            char c = src[i];
            if (c == '"' || c == '\\') val_str[val_len++] = '\\';
            val_str[val_len++] = c;
        }
        val_str[val_len++] = '"';
    } else {
        val_len = sprintf(val_str, "null");
    }
    val_str[val_len] = '\0';
    
    // Снимаем значение со стека
    stack_ptr += elem_size(top);

    // 4. Дописываем пару в буфер
    char pair[256];
    int pair_len = sprintf(pair, "%s\"%s\":%s", g_json_acc_first ? "" : ",", key, val_str);
    g_json_acc_first = false;
    
    if (g_json_acc_len + pair_len < 250) {
        memcpy(&g_json_acc[g_json_acc_len], pair, pair_len);
        g_json_acc_len += pair_len;
        g_json_acc[g_json_acc_len] = '\0';
    } else {
        currentOutput->println("json-set: buffer overflow");
    }
}

void jsonToVarWord() {
    if (g_json_acc_len == 0) {
        currentOutput->println("json>var: empty buffer");
        return;
    }
    
    // Формируем валидный JSON объект
    char full_json[256];
    sprintf(full_json, "{%s}", g_json_acc);
    
    // 🔑 КРИТИЧЕСКИЙ ФИКС: Сохраняем состояние токенизатора!
    // Так как executeLine вызывается рекурсивно из скомпилированного слова (PINOUT),
    // без сохранения глобальных g_tok_* внешний цикл исполнения будет разрушен.
    uint8_t saved_tok_count = g_tok_count;
    int8_t saved_tok_idx = g_tok_idx;
    const char* saved_tok_start[64];
    uint8_t saved_tok_len[64];
    memcpy(saved_tok_start, g_tok_start, sizeof(g_tok_start));
    memcpy(saved_tok_len, g_tok_len, sizeof(g_tok_len));
    
    // Нативный парсер executeLine сам превратит JSON в "key = value" 
    // и автоматически создаст переменные в ТЕКУЩЕМ контексте!
    executeLine(full_json);
    
    // Восстанавливаем состояние токенизатора
    g_tok_count = saved_tok_count;
    g_tok_idx = saved_tok_idx;
    memcpy(g_tok_start, saved_tok_start, sizeof(g_tok_start));
    memcpy(g_tok_len, saved_tok_len, sizeof(g_tok_len));
    
    // Очищаем буфер для следующего вызова
    g_json_acc_len = 0;
    g_json_acc_first = true;
    g_json_acc[0] = '\0';
}
void setup() {
  Serial.begin(115200);
  Serial.println(); delay(500);
  for (int i = 0; i <= 255; i++) Serial.println();
  stack_clear();
  if (!FILESYSTEM.begin()) currentOutput->println("FS Mount Failed");
  addInternalWord("lit", wordLit);
  addInternalWord("main", word_main);
  addInternalWord("cont",  word_cont);
  addInternalWord("device", word_device);
  addInternalWord("heap", word_heap);
  addInternalWord("`",     word_dict_dump);
  addInternalWord("hex", word_hexdump);
  addInternalWord("print", word_print);
  addInternalWord("json>", word_json_export);
  addInternalWord("json>>", word_json_delta);
  addInternalWord("json>serial", word_json_export_serial);
  addInternalWord("json>file", word_json_export_file);
  addInternalWord("json-set", jsonSetWord);
  addInternalWord("json>var", jsonToVarWord);
  addInternalWord("words", word_words);
  addInternalWord("locals", word_lwords);
  addInternalWord("reset", word_reset);
  addInternalWord("body", word_body);
  addInternalWord("<--", word_checkpoint);
  addInternalWord("xxx", word_forget);
  addInternalWord("cord", word_cord);
  addInternalWord("bead", word_bead);
  addInternalWord("pool>", word_pool_dump);
  addInternalWord("check", word_check);
  addInternalWord("if", word_if);
  create_internal_word_str("chip", getChipName(), 0x06, choiceFunc);
  // addChipWord();
  addInternalWord("->",    word_drop);
  addInternalWord("as", word_as);
  addInternalWord("view", word_view);
  executeLine("-> as var");
  addInternalWord("oops",  stack_clear);
  addInternalWord("const", word_const);
  addInternalWord("while", word_while);
  addInternalWord("goto",  word_goto);
  addInternalWord("+task", word_add_task);
  addInternalWord("-task", word_remove_task);
  // Служебные функции для скомпилированного байт-кода
  addInternalWord("__schedule_task__", word_schedule_task_runtime);
  addInternalWord("__remove_task__", word_remove_task_runtime);
  addInternalWord("+loop", word_add_loop);
  addInternalWord("-loop", word_remove_loop);
  addInternalWord("len", lenWord);       // len → измерить и удалить
  addInternalWord("len!", lenPeekWord);  // len! → измерить и оставить
  addInternalWord("delayMicroseconds", delayMicrosecondsWord);
  addInternalWord("__local__", word_local_stub); // <-- ИСПРАВЛЕНО: регистрируем именно __local__
  addInternalWord(":", word_colon);
  addInternalWord(";", word_semicolon);
  addInternalWord("exit", word_exit);
addInternalWord("copy", word_copy);
addInternalWord("rgb2grb", word_rgb2grb);
addInternalWord("rgb2wrgb", word_rgb2wrgb);
  executeLine("cont vars");
  addMarkerWord("u8"); addMarkerWord("u24"); addMarkerWord("u32"); addMarkerWord("u16");
  addMarkerWord("i8"); addMarkerWord("i16"); addMarkerWord("i32"); addMarkerWord("f");    addMarkerWord("$S"); // Разрешаем использование $S как типа массива
  addMarkerWord("["); addMarkerWord("]");
  addMarkerWord("array"); addMarkerWord("@");
  executeLine("cont math");
  addInternalWord("randRange", word_randRange);
  addInternalWord("!", wordNot);
  addMarkerWord("-"); addMarkerWord("+"); addMarkerWord("="); addMarkerWord("/"); addMarkerWord("*");
  addMarkerWord("%"); addMarkerWord("^"); addMarkerWord("<<"); addMarkerWord(">>");
  addMarkerWord("+="); addMarkerWord("-="); addMarkerWord("*="); addMarkerWord("/=");
  addMarkerWord("&"); addMarkerWord("|");
  executeLine("cont logics");
  addMarkerWord("=="); addMarkerWord("!="); addMarkerWord("<"); addMarkerWord(">");
  addMarkerWord("<="); addMarkerWord(">=");
  executeLine("cont shell");
  addInternalWord("ls", word_ls);
  executeLine("ls as dir");
  addInternalWord("cd", word_cd);
  addInternalWord("type", word_type);
  addInternalWord("load", word_load);
  addInternalWord("save", word_save);
  addInternalWord("restore", word_restore);
  addInternalWord("out>file", word_out_file);
  addInternalWord("out>serial", word_out_serial);

  gpioInit();
  wifiInit();
  webInit();
  rtmInit();
  i2cInit();
#if ENABLE_TERM_LAYER
  addInternalWord("term", word_term);
#endif
  // 🔒 Безопасная загрузка стартового файла
  if (FILESYSTEM.exists("/startup.wrd")) {
      executeLine("load startup.wrd main");
  }
  // executeLine("load ex/net/net.words main");
  printStack();
  printActiveTasks(); // ← СПИСОК ЗАДАЧ
  word_prompt();
}
void loop() {
  // Кооперативное исполнение задач (без блокировки ввода)
  if (g_task_count > 0 && Serial.available() == 0) {
    uint32_t now = millis();
    for (uint8_t i = 0; i < g_task_count; i++) {
      if (g_tasks[i].interval == 0) {
        // 🔁 Непрерывный цикл (без тайминга)
        uint16_t sp_save = stack_ptr;
        exec_word(g_tasks[i].addr);
        stack_ptr = sp_save;
      }
      else if (now - g_tasks[i].last >= g_tasks[i].interval) {
        // ⏱ Таймерная задача
        g_tasks[i].last = now;
        uint16_t sp_save = stack_ptr;
        exec_word(g_tasks[i].addr);
        stack_ptr = sp_save;
      }
    }
  }
  // Ваш оригинальный обработчик Serial
  while (Serial.available()) {
    char c = Serial.read();
#if ENABLE_TERM_LAYER
    if (g_termMode) {
      handleTermChar(c);
      continue;
    }
#endif
    static uint8_t idx = 0;
    if (c == '\r' || c == '\n') {
      if (idx > 0) {
        line_buf[idx] = '\0';
        Serial.print(line_buf); Serial.println();
        executeLine(line_buf);
        Serial.println();
        printStack();
        printActiveTasks(); // ← СПИСОК ЗАДАЧ
        word_prompt();
      }
      idx = 0;
    } else if (c >= 32 && idx < 255) {
      line_buf[idx++] = c;
    }
  }
}
