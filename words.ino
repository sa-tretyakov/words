#define ETH_ENC28J60_ENABLED 0
// #define pril
#include <Arduino.h>
#include <Wire.h>
#include "driver/i2s.h"
#ifdef ESP8266
#include <ESP8266WiFi.h>
#else
#include <WiFi.h>
#endif
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
struct BlockEntry {
    uint8_t  type;       // 0 = while, 1 = if
    uint16_t start;      // адрес начала условия (для goto)
    uint16_t skip_pos;   // адрес для патча пропуска
};
static BlockEntry blk_stack[8];
static uint8_t    blk_depth = 0;

uint16_t g_else_goto_pos   = 0; // Где лежит адрес пары goto из else
static uint16_t g_local_id = 0; // <-- Счетчик для локальных переменных (l1, l2, l3...)
// 🔹 НОВОЕ: Карта соответствий "исходное имя" -> "ID локальной переменной"
struct LocalVarMap {
  char original_name[32]; // Исходное имя (например, "loc")
  uint16_t local_id;      // Сгенерированный ID (например, 1 для "l1")
};
LocalVarMap g_local_map[16]; // Максимум 16 уникальных локальных переменных на одно слово
uint8_t g_local_map_count = 0; // Текущее количество записей в карте
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
void process_tasks(bool only_continuous);
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
//---------- WEB сервера
#if defined(ESP8266)
#include <ESP8266WebServer.h>        //Содержится в пакете
ESP8266WebServer HTTP(80);
#else
#include <WebServer.h>
WebServer HTTP(80);
#endif
File fsUploadFile;

// web.ino — ИСПРАВЛЕННАЯ ВЕРСИЯ
#include <WebSocketsServer.h> // ⚠️ Обязательно здесь, иначе WStype_t не виден в этой вкладке

// Доступ к глобальным переменным и функциям из concWords.ino
extern Print* currentOutput;
extern void executeLine(const char* line);
extern void word_prompt();
extern void addInternalWord(const char* name, void (*func)());

WebSocketsServer wsServer(82);
#ifdef pril // #endif 
WebSocketsServer prilServer(81);
#endif 
#if defined(ESP8266)

#else
#include <esp_system.h>
RTC_NOINIT_ATTR uint8_t crashCounter;
#endif
#if defined(ESP8266)

#else

#endif


class HDLStream : public Print {
public:
    // === ТИПЫ КАНАЛОВ ===
    enum Target : uint8_t {
        T_NONE,   // не активен
        T_WS,     // WebSocket
        T_TCP,    // TCP-сокет
        T_UDP,    // UDP-сокет
        T_I2C,    // I²C
        T_I2S,    // I²S аудио
        T_HTTP    // HTTP-ответ
    };

    // === КОНСТРУКТОР ===
    HDLStream() : target(T_NONE), ws(nullptr), tcp(nullptr),
                  udp(nullptr), i2c_dev(0), i2s_port(0),
                  http_srv(nullptr), buf_len(0) {}

    // ============================================================
    // === ПОДКЛЮЧЕНИЕ КАНАЛОВ (ATTACH) ===
    // ============================================================
    
    void attachWs(WebSocketsServer* srv) {
        flush(); 
        target = T_WS; 
        ws = srv;
        tcp = nullptr; 
        udp = nullptr; 
        http_srv = nullptr;
    }

    void attachTcp(WiFiClient* c) {
        flush(); 
        target = T_TCP; 
        tcp = c;
        ws = nullptr; 
        udp = nullptr; 
        http_srv = nullptr;
    }

    void attachUdp(WiFiUDP* u, IPAddress ip, uint16_t port) {
        flush(); 
        target = T_UDP; 
        udp = u; 
        dest_ip = ip; 
        dest_port = port;
        ws = nullptr; 
        tcp = nullptr; 
        http_srv = nullptr;
    }

    void attachI2c(uint8_t dev) {
        flush(); 
        target = T_I2C; 
        i2c_dev = dev;
        ws = nullptr; 
        tcp = nullptr; 
        udp = nullptr; 
        http_srv = nullptr;
    }

    void attachI2s(uint8_t port) {
        flush(); 
        target = T_I2S; 
        i2s_port = port;
        ws = nullptr; 
        tcp = nullptr; 
        udp = nullptr; 
        http_srv = nullptr;
    }

void attachHttp(WebServer* srv) {
    flush();
    target = T_HTTP;
    http_srv = srv;
    
    // 🔑 КРИТИЧНО: не обнуляем ws, если WebSocket клиенты всё ещё подключены!
    // Это позволяет восстановить WS-канал после завершения HTTP-запроса.
    if (!isWsConnected()) {
        ws = nullptr;
    }
    tcp = nullptr; 
    udp = nullptr;
}

    // ============================================================
    // === ОТКЛЮЧЕНИЕ (DETACH) ===
    // ============================================================
    
// 🔴 ЖЁСТКИЙ DETACH — всегда сбрасывает в T_NONE (если force=true)
// Или восстанавливает WS, если клиенты подключены (если force=false)
void detach(bool force = false) {
    flush();
    
    // 🔑 КРИТИЧНО: если WebSocket клиенты всё ещё подключены и нет принудительного сброса,
    // восстанавливаем WS как активный канал вместо полного обнуления.
    if (!force && isWsConnected()) {
        target = T_WS;
        http_srv = nullptr; // HTTP-запрос завершён, обнуляем только его
    } else {
        target = T_NONE;
        ws = nullptr;
        tcp = nullptr;
        udp = nullptr;
        http_srv = nullptr;
    }
}

    // ============================================================
    // === ДИАГНОСТИКА ===
    // ============================================================
    
    // Возвращает имя активного канала (для слова out?)
    const char* targetName() const {
        switch (target) {
            case T_WS:   return "ws";
            case T_TCP:  return "tcp";
            case T_UDP:  return "udp";
            case T_I2C:  return "i2c";
            case T_I2S:  return "i2s";
            case T_HTTP: return "http";
            default:     return "none";
        }
    }

    // 🔧 НОВЫЙ МЕТОД: получение текущего таргета
    // Возвращает текущий Target, чтобы внешний код мог проверить состояние
    // без прямого доступа к приватному полю.
    Target getTarget() const { 
        return target; 
    }

    // ============================================================
    // === ЗАПИСЬ ДАННЫХ ===
    // ============================================================
    
    size_t write(uint8_t c) override {
        if (target == T_NONE) return 0;
        if (buf_len >= 254) flush();
        buf[buf_len++] = c;
        if (c == '\n') flush();
        return 1;
    }

    size_t write(const uint8_t* data, size_t size) override {
        if (target == T_NONE) return 0;
        size_t i = 0;
        while (i < size) {
            if (buf_len >= 254) {
                flush();
                process_tasks(true);
            }
            buf[buf_len++] = data[i++];
        }
        return i;
    }

    // ============================================================
    // === СБРОС БУФЕРА ===
    // ============================================================
    
    void flush() override {
        if (buf_len == 0 || target == T_NONE) return;
        
        switch (target) {
            case T_WS:
                if (ws) ws->broadcastTXT((uint8_t*)buf, buf_len);
                break;
                
            case T_TCP:
                if (tcp && tcp->connected())
                    tcp->write((uint8_t*)buf, buf_len);
                break;
                
            case T_UDP:
                if (udp) {
                    udp->beginPacket(dest_ip, dest_port);
                    udp->write((uint8_t*)buf, buf_len);
                    udp->endPacket();
                }
                break;
                
            case T_I2C:
                Wire.beginTransmission(i2c_dev);
                Wire.write((uint8_t*)buf, buf_len);
                Wire.endTransmission();
                break;
                
            case T_I2S: {
                size_t w;
                i2s_write((i2s_port_t)i2s_port, buf, buf_len, &w, 0);
                break;
            }
                
            case T_HTTP:
                if (http_srv) 
                    http_srv->sendContent(String((char*)buf, buf_len));
                break;
                
            default: 
                break;
        }
        
        buf_len = 0;
    }
     // 🔧 НОВЫЕ МЕТОДЫ: управление счетчиком WS-клиентов
    void wsClientConnected() { 
        ws_client_count++; 
    }
    
    void wsClientDisconnected() { 
        if (ws_client_count > 0) ws_client_count--; 
    }
    
    bool isWsConnected() const { 
        return ws_client_count > 0; 
    }
private:
    Target target;
    WebSocketsServer* ws;
    WiFiClient* tcp;
    WiFiUDP* udp;
    uint8_t i2c_dev;
    uint8_t i2s_port;
    WebServer* http_srv;
    IPAddress dest_ip;
    uint16_t dest_port;
    char buf[256];
    uint8_t buf_len;
    uint8_t ws_client_count = 0;
 
};

// Глобальный экземпляр мультиплексора
static HDLStream g_stream;


// Глобальные переменные для асинхронного приёма (в начале web.ino)
char ws_cmd[256];
volatile bool ws_cmd_ready = false;
uint8_t ws_len = 0;

// === ПАМЯТЬ ===
// === РАЗМЕРЫ ПУЛОВ (настраиваемые, берутся из heap) ===
// На ESP32 свободно ~150-250 КБ heap. Сумма ~144 КБ — безопасно.
#define STACK_SIZE      4096    // было 4096  (×2)
#define DICT_POOL_SIZE  32768   // было 65536 (×2)
#define DATA_POOL_SIZE  32768  // было 32768 (×2)
#define RSTACK_SIZE     1024    // было 512   (×2)
#define MAX_STACK_PRINT 32

// === УКАЗАТЕЛИ НА ПУЛЫ (выделяются в heap при старте) ===
uint8_t* stack_mem = nullptr;
uint8_t* dict_pool = nullptr;
uint8_t* data_pool = nullptr;
uint8_t* rstack_mem = nullptr;

uint16_t stack_ptr = STACK_SIZE;
uint16_t dict_ptr = 0;
uint16_t dict_last = 0xFFFF;
uint16_t data_ptr = 0;
uint16_t rstack_ptr = RSTACK_SIZE;
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
uint8_t  currentContext = 0;
uint8_t  g_next_ctx  = 1;
uint16_t ip = 0;
static uint16_t g_device_counter = 0;
// Гард: автоматически восстанавливает currentOutput при выходе из области видимости {}
struct OutputGuard {
    Print* saved;
    bool   owns_stream;
    
    OutputGuard() : saved(currentOutput), owns_stream(false) {}
    
    ~OutputGuard() {
        // 🔑 КРИТИЧЕСКАЯ ПРОВЕРКА:
        // Мы трогаем g_stream ТОЛЬКО если он всё ещё является текущим каналом вывода.
        // Если внутри executeLine() был вызван abort(), currentOutput уже стал &Serial,
        // и это условие НЕ сработает, что предотвратит ложный detach().
        if (owns_stream && currentOutput == &g_stream) {
            g_stream.detach(); // Используем публичный метод вместо g_stream.target = ...
        }
        // Восстанавливаем предыдущий канал вывода
        currentOutput = saved;
    }
};
// === ИНИЦИАЛИЗАЦИЯ ПУЛОВ В HEAP ===
// Выделяет память динамически → не занимает .bss → нет dram0_0_seg overflow.
// Возвращает false при ошибке (не хватает heap).
static bool init_pools() {
    // Выравнивание 4 байта критично для uint16_t/uint32_t операций.
    // malloc на ESP32 и так даёт 4-байтное выравнивание, но явно — надёжнее.
    stack_mem = (uint8_t*)malloc(STACK_SIZE);
    dict_pool = (uint8_t*)malloc(DICT_POOL_SIZE);
    data_pool = (uint8_t*)malloc(DATA_POOL_SIZE);
    rstack_mem = (uint8_t*)malloc(RSTACK_SIZE);

    if (!stack_mem || !dict_pool || !data_pool || !rstack_mem) {
        // Очистка частично выделенного
        free(stack_mem); free(dict_pool); free(data_pool); free(rstack_mem);
        stack_mem = dict_pool = data_pool = rstack_mem = nullptr;
        return false;
    }

    // Начальные значения указателей стеков
    stack_ptr = STACK_SIZE;
    rstack_ptr = RSTACK_SIZE;
    dict_ptr = 0;
    dict_last = 0xFFFF;
    data_ptr = 0;

    // Обнуление (важно для стабильности)
    memset(stack_mem, 0, STACK_SIZE);
    memset(dict_pool, 0, DICT_POOL_SIZE);
    memset(data_pool, 0, DATA_POOL_SIZE);
    memset(rstack_mem, 0, RSTACK_SIZE);

    return true;
}



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
// ============================================================
// === ЕДИНЫЙ БЛОК ХЕЛПЕРОВ СТЕКА (все в одном месте) ===
// ============================================================

// --- POP: снятие со стека ---
static inline bool popUInt8(uint8_t &out) {
    if (stack_is_empty()) return false;
    uint8_t* top = &stack_mem[stack_ptr];
    if (top[0] < 4 || top[0] > 11) return false;
    uint16_t sz = elem_size(top);
    if (sz < 2) return false;
    out = top[1];
    stack_ptr += sz;
    return true;
}

static inline bool popUInt16(uint16_t &out) {
    if (stack_is_empty()) return false;
    uint8_t* top = &stack_mem[stack_ptr];
    if (top[0] < 4 || top[0] > 11) return false;
    uint16_t sz = elem_size(top);
    if (sz < 3) return false;
    out = top[1] | (top[2] << 8);
    stack_ptr += sz;
    return true;
}

static inline bool popUInt32(uint32_t &out) {
    if (stack_is_empty()) return false;
    uint8_t* top = &stack_mem[stack_ptr];
    if (top[0] < 4 || top[0] > 11) return false;
    uint16_t sz = elem_size(top);
    if (sz < 2) return false;
    out = 0;
    uint16_t d = sz - 1;
    for (uint16_t i = 0; i < d && i < 4; i++)
        out |= (uint32_t)top[1 + i] << (i * 8);
    stack_ptr += sz;
    return true;
}

static inline bool popAddrInfo(uint16_t &addr, uint16_t &len) {
    if (stack_is_empty()) return false;
    uint8_t* top = &stack_mem[stack_ptr];
    if ((top[0] != 17 && top[0] != 20) || elem_size(top) != 6) return false;
    addr = top[1] | (top[2] << 8);
    len  = top[3] | (top[4] << 8);
    stack_ptr += 6;
    return true;
}

static inline bool popString(String &out) {
    if (stack_is_empty()) return false;
    uint8_t* top = &stack_mem[stack_ptr];
    if (top[0] != 0x0E && top[0] != 0x0D) return false;  // ← добавить 0x0D
    out = String((char*)&top[2], top[1]);
    stack_ptr += elem_size(top);
    return true;
}

// === ДЕКОДИРОВАНИЕ ЧИСЕЛ ИЗ БУФЕРА ===
// Универсальный декодер: little-endian + знаковое расширение для signed-тегов
// buf — указатель на ДАННЫЕ (без тега!), data_bytes — количество байт данных
static inline int32_t decode_int_le(const uint8_t* buf, uint16_t data_bytes, uint8_t tag) {
    uint32_t v = 0;
    uint16_t lim = (data_bytes > 4) ? 4 : data_bytes;
    for (uint16_t i = 0; i < lim; i++)
        v |= (uint32_t)buf[i] << (i * 8);
    // Знаковое расширение только для i8(5), i16(7), i32(10)
    if (tag == 5 || tag == 7 || tag == 10) {
        if (data_bytes == 1 && (buf[0] & 0x80)) v |= 0xFFFFFF00;
        if (data_bytes == 2 && (buf[1] & 0x80)) v |= 0xFFFF0000;
        if (data_bytes == 3 && (buf[2] & 0x80)) v |= 0xFF000000;
    }
    return (int32_t)v;
}

// Извлекает сырые байты из STRING / NAME / $STRING / ARRAY / REF_ARR
// Возвращает указатель на байты и их длину. Для $STRING/ARRAY — прямо из data_pool.

// Unsigned декодирование (без знакового расширения)
static inline uint32_t decode_uint_le(const uint8_t* buf, uint16_t data_bytes) {
    uint32_t v = 0;
    uint16_t lim = (data_bytes > 4) ? 4 : data_bytes;
    for (uint16_t i = 0; i < lim; i++)
        v |= (uint32_t)buf[i] << (i * 8);
    return v;
}
// === ПРОВЕРКИ ТИПОВ ===
static inline bool is_numeric_tag(uint8_t tag) {
    return tag >= 4 && tag <= 11;
}

static inline bool is_text_tag(uint8_t tag) {
    return tag >= 12 && tag <= 14;
}

static inline bool is_signed_tag(uint8_t tag) {
    return tag == 5 || tag == 7 || tag == 10; // i8, i16, i32
}

// === КОДИРОВАНИЕ ЧИСЕЛ В БУФЕР ===
// Записывает значение v в buf в формате little-endian (без тега!)
static inline void encode_uint_le(uint8_t* buf, uint32_t v, uint16_t data_bytes) {
    uint16_t lim = (data_bytes > 4) ? 4 : data_bytes;
    for (uint16_t i = 0; i < lim; i++) {
        buf[i] = (v >> (i * 8)) & 0xFF;
    }
}
// === ЗАХВАТ ТОКЕНОВ ИЗ R2L-ПОТОКА ===
// offset: 1 = предыдущий токен, 2 = через один, и т.д.
// Возвращает длину имени (0 = ошибка)
static inline uint8_t grab_token(int offset, char* out, uint8_t max_len) {
    int idx = g_tok_idx - offset;
    if (idx < 0) return 0;
    uint8_t len = g_tok_len[idx];
    if (len == 0 || len > max_len) return 0;
    memcpy(out, g_tok_start[idx], len);
    out[len] = '\0';
    return len;
}
// --- PUSH: кладём на стек ---
static inline void pushBool(bool v) {
    uint8_t b[1] = {(uint8_t)(v ? 1 : 0)};
    stack_push(b, 1);
}

static inline void pushUInt8(uint8_t v) {
    uint8_t b[2] = {4, v};
    stack_push(b, 2);
}

static inline void pushUInt16(uint16_t v) {
    uint8_t b[3] = {6, (uint8_t)(v & 0xFF), (uint8_t)(v >> 8)};
    stack_push(b, 3);
}

static inline void pushUInt32(uint32_t v) {
    uint8_t b[5] = {9};
    memcpy(&b[1], &v, 4);
    stack_push(b, 5);
}

static inline void pushInt32(int32_t v) {
    uint8_t b[5] = {10};
    memcpy(&b[1], &v, 4);
    stack_push(b, 5);
}

static inline void pushStringRaw(const char* s) {
    uint8_t l = strlen(s);
    if (l > 255) l = 255;
    uint8_t b[257] = {0x0E, l};
    memcpy(&b[2], s, l);
    stack_push(b, 2 + l);
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
// === peek_bytes: посмотреть на элемент стека и получить сырые байты ===
// НЕ сдвигает stack_ptr — это делает вызывающий код.
// Поддерживает: STRING(0x0E), NAME(0x0D), $STRING(15), ARRAY(17), REF_ARR(20)
// Возвращает false если тип не поддерживается или данные вне границ.
static inline bool peek_bytes(const uint8_t* top, const uint8_t*& out_ptr, uint16_t& out_len) {
    if (!top) return false;
    uint8_t tag = top[0];
    if (tag == 0x0E || tag == 0x0D) {          // STRING / NAME
        out_ptr = &top[2];
        out_len = top[1];
        return true;
    }
    if (tag == 15) {                           // $STRING
        out_len = top[1];
        uint16_t a = top[2] | (top[3] << 8);
        if (a + out_len > DATA_POOL_SIZE) return false;
        out_ptr = &data_pool[a];
        return true;
    }
    if (tag == 17 || tag == 20) {              // ARRAY / REF_ARR
        uint16_t base = top[1] | (top[2] << 8);
        uint16_t len  = top[3] | (top[4] << 8);
        uint8_t  esz  = type_registry[top[5]].size;
        uint32_t tot  = (uint32_t)len * esz;
        if (tot > 65535 || base + tot > DATA_POOL_SIZE) return false;
        out_ptr = &data_pool[base];
        out_len = (uint16_t)tot;
        return true;
    }
    return false;
}
// === УНИВЕРСАЛЬНЫЙ ДИСПЕТЧЕР РЕСУРСОВ ===
// Вызывает FUNC из тела слова-дескриптора
// Передаёт: адрес ресурса (ctx) через стек
// === УНИВЕРСАЛЬНЫЙ ДИСПЕТЧЕР РЕСУРСОВ (ИСПРАВЛЕННЫЙ) ===
static void dispatch_resource(uint16_t res_addr, uint16_t desc_addr, const char* op) {
  uint8_t vn = dict_pool[desc_addr + 2];
  uint8_t flags = dict_pool[desc_addr + 4 + vn];
  uint32_t fn = 0;

  // Вариант А: Стандартное внутреннее слово (созданное через addInternalWord)
  // Функция лежит в конце структуры слова
  if (flags & 0x04) {
    uint16_t next = dict_pool[desc_addr] | (dict_pool[desc_addr + 1] << 8);
    uint16_t end = next ? next : dict_ptr;
    memcpy(&fn, &dict_pool[end - 4], 4);
  }
  // Вариант Б: Кастомное слово, где тело начинается с тега 19 (FUNC)
  else if (dict_pool[desc_addr + 5 + vn] == 19) {
    memcpy(&fn, &dict_pool[desc_addr + 5 + vn + 1], 4);
  }

  if (fn == 0) {
    currentOutput->print(op);
    currentOutput->println(": invalid descriptor");
    return;
  }

  // Кладём адрес ресурса на стек (как ADDR, тег 0x12) и вызываем функцию
  uint8_t ctx[3] = {0x12, (uint8_t)(res_addr & 0xFF), (uint8_t)(res_addr >> 8)};
  stack_push(ctx, 3);
  ((void(*)())fn)();
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

    // Структурные маркеры не обрабатываем как операторы
    if (op[0] == ')' || op[0] == '(' || op[0] == '[' || op[0] == ']') {
        // Возвращаем маркер на место — он должен остаться на стеке
        return;
    }

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

    if (l_tag == 0x12 && l_len == 2) {
        uint16_t var_addr = l_data[0] | (l_data[1] << 8);
        if (var_addr < dict_ptr || var_addr >= local_dict_ptr) {
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

    // 🔹 ПРОВЕРКА СОВМЕСТИМОСТИ ТИПОВ
    // При несовместимости — молча return. Маркер и RHS остаются на стеке.
    {
        bool l_num = (actual_l_tag >= 4 && actual_l_tag <= 11);
        bool r_num = (r_tag >= 4 && r_tag <= 11);
        bool l_txt = (actual_l_tag >= 12 && actual_l_tag <= 14) || actual_l_tag == 15;
        bool r_txt = (r_tag >= 12 && r_tag <= 14) || r_tag == 15;
        bool l_addr = (l_tag == 0x12);
        bool compatible = false;

        if (op_len == 1) {
            char c = op[0];
            if (c == '+') {
                if ((l_num && r_num) || (l_txt && r_txt)) compatible = true;
            }
            else if (c == '-' || c == '*' || c == '/' || c == '%' || c == '^' || 
                     c == '&' || c == '|') {
                if (l_num && r_num) compatible = true;
            }
            else if (c == '=') {
                if (l_addr) compatible = true;
            }
            else if (c == '<' || c == '>') {
                if ((l_num && r_num) || (l_txt && r_txt)) compatible = true;
            }
        }
        else if (op_len == 2) {
            if (strcmp(op, "==") == 0 || strcmp(op, "!=") == 0) {
                compatible = true;
            }
            else if (strcmp(op, "<=") == 0 || strcmp(op, ">=") == 0) {
                if ((l_num && r_num) || (l_txt && r_txt)) compatible = true;
            }
            else if (strcmp(op, "<<") == 0 || strcmp(op, ">>") == 0) {
                if (l_num && r_num) compatible = true;
            }
            else if (strcmp(op, "+=") == 0 || strcmp(op, "-=") == 0 || 
                     strcmp(op, "*=") == 0 || strcmp(op, "/=") == 0) {
                if (l_addr && ((l_num && r_num) || (l_txt && r_txt))) compatible = true;
            }
        }

        if (!compatible) {
            // 🔑 LHS не на стеке (его передал вызывающий код как параметры).
            // Кладём его обратно, чтобы следующий вызов (например, find) мог снять.
            if (l_tag >= 12 && l_tag <= 14) {
                // Строка: l_data[0] = длина, l_data[1..] = данные
                uint8_t tmp[257];
                tmp[0] = l_tag;
                tmp[1] = l_data[0];
                uint8_t slen = l_data[0];
                if (slen > 253) slen = 253;
                memcpy(&tmp[2], &l_data[1], slen);
                stack_push(tmp, 2 + slen);
            }
            else if (l_tag == 0x12) {
                // ADDR
                uint8_t tmp[3] = {0x12, l_data[0], l_data[1]};
                stack_push(tmp, 3);
            }
            else {
                // Число или другой тип: l_data = данные, l_len = длина данных
                uint8_t tmp[8];
                tmp[0] = l_tag;
                memcpy(&tmp[1], l_data, l_len);
                stack_push(tmp, 1 + l_len);
            }
            return;
        }

        // 🔹 == и != между разными типами → false / true
        if (op_len == 2 && (strcmp(op, "==") == 0 || strcmp(op, "!=") == 0)) {
            bool same_kind = (actual_l_tag == r_tag) || (l_num && r_num) || (l_txt && r_txt);
            if (!same_kind) {
                stack_ptr += elem_size(&stack_mem[stack_ptr]);  // RHS
                stack_ptr += 2 + op_len;                         // маркер
                stack_ptr += elem_size(&stack_mem[stack_ptr]);  // LHS
                pushBool(strcmp(op, "!=") == 0);
                return;
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

    // 🔽 Декодирование LHS (Числа)
    int32_t l_i = 0; float l_f = 0;
    bool l_is_float = (actual_l_tag == 11);
    if (l_is_float) memcpy(&l_f, actual_l_data, 4);
    else {
        l_i = decode_int_le(actual_l_data, actual_l_len, actual_l_tag);
        l_f = (float)l_i;
    }

    // 🔽 Декодирование RHS (Числа)
    bool r_is_float = (r_tag == 11);
    int32_t r_i = 0; float r_f = 0;
    if (r_is_float) memcpy(&r_f, &stack_mem[r_addr + 1], 4);
    else {
        r_i = decode_int_le(&stack_mem[r_addr + 1], r_sz - 1, r_tag);
        r_f = (float)r_i;
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
    else if (strcmp(op, "+") == 0) {
        res_i = l_i + r_i;
        res_f = L + R;
    }
    else if (strcmp(op, "-") == 0) {
        res_i = l_i - r_i;
        res_f = L - R;
    }
    else if (strcmp(op, "*") == 0) {
        res_i = l_i * r_i;
        res_f = L * R;
    }
    else if (strcmp(op, "/") == 0) {
        res_i = (r_i != 0) ? (l_i / r_i) : 0;
        res_f = (R != 0.0f) ? (L / R) : 0.0f;
    }
    else if (strcmp(op, "%") == 0) {
        res_i = (r_i != 0) ? (l_i % r_i) : 0;
        res_float = false;
    }
    else if (strcmp(op, "^") == 0) {
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
    else if (strcmp(op, "&") == 0) {
        res_i = l_i & r_i;
        res_float = false;
    }
    else if (strcmp(op, "|") == 0) {
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
    else if (strcmp(op, "<") == 0) {
        res_i = (L < R);
        is_compare = true;
        res_float = false;
    }
    else if (strcmp(op, ">") == 0) {
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
            encode_uint_le(casted, (uint32_t)v, actual_l_len);
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
// 🔧 ФИКС: расширение типа для операций, которые могут увеличить значение
uint8_t res_tag = actual_l_tag;
uint16_t res_len = actual_l_len;
if (!res_float) {
    bool is_shift_or_mul = (strcmp(op, "<<") == 0 || strcmp(op, ">>") == 0 || strcmp(op, "*") == 0);
    if (is_shift_or_mul) {
        // u8(4), i8(5), u16(6), i16(7), u24(8) → u32(9)
        // i32(10) → остаётся i32(10)
        // u32(9) → остаётся u32(9)
        if (res_tag >= 4 && res_tag <= 8) {
            res_tag = 9;   // u32
            res_len = 4;
        } else if (res_tag == 10) {
            res_len = 4;   // i32
        }
    }
}

uint8_t res_buf[8];
res_buf[0] = res_float ? 11 : res_tag;
if (res_float) {
    memcpy(&res_buf[1], &res_f, 4);
    stack_push(res_buf, 5);
} else {
    encode_uint_le(&res_buf[1], (uint32_t)res_i, res_len);
    stack_push(res_buf, 1 + res_len);
}
}
void choiceFunc() {
    uint8_t abuf[3];
    if (stack_pop(abuf, 3) != 3 || abuf[0] != 0x12) return;
    uint16_t addr = abuf[1] | (abuf[2] << 8);
    if (addr == 0) return;
    uint8_t nlen = dict_pool[addr + 2];
    uint16_t next = dict_pool[addr] | (dict_pool[addr + 1] << 8);
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
                    mark_dirty(addr);
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

    // 🔹 $STRING (тег 15)
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
                stack_ptr += 3;
                if (stack_is_empty()) {
                    stack_ptr = old_sp;
                    return;
                }
                uint8_t* src = &stack_mem[stack_ptr];
                uint16_t src_sz = elem_size(src);
                uint16_t new_len = 0;
                const uint8_t* new_data = nullptr;
                if (src[0] == 0x0E) {
                    new_len = src[1];
                    new_data = &src[2];
                } else if (src[0] >= 12 && src[0] <= 13) {
                    new_len = src[1];
                    new_data = &src[2];
                } else {
                    stack_ptr += src_sz;
                    stack_ptr = old_sp;
                    return;
                }
                uint16_t new_addr;
                if (new_len <= slen) {
                    new_addr = d_addr;
                } else {
                    if (data_ptr + new_len > DATA_POOL_SIZE) {
                        currentOutput->println("data_pool overflow");
                        stack_ptr += src_sz;
                        stack_ptr = old_sp;
                        return;
                    }
                    new_addr = data_ptr;
                    data_ptr += new_len;
                }
                if (new_len > 0) {
                    memcpy(&data_pool[new_addr], new_data, new_len);
                }
                dict_pool[val_start + 1] = new_len;
                dict_pool[val_start + 2] = new_addr & 0xFF;
                dict_pool[val_start + 3] = new_addr >> 8;
                stack_ptr += src_sz;
                mark_dirty(addr);
                return;
            }
            apply_op(&tmp[1], slen, 0x0E, 0);
            return;
        }
        stack_push(tmp, 2 + slen);
        return;
    }

    // 🔹 STRING / NAME / MARKER (теги 12-14) — in-place перезапись
    if (var_tag >= 12 && var_tag <= 14) {
        uint8_t old_len = dict_pool[val_start + 1];
        uint16_t max_data = val_size - 2; // вычитаем тег(1) + len(1)
        if (!stack_is_empty() && stack_mem[stack_ptr] == 0x0C) {
            uint8_t* top = &stack_mem[stack_ptr];
            if (top[1] == 1 && top[2] == '=') {
                stack_ptr += 3;
                if (stack_is_empty()) { stack_ptr = old_sp; return; }
                uint8_t* src = &stack_mem[stack_ptr];
                uint16_t src_sz = elem_size(src);
                uint16_t new_len = 0;
                const uint8_t* new_data = nullptr;
                if (src[0] == 0x0E || (src[0] >= 12 && src[0] <= 14)) {
                    new_len = src[1];
                    new_data = &src[2];
                } else if (src[0] == 15) {
                    new_len = src[1];
                    uint16_t a = src[2] | (src[3] << 8);
                    if (a + new_len > DATA_POOL_SIZE) { stack_ptr += src_sz; stack_ptr = old_sp; return; }
                    new_data = &data_pool[a];
                } else {
                    stack_ptr += src_sz; stack_ptr = old_sp; return;
                }
                if (new_len > max_data) {
                    currentOutput->println("string too long");
                    stack_ptr += src_sz; stack_ptr = old_sp; return;
                }
                dict_pool[val_start + 1] = new_len;
                if (new_len > 0) memcpy(&dict_pool[val_start + 2], new_data, new_len);
                stack_ptr += src_sz;
                mark_dirty(addr);
                return;
            }
            // Не '=' — передаём в apply_op (конкатенация, сравнение)
            uint8_t tmp[257];
            tmp[0] = 0x0E; tmp[1] = old_len;
            if (old_len > 0) memcpy(&tmp[2], &dict_pool[val_start + 2], old_len);
            apply_op(&tmp[1], old_len, 0x0E, 0);
            return;
        }
        // Нет маркера — чтение: кладём строку на стек
        uint8_t tmp[257];
        tmp[0] = 0x0E; tmp[1] = old_len;
        if (old_len > 0) memcpy(&tmp[2], &dict_pool[val_start + 2], old_len);
        stack_push(tmp, 2 + old_len);
        return;
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

        // === МАССОВАЯ ЗАПИСЬ arr[] = [...] ===
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
                if (tp == 18) {
                    if (v[0] != 0x12) RESTORE_AND_PUSH();
                } else {
                    if (v[0] != 14 && v[0] != 15 && (v[0] < 4 || v[0] > 11)) RESTORE_AND_PUSH();
                }
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
                }
                else if (tp == 18) {
                    data_pool[target]     = v[1];
                    data_pool[target + 1] = v[2];
                }
                else {
                    // ✅ ХЕЛПЕРЫ decode_uint_le + encode_uint_le
                    uint32_t val32 = decode_uint_le(&v[1], vsz - 1);
                    encode_uint_le(&data_pool[target], val32, esz);
                }
                stack_ptr += vsz;
            }
            mark_dirty(addr);
            return;
        }

        // === ЗАПИСЬ/ЧТЕНИЕ ПО ИНДЕКСУ arr[i] ===
        if (nxt[0] != 14 && nxt[0] != 15 && (nxt[0] < 4 || nxt[0] > 11)) RESTORE_AND_PUSH();
        uint16_t idx_sz = elem_size(nxt);
        // ✅ ХЕЛПЕР decode_uint_le вместо ручного цикла
        uint32_t idx_val = decode_uint_le(&nxt[1], idx_sz - 1);
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

        // === ЗАПИСЬ ПО ИНДЕКСУ arr[i] = val ===
        if (is_assign) {
            if (stack_is_empty()) RESTORE_AND_PUSH();
            uint8_t* v = &stack_mem[stack_ptr];
            if (tp == 18) {
                if (v[0] != 0x12) RESTORE_AND_PUSH();
            } else {
                if (v[0] != 14 && v[0] != 15 && (v[0] < 4 || v[0] > 11)) RESTORE_AND_PUSH();
            }
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
            }
            else if (tp == 18) {
                data_pool[data_addr]     = v[1];
                data_pool[data_addr + 1] = v[2];
            }
            else {
                // ✅ ХЕЛПЕРЫ decode_uint_le + encode_uint_le
                uint32_t val32 = decode_uint_le(&v[1], vsz - 1);
                encode_uint_le(&data_pool[data_addr], val32, esz);
            }
            stack_ptr += vsz;
            mark_dirty(addr);
            return;
        }

// === ЧТЕНИЕ ПО ИНДЕКСУ arr[i] ===
if (tp == 15) {
    uint8_t slen  = data_pool[data_addr];
    uint16_t d_addr = data_pool[data_addr + 1] | (data_pool[data_addr + 2] << 8);
    uint8_t tmp[257] = {0x0E, slen};
    if (slen > 0 && d_addr + slen <= DATA_POOL_SIZE) memcpy(&tmp[2], &data_pool[d_addr], slen);
    // 🔧 ФИКС: проверяем маркер операции на стеке
    if (!stack_is_empty() && stack_mem[stack_ptr] == 0x0C) {
        uint8_t* top = &stack_mem[stack_ptr];
        uint8_t op_len = top[1];
        if (op_len >= 1 && op_len <= 2) {
            char op[3] = {0};
            memcpy(op, &top[2], op_len);
            // Не сворачиваем, если это скобка
            if (op[0] != '(' && op[0] != ')' && op[0] != '[' && op[0] != ']') {
                apply_op(&tmp[1], slen, 0x0E, 0);
                return;
            }
        }
    }
    stack_push(tmp, 2 + slen);
    return;
}
if (tp == 18) {
    uint16_t word_addr = data_pool[data_addr] | (data_pool[data_addr + 1] << 8);
    if (word_addr != 0 && word_addr < dict_ptr) {
        exec_word(word_addr);
    }
    return;
}
uint8_t res[8]; res[0] = tp;
memcpy(&res[1], &data_pool[data_addr], esz);
// 🔧 ФИКС: проверяем маркер операции на стеке
if (!stack_is_empty() && stack_mem[stack_ptr] == 0x0C) {
    uint8_t* top = &stack_mem[stack_ptr];
    uint8_t op_len = top[1];
    if (op_len >= 1 && op_len <= 2) {
        char op[3] = {0};
        memcpy(op, &top[2], op_len);
        // Не сворачиваем, если это скобка
        if (op[0] != '(' && op[0] != ')' && op[0] != '[' && op[0] != ']') {
            apply_op(&res[1], esz, tp, 0);
            return;
        }
    }
}
stack_push(res, 1 + esz);
return;
    }

    // 🔑 5. Скалярные операции и присваивания (=, +=, -= и т.д.)
    mark_dirty(addr);
    uint8_t addr_payload[2] = { (uint8_t)(addr & 0xFF), (uint8_t)(addr >> 8) };
    
    // 🔧 ФИКС: Проверяем маркер перед вызовом apply_op (как сделано для arr[i])
    bool can_apply = false;
    if (!stack_is_empty() && stack_mem[stack_ptr] == 0x0C) {
        uint8_t* top = &stack_mem[stack_ptr];
        uint8_t op_len = top[1];
        if (op_len >= 1 && op_len <= 2) {
            char op[3] = {0};
            memcpy(op, &top[2], op_len);
            // Если это НЕ скобка, разрешаем apply_op свернуть выражение
            if (op[0] != '(' && op[0] != ')' && op[0] != '[' && op[0] != ']') {
                can_apply = true;
            }
        }
    }
    
    if (can_apply) {
        // Маркер есть и это оператор (+, -, / и т.д.) — сворачиваем
        apply_op(addr_payload, 2, 0x12, val_start + 1);
    } else {
        // Маркера нет или это скобка ')' — просто кладём значение переменной на стек
        stack_push(&dict_pool[val_start], val_size);
    }
}


void printRawValue(const uint8_t* buf) {
    if (!buf) return;
    uint8_t tag = buf[0];
    
    // 🔹 ОБРАБОТКА BOOL (теги 0 и 1)
    if (tag == 0 || tag == 1) {
        currentOutput->printf("%d", tag);
        return;
    }
    // 🔹 ДОБАВИТЬ: Обработка $STRING (тег 15)
    if (tag == 15) {
        uint8_t len = buf[1];
        uint16_t addr = buf[2] | (buf[3] << 8);
        if (addr + len <= DATA_POOL_SIZE && len > 0) {
            currentOutput->write(&data_pool[addr], len);
        }
        return;
    }
    
    uint16_t len = tag_size(buf);
    if (tag >= 12 && tag <= 14) {
        currentOutput->write(&buf[2], len);
        return;
    }    

    
    // ✅ ХЕЛПЕР decode_uint_le вместо ручного цикла
    uint32_t v = decode_uint_le(&buf[1], len);
    
    switch (tag) {
        case 10: 
            // Для i32 применяем знаковое расширение
            currentOutput->printf("%ld", (long)decode_int_le(&buf[1], len, tag));
            break;
        case 11: {
            float f;
            memcpy(&f, &buf[1], 4);
            currentOutput->printf("%g", f);
        } break;
        default: 
            currentOutput->printf("%lu", (unsigned long)v);
            break;
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
    
    if (flags & 0x04) scan_end -= 4;
    
    // Печать заголовка
    currentOutput->printf("[%04X] \"", addr);
    currentOutput->write(&dict_pool[addr + 3], name_len);
    currentOutput->printf("\" flags:0x%02X ctx:%u\n", 
        flags, dict_pool[addr + 3 + name_len]);
    
    if (flags & 0x04) {
        uint32_t fn;
        memcpy(&fn, &dict_pool[(next ? next : dict_ptr) - 4], 4);
        currentOutput->printf("  C-func: 0x%08lX\n", (unsigned long)fn);
    }
    
    currentOutput->print("  body: ");
    
    // 🔑 Защита от циклов
    uint16_t visited[32];
    uint8_t visited_count = 0;
    
    // 🔑 Находим адрес nop
    uint16_t nop_addr = dict_find("nop");
    
    // 🔑 Проверяем, это цепь (тело содержит адрес бусины)?
    if (body_start + 1 < scan_end) {
        uint16_t first_bead = dict_pool[body_start] | (dict_pool[body_start + 1] << 8);
        
        // Проверяем, что first_bead — это бусина (анонимное слово с nlen=0)
        if (first_bead < dict_ptr && first_bead != nop_addr) {
            uint8_t bead_nlen = dict_pool[first_bead + 2];
            uint8_t bead_flags = dict_pool[first_bead + 4 + bead_nlen];
            
            if (bead_nlen == 0 && (bead_flags & 0x08)) {
                // 🔑 ЭТО ЦЕПЬ — идём по связному списку бусин
                uint16_t curr_bead = first_bead;
                
                while (curr_bead != 0 && curr_bead != nop_addr && curr_bead < dict_ptr) {
                    // Защита от циклов
                    bool already_visited = false;
                    for (uint8_t v = 0; v < visited_count; v++) {
                        if (visited[v] == curr_bead) {
                            already_visited = true;
                            break;
                        }
                    }
                    if (already_visited) {
                        currentOutput->print("[CYCLE] ");
                        break;
                    }
                    if (visited_count < 32) {
                        visited[visited_count++] = curr_bead;
                    }
                    
                    // Читаем действие из тела бусины
                    uint16_t bead_body = curr_bead + 5; // nlen=0
                    uint16_t action_addr = dict_pool[bead_body] | (dict_pool[bead_body + 1] << 8);
                    uint16_t next_bead = dict_pool[bead_body + 2] | (dict_pool[bead_body + 3] << 8);
                    
                    // Ищем имя действия
                    const char* action_name = nullptr;
                    char buf[64];
                    uint16_t p = 0;
                    while (p < dict_ptr) {
                        if (p == action_addr) {
                            uint8_t l = dict_pool[p + 2];
                            if (l > 63) l = 63;
                            memcpy(buf, &dict_pool[p + 3], l);
                            buf[l] = '\0';
                            action_name = buf;
                            break;
                        }
                        uint16_t nx = dict_pool[p] | (dict_pool[p + 1] << 8);
                        if (nx == 0) break;
                        p = nx;
                    }
                    
                    currentOutput->printf("[%04X:", curr_bead);
                    if (action_name) {
                        currentOutput->print(action_name);
                    } else {
                        currentOutput->printf("@%04X", action_addr);
                    }
                    currentOutput->print("] ");
                    
                    // Переход к следующей бусине
                    curr_bead = next_bead;
                }
                
                currentOutput->println();
                return;
            }
        }
    }
    
    // 🔑 Обычное слово (не цепь) — старая логика
    bool is_local = false;  // Флаг: предыдущее слово было __local__
    for (uint16_t i = body_start; i < scan_end; ) {
        uint16_t wa = dict_pool[i] | (dict_pool[i + 1] << 8);
        
if (wa == 0x0000) {
    if (i + 3 > scan_end) break;
    uint8_t tag = dict_pool[i + 2];
    uint16_t lit_sz = elem_size(&dict_pool[i + 2]);
    
    // БЫЛО: currentOutput->printf("lit(%s) ", tag_short(tag));
    // СТАЛО:
    currentOutput->print("lit ");
    printValue(&dict_pool[i + 2]);
    currentOutput->print(" ");
    
    i += 2 + lit_sz;
}
else if (dict_pool[i] >= 4 && dict_pool[i] <= 14) {
    uint8_t tag = dict_pool[i];
    uint16_t lit_sz = elem_size(&dict_pool[i]);
    if (is_local && tag == 0x0D) {
        // После __local__ идёт NAME — печатаем имя переменной
        uint8_t nlen = dict_pool[i + 1];
        if (i + 2 + nlen > scan_end) nlen = 0;
        if (nlen > 63) nlen = 63;
        currentOutput->write(&dict_pool[i + 2], nlen);
        currentOutput->print(" ");
    } else {
        currentOutput->printf("%s ", tag_short(tag));
    }
    is_local = false;
    i += lit_sz;
}
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
            
if (wname) {
    currentOutput->print(wname);
    if ((strcmp(wname, "if") == 0 || strcmp(wname, "goto") == 0) && i + 4 <= scan_end) {
        uint16_t target = dict_pool[i + 2] | (dict_pool[i + 3] << 8);
        currentOutput->printf("@%04X", target);
        i += 2;
    }
    currentOutput->print(" ");
    is_local = (strcmp(wname, "__local__") == 0);
} else {
    currentOutput->printf("@%04X ", wa);
    is_local = false;
}
i += 2;
        }
    }
    
    currentOutput->println();
}
void exec_word(uint16_t addr) {
uint8_t nlen  = dict_pool[addr + 2];
uint8_t flags = dict_pool[addr + 4 + nlen];


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
    uint16_t body_start = addr + 5 + nlen;
    
    bool is_bead = (nlen == 0);
    bool is_cord = false;
    
    if (!is_bead && body_start + 1 < body_end) {
        uint16_t first_word = dict_pool[body_start] | (dict_pool[body_start + 1] << 8);
        if (first_word < dict_ptr) {
            uint8_t fw_nlen = dict_pool[first_word + 2];
            uint8_t fw_flags = dict_pool[first_word + 4 + fw_nlen];
            if (fw_nlen == 0 && (fw_flags & 0x08)) {
                is_cord = true;
            }
        }
    }
    
    // 🔹 БУСИНА: дублируем стек через memcpy
    uint16_t saved_sp_for_bead = stack_ptr;
    if (is_bead) {
        uint16_t data_size = STACK_SIZE - stack_ptr;
        if (data_size > 0 && stack_ptr >= data_size) {
            memcpy(&stack_mem[stack_ptr - data_size], &stack_mem[stack_ptr], data_size);
            stack_ptr -= data_size;
        }
    }
    
    // 🔹 НИТЬ: сохраняем паттерн стека
    if (is_cord) {
        // Можно сохранить stack_ptr в глобальную переменную, если нужно
    }
    
    // СОХРАНЯЕМ кадр локалов
    uint16_t saved_local_dict_ptr = local_dict_ptr;
    if (local_frame_ptr < LOCAL_FRAME_STACK_SIZE) {
        local_frame_stack[local_frame_ptr++] = local_dict_ptr;
    }
    
    rstack_push_frame(ip, body_end);
    vm_run(body_start, body_end);
    ip = rstack_pop_ret();
    
    // ВОССТАНАВЛИВАЕМ кадр локалов
    if (local_frame_ptr > 0) {
        local_dict_ptr = local_frame_stack[--local_frame_ptr];
    } else {
        local_dict_ptr = saved_local_dict_ptr;
    }
    
    // 🔹 БУСИНА: откатываем дубль
    if (is_bead) {
        stack_ptr = saved_sp_for_bead;
    }
    
    // 🔹 НИТЬ: очищаем стек
    if (is_cord) {
        stack_ptr = STACK_SIZE;
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
    currentOutput->println("data_pool dump:");
    for (uint16_t i = 0; i < data_ptr; i += 16) {
        currentOutput->printf("%04X: ", i);
        for (int j = 0; j < 16; j++) {
            if (i + j < data_ptr) currentOutput->printf("%02X ", data_pool[i + j]);
            else currentOutput->print("   ");
        }
        currentOutput->print("| ");
        for (int j = 0; j < 16; j++) {
            if (i + j < data_ptr) {
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
    if (!token || !token) return;
    size_t t_len = strlen(token);
    
    // === 1. СТРОКА "..." ===
    if (token[0] == '"' && token[t_len - 1] == '"' && t_len >= 2) {
        size_t str_len = t_len - 2;
        if (str_len > 255) str_len = 255;
        uint8_t buf[257]; buf[0] = 0x0E; buf[1] = (uint8_t)str_len;
        memcpy(&buf[2], &token[1], str_len);
        if (!stack_is_empty() && stack_mem[stack_ptr] == 0x0C) apply_op(&buf[1], str_len, 0x0E, 0);
        else stack_push(buf, 2 + str_len); return;
    }
    
    // === 2. ПОИСК В СЛОВАРЕ ===
    uint16_t addr = dict_find(token);
    if (addr != 0xFFFF) {
        exec_word(addr);
        return;
    }
    
    // === 3. ПАРСИНГ ЧИСЛА ===
    uint8_t tag = 0; uint32_t val_u = 0; int32_t val_s = 0; bool is_num = false; char* endptr;
    const char* suffixes[] = {"u8", "i8", "u16", "i16", "u24", "u32", "i32", "f"};
    const uint8_t suf_tags[] = {4, 5, 6, 7, 8, 9, 10, 11};
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
        
        // 🔧 АВТО-ОПРЕДЕЛЕНИЕ: если есть точка/экспонента — это float (игнорируем суффикс)
        bool has_dot = false;
        for (size_t k = 0; k < num_len; k++) {
            if (num_buf[k] == '.' || num_buf[k] == 'e' || num_buf[k] == 'E') {
                has_dot = true; break;
            }
        }
        if (has_dot) tag = 11;  // принудительно float
        
        if (tag == 11) {
            strtof(num_buf, &endptr);
            if (endptr == num_buf + num_len) is_num = true;
        } else {
            val_u = strtoul(num_buf, &endptr, 0);
            if (endptr == num_buf + num_len) {
                is_num = true;
                val_s = (int32_t)val_u;
            }
        }
    } else {
        // Hex формат
        if (t_len > 2 && token[0] == '0' && (token[1] == 'x' || token[1] == 'X')) {
            val_u = strtoul(token + 2, &endptr, 16);
            if (endptr == token + t_len) {
                is_num = true;
                size_t hex_digits = t_len - 2;
                if (hex_digits > 8) hex_digits = 8;
                if (hex_digits <= 2)      tag = 4;
                else if (hex_digits <= 4) tag = 6;
                else if (hex_digits <= 6) tag = 8;
                else                      tag = 9;
            }
        }
        if (!is_num) {
            // Float формат
            bool is_float_fmt = false;
            for (size_t k = 0; k < t_len; k++) {
                if (token[k] == '.' || token[k] == 'e' || token[k] == 'E') {
                    is_float_fmt = true; break;
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
    
    // === 4. ЧИСЛО РАСПОЗНАНО ===
    if (is_num) {
        uint8_t buf[8]; size_t sz = 0; buf[0] = tag;
        switch (tag) {
            case 4:  buf[1] = (uint8_t)val_u; sz = 2; break;
            case 5:  buf[1] = (int8_t)val_s;  sz = 2; break;
            case 6:  *(uint16_t*)&buf[1] = (uint16_t)val_u; sz = 3; break;
            case 7:  *(int16_t*)&buf[1]  = (int16_t)val_s;  sz = 3; break;
            case 8:  buf[1] = (uint8_t)val_u; buf[2] = (uint8_t)(val_u >> 8); buf[3] = (uint8_t)(val_u >> 16); sz = 4; break;
            case 9:  *(uint32_t*)&buf[1] = val_u; sz = 5; break;
            case 10: *(int32_t*)&buf[1]  = val_s; sz = 5; break;
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
                if (c != '[' && c != ']' && c != '=' && c != ')' && c != '(') call_apply = true;
            } else {
                call_apply = true;
            }
        }
        if (call_apply) apply_op(&buf[1], sz - 1, tag, 0); else stack_push(buf, sz); return;
    }
    
    // === 5. ПРИСВАИВАНИЕ ===
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
                    const char* tnames[] = {"u8", "i8", "u16", "i16", "u24", "u32", "i32", "f", "$S", "word"};
                    const uint8_t ttags[] = {4, 5, 6, 7, 8, 9, 10, 11, 15, 18};
                    for (int i = 0; i < 10; i++) {
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
                        // ✅ ХЕЛПЕР decode_uint_le вместо ручного цикла
                        uint32_t v = decode_uint_le(&p[1], elem_size(p) - 1);
                        arr_len = (uint16_t)v;
                        sz_sz = elem_size(p);
                    }
                }
                if (arr_len > 0) {
                    uint8_t el_sz = type_registry[type_tag].size; uint32_t total = (uint32_t)arr_len * el_sz;
                    if (data_ptr + total <= DATA_POOL_SIZE) {
                        uint16_t a = data_ptr; data_ptr += total;
                        if (type_tag == 18) {
                            uint16_t nop_addr = dict_find("nop");
                            if (nop_addr != 0xFFFF) {
                                for (uint16_t i = 0; i < arr_len; i++) {
                                    data_pool[a + i * 2]     = nop_addr & 0xFF;
                                    data_pool[a + i * 2 + 1] = nop_addr >> 8;
                                }
                            } else {
                                memset(&data_pool[a], 0, total);
                            }
                        } else {
                            memset(&data_pool[a], 0, total);
                        }
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
                
                // $STRING присваивание
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
/*
// In-place строка (тег 0x0E в теле слова, без data_pool)
if (val_sz > 0 && (val[0] >= 0x0C && val[0] <= 0x0E)) {
    uint8_t slen = val[1];          // длина у всех текстовых тегов в val[1]
    const uint8_t* src = &val[2];   // байты в val[2..]
    uint16_t body_sz = 2 + slen;    // [тег][len][байты]
    uint16_t ws = 9 + t_len + body_sz;
    if (dict_ptr + ws > DICT_POOL_SIZE) {
        currentOutput->println("dict overflow");
        return;
    }
    uint16_t a = dict_ptr; dict_ptr += ws;
    dict_pool[a + 2] = (uint8_t)t_len;
    memcpy(&dict_pool[a + 3], token, t_len);
    uint16_t p = a + 3 + t_len;
    dict_pool[p]     = currentContext;
    dict_pool[p + 1] = 0x06;        // VAR | INTERNAL
    dict_pool[p + 2] = 0x0E;        // STRING
    dict_pool[p + 3] = slen;
    if (slen > 0) memcpy(&dict_pool[p + 4], src, slen);
    uint32_t fn = (uint32_t)(uintptr_t)choiceFunc;
    memcpy(&dict_pool[p + 2 + body_sz], &fn, 4);
    link_word(a);
    stack_ptr += eq_sz + val_sz;
    return;
} 
*/               
                // Массив/ссылка
                uint8_t* src_header = nullptr;
                if (val_sz == 6 && (val[0] == 17 || val[0] == 20)) {
                    src_header = val;
                } else if (val_sz == 3 && val[0] == 0x12) {
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
                
                // === СКАЛЯРНОЕ ПРИСВАИВАНИЕ С ПРИВЕДЕНИЕМ ТИПОВ ===
                if (val_sz > 0 && val[0] <= 20) {
                    uint8_t final_tag = type_tag; uint8_t data_sz = type_registry[final_tag].size; uint16_t final_sz = 1 + data_sz;
                    uint8_t new_body[8]; new_body[0] = final_tag; int32_t src_i = 0; float src_f = 0.0;
                    
                    if (val[0] == 11) {
                        memcpy(&src_f, &val[1], 4);
                        src_i = (int32_t)src_f;
                    } else {
                        // ✅ ХЕЛПЕР decode_int_le вместо ручного цикла + sign extend
                        src_i = decode_int_le(&val[1], val_sz - 1, val[0]);
                        src_f = (float)src_i;
                    }
                    
                    if (final_tag == 11) {
                        float v = (val[0] == 11 || type_tag != current_type) ? src_f : (float)src_i;
                        memcpy(&new_body[1], &v, 4);
                        final_sz = 5;
                    } else {
                        int32_t v = (val[0] == 11) ? (int32_t)src_f : src_i;
                        // ✅ ХЕЛПЕР encode_uint_le вместо ручного цикла
                        encode_uint_le(&new_body[1], (uint32_t)v, data_sz);
                        final_sz = 1 + data_sz;
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
    
    // === 6. НЕИЗВЕСТНОЕ СЛОВО → NAME ===
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
    currentOutput->println(getMsg("stack empty"));
    return;
  }
  uint8_t tag = stack_mem[stack_ptr];
  if (tag != 0x0C && tag != 0x0D && tag != 0x0E) {
    currentOutput->println(getMsg("invalid type"));
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
    currentOutput->println(getMsg("changed to root"));
    return;
  }
  if (strcmp(txt, "..") == 0) {
    if (strlen(g_currentDir) <= 1) {
      currentOutput->println(getMsg("already at root"));
      return;
    }
    char* last_slash = strrchr(g_currentDir + 1, '/');
    if (last_slash) *last_slash = '\0'; else strcpy(g_currentDir, "/");
    currentOutput->println(getMsg("moved up"));
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
    currentOutput->println(getMsg("directory changed"));
  }
  else {
    currentOutput->println(getMsg("directory not found"));
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
// === СЛОВО-АДРЕС ===
// Создаёт слово, которое при вызове кладёт свой адрес на стек и больше ничего не делает.
static void create_self_address_word(const char* name) {
  uint8_t nlen = strlen(name);
  if (nlen == 0 || nlen > 63) return;
  
  uint8_t body[1] = {0}; // Тело не важно, оно не будет прочитано
  uint8_t name_buf[65] = {0x0D, nlen};
  memcpy(&name_buf[2], name, nlen);
  
  // 0x02 = VAR. 
  // В exec_word: if (flags & 0x02) { stack_push(addr); }
  // if (flags & 0x04) { call func(); } -> НЕ ВЫПОЛНИТСЯ, так как флага 0x04 нет!
  create_internal_word(name_buf, body, 1, 0x02, wordNop);
}
void word_cont() {
    // 1. Захватываем имя контекста из потока токенов (R2L: предыдущий токен в тексте)
    int name_idx = g_tok_idx - 1;
    if (name_idx < 0) {
        currentOutput->println("cont: укажите имя контекста");
        return;
    }
    uint8_t nlen = g_tok_len[name_idx];
    if (nlen == 0 || nlen > 63) {
        currentOutput->println("cont: недопустимая длина имени");
        return;
    }
    char name[64];
    memcpy(name, g_tok_start[name_idx], nlen);
    name[nlen] = '\0';
    
    // 🔑 СПЕЦИАЛЬНЫЙ СЛУЧАЙ: main
    if (strcmp(name, "main") == 0) {
        currentContext = 0;
        g_tok_idx -= 1;
        return;
    }
    
    // 2. Ищем контекст в словаре
    uint16_t addr = dict_find(name);
    if (addr != 0xFFFF) {
        // Контекст уже существует — просто переключаемся
        uint8_t flags = dict_pool[addr + 4 + nlen];
        if (flags == 0x0C) {
            uint16_t body_start = addr + 5 + nlen;
            if (body_start < dict_ptr && dict_pool[body_start] == 0x03) {
                currentContext = dict_pool[body_start + 1];
            }
        }
    } else {
        // 3. Контекста нет — создаём новый
        uint8_t parent_ctx = currentContext;  // ← СОХРАНЯЕМ текущего родителя
        uint8_t body[2] = {0x03, g_next_ctx};
        
        // Временно кладём NAME на стек для create_internal_word
        uint8_t name_buf[65];
        name_buf[0] = 0x0D;
        name_buf[1] = nlen;
        memcpy(&name_buf[2], name, nlen);
        stack_push(name_buf, 2 + nlen);
        create_internal_word(&stack_mem[stack_ptr], body, 2, 0x0C, exec_context_word);
        stack_ptr += 2 + nlen;
        
        // ← ИСПРАВЛЕНИЕ: записываем родителя в поле ctx нового контекста
        if (dict_last != 0xFFFF) {
            uint8_t new_nlen = dict_pool[dict_last + 2];
            dict_pool[dict_last + 3 + new_nlen] = parent_ctx;
        }
        currentContext = g_next_ctx;
        g_next_ctx++;
    }
    
    // 4. Пропускаем токен имени в R2L-цикле (как в cord/body/as)
    g_tok_idx -= 1;
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
    uint32_t v = decode_uint_le(&buf[1], t->size);
    currentOutput->printf("%s(%lu)", name, (unsigned long)v);
    return;
}
case CAT_INT: {
    int32_t v = decode_int_le(&buf[1], t->size, tag);
    currentOutput->printf("%s(%ld)", name, (long)v);
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
  // 1. Захват имени из R2L-потока (предыдущий токен)
  int name_idx = g_tok_idx - 1;
  if (name_idx < 0) {
    currentOutput->println("device: name expected before");
    return;
  }
  uint8_t nlen = g_tok_len[name_idx];
  if (nlen == 0 || nlen > 63) {
    currentOutput->println("device: invalid name");
    return;
  }
  char name[64];
  memcpy(name, g_tok_start[name_idx], nlen);
  name[nlen] = '\0';

  // 2. Проверка: слово не должно уже существовать
  if (dict_find(name) != 0xFFFF) {
    currentOutput->print("device: exists: ");
    currentOutput->println(name);
    g_tok_idx -= 1;
    return;
  }

  // 3. Формируем тело слова: [TAG=6 (UINT16), counter_lo, counter_hi]
  uint8_t body[3] = {6, (uint8_t)(g_device_counter & 0xFF), (uint8_t)(g_device_counter >> 8)};

  // 4. Формируем NAME-буфер для create_internal_word
  uint8_t name_buf[65];
  name_buf[0] = 0x0D;
  name_buf[1] = nlen;
  memcpy(&name_buf[2], name, nlen);

  // 5. Создаём слово в словаре (флаги: VAR | INTERNAL | CONST = 0x07)
  create_internal_word(name_buf, body, 3, 0x07, choiceFunc);

  // 6. Устанавливаем контекст (как было)
  uint16_t ctx_pos = dict_last + 3 + dict_pool[dict_last + 2];
  dict_pool[ctx_pos] = currentContext;

  // 7. Увеличиваем счётчик устройств
  g_device_counter++;

  // 8. 🔑 КЛЮЧЕВОЕ: пропускаем токен имени в R2L-цикле
  g_tok_idx -= 1;
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
    
    // 🔹 Обработка массива/ссылки (тег 17 = ARRAY, тег 20 = REF_ARR)
    if (tag == 17 || tag == 20) {
        uint16_t arr_len = top[3] | (top[4] << 8);  // длина массива
        if (dropOriginal) stack_ptr += sz;
        pushInt32((int32_t)arr_len);
        return;
    }
    
    // 🔹 Обработка строки (тег 14 = STRING)
    if (tag == 14) {
        uint8_t str_len = top[1];
        if (dropOriginal) stack_ptr += sz;
        pushInt32((int32_t)str_len);
        return;
    }
    
    // 🔹 Обработка $STRING (тег 15)
    if (tag == 15) {
        uint8_t str_len = top[1];  // длина строки
        if (dropOriginal) stack_ptr += sz;
        pushInt32((int32_t)str_len);
        return;
    }
    
    // 🔹 Скаляры: возвращаем размер элемента в байтах
    uint8_t elem_sz = type_registry[tag].size;
    if (elem_sz == 0 && tag >= 12 && tag <= 14) elem_sz = top[1];
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
      // 🔧 FIX: добавлена проверка на скобки (как в processToken)
      if (c != '[' && c != ']' && c != '=' && c != ')' && c != '(') 
        call_apply = true;
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
// 🔹 Вспомогательная: печатает "▸ имя_контекста" по его ID (только для режима all)
static void print_ctx_header(uint8_t ctx_id) {
    currentOutput->println();
    if (ctx_id == 0) {
        currentOutput->println("▸ main");
        return;
    }
    uint16_t cp = 0;
    while (cp < dict_ptr) {
        uint16_t cnext = dict_pool[cp] | (dict_pool[cp + 1] << 8);
        uint8_t cnlen  = dict_pool[cp + 2];
        uint8_t cflags = dict_pool[cp + 4 + cnlen];
        if (cflags == 0x0C) {
            uint16_t cbody = cp + 5 + cnlen;
            if (cbody < dict_ptr && dict_pool[cbody] == 0x03) {
                uint8_t stored_ctx = dict_pool[cbody + 1];
                if (stored_ctx == ctx_id) {
                    currentOutput->print("▸ ");
                    currentOutput->write(&dict_pool[cp + 3], cnlen);
                    currentOutput->println();
                    return;
                }
            }
        }
        if (cnext == 0) break;
        cp = cnext;
    }
    currentOutput->printf("▸ ctx_%u\n", ctx_id);
}

// === ЕДИНОЕ ЯДРО: вывод списка слов ===
// filter_by_context = true  → только текущий контекст (слово words)
// filter_by_context = false → все слова системы (слово all)
static void words_core(bool filter_by_context) {
    uint16_t p = 0;
    uint8_t line_pos = 0;
    bool any_printed = false;
    uint8_t last_printed_ctx = 255;

    while (p < dict_ptr) {
        uint16_t next = dict_pool[p] | (dict_pool[p + 1] << 8);
        uint8_t nlen  = dict_pool[p + 2];
        uint8_t ctx   = dict_pool[p + 3 + nlen];
        uint8_t flags = dict_pool[p + 4 + nlen];

        // Фильтр по текущему контексту (для words)
        if (filter_by_context && ctx != currentContext) {
            if (next == 0) break;
            p = next;
            continue;
        }

        // Проверка: это слово является определением контекста?
        bool is_ctx_def = false;
        if (flags == 0x0C) {
            uint16_t body = p + 5 + nlen;
            if (body < dict_ptr && dict_pool[body] == 0x03) is_ctx_def = true;
        }

        // === РЕЖИМ all: заголовок при смене контекста + пропуск самих определений ===
        if (!filter_by_context) {
            if (is_ctx_def) {
                // В режиме all сами определения контекстов не выводим как слова —
                // они уже показаны в заголовке группы.
                if (next == 0) break;
                p = next;
                continue;
            }
            if (ctx != last_printed_ctx) {
                if (line_pos > 0) {
                    currentOutput->println();
                    line_pos = 0;
                }
                print_ctx_header(ctx);
                line_pos = 0;
                last_printed_ctx = ctx;
            }
        }

        // === Печать слова ===
        // Если это определение контекста (в режиме words) — печатаем со значком ▸
        uint8_t prefix_len = is_ctx_def ? 2 : 0;   // "▸ "
        uint8_t word_len = prefix_len + nlen + 1;  // +1 за пробел

        if (line_pos > 0 && line_pos + word_len > 64) {
            currentOutput->println();
            line_pos = 0;
        }

        if (is_ctx_def) currentOutput->print("▸");
        currentOutput->write(&dict_pool[p + 3], nlen);
        currentOutput->print(' ');
        line_pos += word_len;
        any_printed = true;

        if (next == 0) break;
        p = next;
    }
    if (any_printed && line_pos > 0) currentOutput->println();
}
// === ДВЕ ТОНКИЕ ОБЁРТКИ ===
void word_words()     { words_core(true);  }
void word_words_all() { words_core(false); }

void word_cord() {
    // Забираем имя из потока (R2L — предыдущий токен)
    int name_idx = g_tok_idx - 1;
    if (name_idx < 0) {
        currentOutput->println("cord: name expected before 'cord'");
        return;
    }
    
    uint8_t nlen = g_tok_len[name_idx];
    if (nlen == 0 || nlen > 63) {
        currentOutput->println("cord: invalid name");
        return;
    }
    
    char name[64];
    memcpy(name, g_tok_start[name_idx], nlen);
    name[nlen] = '\0';
    
    // Проверяем, не существует ли уже такое слово
    if (dict_find(name) != 0xFFFF) {
        currentOutput->print("cord: exists: ");
        currentOutput->println(name);
        g_tok_idx -= 1;
        return;
    }
    
    // Находим адрес nop
    uint16_t nop_addr = dict_find("nop");
    if (nop_addr == 0xFFFF) {
        currentOutput->println("cord: nop not found");
        g_tok_idx -= 1;
        return;
    }
    
    // Размер записи: next(2) + nlen(1) + name(nlen) + ctx(1) + flags(1) + body(2)
    uint16_t rec_sz = 2 + 1 + nlen + 1 + 1 + 2;
    if (dict_ptr + rec_sz > DICT_POOL_SIZE) {
        currentOutput->println("cord: dict_pool full");
        g_tok_idx -= 1;
        return;
    }
    
    uint16_t a = dict_ptr; dict_ptr += rec_sz;
    
    // Заголовок
    dict_pool[a] = 0;                          // next_lo
    dict_pool[a + 1] = 0;                      // next_hi
    dict_pool[a + 2] = nlen;                   // длина имени
    memcpy(&dict_pool[a + 3], name, nlen);     // имя
    dict_pool[a + 3 + nlen] = currentContext;  // ctx
    dict_pool[a + 4 + nlen] = 0x08;            // flags = COMPILED
    
    // Тело: nop_addr (2 байта)
    uint16_t body = a + 5 + nlen;
    dict_pool[body]     = nop_addr & 0xFF;
    dict_pool[body + 1] = nop_addr >> 8;
    
    link_word(a);
    
    // Пропускаем токен имени в R2L цикле
    g_tok_idx -= 1;
}
void word_dup() {
    if (stack_is_empty()) {
        currentOutput->println("dup: stack empty");
        return;
    }
    uint8_t* top = &stack_mem[stack_ptr];
    uint16_t sz = elem_size(top);
    if (stack_ptr < sz) {
        currentOutput->println("dup: stack overflow");
        return;
    }
    stack_push(top, sz);
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

// === Построение полного пути ===
// Если имя начинается с '/' — это абсолютный путь (используем как есть)
// Иначе — относительный путь (добавляем g_currentDir)
static void build_full_path(char* out, size_t out_size, const char* fname) {
    if (fname[0] == '/') {
        // Абсолютный путь — копируем как есть
        strncpy(out, fname, out_size - 1);
        out[out_size - 1] = '\0';
    } else {
        // Относительный путь — добавляем текущую директорию
        size_t dlen = strlen(g_currentDir);
        if (dlen > 1) {
            snprintf(out, out_size, "%s/%s", g_currentDir, fname);
        } else {
            snprintf(out, out_size, "/%s", fname);
        }
    }
}


void word_type() {
    if (stack_is_empty()) {
        currentOutput->println(getMsg("type: stack empty"));
        return;
    }
    uint8_t* top = &stack_mem[stack_ptr];
    uint8_t tag = top[0];
    char fname[257];
    uint8_t len = 0;
    
    if (tag == 0x0E || tag == 0x0D) {
        len = top[1];
        if (len > 255) len = 255;
        memcpy(fname, &top[2], len);
        fname[len] = '\0';
        stack_ptr += elem_size(top);
    }
    else if (tag == 15) {
        len = top[1];
        uint16_t addr = top[2] | (top[3] << 8);
        if (addr + len <= DATA_POOL_SIZE) {
            if (len > 255) len = 255;
            memcpy(fname, &data_pool[addr], len);
            fname[len] = '\0';
        }
        stack_ptr += elem_size(top);
    }
    else {
        currentOutput->print(getMsg("type: expected STRING/NAME/$STRING, got tag "));
        currentOutput->println(tag);
        return;
    }
    
    char full_path[256];
    build_full_path(full_path, sizeof(full_path), fname);
    
    // 🔑 ПРОВЕРКА СУЩЕСТВОВАНИЯ ПЕРЕД ОТКРЫТИЕМ
    if (!FILESYSTEM.exists(full_path)) {
        currentOutput->print(getMsg("type: file not found "));
        currentOutput->println(full_path);
        return;
    }
    
    File f = FILESYSTEM.open(full_path, "r");
    if (!f) {
        currentOutput->print(getMsg("type: cannot open "));
        currentOutput->println(full_path);
        return;
    }
    
    uint8_t buf[64];
    size_t n;
    while ((n = f.read(buf, sizeof(buf))) > 0) {
        currentOutput->write(buf, n);
        process_tasks(true);
    }
    f.close();
}
// === ОБЩАЯ ФУНКЦИЯ ЗАГРУЗКИ ФАЙЛА ===
// Возвращает true если файл успешно загружен, false при ошибке.
// Все сообщения об ошибках печатает сама.
static bool execute_file(const char* full_path) {
    File f = FILESYSTEM.open(full_path, "r");
    if (!f || f.isDirectory()) {
        currentOutput->print(getMsg("load error: file not found or is directory: "));
        currentOutput->println(full_path);
        if (f) f.close();
        return false;
    }
    
    String line;
    while (f.available()) {
        line = f.readStringUntil('\n');
        if (line.length() == 0) continue;
        if (line.endsWith("\r")) line.remove(line.length() - 1);
        process_tasks(true);
        executeLine(line.c_str());
    }
    f.close();
    return true;
}
void word_load() {
    if (stack_is_empty()) {
        currentOutput->println(getMsg("load error: stack empty"));
        return;
    }
    uint8_t* top = &stack_mem[stack_ptr];
    if (top[0] != 0x0D && top[0] != 0x0E) {
        currentOutput->println(getMsg("load error: expected filename"));
        return;
    }
    uint16_t sz = elem_size(top);
    uint8_t len = top[1];
    if (len > 255) len = 255;
    
    char fname[257];
    memcpy(fname, &top[2], len);
    fname[len] = '\0';
    stack_ptr += sz;
    
    char full_path[256];
    build_full_path(full_path, sizeof(full_path), fname);

    // 🔑 Сохраняем состояние памяти ДО загрузки
    uint16_t dict_before = dict_ptr;
    uint16_t data_before = data_ptr;

    execute_file(full_path);

    // 🔑 Выводим статистику использованной памяти С ИМЕНЕМ ФАЙЛА
    uint16_t dict_used = dict_ptr - dict_before;
    uint16_t data_used = data_ptr - data_before;

    currentOutput->printf("load %s: dict +%u (free: %u/%u)\n", fname, dict_used, DICT_POOL_SIZE - dict_ptr, DICT_POOL_SIZE);
    currentOutput->printf("load %s: data +%u (free: %u/%u)\n", fname, data_used, DATA_POOL_SIZE - data_ptr, DATA_POOL_SIZE);
}
void word_load_query() {
    // 1. БЕРЕМ ИМЯ ФАЙЛА СО СТЕКА (оно там, так как в R2L строка читается первой)
    if (stack_is_empty()) {
        currentOutput->println(getMsg("load?: filename expected"));
        return;
    }
    uint8_t* top = &stack_mem[stack_ptr];
    if (top[0] != 0x0D && top[0] != 0x0E) {
        currentOutput->println(getMsg("load?: filename must be string/name"));
        return;
    }
    uint16_t sz = elem_size(top);
    uint8_t fn_len = top[1];
    if (fn_len > 255) fn_len = 255;
    
    char fname[257];
    memcpy(fname, &top[2], fn_len);
    fname[fn_len] = '\0';
    stack_ptr += sz; // 🔑 Снимаем имя файла со стека
    
    // 2. СМОТРИМ СЛЕДУЮЩИЙ ТОКЕН в R2L-потоке (это имя слова, например "webconst")
    int idx = g_tok_idx - 1;
    if (idx < 0) {
        currentOutput->println(getMsg("load?: word name expected"));
        return;
    }
    uint8_t len = g_tok_len[idx];
    if (len == 0 || len > 63) {
        currentOutput->println(getMsg("load?: invalid word name"));
        return;
    }
    char word_name[64];
    memcpy(word_name, g_tok_start[idx], len);
    word_name[len] = '\0';
    
    // 3. ПРОВЕРЯЕМ, есть ли слово в словаре
    if (dict_find(word_name) != 0xFFFF) {
        // Слово УЖЕ есть. Ничего не загружаем.
        // 🔑 КЛЮЧЕВОЕ: пропускаем этот токен, чтобы он не исполнился как отдельная команда!
        g_tok_idx -= 1;
        return;
    }
    
    // 4. Слова НЕТ. Загружаем файл.
    char full_path[256];
    build_full_path(full_path, sizeof(full_path), fname);
    execute_file(full_path);
    
    // 🔑 КЛЮЧЕВОЕ: даже после загрузки мы должны пропустить токен имени, 
    // так как он был аргументом для load?, а не самостоятельной командой.
    g_tok_idx -= 1;
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
    currentOutput->print(getMsg("save error: "));
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
  currentOutput->println(getMsg("saved"));
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
    currentOutput->print(getMsg("restore error: "));
    currentOutput->println(full_path);
    return;
  }
  // 3. Читаем заголовок
  uint8_t hdr[16];
  if (f.read(hdr, 16) != 16) {
    f.close();
    currentOutput->println(getMsg("restore error: invalid header"));
    return;
  }
  // 4. Проверка магии "HDLS"
  if (hdr[0] != 0x48 || hdr[1] != 0x44 || hdr[2] != 0x4C || hdr[3] != 0x53) {
    f.close(); currentOutput->println(getMsg("restore error: bad magic")); return;
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
    f.close(); currentOutput->println(getMsg("restore error: pointers out of range")); return;
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
  currentOutput->println(getMsg("restored"));
}

void compile_token(const char* token) {
size_t t_len = strlen(token);
if (t_len == 0) return;

// === ПЕРЕХВАТ # (адрес слова) ===
if (strcmp(token, "#") == 0) {
    char wname[64];
    uint8_t ln = grab_token(1, wname, 63);
    if (ln == 0) {
        currentOutput->println("#: expected word before");
        return;
    }
    uint16_t addr = dict_find(wname);
    if (addr == 0xFFFF) {
        currentOutput->print("#: word not found: ");
        currentOutput->println(wname);
        return;
    }
    uint16_t hash_addr = dict_find("#");
    if (hash_addr != 0xFFFF) {
        dict_pool[dict_ptr++] = hash_addr & 0xFF;
        dict_pool[dict_ptr++] = hash_addr >> 8;
    }
    dict_pool[dict_ptr++] = addr & 0xFF;
    dict_pool[dict_ptr++] = addr >> 8;
    g_tok_idx -= 1;
    return;
}

// === ИСПРАВЛЕННЫЙ ПЕРЕХВАТ +task ===
if (strcmp(token, "+task") == 0) {
    // В R2L порядке токены идут так: "имя_задачи" "интервал" "+task"
    // g_tok_idx указывает на "+task". 
    // Заглядываем на 1 и 2 токена назад.
    char str_interval[32], str_word[64];
    uint8_t len_int = grab_token(1, str_interval, 31);
    uint8_t len_word = grab_token(2, str_word, 63);
    
    if (len_int == 0 || len_word == 0) {
        currentOutput->println("+task: missing name or interval");
        return;
    }
    
    // 1. Парсим интервал
    char* endptr;
    uint32_t interval = (uint32_t)strtoul(str_interval, &endptr, 10);
    if (endptr == str_interval || *endptr != '\0') {
        currentOutput->print("+task: invalid interval: ");
        currentOutput->println(str_interval);
        return;
    }
    
    // 2. Ищем адрес слова
    uint16_t addr = dict_find(str_word);
    if (addr == 0xFFFF) {
        currentOutput->print("+task: word not found: ");
        currentOutput->println(str_word);
        return;
    }
    
    // 3. Компилируем литерал интервала (u32, тег 9)
    dict_pool[dict_ptr++] = 0x00; dict_pool[dict_ptr++] = 0x00;
    dict_pool[dict_ptr++] = 9;
    memcpy(&dict_pool[dict_ptr], &interval, 4);
    dict_ptr += 4;
    
    // 4. Компилируем литерал адреса слова (u16, тег 6)
    dict_pool[dict_ptr++] = 0x00; dict_pool[dict_ptr++] = 0x00;
    dict_pool[dict_ptr++] = 6;
    dict_pool[dict_ptr++] = addr & 0xFF;
    dict_pool[dict_ptr++] = addr >> 8;
    
    // 5. Компилируем ВЫЗОВ функции планировщика (ОБЯЗАТЕЛЬНО!)
    uint16_t sa = dict_find("__schedule_task__");
    if (sa != 0xFFFF) {
        dict_pool[dict_ptr++] = sa & 0xFF;
        dict_pool[dict_ptr++] = sa >> 8;
    } else {
        currentOutput->println("+task CRITICAL: schedule_task not found in dict!");
    }
    
    // Пропускаем оба обработанных токена в R2L-цикле
    g_tok_idx -= 2;
    return;
}
// === 1. ЗАВЕРШЕНИЕ КОМПИЛЯЦИИ ===
if (token[0] == ';' && t_len == 1) {
    if (g_compile_mode) {
        link_word(g_compile_header);
        g_compile_mode = false;
        g_compile_header = 0xFFFF;
    }
    return;
}

// === 2. ПЕРВЫЙ ТОКЕН ПОСЛЕ ':' — ЭТО ИМЯ СЛОВА ===
if (g_compile_mode && g_compile_header != 0xFFFF && dict_pool[g_compile_header + 2] == 0) {
    if (dict_find(token) != 0xFFFF) {
        currentOutput->print("error: word '");
        currentOutput->print(token);
        currentOutput->println("' already exists");
        dict_ptr = g_compile_header;
        g_compile_mode = false;
        g_compile_header = 0xFFFF;
        for (int i = g_tok_idx - 1; i >= 0; i--) {
            if (g_tok_len[i] == 1 && g_tok_start[i][0] == ';') {
                g_tok_idx = i;
                break;
            }
        }
        return;
    }
    uint8_t nlen = t_len;
    if (nlen == 0 || nlen > 63) {
        currentOutput->println("error: invalid word name");
        dict_ptr = g_compile_header;
        g_compile_mode = false;
        g_compile_header = 0xFFFF;
        return;
    }
    dict_pool[g_compile_header + 2] = nlen;
    memcpy(&dict_pool[g_compile_header + 3], token, nlen);
    dict_pool[g_compile_header + 3 + nlen] = currentContext;
    dict_pool[g_compile_header + 4 + nlen] = 0x08;
    dict_ptr = g_compile_header + 5 + nlen;
    g_local_map_count = 0;
    return;
}

// === БЛОК { ===
if (strcmp(token, "{") == 0) {
    if (blk_depth < 8) {
        blk_stack[blk_depth].start    = dict_ptr;
        blk_stack[blk_depth].type     = 0;
        blk_stack[blk_depth].skip_pos = 0;
    }
    blk_depth++;
    return;
}

// === БЛОК while ===
if (strcmp(token, "while") == 0) {
    uint16_t addr = dict_find(token);
    if (addr != 0xFFFF) {
        dict_pool[dict_ptr++] = addr & 0xFF;
        dict_pool[dict_ptr++] = addr >> 8;
    }
    if (blk_depth > 0) {
        blk_stack[blk_depth - 1].type     = 0;
        blk_stack[blk_depth - 1].skip_pos = dict_ptr;
    }
    dict_pool[dict_ptr++] = 0;
    dict_pool[dict_ptr++] = 0;
    return;
}

// === БЛОК if ===
if (strcmp(token, "if") == 0) {
    uint16_t addr = dict_find("if");
    if (addr != 0xFFFF) {
        dict_pool[dict_ptr++] = addr & 0xFF;
        dict_pool[dict_ptr++] = addr >> 8;
    }
    if (blk_depth > 0) {
        blk_stack[blk_depth - 1].type     = 1;
        blk_stack[blk_depth - 1].skip_pos = dict_ptr;
    }
    dict_pool[dict_ptr++] = 0;
    dict_pool[dict_ptr++] = 0;
    return;
}

// === БЛОК } ===
if (strcmp(token, "}") == 0) {
    if (blk_depth == 0) return;
    blk_depth--;
    uint16_t end_pos = dict_ptr;
    BlockEntry* b = &blk_stack[blk_depth];
    if (b->type == 1) {
        // IF: патчим адрес пропуска
        dict_pool[b->skip_pos]     = end_pos & 0xFF;
        dict_pool[b->skip_pos + 1] = end_pos >> 8;
    } else {
        // WHILE: компилируем goto на начало условия + патчим адрес пропуска
        uint16_t addr_goto = dict_find("goto");
        if (addr_goto != 0xFFFF) {
            dict_pool[dict_ptr++] = addr_goto & 0xFF;
            dict_pool[dict_ptr++] = addr_goto >> 8;
        }
        dict_pool[dict_ptr++] = b->start & 0xFF;
        dict_pool[dict_ptr++] = b->start >> 8;
        uint16_t skip_target = dict_ptr;
        dict_pool[b->skip_pos]     = skip_target & 0xFF;
        dict_pool[b->skip_pos + 1] = skip_target >> 8;
    }
    return;
}

// === 3. СТРОКА "..." ===
if (token[0] == '"' && token[t_len - 1] == '"' && t_len >= 2) {
    size_t l = t_len - 2; if (l > 255) l = 255;
    dict_pool[dict_ptr++] = 0x00; dict_pool[dict_ptr++] = 0x00;
    dict_pool[dict_ptr++] = 0x0E;
    dict_pool[dict_ptr++] = (uint8_t)l;
    for (size_t i = 0; i < l; i++) dict_pool[dict_ptr++] = token[1 + i];
    return;
}

// === 4. ПРОВЕРКА ПРИСВАИВАНИЯ ===
bool is_assign_attempt = false;
if (g_tok_idx + 1 < g_tok_count) {
    uint8_t prev_len = g_tok_len[g_tok_idx + 1];
    if (prev_len == 1 && g_tok_start[g_tok_idx + 1][0] == '=') {
        is_assign_attempt = true;
    }
}
// === ПЕРЕХВАТ name ===
if (strcmp(token, "name") == 0) {
    int prev_idx = g_tok_idx - 1;
    if (prev_idx < 0) {
        currentOutput->println("name: no word before");
        return;
    }
    uint8_t plen = g_tok_len[prev_idx];
    if (plen == 0 || plen > 63) {
        currentOutput->println("name: invalid name");
        return;
    }
    // Извлекаем имя предыдущего токена
    char pname[64];
    memcpy(pname, g_tok_start[prev_idx], plen);
    pname[plen] = '\0';
    // Проверяем, что это слово в словаре
    uint16_t paddr = dict_find(pname);
    if (paddr == 0xFFFF) {
        currentOutput->print("name: not found: ");
        currentOutput->println(pname);
        return;
    }
    // Компилируем STRING-литерал [0x00 0x00] [0x0E] [len] [bytes...]
    dict_pool[dict_ptr++] = 0x00; dict_pool[dict_ptr++] = 0x00;
    dict_pool[dict_ptr++] = 0x0E;
    dict_pool[dict_ptr++] = plen;
    for (uint8_t i = 0; i < plen; i++) dict_pool[dict_ptr++] = pname[i];
    // Пропускаем токен-источник в R2L-цикле
    g_tok_idx -= 1;
    return;
}
// === 5. ПОИСК В СЛОВАРЕ ===
uint16_t addr = dict_find(token);

// Если присваивание и НЕ найдено — создаём локальную
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

// Если слово найдено — компилируем его адрес
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

// === 6. ЧИСЛО → ЛИТЕРАЛ ===
uint8_t tag = 0;
uint32_t val_u = 0;
float val_f = 0.0f;
bool is_num = false;
char* endptr;
const char* suffixes[]  = {"u8", "i8", "u16", "i16", "u24", "u32", "i32", "f"};
const uint8_t suf_tags[] = {4, 5, 6, 7, 8, 9, 10, 11};
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
        bool has_dot = false;
        for (size_t k = 0; k < num_len; k++) {
            if (num_buf[k] == '.' || num_buf[k] == 'e' || num_buf[k] == 'E') {
                has_dot = true; break;
            }
        }
        if (has_dot) tag = 11;
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
            size_t hex_digits = t_len - 2;
            if (hex_digits > 8) hex_digits = 8;
            if (hex_digits <= 2)      tag = 4;
            else if (hex_digits <= 4) tag = 6;
            else if (hex_digits <= 6) tag = 8;
            else                      tag = 9;
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

// === 7. ИЗВЕСТНАЯ ЛОКАЛЬНАЯ ПЕРЕМЕННАЯ (чтение) ===
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

// === 8. НЕИЗВЕСТНОЕ СЛОВО ===
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
    stack_ptr += elem_size(dst_ptr);
    
    // 2. Начало
    if (stack_is_empty()) {
        currentOutput->printf("%s: ожидается начало\n", name);
        return;
    }
    uint8_t* start_ptr = &stack_mem[stack_ptr];
    if (!is_numeric_tag(start_ptr[0])) {
        currentOutput->printf("%s: начало должно быть числом\n", name);
        return;
    }
    uint32_t start = decode_uint_le(&start_ptr[1], elem_size(start_ptr) - 1);
    stack_ptr += elem_size(start_ptr);
    
    // 3. Конец (ЗАКРЫТЫЙ диапазон — end включается!)
    if (stack_is_empty()) {
        currentOutput->printf("%s: ожидается конец\n", name);
        return;
    }
    uint8_t* end_ptr = &stack_mem[stack_ptr];
    if (!is_numeric_tag(end_ptr[0])) {
        currentOutput->printf("%s: конец должен быть числом\n", name);
        return;
    }
    uint32_t end = decode_uint_le(&end_ptr[1], elem_size(end_ptr) - 1);
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
    else if (is_numeric_tag(src_ptr[0])) {
        uint32_t val = decode_uint_le(&src_ptr[1], elem_size(src_ptr) - 1);
        src_esz = type_registry[src_ptr[0]].size;
        encode_uint_le(temp_buf, val, src_esz);
        src_len = 1;
        use_temp = true;
        stack_ptr += elem_size(src_ptr);
    }
    else {
        currentOutput->printf("%s: источник должен быть массивом или числом\n", name);
        return;
    }
    
    // 5. Границы (ЗАКРЫТЫЙ диапазон)
    if (end >= dst_len) end = dst_len - 1;  // end — индекс последнего элемента
    if (end < start) {
        currentOutput->printf("%s: конец должен быть >= начала\n", name);
        return;
    }
    uint32_t count = end - start + 1;  // ✅ +1 для закрытого диапазона
    uint16_t dst_addr = dst_base + start * dst_esz;
    
    // 6. ЗАПИСЬ
    if (convert_mode == 1) {
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
  // Просто включаем режим компиляции
  if (dict_ptr + 16 > DICT_POOL_SIZE) {
    currentOutput->println("error: dict_pool full");
    return;
  }
  
  g_compile_header = dict_ptr;
  // Резервируем место под заголовок (заполним, когда придёт имя)
  dict_pool[dict_ptr] = 0; 
  dict_pool[dict_ptr + 1] = 0;
  dict_pool[dict_ptr + 2] = 0; // nlen = 0 (заполним позже)
  dict_ptr += 3;
  
  g_compile_mode = true;
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
 int32_t L = decode_int_le(&l_ptr[1], l_sz - 1, l_ptr[0]);

    // Декодирование R
 int32_t R = decode_int_le(&r_ptr[1], r_sz - 1, r_ptr[0]);

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
    
    // 🔹 BOOL (теги 0 и 1)
    if (tag == 0 || tag == 1) {
        uint8_t res = (tag == 0) ? 1 : 0;
        stack_ptr += 1;
        uint8_t out[2] = {4, res};  // Возвращаем как u8
        stack_push(out, 2);
        return;
    }
    
    // 🔹 Числовые типы (u8..f, теги 4..11)
    if (tag < 4 || tag > 11) return;
    
    uint16_t sz = elem_size(top);
    bool is_zero = true;
    for (uint16_t i = 1; i < sz; i++) {
        if (top[i] != 0) {
            is_zero = false;
            break;
        }
    }
    stack_ptr += sz;
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
// === ЕДИНЫЙ ПЛАНИРОВЩИК ЗАДАЧ ===
// only_continuous = true  -> исполняет ТОЛЬКО +loop (для yield в длинных операциях)
// only_continuous = false -> исполняет ВСЕ задачи (+loop и +task) (для главного loop)
void process_tasks(bool only_continuous) {
    if (g_task_count > 0 && Serial.available() == 0) {
        uint32_t now = millis();
        for (uint8_t i = 0; i < g_task_count; i++) {
            if (g_tasks[i].interval == 0) {
                // 🔁 Непрерывный цикл: исполняем всегда
                uint16_t sp_save = stack_ptr;
                exec_word(g_tasks[i].addr);
                stack_ptr = sp_save;
            }
            else if (!only_continuous && (now - g_tasks[i].last >= g_tasks[i].interval)) {
                // ⏱ Таймерная задача: исполняем ТОЛЬКО если флаг false
                g_tasks[i].last = now;
                uint16_t sp_save = stack_ptr;
                exec_word(g_tasks[i].addr);
                stack_ptr = sp_save;
            }
        }
    }
}

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
    uint32_t us;
    if (!popUInt32(us)) return;
    ::delayMicroseconds(us);
}


// 🔹 ЕДИНОЕ ЯДРО ЭКСПОРТА JSON
// delta_mode = true  → только dirty-переменные (json>>)
// recursive  = true  → рекурсивный обход дочерних контекстов (json*>)
static void json_export_core(bool delta_mode, bool recursive) {
    currentOutput->print('{');
    bool first = true;

    // === ШАГ 1: Печатаем переменные ТЕКУЩЕГО контекста ===
    uint16_t p = 0;
    while (p < dict_ptr) {
        uint16_t next = dict_pool[p] | (dict_pool[p + 1] << 8);
        uint8_t nlen  = dict_pool[p + 2];
        uint8_t ctx   = dict_pool[p + 3 + nlen];
        uint8_t flags = dict_pool[p + 4 + nlen];

        // Фильтр: только наш контекст, только переменные
        if (ctx != currentContext || (flags & 0x10)) {
            if (next == 0) break;
            p = next;
            continue;
        }

        bool is_var   = (flags & 0x02) != 0;
        bool is_alias = (flags & 0x20) != 0;
        bool is_chain = (flags & 0x40) != 0;
        bool is_dirty = (flags & 0x80) != 0;
        bool is_ctx   = (flags == 0x0C);  // ← это определение контекста, пропускаем

        if (!is_var || is_alias || is_chain || is_ctx || (delta_mode && !is_dirty)) {
            if (next == 0) break;
            p = next;
            continue;
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

        if (!first) currentOutput->print(',');
        first = false;

        // Имя переменной
        currentOutput->print('"');
        for (uint8_t i = 0; i < nlen; i++) currentOutput->write(dict_pool[p + 3 + i]);
        currentOutput->print("\":");

        // === ВЫВОД ЗНАЧЕНИЯ ===
        if (tag == 17 || tag == 20) {
            uint16_t base = dict_pool[data_ptr_addr + 1] | (dict_pool[data_ptr_addr + 2] << 8);
            uint16_t len  = dict_pool[data_ptr_addr + 3] | (dict_pool[data_ptr_addr + 4] << 8);
            uint8_t  el_tp = dict_pool[data_ptr_addr + 5];
            uint8_t esz = type_registry[el_tp].size;
            currentOutput->print('[');
            for (uint16_t i = 0; i < len; i++) {
                if (i > 0) currentOutput->print(',');
                uint16_t addr = base + i * esz;
                if (addr + esz > DATA_POOL_SIZE) { currentOutput->print("null"); continue; }
                if (el_tp == 15) {
                    uint8_t slen = data_pool[addr];
                    uint16_t d_addr = data_pool[addr + 1] | (data_pool[addr + 2] << 8);
                    if (slen == 0 || d_addr + slen > DATA_POOL_SIZE) {
                        currentOutput->print("null");
                    } else {
                        currentOutput->print('"');
                        for (uint8_t j = 0; j < slen; j++) {
                            char c = data_pool[d_addr + j];
                            if (c == '"' || c == '\\') currentOutput->print('\\');
                            currentOutput->write(c);
                        }
                        currentOutput->print('"');
                    }
                } else if (el_tp == 11) {
                    float f; memcpy(&f, &data_pool[addr], 4);
                    currentOutput->printf("%.4g", f);
                } else {
                    int32_t val = decode_int_le(&data_pool[addr], esz, el_tp);
                    if (is_signed_tag(el_tp)) currentOutput->printf("%ld", (long)val);
                    else currentOutput->printf("%lu", (unsigned long)(uint32_t)val);
                }
            }
            currentOutput->print(']');
        }
        else if (tag == 0 || tag == 1) {
            currentOutput->print(tag == 1 ? "true" : "false");
        }
        else if (tag >= 4 && tag <= 11) {
            if (tag == 11) {
                float f; memcpy(&f, &dict_pool[data_ptr_addr + 1], 4);
                currentOutput->printf("%.4g", f);
            } else {
                int32_t val = decode_int_le(&dict_pool[data_ptr_addr + 1], val_size - 1, tag);
                if (is_signed_tag(tag)) currentOutput->printf("%ld", (long)val);
                else currentOutput->printf("%lu", (unsigned long)(uint32_t)val);
            }
        }
        else if (tag == 0x0E || tag == 15) {
            uint16_t slen; const uint8_t* src;
            if (tag == 0x0E) {
                slen = dict_pool[data_ptr_addr + 1];
                src = &dict_pool[data_ptr_addr + 2];
            } else {
                slen = dict_pool[data_ptr_addr + 1];
                uint16_t a = dict_pool[data_ptr_addr + 2] | (dict_pool[data_ptr_addr + 3] << 8);
                src = &data_pool[a];
            }
            currentOutput->print('"');
            for (uint16_t i = 0; i < slen; i++) {
                char c = src[i];
                if (c == '"' || c == '\\') currentOutput->print('\\');
                currentOutput->write(c);
            }
            currentOutput->print('"');
        }
        else {
            currentOutput->print("null");
        }

        // В delta-режиме сбрасываем флаг dirty
        if (delta_mode) dict_pool[p + 4 + nlen] &= ~0x80;

        if (next == 0) break;
        p = next;
    }

    // === ШАГ 2: РЕКУРСИВНЫЙ ОБХОД ДОЧЕРНИХ КОНТЕКСТОВ ===
    if (recursive) {
        p = 0;
        while (p < dict_ptr) {
            uint16_t next = dict_pool[p] | (dict_pool[p + 1] << 8);
            uint8_t nlen  = dict_pool[p + 2];
            uint8_t ctx   = dict_pool[p + 3 + nlen];   // ← ПОЛЕ РОДИТЕЛЯ
            uint8_t flags = dict_pool[p + 4 + nlen];

            // Нашли определение контекста, чей родитель = текущий контекст
            if (flags == 0x0C && ctx == currentContext) {
                uint16_t body_start = p + 5 + nlen;
                if (body_start + 2 <= dict_ptr && dict_pool[body_start] == 0x03) {
                    uint8_t child_ctx_id = dict_pool[body_start + 1];

                    if (!first) currentOutput->print(',');
                    first = false;

                    // Имя дочернего контекста как ключ JSON
                    currentOutput->print('"');
                    for (uint8_t i = 0; i < nlen; i++) currentOutput->write(dict_pool[p + 3 + i]);
                    currentOutput->print("\":");

                    // 🔑 РЕКУРСИВНЫЙ ВЫЗОВ с временным переключением контекста
                    uint8_t saved_ctx = currentContext;
                    currentContext = child_ctx_id;
                    json_export_core(delta_mode, true);
                    currentContext = saved_ctx;
                }
            }

            if (next == 0) break;
            p = next;
        }
    }

    currentOutput->print('}');
}

// 🔹 ТОНКИЕ ОБЁРТКИ (интерфейсы намерения)
void word_json_export()       { json_export_core(false, false); }  // json>
void word_json_delta()        { json_export_core(true,  false); }  // json>>
void word_json_star_export()  { json_export_core(false, true);  }  // json*>
inline void mark_dirty(uint16_t var_addr) {
  if (var_addr < dict_ptr) {
    dict_pool[var_addr + 4 + dict_pool[var_addr + 2]] |= 0x80;
  }
}
void word_view() {
// 1. Захватываем предыдущий токен из потока (имя исходного массива)
int idx = g_tok_idx - 1;
if (idx < 0) {
currentOutput->println(getMsg("view: no source array before"));
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
currentOutput->print(getMsg("view: not found: ") );
currentOutput->println(src_name);
return;
}
// 4. Проверяем, что исходное слово — массив или ссылка
uint8_t src_nlen = dict_pool[src_addr + 2];
uint16_t src_body = src_addr + 5 + src_nlen;
uint8_t src_tag = dict_pool[src_body];
if (src_tag != 17 && src_tag != 20) {
currentOutput->println(getMsg("view: source is not an array"));
return;
}
// 5. Снимаем со стека: NAME нового имени, тип
// (в R2L-потоке они были прочитаны раньше, поэтому на стеке в обратном порядке)
// 5.1. Снимаем NAME нового имени (сверху стека)
if (stack_is_empty()) {
currentOutput->println(getMsg("view: new name expected"));
return;
}
uint8_t* name_ptr = &stack_mem[stack_ptr];
if (name_ptr[0] != 0x0D) {
currentOutput->println(getMsg("view: NAME expected"));
return;
}
uint8_t new_nlen = name_ptr[1];
if (new_nlen == 0 || new_nlen > 63) {
currentOutput->println(getMsg("view: invalid name length"));
return;
}
char new_name[64];
memcpy(new_name, &name_ptr[2], new_nlen);
new_name[new_nlen] = '\0';
stack_ptr += elem_size(name_ptr);
// 5.2. Снимаем тип (маркер или число)
if (stack_is_empty()) {
currentOutput->println(getMsg("view: type expected"));
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
currentOutput->println(getMsg("view: invalid type"));
return;
}
stack_ptr += elem_size(type_ptr);
// 🔑 6. ВЫЧИСЛЯЕМ ДЛИНУ АВТОМАТИЧЕСКИ
uint8_t old_type = dict_pool[src_body + 5];
uint16_t old_len = dict_pool[src_body + 3] | (dict_pool[src_body + 4] << 8);
uint32_t old_bytes = (uint32_t)old_len * type_registry[old_type].size;
uint32_t new_type_size = type_registry[new_type].size;
// Вычисляем новую длину целочисленным делением
uint32_t new_len = old_bytes / new_type_size;
// Проверяем, что буфер делится без остатка (опциональное предупреждение)
if (old_bytes % new_type_size != 0) {
currentOutput->println(getMsg("view: warning - buffer not evenly divisible"));
}
// 7. Проверяем, не существует ли уже слово с таким именем
uint16_t existing = dict_find(new_name);
if (existing != 0xFFFF) {
uint8_t ex_nlen = dict_pool[existing + 2];
uint8_t ex_flags = dict_pool[existing + 4 + ex_nlen];
if (ex_flags != FLAG_ALIAS) {
currentOutput->println(getMsg("view: name exists"));
return;
}
}
// 8. Создаём ссылку на массив (REF_ARR, тег 20)
uint16_t ws = 9 + new_nlen + 6;
if (dict_ptr + ws > DICT_POOL_SIZE) {
currentOutput->println(getMsg("view: dict overflow"));
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


    // 4. Обычное слово — печатаем тело как раньше
    print_word_body(addr);

    // 5. Пропускаем токен в R2L-цикле
    g_tok_idx -= 1;
}
static File g_outFile;
// === ПЕРЕНАПРАВЛЕНИЕ ВЫВОДА В ФАЙЛ ===
// HDL-запись:  out>file "filename"
// Переключает весь последующий вывод (print, stack, json> и т.д.) в указанный файл.
//
// 🔑 КЛЮЧЕВЫЕ ПРАВИЛА:
//   1. Закрываем предыдущий файл (если был открыт) ПЕРЕД открытием нового.
//   2. При ошибке открытия НЕ сбрасываем currentOutput на Serial —
//      это ломает WS/HTTP/TCP-каналы. Сообщаем об ошибке в ТЕКУЩИЙ канал.
//   3. Используем build_full_path — единая логика путей (абсолютные/относительные).
//   4. Используем getMsg — поддержка локализации.
//
void word_out_file() {
    // === 1. ПРОВЕРКА СТЕКА ===
    if (stack_is_empty()) {
        currentOutput->println(getMsg("out>file: filename expected"));
        return;
    }

    uint8_t* top = &stack_mem[stack_ptr];
    uint8_t tag = top[0];

    // Поддерживаем STRING (0x0E), NAME (0x0D) и $STRING (15)
    char fname[257];
    uint8_t len = 0;

    if (tag == 0x0E || tag == 0x0D) {
        len = top[1];
        if (len > 255) len = 255;
        memcpy(fname, &top[2], len);
        fname[len] = '\0';
    }
    else if (tag == 15) {
        len = top[1];
        uint16_t addr = top[2] | (top[3] << 8);
        if (addr + len <= DATA_POOL_SIZE) {
            if (len > 255) len = 255;
            memcpy(fname, &data_pool[addr], len);
            fname[len] = '\0';
        } else {
            currentOutput->println(getMsg("out>file: $STRING out of data_pool"));
            return;
        }
    }
    else {
        currentOutput->println(getMsg("out>file: NAME/STRING/$STRING expected"));
        return;
    }

    // Снимаем имя со стека
    stack_ptr += elem_size(top);

    // === 2. СБОРКА ПОЛНОГО ПУТИ ===
    char full_path[256];
    build_full_path(full_path, sizeof(full_path), fname);

    // === 3. ЗАКРЫТИЕ ПРЕДЫДУЩЕГО ФАЙЛА (если был открыт) ===
    if (g_outFile) {
        g_outFile.flush();   // гарантируем запись буфера на диск
        g_outFile.close();
    }

    // === 4. ОТКРЫТИЕ НОВОГО ФАЙЛА ===
    g_outFile = FILESYSTEM.open(full_path, "w");

    if (g_outFile) {
        // ✅ Успех — переключаем вывод в файл
        currentOutput = &g_outFile;
        // Сообщение об успехе уже пойдёт в файл (это нормально — пользователь сам открыл файл)
    }
    else {
        // 🔑 КРИТИЧНО: НЕ сбрасываем currentOutput на Serial!
        // Если сейчас активен WS/HTTP/TCP — они должны остаться активными.
        // Сообщаем об ошибке в ТЕКУЩИЙ канал.
        currentOutput->print(getMsg("out>file: FAILED -> "));
        currentOutput->println(full_path);
    }
}
void word_out_serial() {
    if (g_outFile) {
        g_outFile.flush();
        g_outFile.close();
    }
    if (currentOutput == &g_stream) {
        g_stream.flush();
        g_stream.detach(true); // ← ДОБАВИТЬ true: пользователь явно хочет Serial
    }
    currentOutput = &Serial;
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
    currentOutput->println(getMsg("json>file: filename expected"));
    return;
  }
  uint8_t* top = &stack_mem[stack_ptr];
  if (top[0] != 0x0D && top[0] != 0x0E) {
    currentOutput->println(getMsg("json>file: NAME or STRING expected"));
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
    currentOutput->print(getMsg("json>file: cannot open "));
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

  currentOutput->print(getMsg("json>file: saved to ") );
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
blk_depth = 0;
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
blk_depth = 0;
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
    
    g_tok_idx -= 2; // Пропускаем оба токена
    
    // 2. Ищем ACTION и CHAIN в словаре
    uint16_t action_addr = dict_find(action_name);
    if (action_addr == 0xFFFF) {
        currentOutput->print("bead: action '"); currentOutput->print(action_name); currentOutput->println("' not found");
        return;
    }
    
    uint16_t chain_addr = dict_find(chain_name);
    if (chain_addr == 0xFFFF) {
        currentOutput->print("bead: chain '"); currentOutput->print(chain_name); currentOutput->println("' not found");
        return;
    }
    
    // 3. Проверяем, что chain — скомпилированное слово (флаг 0x08)
    uint8_t c_nlen  = dict_pool[chain_addr + 2];
    uint8_t c_flags = dict_pool[chain_addr + 4 + c_nlen];
    if (!(c_flags & 0x08)) {
        currentOutput->print("bead: '"); currentOutput->print(chain_name); currentOutput->println("' is not compiled (use cord first)");
        return;
    }
    
    // 4. Находим адрес nop
    uint16_t nop_addr = dict_find("nop");
    if (nop_addr == 0xFFFF) {
        currentOutput->println("bead: nop not found");
        return;
    }
    
    // 5. Создаём бусину как анонимное скомпилированное слово
    // Размер: 5 (заголовок) + 0 (имя) + 4 (тело) = 9 байт
    uint16_t bead_sz = 9;
    if (dict_ptr + bead_sz > DICT_POOL_SIZE) {
        currentOutput->println("bead: dict_pool full");
        return;
    }
    
    uint16_t bead_addr = dict_ptr; dict_ptr += bead_sz;
    
    // Заголовок бусины
    dict_pool[bead_addr] = 0; dict_pool[bead_addr + 1] = 0; // next (заполнит link_word)
    dict_pool[bead_addr + 2] = 0; // nlen = 0 (анонимное слово)
    dict_pool[bead_addr + 3] = currentContext; // ctx
    dict_pool[bead_addr + 4] = 0x08; // flags = COMPILED
    
    // Тело бусины: [action_addr] [nop_addr]
    uint16_t body = bead_addr + 5;
    dict_pool[body]     = action_addr & 0xFF;
    dict_pool[body + 1] = action_addr >> 8;
    dict_pool[body + 2] = nop_addr & 0xFF;
    dict_pool[body + 3] = nop_addr >> 8;
    
    link_word(bead_addr);
    
    // 6. Переписываем nop_addr в теле цепи на адрес бусины
    uint16_t chain_body = chain_addr + 5 + c_nlen;
    uint16_t first_addr = dict_pool[chain_body] | (dict_pool[chain_body + 1] << 8);
    
    if (first_addr == nop_addr) {
        // Цепь пуста → первая бусина
        dict_pool[chain_body]     = bead_addr & 0xFF;
        dict_pool[chain_body + 1] = bead_addr >> 8;
    } else {
        // Цепь не пуста → ищем последнюю бусину (где next == nop_addr)
        uint16_t curr = first_addr;
        uint16_t safety = 0;
        while (safety++ < 256) {
            uint8_t curr_nlen = dict_pool[curr + 2];
            uint16_t curr_body = curr + 5 + curr_nlen;
            uint16_t next_addr = dict_pool[curr_body + 2] | (dict_pool[curr_body + 3] << 8);
            
            if (next_addr == nop_addr) {
                // Нашли последнюю → переписываем nop_addr на bead_addr
                dict_pool[curr_body + 2] = bead_addr & 0xFF;
                dict_pool[curr_body + 3] = bead_addr >> 8;
                break;
            }
            
            curr = next_addr;
            if (curr == 0 || curr >= dict_ptr) break;
        }
    }
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
    // === Фоллбэк: работа со стеком (если вызвана из скомпилированного кода) ===
    if (stack_is_empty()) return;
    uint8_t* top = &stack_mem[stack_ptr];
    if (top[0] != 0x0E) {
        currentOutput->println("task name expected");
        return;
    }
    uint8_t nlen = top[1]; if (nlen >= 64) nlen = 63;
    char tname[64]; memcpy(tname, &top[2], nlen); tname[nlen] = '\0';
    stack_ptr += elem_size(top);
    
    // ✅ ИСПОЛЬЗУЕМ ХЕЛПЕР popUInt32 вместо ручного цикла
    uint32_t interval;
    if (!popUInt32(interval) || interval == 0) {
        currentOutput->println("interval expected");
        return;
    }
    
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
    uint16_t addr;
    if (!popUInt16(addr)) {
        currentOutput->println("runtime err: expected word addr");
        return;
    }
    
    // 2. Снимаем интервал (число)
    uint32_t interval;
    if (!popUInt32(interval) || interval == 0) {
        currentOutput->println("runtime err: expected interval");
        return;
    }
    
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
    // 1. Снимаем min
    if (stack_is_empty()) {
        currentOutput->println("randRange: min expected");
        return;
    }
    uint8_t* min_ptr = &stack_mem[stack_ptr];
    if (!is_numeric_tag(min_ptr[0])) {
        currentOutput->println("randRange: min must be a number");
        return;
    }
    uint16_t min_sz = elem_size(min_ptr);
    // ✅ ИСПОЛЬЗУЕМ decode_int_le — применяет знаковое расширение для i8/i16/i32
    int32_t min_val = decode_int_le(&min_ptr[1], min_sz - 1, min_ptr[0]);
    stack_ptr += min_sz;

    // 2. Снимаем max
    if (stack_is_empty()) {
        currentOutput->println("randRange: max expected");
        return;
    }
    uint8_t* max_ptr = &stack_mem[stack_ptr];
    if (!is_numeric_tag(max_ptr[0])) {
        currentOutput->println("randRange: max must be a number");
        return;
    }
    uint8_t res_tag = max_ptr[0];
    uint16_t max_sz = elem_size(max_ptr);
    // ✅ ИСПОЛЬЗУЕМ decode_int_le
    int32_t max_val = decode_int_le(&max_ptr[1], max_sz - 1, max_ptr[0]);
    stack_ptr += max_sz;

    // 3. Проверяем min <= max (теперь как ЗНАКОВЫЕ числа!)
    if (min_val > max_val) {
        currentOutput->println("randRange: min > max");
        return;
    }

    // 4. Генерируем случайное число
    int32_t range = max_val - min_val + 1;
    int32_t r = min_val + (esp_random() % range);

    // 5. Возвращаем результат
    uint8_t out[8];
    out[0] = res_tag;
    switch (res_tag) {
        case 4:  out[1] = (uint8_t)r; stack_push(out, 2); break;
        case 5:  out[1] = (int8_t)r; stack_push(out, 2); break;
        case 6:  *(uint16_t*)&out[1] = (uint16_t)r; stack_push(out, 3); break;
        case 7:  *(int16_t*)&out[1] = (int16_t)r; stack_push(out, 3); break;
        case 8:  out[1] = (uint8_t)r; out[2] = (uint8_t)(r >> 8); out[3] = (uint8_t)(r >> 16); stack_push(out, 4); break;
        case 9:  *(uint32_t*)&out[1] = (uint32_t)r; stack_push(out, 5); break;
        case 10: *(int32_t*)&out[1] = r; stack_push(out, 5); break;
        case 11: { float f = (float)r; memcpy(&out[1], &f, 4); stack_push(out, 5); } break;
        default: out[0] = 9; *(uint32_t*)&out[1] = (uint32_t)r; stack_push(out, 5); break;
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
// ✅ ИСПОЛЬЗУЕМ ХЕЛПЕРЫ decode_int_le И is_signed_tag
int32_t val = decode_int_le(&top[1], elem_size(top) - 1, tag);
if (is_signed_tag(tag)) val_len = sprintf(val_str, "%ld", (long)val);
else val_len = sprintf(val_str, "%lu", (unsigned long)(uint32_t)val);
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
void word_mem() {
  // dict_pool: свободно / занято / всего
  uint16_t dict_free = DICT_POOL_SIZE - dict_ptr;
  currentOutput->printf("dict %u/%u/%u\n", dict_free, dict_ptr, DICT_POOL_SIZE);

  // data_pool: свободно / занято / всего
  uint16_t data_free = DATA_POOL_SIZE - data_ptr;
  currentOutput->printf("data %u/%u/%u\n", data_free, data_ptr, DATA_POOL_SIZE);

  // heap (ESP-куча): только свободное (общий размер динамический)
  uint32_t heap_free = ESP.getFreeHeap();
  currentOutput->printf("heap %lu free\n", (unsigned long)heap_free);
}

void word_open_paren() {
  // 1. Ищем ')' на стеке
  uint16_t p = stack_ptr;
  uint16_t marker_pos = 0xFFFF;
  while (p + 3 <= STACK_SIZE) {
    if (stack_mem[p] == 0x0C && stack_mem[p+1] == 1 && stack_mem[p+2] == ')') {
      marker_pos = p;
      break;
    }
    uint16_t sz = elem_size(&stack_mem[p]);
    if (sz == 0 || sz > 10) break;
    p += sz;
  }
  if (marker_pos == 0xFFFF) {
    currentOutput->println("(: no matching )");
    return;
  }

  // 2. Удаляем ')'
  uint16_t above_size = marker_pos - stack_ptr;
  if (above_size > 0) {
    memmove(&stack_mem[stack_ptr + 3], &stack_mem[stack_ptr], above_size);
  }
  stack_ptr += 3;

  // 3. Проверяем, что под LHS лежит ОПЕРАТОР, а не ещё одна ')'
  if (stack_is_empty()) return;
  uint8_t* val = &stack_mem[stack_ptr];
  uint16_t vsz = elem_size(val);
  if (vsz == 0 || vsz > 8) return;
  
  uint16_t m_addr = stack_ptr + vsz;
  if (m_addr + 3 > STACK_SIZE) return;
  
  // 🔧 FIX: проверяем, что это оператор, а не скобка
  if (stack_mem[m_addr] != 0x0C || stack_mem[m_addr+1] != 1) return;
  char op = stack_mem[m_addr+2];
  if (op == ')' || op == '(' || op == '[' || op == ']') {
    // Под LHS лежит скобка, а не оператор — не сворачиваем
    return;
  }

  // 4. Свёртка одной операции
  uint16_t r_addr = m_addr + 3;
  if (r_addr >= STACK_SIZE) return;
  uint16_t rsz = elem_size(&stack_mem[r_addr]);
  if (rsz == 0) return;

  uint8_t saved[8];
  memcpy(saved, val, vsz);
  stack_ptr += vsz;
  apply_op(&saved[1], vsz - 1, saved[0], 0);
}
// 1. num>char : Преобразует число (код) в строку из одного символа (STRING)
void word_num_to_char() {
    if (stack_is_empty()) return;
    uint8_t* top = &stack_mem[stack_ptr];
    // Ожидаем числовой тип (теги 4..11)
    if (top[0] < 4 || top[0] > 11) return; 
    uint16_t sz = elem_size(top);
    // ✅ ХЕЛПЕР decode_uint_le вместо ручного цикла
    uint32_t val = decode_uint_le(&top[1], sz - 1);
    stack_ptr += sz; // Снимаем число со стека
    // Кладём на стек как STRING (тег 14), длина 1 байт
    uint8_t out[3] = {14, 1, (uint8_t)(val & 0xFF)}; 
    stack_push(out, 3);
}
// 2. char>num : Преобразует строку/символ в число (код первого байта)
void word_char_to_num() {
    if (stack_is_empty()) return;
    uint8_t* top = &stack_mem[stack_ptr];
    uint8_t char_code = 0;
    
    // Работаем с STRING (14), NAME (13) или $STRING (15)
    if (top[0] == 14 || top[0] == 13) {
        if (top[1] > 0) char_code = top[2]; // Берём первый байт
        stack_ptr += elem_size(top);        // Снимаем строку
    } 
    else if (top[0] == 15) { // $STRING (ссылка на пул)
        uint8_t slen = top[1];
        uint16_t addr = top[2] | (top[3] << 8);
        if (slen > 0 && addr < DATA_POOL_SIZE) {
            char_code = data_pool[addr];
        }
        stack_ptr += elem_size(top);
    } 
    else {
        return; // Не текстовый тип
    }
    
    // Кладём на стек как UINT8 (тег 4)
    uint8_t out[2] = {4, char_code}; 
    stack_push(out, 2);         
}
// === КОНТЕКСТ time ===
void word_millis() {
    // Возвращает время с запуска как UINT32 (тег 9)
    uint32_t t = millis();
    uint8_t out[5] = {9}; // Тег 9 = UINT32
    memcpy(&out[1], &t, 4);
    stack_push(out, 5);
}

// === КОНТЕКСТ types ===
void word_toInt() {
    if (stack_is_empty()) return;
    uint8_t* top = &stack_mem[stack_ptr];
    uint8_t tag = top[0];

    char buf[64];
    uint8_t len = 0;

    // Поддерживаем STRING (14), NAME (13) и $STRING (15)
    if (tag == 14 || tag == 13) { 
        len = top[1];
        if (len > 63) len = 63;
        memcpy(buf, &top[2], len);
        buf[len] = '\0';
        stack_ptr += elem_size(top);
    } 
    else if (tag == 15) { // $STRING (ссылка на пул)
        len = top[1];
        uint16_t addr = top[2] | (top[3] << 8);
        if (len > 63) len = 63;
        if (addr + len <= DATA_POOL_SIZE) {
            memcpy(buf, &data_pool[addr], len);
        }
        buf[len] = '\0';
        stack_ptr += elem_size(top);
    } 
    else {
        return; // Не текстовый тип
    }

    // Парсим число (база 0 позволяет распознавать 0x для hex)
    char* endptr;
    long val = strtol(buf, &endptr, 0); 

    // Кладем результат как INT32 (тег 10)
    uint8_t out[5] = {10}; 
    int32_t v32 = (int32_t)val;
    memcpy(&out[1], &v32, 4);
    stack_push(out, 5);
}

// === КОНТЕКСТ stack (или main) ===
void word_swap() {
    if (stack_is_empty()) return;
    
    uint8_t* top1 = &stack_mem[stack_ptr];
    uint16_t sz1 = elem_size(top1);

    uint16_t addr2 = stack_ptr + sz1;
    if (addr2 >= STACK_SIZE) return; // На стеке только один элемент

    uint8_t* top2 = &stack_mem[addr2];
    uint16_t sz2 = elem_size(top2);

    // Буфер для временного хранения (макс размер строки 255 + 2 байта заголовок)
    static uint8_t tmp[260]; 
    if (sz1 > 258) return; // Защита от переполнения
    
    memcpy(tmp, top1, sz1);
    memmove(top1, top2, sz2);
    memcpy(top1 + sz2, tmp, sz1);

    // Корректируем указатель стека, если размеры элементов разные
    stack_ptr += (sz1 - sz2);
}
void word_abort() {
    // 1. Очистка стеков (данных и возвратов)
    stack_clear();
    rstack_ptr = RSTACK_SIZE;
    
    // 2. Остановка всех фоновых задач и циклов
    g_task_count = 0;
    
    // 3. Принудительный выход из режима компиляции
    g_compile_mode = false;
    g_compile_header = 0xFFFF;
blk_depth = 0;
    g_in_block_comment = false;
    
    // 4. Возврат канала вывода в Serial (спасение от "немого" терминала)
    currentOutput = &Serial;
    
    // 5. Принудительный разрыв текущего исполнения (если слово вызвано внутри vm_run)
    ip = dict_ptr; 
    
    // 6. Визуальное подтверждение и возврат промпта
    currentOutput->println("\n[ABORTED]");
    word_prompt();
}
void word_help() {
    // 1. Захват имени слова (R2L)
    int idx = g_tok_idx - 1;
if (idx < 0) {
    currentOutput->println(getMsg("?: word_name ? — shows help for a word"));
    currentOutput->println(getMsg("?: It explains what the word does and how it works"));
    currentOutput->println(getMsg("?: To see a list of available words, type: words"));
    return;
}
    uint8_t len = g_tok_len[idx];
    if (len == 0 || len > 63) {
        currentOutput->println(getMsg("?: invalid word name"));
        return;
    }
    char tname[64];
    memcpy(tname, g_tok_start[idx], len);
    tname[len] = '\0';
    g_tok_idx -= 1;

    // 2. Поиск слова в словаре
    uint16_t addr = dict_find(tname);
    if (addr == 0xFFFF) {
        char msg[128];
        snprintf(msg, sizeof(msg), getMsg("?: word '%s' not found"), tname);
        currentOutput->println(msg);
        return;
    }
    if (addr + 4 >= dict_ptr) return;
    uint8_t nlen = dict_pool[addr + 2];
    if (nlen > 63 || addr + 3 + nlen >= dict_ptr) return;
    uint8_t ctx_id = dict_pool[addr + 3 + nlen];

    // 3. Определение имени контекста
    char ctx_name[32] = "main";
    if (ctx_id != 0) {
        uint16_t p = 0;
        bool found_ctx = false;
        while (p < dict_ptr) {
            if (p + 4 >= dict_ptr) break;
            uint16_t next = dict_pool[p] | (dict_pool[p + 1] << 8);
            uint8_t nlen_ctx = dict_pool[p + 2];
            if (nlen_ctx > 63 || p + 4 + nlen_ctx >= dict_ptr) break;
            uint8_t flags = dict_pool[p + 4 + nlen_ctx];
            if (flags == 0x0C) {
                uint16_t body_start = p + 5 + nlen_ctx;
                if (body_start + 2 <= dict_ptr && dict_pool[body_start] == 0x03) {
                    if (dict_pool[body_start + 1] == ctx_id) {
                        uint8_t copy_len = (nlen_ctx < 31) ? nlen_ctx : 31;
                        memcpy(ctx_name, &dict_pool[p + 3], copy_len);
                        ctx_name[copy_len] = '\0';
                        found_ctx = true;
                        break;
                    }
                }
            }
            if (next == 0) break;
            p = next;
        }
        if (!found_ctx) snprintf(ctx_name, sizeof(ctx_name), "ctx_%u", ctx_id);
    }

    // 4. Чтение языка
    char lang_code[8] = "ru";
    uint16_t lang_addr = dict_find("lang");
    if (lang_addr != 0xFFFF && lang_addr + 4 < dict_ptr) {
        uint8_t ln_len = dict_pool[lang_addr + 2];
        if (ln_len <= 63 && lang_addr + 5 + ln_len < dict_ptr) {
            uint16_t body_start = lang_addr + 5 + ln_len;
            uint8_t tag = dict_pool[body_start];
            if (tag == 15 && body_start + 4 <= dict_ptr) {
                uint8_t slen = dict_pool[body_start + 1];
                uint16_t d_addr = dict_pool[body_start + 2] | (dict_pool[body_start + 3] << 8);
                if (slen > 0 && slen < 8 && d_addr + slen <= DATA_POOL_SIZE) {
                    memcpy(lang_code, &data_pool[d_addr], slen);
                    lang_code[slen] = '\0';
                }
            }
        }
    }

    // 5. Открытие файла
    char path[64];
    snprintf(path, sizeof(path), "/help/%s/%s.txt", lang_code, ctx_name);
    File f = FILESYSTEM.open(path, "r");
    if (!f) {
        snprintf(path, sizeof(path), "/help/en/%s.txt", ctx_name);
        f = FILESYSTEM.open(path, "r");
        if (!f) {
            char msg[128];
            snprintf(msg, sizeof(msg), getMsg("?: no help for '%s' (context: %s). Teach me!"), tname, ctx_name);
            currentOutput->println(msg);
            return;
        }
    }

    // 6. Чтение файла
    char marker[66];
    snprintf(marker, sizeof(marker), "## %s", tname);
    size_t marker_len = strlen(marker);
    bool found = false;
    uint8_t buf[128];
    size_t n;
    char line[512];
    int line_idx = 0;

    while ((n = f.read(buf, sizeof(buf))) > 0) {
        for (size_t i = 0; i < n; i++) {
            char c = buf[i];
            if (c == '\n' || line_idx >= 511) {
                line[line_idx] = '\0';
                if (!found) {
                    if ((size_t)line_idx >= marker_len && strncmp(line, marker, marker_len) == 0) {
                        found = true;
                    }
                } else {
                    if (line_idx >= 3 && strncmp(line, "## ", 3) == 0) {
                        f.close();
                        return;
                    }
                    currentOutput->println(line);
                    process_tasks(true);
                }
                line_idx = 0;
            } else if (c != '\r') {
                if (line_idx < 511) {
                    line[line_idx++] = c;
                }
            }
        }
    }

    if (line_idx > 0 && found) {
        line[line_idx] = '\0';
        currentOutput->println(line);
    }
    f.close();

    if (!found) {
        char msg[128];
        snprintf(msg, sizeof(msg), getMsg("?: description for '%s' not found in file. Teach me!"), tname);
        currentOutput->println(msg);
    }
}
// === ДВУХУРОВНЕВАЯ СИСТЕМА СООБЩЕНИЙ ===
// Уровень 1: Файл /lang/{lang}.txt (пользователь редактирует)
// Уровень 2: Английские строки в коде (ключ = fallback)

// === КОНФИГУРАЦИЯ СИСТЕМЫ СООБЩЕНИЙ ===
#define MSG_CACHE_SIZE       16     // Максимум кэшированных сообщений
#define MSG_VALUE_MAX        192    // Макс. длина перевода (96 кириллических символов в UTF-8)
#define MSG_KEY_PREFIX_MAX   128    // Макс. длина ключа с "=" на конце
#define MSG_LANG_CODE_MAX    8      // Макс. длина кода языка ("ru", "en", ...)
#define MSG_PATH_MAX         32     // Макс. длина пути "/lang/XX.txt"

struct MsgCacheEntry {
    uint32_t key_hash;
    char value[MSG_VALUE_MAX];
};
static MsgCacheEntry g_msg_cache[MSG_CACHE_SIZE];
static uint8_t g_msg_cache_count = 0;

static uint32_t hash_key(const char* key) {
    uint32_t h = 0;
    while (*key) { h = (h * 31) + *key++; }
    return h;
}

static const char* getMsg(const char* key) {
    uint32_t h = hash_key(key);
    
    // 1. Проверяем кэш
    for (uint8_t i = 0; i < g_msg_cache_count; i++) {
        if (g_msg_cache[i].key_hash == h) {
            return g_msg_cache[i].value;
        }
    }
    
    // 2. Читаем язык из переменной "lang"
    char lang_code[MSG_LANG_CODE_MAX] = "ru";
    uint16_t lang_addr = dict_find("lang");
    if (lang_addr != 0xFFFF) {
        uint8_t ln_len = dict_pool[lang_addr + 2];
        if (ln_len > 0 && ln_len < MSG_LANG_CODE_MAX) {
            uint16_t body = lang_addr + 5 + ln_len;
            if (dict_pool[body] == 15) {  // $STRING
                uint8_t slen = dict_pool[body + 1];
                uint16_t d_addr = dict_pool[body + 2] | (dict_pool[body + 3] << 8);
                if (slen < MSG_LANG_CODE_MAX && d_addr + slen <= DATA_POOL_SIZE) {
                    memcpy(lang_code, &data_pool[d_addr], slen);
                    lang_code[slen] = '\0';
                }
            }
        }
    }
    
    // 3. Открываем файл /lang/{lang}.txt
    char path[MSG_PATH_MAX];
    snprintf(path, sizeof(path), "/lang/%s.txt", lang_code);
    File f = FILESYSTEM.open(path, "r");
    if (!f) return key;  // Файла нет → возвращаем сам ключ
    
    // 4. Ищем ключ в файле
    char key_prefix[MSG_KEY_PREFIX_MAX];
    snprintf(key_prefix, sizeof(key_prefix), "%s=", key);
    size_t prefix_len = strlen(key_prefix);
    
    while (f.available()) {
        String s = f.readStringUntil('\n');
        if (s.endsWith("\r")) s.remove(s.length() - 1);
        if (s.length() == 0 || s[0] == '#') continue;
        if (s.startsWith(key_prefix)) {
            const char* value = s.c_str() + prefix_len;
            size_t vlen = strlen(value);
            if (vlen < MSG_VALUE_MAX) {
                if (g_msg_cache_count < MSG_CACHE_SIZE) {
                    g_msg_cache[g_msg_cache_count].key_hash = h;
                    strcpy(g_msg_cache[g_msg_cache_count].value, value);
                    g_msg_cache_count++;
                }
                f.close();
                return g_msg_cache[g_msg_cache_count - 1].value;
            }
        }
    }
    f.close();
    return key;  // Ключ не найден → возвращаем сам ключ
}

void word_open() {
  // 🔑 СОХРАНЯЕМ состояние стека для отката при ошибке
  uint16_t old_sp = stack_ptr;

  // 1. НАИМЕНОВАНИЕ: захват имени из R2L-потока (предыдущий токен)
  int name_idx = g_tok_idx - 1;
  if (name_idx < 0) {
    currentOutput->println(getMsg("open: name not specified"));
    return;
  }
  uint8_t nlen = g_tok_len[name_idx];
  if (nlen == 0 || nlen > 63) {
    currentOutput->println(getMsg("open: invalid name"));
    return;
  }
  char new_name[64];
  memcpy(new_name, g_tok_start[name_idx], nlen);
  new_name[nlen] = '\0';

  // 2. Проверка: слово не должно существовать
  if (dict_find(new_name) != 0xFFFF) {
    currentOutput->print(getMsg("open: already exists: "));
    currentOutput->println(new_name);
    g_tok_idx -= 1;
    return;
  }

  // 3. Снимаем со стека (R2L порядок):
  //    СТЕК (сверху вниз): ADDR(file) → STRING(filename) → i32(mode)

  // 3.1. Дескриптор (ADDR = 0x12) — ВЕРХ стека
  if (stack_is_empty()) {
    currentOutput->println(getMsg("open: descriptor (ADDR) expected"));
    stack_ptr = old_sp;  // 🔑 ОТКАТ стека
    return;
  }
  uint8_t* desc_ptr = &stack_mem[stack_ptr];
  if (desc_ptr[0] != 0x12) {
    currentOutput->println(getMsg("open: descriptor must be ADDR"));
    stack_ptr = old_sp;  // 🔑 ОТКАТ стека
    return;
  }
  uint16_t desc_addr = desc_ptr[1] | (desc_ptr[2] << 8);
  stack_ptr += elem_size(desc_ptr);

  // 3.2. Filename (STRING или NAME) — СЕРЕДИНА стека
  if (stack_is_empty()) {
    currentOutput->println(getMsg("open: filename expected"));
    stack_ptr = old_sp;  // 🔑 ОТКАТ стека
    return;
  }
  uint8_t* fn_ptr = &stack_mem[stack_ptr];
  if (fn_ptr[0] != 0x0D && fn_ptr[0] != 0x0E) {
    currentOutput->println(getMsg("open: filename must be string/name"));
    stack_ptr = old_sp;  // 🔑 ОТКАТ стека
    return;
  }
  uint8_t fn_len = fn_ptr[1];
  if (fn_len > 63) fn_len = 63;
  stack_ptr += elem_size(fn_ptr);

  // 3.3. Mode (число) — НИЗ стека
  if (stack_is_empty()) {
    currentOutput->println(getMsg("open: mode expected"));
    stack_ptr = old_sp;  // 🔑 ОТКАТ стека
    return;
  }
  uint8_t* mode_ptr = &stack_mem[stack_ptr];
  if (mode_ptr[0] < 4 || mode_ptr[0] > 11) {
    currentOutput->println(getMsg("open: mode must be number"));
    stack_ptr = old_sp;  // 🔑 ОТКАТ стека
    return;
  }
  uint8_t mode = mode_ptr[1];
  stack_ptr += elem_size(mode_ptr);

  // 4. Формируем тело слова: [ADDR desc][fn_len][fname...][mode]
  uint16_t body_size = 3 + 1 + fn_len + 1;
  uint8_t* body = (uint8_t*)malloc(body_size);
  uint16_t p = 0;
  body[p++] = 0x12;
  body[p++] = desc_addr & 0xFF;
  body[p++] = desc_addr >> 8;
  body[p++] = fn_len;
  memcpy(&body[p], &fn_ptr[2], fn_len);
  p += fn_len;
  body[p++] = mode;

  // 5. Создаём слово в словаре
  uint8_t name_buf[65] = {0x0D, nlen};
  memcpy(&name_buf[2], new_name, nlen);
  create_internal_word(name_buf, body, body_size, 0x06, choiceFunc);
  free(body);

  // 6. Пропускаем токен имени в R2L-цикле
  g_tok_idx -= 1;
}
void word_write() {
  if (stack_is_empty()) {
    currentOutput->println(getMsg("write: stack empty"));
    return;
  }
  uint8_t* res_ptr = &stack_mem[stack_ptr];
  if (res_ptr[0] != 0x12) {
    currentOutput->println(getMsg("write: expected resource word (ADDR)"));
    return;
  }
  uint16_t res_addr = res_ptr[1] | (res_ptr[2] << 8);
  stack_ptr += elem_size(res_ptr);

  if (res_addr >= dict_ptr) {
    currentOutput->println(getMsg("write: invalid resource address"));
    return;
  }
  uint8_t vn = dict_pool[res_addr + 2];
  uint16_t body = res_addr + 5 + vn;

  if (dict_pool[body] != 0x12) {
    currentOutput->println(getMsg("write: resource has no descriptor"));
    return;
  }
  uint16_t desc_addr = dict_pool[body + 1] | (dict_pool[body + 2] << 8);

  dispatch_resource(res_addr, desc_addr, "write");
}
// === ДЕСКРИПТОР ФАЙЛА ===
// Вызывается через dispatch_resource
// На стеке: ADDR ресурса (от dispatch), данные для записи (от пользователя)
void file_desc_func() {
  // 1. Снимаем ADDR ресурса (его положил dispatch_resource)
  if (stack_is_empty()) { pushBool(false); return; }
  uint8_t* ctx_ptr = &stack_mem[stack_ptr];
  if (ctx_ptr[0] != 0x12) { pushBool(false); return; }
  uint16_t res_addr = ctx_ptr[1] | (ctx_ptr[2] << 8);
  stack_ptr += 3;

  // 2. Читаем метаданные из тела ресурса
  //    Тело: [0x12][desc_lo][desc_hi][mode][fn_len][fname...]
  if (res_addr >= dict_ptr) { pushBool(false); return; }
  uint8_t vn = dict_pool[res_addr + 2];
  uint16_t body = res_addr + 5 + vn;
  
  // Пропускаем ADDR дескриптора (3 байта: тег + 2 байта адреса)
  uint8_t mode = dict_pool[body + 3];
  uint8_t fn_len = dict_pool[body + 4];
  if (fn_len > 63) fn_len = 63;
  char fname[65];
  memcpy(fname, &dict_pool[body + 5], fn_len);
  fname[fn_len] = '\0';

  // 3. Снимаем данные для записи
  if (stack_is_empty()) { pushBool(false); return; }
  uint8_t* d_ptr = &stack_mem[stack_ptr];
  uint8_t d_tag = d_ptr[0];
  uint16_t d_sz = elem_size(d_ptr);

  // 4. Stateless: открываем → пишем → закрываем
  char full_path[256];
  if (strlen(g_currentDir) > 1)
    snprintf(full_path, sizeof(full_path), "%s/%s", g_currentDir, fname);
  else
    snprintf(full_path, sizeof(full_path), "/%s", fname);

  const char* mode_str = (mode == 0) ? "r" : (mode == 1) ? "w" : "a";
  File f = FILESYSTEM.open(full_path, mode_str);
  size_t written = 0;
  
  if (f) {
    if (d_tag == 0x0E || d_tag == 0x0D) {          // STRING / NAME
      written = f.write(&d_ptr[2], d_ptr[1]);
    }
    else if (d_tag == 15) {                         // $STRING
      uint16_t addr = d_ptr[2] | (d_ptr[3] << 8);
      written = f.write(&data_pool[addr], d_ptr[1]);
    }
    else if (d_tag == 17 || d_tag == 20) {          // ARRAY / REF_ARR
      uint16_t base = d_ptr[1] | (d_ptr[2] << 8);
      uint16_t len  = d_ptr[3] | (d_ptr[4] << 8);
      uint8_t esz = type_registry[d_ptr[5]].size;
      written = f.write(&data_pool[base], len * esz);
    }
    f.close();
    stack_ptr += d_sz;
    pushBool(written > 0);
  } else {
    stack_ptr += d_sz;
    pushBool(false);
  }
}

// === ДЕСКРИПТОР SERIAL ===
// Тело ресурса: [0x12][desc_lo][desc_hi][port][baud(4)][rx][tx][state]
void serial_desc_func() {
  // 1. Снимаем ADDR ресурса
  if (stack_is_empty()) { pushBool(false); return; }
  uint8_t* ctx_ptr = &stack_mem[stack_ptr];
  if (ctx_ptr[0] != 0x12) { pushBool(false); return; }
  uint16_t res_addr = ctx_ptr[1] | (ctx_ptr[2] << 8);
  stack_ptr += 3;

  // 2. Читаем метаданные
  if (res_addr >= dict_ptr) { pushBool(false); return; }
  uint8_t vn = dict_pool[res_addr + 2];
  uint16_t body = res_addr + 5 + vn;
  
  uint8_t port = dict_pool[body + 3];
  uint32_t baud = dict_pool[body + 4] | (dict_pool[body + 5] << 8) |
                  (dict_pool[body + 6] << 16) | (dict_pool[body + 7] << 24);
  uint8_t rx = dict_pool[body + 8];
  uint8_t tx = dict_pool[body + 9];
  uint8_t state = dict_pool[body + 10];

  // 3. Получаем указатель на UART
  Stream* uart = nullptr;
  if (port == 0) uart = &Serial;
  else if (port == 1) uart = &Serial1;
  else if (port == 2) uart = &Serial2;
  else { pushBool(false); return; }

  // 4. Авто-инициализация
  if (state == 0 && port > 0) {
    ((HardwareSerial*)uart)->begin(baud, SERIAL_8N1, rx, tx);
    dict_pool[body + 10] = 1;  // Обновляем состояние в теле слова
  }

  // 5. Снимаем данные для записи
  if (stack_is_empty()) { pushBool(false); return; }
  uint8_t* d_ptr = &stack_mem[stack_ptr];
  uint8_t d_tag = d_ptr[0];
  uint16_t d_sz = elem_size(d_ptr);

  size_t written = 0;
  if (d_tag == 0x0E || d_tag == 0x0D) {
    written = uart->write(&d_ptr[2], d_ptr[1]);
  }
  else if (d_tag == 15) {
    uint16_t addr = d_ptr[2] | (d_ptr[3] << 8);
    written = uart->write(&data_pool[addr], d_ptr[1]);
  }
  else if (d_tag == 17 || d_tag == 20) {
    uint16_t base = d_ptr[1] | (d_ptr[2] << 8);
    uint16_t len  = d_ptr[3] | (d_ptr[4] << 8);
    uint8_t esz = type_registry[d_ptr[5]].size;
    written = uart->write(&data_pool[base], len * esz);
  }

  stack_ptr += d_sz;
  pushBool(written > 0);
}
void word_addr_of() {
    // === РЕЖИМ 1: Исполнение из скомпилированного кода ===
    // Если ip указывает на валидный адрес в dict_pool —
    // читаем следующие 2 байта как адрес целевого слова
    if (ip > 0 && ip + 2 <= dict_ptr) {
        uint16_t target = dict_pool[ip] | (dict_pool[ip + 1] << 8);
        if (target < dict_ptr) {
            // Проверяем, что это валидный адрес слова
            uint8_t nlen = dict_pool[target + 2];
            if (nlen <= 63 && target + 5 + nlen <= dict_ptr) {
                // Кладём ADDR на стек
                uint8_t a[3] = {0x12, (uint8_t)(target & 0xFF), (uint8_t)(target >> 8)};
                stack_push(a, 3);
                ip += 2;  // 🔑 Пропускаем адрес следующего слова
                return;
            }
        }
    }
    
    // === РЕЖИМ 2: REPL — захват из R2L-потока ===
    int idx = g_tok_idx - 1;
    if (idx < 0) {
        currentOutput->println("#: no word before");
        return;
    }
    uint8_t len = g_tok_len[idx];
    if (len == 0 || len > 63) {
        currentOutput->println("#: invalid name");
        return;
    }
    char name[64];
    memcpy(name, g_tok_start[idx], len);
    name[len] = '\0';
    
    uint16_t addr = dict_find(name);
    if (addr == 0xFFFF) {
        currentOutput->print("#: not found: ");
        currentOutput->println(name);
        return;
    }
    
    // Кладём ADDR на стек
    uint8_t a[3] = {0x12, (uint8_t)(addr & 0xFF), (uint8_t)(addr >> 8)};
    stack_push(a, 3);
    
    // Пропускаем токен имени в R2L-цикле
    g_tok_idx -= 1;
}
// === meta — индексатор массивов для HDL ===
// Человеческая запись: meta ssdp.arr ssdp.arr? sep
// Исполнение (R2L): sep → ssdp.arr? → ssdp.arr → meta
// Стек (сверху вниз): ssdp.arr, ssdp.arr?, sep
//
// Заполняет существующий u16-массив индексами вхождений разделителя.
// Возвращает на стек количество найденных индексов.
// НЕ создаёт ничего нового в data_pool — только перезаписывает буфер.

// === meta — индексатор массивов для HDL ===
// Человеческая запись: meta ssdp.arr ssdp.arr? sep
// Исполнение (R2L): sep → ssdp.arr? → ssdp.arr → meta
// Стек (сверху вниз): ssdp.arr, ssdp.arr?, sep
//
// Заполняет существующий u16-массив индексами вхождений разделителя.
// Возвращает на стек количество найденных индексов.
// НЕ создаёт ничего нового в data_pool — только перезаписывает буфер.

void word_meta() {
    // 1. Снимаем исходный массив (вершина стека)
    if (stack_is_empty()) {
        currentOutput->println("⚠️ meta: source array expected");
        pushUInt16(0);
        return;
    }
    uint8_t* src_ptr = &stack_mem[stack_ptr];
    if (src_ptr[0] != 17 && src_ptr[0] != 20) {
        currentOutput->println("⚠️ meta: source must be array");
        stack_ptr += elem_size(src_ptr);
        pushUInt16(0);
        return;
    }
    uint16_t src_base  = src_ptr[1] | (src_ptr[2] << 8);
    uint16_t src_len   = src_ptr[3] | (src_ptr[4] << 8);
    uint8_t  src_tp    = src_ptr[5];
    uint8_t  src_esz   = type_registry[src_tp].size;
    uint32_t src_total = (uint32_t)src_len * src_esz;
    stack_ptr += elem_size(src_ptr);
    
    // 2. Снимаем целевой массив (под вершиной)
    if (stack_is_empty()) {
        currentOutput->println("⚠️ meta: target array expected");
        pushUInt16(0);
        return;
    }
    uint8_t* dst_ptr = &stack_mem[stack_ptr];
    if (dst_ptr[0] != 17 && dst_ptr[0] != 20) {
        currentOutput->println("⚠️ meta: target must be array");
        stack_ptr += elem_size(dst_ptr);
        pushUInt16(0);
        return;
    }
    uint16_t dst_base = dst_ptr[1] | (dst_ptr[2] << 8);
    uint16_t dst_max  = dst_ptr[3] | (dst_ptr[4] << 8);  // максимальная вместимость
    uint8_t  dst_tp   = dst_ptr[5];
    
    if (dst_tp != 6) {  // тип должен быть u16
        currentOutput->println("⚠️ meta: target must be u16 array");
        stack_ptr += elem_size(dst_ptr);
        pushUInt16(0);
        return;
    }
    if (dst_base + dst_max * 2 > DATA_POOL_SIZE) {
        currentOutput->println("⚠️ meta: target array out of data_pool");
        stack_ptr += elem_size(dst_ptr);
        pushUInt16(0);
        return;
    }
    stack_ptr += elem_size(dst_ptr);
    
    // 3. Снимаем разделитель (под целевым массивом)
    if (stack_is_empty()) {
        currentOutput->println("⚠️ meta: separator expected");
        pushUInt16(0);
        return;
    }
    uint8_t* sep_ptr = &stack_mem[stack_ptr];
    uint8_t  sep_tag = sep_ptr[0];
    uint16_t sep_sz  = elem_size(sep_ptr);
    
    // Извлекаем сырые байты разделителя для поиска
    const uint8_t* search_bytes = nullptr;
    uint16_t search_len = 0;
    uint8_t temp_buf[256];
    
    if (sep_tag == 0x0E || sep_tag == 0x0D) {
        // STRING или NAME: [тег][длина][байты...]
        search_bytes = &sep_ptr[2];
        search_len = sep_ptr[1];
    }
    else if (sep_tag >= 4 && sep_tag <= 11) {
        // Числовой тип: [тег][байты...]
        search_bytes = &sep_ptr[1];
        search_len = sep_sz - 1;
    }
    else if (sep_tag == 15) {
        // $STRING: [15][длина][addr_lo][addr_hi] — байты в data_pool
        uint8_t slen = sep_ptr[1];
        uint16_t addr = sep_ptr[2] | (sep_ptr[3] << 8);
        if (slen > 0 && addr + slen <= DATA_POOL_SIZE) {
            memcpy(temp_buf, &data_pool[addr], slen);
            search_bytes = temp_buf;
            search_len = slen;
        }
    }
    else if (sep_tag == 17 || sep_tag == 20) {
        // ARRAY: байты в data_pool
        uint16_t base = sep_ptr[1] | (sep_ptr[2] << 8);
        uint16_t len  = sep_ptr[3] | (sep_ptr[4] << 8);
        uint8_t  tp   = sep_ptr[5];
        uint8_t  esz  = type_registry[tp].size;
        uint32_t total = (uint32_t)len * esz;
        if (total > 0 && total <= 255 && base + total <= DATA_POOL_SIZE) {
            memcpy(temp_buf, &data_pool[base], total);
            search_bytes = temp_buf;
            search_len = (uint16_t)total;
        }
    }
    
    stack_ptr += sep_sz;
    
    // 4. Сканируем исходный массив, ищем вхождения разделителя
    uint16_t idx_count = 0;
    
    if (search_len > 0 && search_bytes && src_total >= search_len &&
        src_base + src_total <= DATA_POOL_SIZE) {
        for (uint32_t i = 0; i <= src_total - search_len && idx_count < dst_max; i++) {
            if (memcmp(&data_pool[src_base + i], search_bytes, search_len) == 0) {
                // Записываем индекс как u16 LE
                uint16_t pos = dst_base + idx_count * 2;
                data_pool[pos]     = i & 0xFF;
                data_pool[pos + 1] = (i >> 8) & 0xFF;
                idx_count++;
            }
        }
    }
    
    // 5. Возвращаем количество найденных индексов на стек
    pushUInt16(idx_count);
}
void word_find() {
    // 1. Снимаем source (строка или массив)
    if (stack_is_empty()) { pushInt32(-1); return; }
    uint8_t* src = &stack_mem[stack_ptr];
    uint8_t src_tag = src[0];
    uint16_t src_sz = elem_size(src);
    const uint8_t* bytes = nullptr;
    uint16_t total_len = 0;
    bool is_string = false;
    
    if (src_tag == 0x0E || src_tag == 0x0D) {          // STRING / NAME
        bytes = &src[2];
        total_len = src[1];
        is_string = true;
    } else if (src_tag == 15) {                        // $STRING
        total_len = src[1];
        uint16_t a = src[2] | (src[3] << 8);
        if (a + total_len <= DATA_POOL_SIZE) bytes = &data_pool[a];
        is_string = true;
    } else if (src_tag == 17 || src_tag == 20) {       // ARRAY / REF_ARR
        uint16_t base = src[1] | (src[2] << 8);
        uint16_t len  = src[3] | (src[4] << 8);
        uint8_t  esz  = type_registry[src[5]].size;
        total_len = len * esz;
        if (base + total_len <= DATA_POOL_SIZE) bytes = &data_pool[base];
        is_string = false;
    } else {
        stack_ptr += src_sz;
        pushInt32(-1);
        return;
    }
    stack_ptr += src_sz;
    
    // 2. Снимаем needle
    if (stack_is_empty()) { pushInt32(-1); return; }
    uint8_t* needle_ptr = &stack_mem[stack_ptr];
    uint16_t needle_sz = elem_size(needle_ptr);
    const uint8_t* needle = nullptr;
    uint16_t needle_len = 0;
    
    if (needle_ptr[0] == 0x0E || needle_ptr[0] == 0x0D) {
        needle = &needle_ptr[2];
        needle_len = needle_ptr[1];
    } else if (needle_ptr[0] == 15) {
        needle_len = needle_ptr[1];
        uint16_t a = needle_ptr[2] | (needle_ptr[3] << 8);
        if (a + needle_len <= DATA_POOL_SIZE) needle = &data_pool[a];
    } else if (needle_ptr[0] >= 4 && needle_ptr[0] <= 11) {
        needle = &needle_ptr[1];
        needle_len = needle_sz - 1;
    }
    stack_ptr += needle_sz;
    
    // 3. Снимаем направление (маркер (- или -))
    if (stack_is_empty()) { pushInt32(-1); return; }
    uint8_t* dir_ptr = &stack_mem[stack_ptr];
    bool forward = true;
    if (dir_ptr[0] == 0x0C && dir_ptr[1] == 2) {
        // Двухсимвольный маркер
        if (dir_ptr[2] == '-' && dir_ptr[3] == ')')      forward = true;   // -) → вперёд
        else if (dir_ptr[2] == '(' && dir_ptr[3] == '-') forward = false;  // (- → назад
        else { stack_ptr += elem_size(dir_ptr); pushInt32(-1); return; }
    } else {
        stack_ptr += elem_size(dir_ptr);
        pushInt32(-1);
        return;
    }
    stack_ptr += elem_size(dir_ptr);
    
    // 4. Снимаем from_pos
    int32_t from_pos;
    if (stack_is_empty()) { pushInt32(-1); return; }
    uint8_t* pos_ptr = &stack_mem[stack_ptr];
    if (pos_ptr[0] >= 4 && pos_ptr[0] <= 11) {
        from_pos = decode_int_le(&pos_ptr[1], elem_size(pos_ptr) - 1, pos_ptr[0]);
    } else {
        stack_ptr += elem_size(pos_ptr);
        pushInt32(-1);
        return;
    }
    stack_ptr += elem_size(pos_ptr);
    
    // 5. Проверки
    if (!bytes || total_len == 0 || needle_len == 0 || needle_len > total_len) {
        pushInt32(-1);
        return;
    }
    
    // 6. Поиск
    int32_t result = -1;
    if (forward) {
        // -) : ищем слева направо от позиции from_pos
        uint32_t start = (from_pos < 0) ? 0 : (uint32_t)from_pos;
        if (start > total_len - needle_len) { pushInt32(-1); return; }
        for (uint32_t i = start; i <= total_len - needle_len; i++) {
            if (memcmp(&bytes[i], needle, needle_len) == 0) {
                result = (int32_t)i;
                break;
            }
        }
    } else {
        // (- : ищем справа налево от позиции from_pos
        uint32_t start_pos;
        if (from_pos < 0) {
            start_pos = total_len - needle_len;  // от конца
        } else {
            start_pos = (uint32_t)from_pos;
            if (start_pos > total_len - needle_len) start_pos = total_len - needle_len;
        }
        for (int32_t i = (int32_t)start_pos; i >= 0; i--) {
            if (memcmp(&bytes[i], needle, needle_len) == 0) {
                result = (int32_t)i;
                break;
            }
        }
    }
    
    // 7. Для строк: корректируем позицию к началу UTF-8 символа
    if (result >= 0 && is_string) {
        while (result > 0 && (bytes[result] & 0xC0) == 0x80) {
            result--;
        }
    }
    
    pushInt32(result);
}
void word_between() {
    // Стек (сверху вниз): source, start_pos, end_pos
    // Снимаем В ПОРЯДКЕ СВЕРХУ ВНИЗ

    // 1. source (ВЕРХ стека)
    if (stack_is_empty()) { pushStringRaw(""); return; }
    uint8_t* src = &stack_mem[stack_ptr];
    uint8_t tag = src[0];
    uint16_t src_sz = elem_size(src);

    const uint8_t* bytes = nullptr;
    uint16_t total_len = 0;
    uint8_t esz = 1;

    if (tag == 0x0E || tag == 0x0D) {          // STRING / NAME
        bytes = &src[2];
        total_len = src[1];
        esz = 1;
    } else if (tag == 15) {                    // $STRING
        total_len = src[1];
        uint16_t a = src[2] | (src[3] << 8);
        if (a + total_len <= DATA_POOL_SIZE) bytes = &data_pool[a];
        esz = 1;
    } else if (tag == 17 || tag == 20) {       // ARRAY / REF_ARR
        uint16_t base = src[1] | (src[2] << 8);
        uint16_t len  = src[3] | (src[4] << 8);
        esz = type_registry[src[5]].size;
        total_len = len;  // длина в элементах
        uint32_t total_bytes = (uint32_t)len * esz;
        if (base + total_bytes <= DATA_POOL_SIZE) bytes = &data_pool[base];
    }
    stack_ptr += src_sz;

    // 2. start_pos (теперь наверху)
    uint16_t start_pos;
    if (!popUInt16(start_pos)) { pushStringRaw(""); return; }

    // 3. end_pos (теперь наверху)
    uint16_t end_pos;
    if (!popUInt16(end_pos)) { pushStringRaw(""); return; }

    // 🔑 АВТО-СОРТИРОВКА
    if (start_pos > end_pos) {
        uint16_t tmp = start_pos;
        start_pos = end_pos;
        end_pos = tmp;
    }

    if (!bytes || total_len == 0) { pushStringRaw(""); return; }

    // 4. Границы (в элементах для ARRAY, в байтах для STRING)
    if (start_pos >= total_len) { pushStringRaw(""); return; }
    if (end_pos > total_len) end_pos = total_len;

    uint32_t start_byte = (uint32_t)start_pos * esz;
    uint32_t end_byte   = (uint32_t)end_pos * esz;
    uint16_t slice_len  = (uint16_t)(end_byte - start_byte);
    const uint8_t* slice_start = bytes + start_byte;

    // 5. Результат
    if (slice_len <= 255) {
        uint8_t out[257];
        out[0] = 0x0E;
        out[1] = (uint8_t)slice_len;
        memcpy(&out[2], slice_start, slice_len);
        stack_push(out, 2 + slice_len);
    } else {
        if (data_ptr + slice_len > DATA_POOL_SIZE) { pushStringRaw(""); return; }
        uint16_t a = data_ptr;
        memcpy(&data_pool[a], slice_start, slice_len);
        data_ptr += slice_len;
        uint8_t hdr[6] = {17,
            (uint8_t)(a & 0xFF), (uint8_t)(a >> 8),
            (uint8_t)(slice_len & 0xFF), (uint8_t)(slice_len >> 8),
            4};
        stack_push(hdr, 6);
    }
}
static bool part_range(const char* name, bool as_string,
                       uint16_t &addr, uint16_t &out_len, uint8_t &tp) {
    if (stack_is_empty()) {
        currentOutput->printf("%s: array expected\n", name);
        return false;
    }
    uint8_t* top = &stack_mem[stack_ptr];
    if ((top[0] != 17 && top[0] != 20) || elem_size(top) != 6) {
        currentOutput->printf("%s: array expected\n", name);
        return false;
    }
    uint16_t base  = top[1] | (top[2] << 8);
    uint16_t count = top[3] | (top[4] << 8);
    tp = top[5];
    uint8_t esz = type_registry[tp].size;
    if (esz == 0) esz = 1;
    stack_ptr += 6;

    uint32_t a = 0, b = 0;
    if (!popUInt32(a) || !popUInt32(b)) {
        currentOutput->printf("%s: two bounds expected\n", name);
        return false;
    }
    uint32_t start = (a < b) ? a : b;
    uint32_t end   = (a < b) ? b : a;
    uint32_t len   = end - start;

    if (as_string && esz != 1) {
        currentOutput->printf("%s: string part expects u8/i8 array\n", name);
        return false;
    }

    if (start > count) start = count;
    if (end   > count) end   = count;
    if (end < start)   end   = start;
    len = end - start;

    uint32_t byte_off = start * (uint32_t)esz;
    uint32_t addr32   = (uint32_t)base + byte_off;
    if (addr32 > DATA_POOL_SIZE) {
        currentOutput->printf("%s: address out of data_pool\n", name);
        return false;
    }
    uint32_t byte_len = len * (uint32_t)esz;
    if (addr32 + byte_len > DATA_POOL_SIZE) {
        currentOutput->printf("%s: range out of data_pool\n", name);
        return false;
    }

    // 🔧 БЫЛО: if (as_string && byte_len > 255) { error; return false; }
    // ТЕПЕРЬ: просто отдаём реальную длину, вызывающий сам решит что делать

    addr = (uint16_t)addr32;
    out_len = as_string ? (uint16_t)byte_len : (uint16_t)len;
    return true;
}
void word_part() {
    uint16_t addr;
    uint16_t len;
    uint8_t tp;

    if (!part_range("part", false, addr, len, tp)) {
        return;
    }

    uint8_t out[6] = {
        20,
        (uint8_t)(addr & 0xFF),
        (uint8_t)(addr >> 8),
        (uint8_t)(len & 0xFF),
        (uint8_t)(len >> 8),
        tp
    };

    stack_push(out, 6);
}
void word_part_str() {
    uint16_t addr;
    uint16_t len;   // реальная длина среза в байтах
    uint8_t tp;
    if (!part_range("part$", true, addr, len, tp)) {
        return;
    }

    // 🔑 Определяем наличие остатка
    bool has_remainder = (len > 255);
    uint16_t out_len = has_remainder ? 255 : len;

    // 🔹 Кладём $STRING (первые 255 байт или весь срез)
    uint8_t str_out[4] = {
        15,
        (uint8_t)out_len,
        (uint8_t)(addr & 0xFF),
        (uint8_t)(addr >> 8)
    };
    stack_push(str_out, 4);

    // 🔹 Кладём BOOL ПОВЕРХ (Вариант А — удобно для if { ... })
    pushBool(has_remainder);
}
static void focusTo(const char* name) {
    char cmd[72];
    snprintf(cmd, sizeof(cmd), "%s focus", name);
    executeLine(cmd);
}
void word_exists() {
    if (stack_is_empty()) {
        currentOutput->println(getMsg("exists: stack empty"));
        pushBool(false);
        return;
    }
    uint8_t* top = &stack_mem[stack_ptr];
    uint8_t tag = top[0];
    char fname[257];
    uint8_t len = 0;

    if (tag == 0x0E || tag == 0x0D) {          // STRING / NAME
        len = top[1];
        if (len > 255) len = 255;
        memcpy(fname, &top[2], len);
        fname[len] = '\0';
        stack_ptr += elem_size(top);
    }
    else if (tag == 15) {                      // $STRING
        len = top[1];
        uint16_t addr = top[2] | (top[3] << 8);
        if (addr + len <= DATA_POOL_SIZE) {
            if (len > 255) len = 255;
            memcpy(fname, &data_pool[addr], len);
            fname[len] = '\0';
        }
        stack_ptr += elem_size(top);
    }
    else {
        currentOutput->println(getMsg("exists: expected STRING/NAME/$STRING"));
        pushBool(false);
        return;
    }

    char full_path[256];
    build_full_path(full_path, sizeof(full_path), fname);

    pushBool(FILESYSTEM.exists(full_path));
}
void word_fs_size() {
    if (stack_is_empty()) { pushUInt32(0); return; }
    uint8_t* top = &stack_mem[stack_ptr];
    uint8_t tag = top[0];
    char fname[257];
    uint8_t len = 0;

    if (tag == 0x0E || tag == 0x0D) {
        len = top[1];
        if (len > 255) len = 255;
        memcpy(fname, &top[2], len);
        fname[len] = '\0';
        stack_ptr += elem_size(top);
    }
    else if (tag == 15) {
        len = top[1];
        uint16_t addr = top[2] | (top[3] << 8);
        if (addr + len <= DATA_POOL_SIZE) {
            if (len > 255) len = 255;
            memcpy(fname, &data_pool[addr], len);
            fname[len] = '\0';
        }
        stack_ptr += elem_size(top);
    }
    else { pushUInt32(0); return; }

    char full_path[256];
    build_full_path(full_path, sizeof(full_path), fname);

    if (!FILESYSTEM.exists(full_path)) { pushUInt32(0); return; }

    File f = FILESYSTEM.open(full_path, "r");
    if (!f) { pushUInt32(0); return; }

    uint32_t sz = (uint32_t)f.size();
    f.close();

    pushUInt32(sz);
}
void word_toStr() {
    if (stack_is_empty()) return;
    uint8_t* top = &stack_mem[stack_ptr];
    uint8_t tag = top[0];
    if (tag < 4 || tag > 11) return;
    uint16_t sz = elem_size(top);

    char buf[32];
    if (tag == 11) {
        float f;
        memcpy(&f, &top[1], 4);
        snprintf(buf, sizeof(buf), "%g", f);
    }
    else if (tag == 10 || tag == 7 || tag == 5) {
        int32_t v = decode_int_le(&top[1], sz - 1, tag);
        snprintf(buf, sizeof(buf), "%ld", (long)v);
    }
    else {
        uint32_t v = decode_uint_le(&top[1], sz - 1);
        snprintf(buf, sizeof(buf), "%lu", (unsigned long)v);
    }

    stack_ptr += sz;
    pushStringRaw(buf);
}
void word_name() {
    // 1. Захват имени слова из R2L-потока (предыдущий токен)
    int idx = g_tok_idx - 1;
    if (idx < 0) { currentOutput->println("name: no word before"); return; }

    uint8_t len = g_tok_len[idx];
    if (len == 0 || len > 63) { currentOutput->println("name: invalid name"); return; }

    char tname[64];
    memcpy(tname, g_tok_start[idx], len);
    tname[len] = '\0';

    // 2. Ищем слово в словаре
    uint16_t addr = dict_find(tname);
    if (addr == 0xFFFF) {
        currentOutput->print("name: not found: ");
        currentOutput->println(tname);
        return;
    }

    // 3. Копируем имя из dict_pool на стек как STRING (0x0E)
    uint8_t nlen = dict_pool[addr + 2];
    if (nlen > 255) nlen = 255;
    uint8_t out[257];
    out[0] = 0x0E;
    out[1] = nlen;
    if (nlen > 0) memcpy(&out[2], &dict_pool[addr + 3], nlen);
    stack_push(out, 2 + nlen);

    // 4. Пропускаем токен имени в R2L-цикле
    g_tok_idx -= 1;
}
void caseConvertFunc() {
  uint8_t direction;  // 0 = toLower, 1 = toUpper
  if (!popUInt8(direction) || direction > 1) return;
  
  if (stack_is_empty()) return;
  uint8_t* top = &stack_mem[stack_ptr];
  uint8_t tag = top[0];
  
  uint8_t* data = nullptr;
  uint16_t len = 0;
  
  // Определяем где лежат данные
  if (tag == 0x0E || tag == 0x0D) {          // STRING / NAME
    data = &top[2];
    len = top[1];
  } else if (tag == 15) {                    // $STRING
    len = top[1];
    uint16_t a = top[2] | (top[3] << 8);
    if (a + len <= DATA_POOL_SIZE) data = &data_pool[a];
  } else {
    return;  // не текстовый тип
  }
  
  if (!data || len == 0) return;
  
  // Модифицируем напрямую in-place
  for (uint16_t i = 0; i < len; i++) {
    if (direction == 1) {  // toUpper
      if (data[i] >= 'a' && data[i] <= 'z') {
        data[i] -= 32;
      }
      else if (i + 1 < len && data[i] == 0xD0 && data[i+1] >= 0xB0 && data[i+1] <= 0xBF) {
        data[i+1] -= 0x20;
        i++;
      }
      else if (i + 1 < len && data[i] == 0xD1 && data[i+1] >= 0x80 && data[i+1] <= 0x8F) {
        data[i] = 0xD0;
        data[i+1] += 0x20;
        i++;
      }
      else if (i + 1 < len && data[i] == 0xD1 && data[i+1] == 0x91) {
        data[i] = 0xD0;
        data[i+1] = 0x81;
        i++;
      }
    } else {  // toLower
      if (data[i] >= 'A' && data[i] <= 'Z') {
        data[i] += 32;
      }
      else if (i + 1 < len && data[i] == 0xD0 && data[i+1] >= 0x90 && data[i+1] <= 0x9F) {
        data[i+1] += 0x20;
        i++;
      }
      else if (i + 1 < len && data[i] == 0xD0 && data[i+1] >= 0xA0 && data[i+1] <= 0xAF) {
        data[i] = 0xD1;
        data[i+1] -= 0x20;
        i++;
      }
      else if (i + 1 < len && data[i] == 0xD0 && data[i+1] == 0x81) {
        data[i] = 0xD1;
        data[i+1] = 0x91;
        i++;
      }
    }
  }
}
// В net.ino добавить:
void word_out_save() {
    uint32_t ptr = (uint32_t)(uintptr_t)currentOutput;
    pushUInt32(ptr);
}


void sha1Func() {
    // R2L: приёмник данные sha1
    // Стек (сверху вниз): приёмник (ARRAY u8 >= 20), данные
    
    // 1. Снимаем приёмник
    if (stack_is_empty()) { pushBool(false); return; }
    uint8_t* dst = &stack_mem[stack_ptr];
    if ((dst[0] != 17 && dst[0] != 20) || elem_size(dst) != 6) {
        stack_ptr += elem_size(dst); pushBool(false); return;
    }
    uint16_t dst_base = dst[1] | (dst[2] << 8);
    uint16_t dst_len  = dst[3] | (dst[4] << 8);
    uint8_t  dst_esz  = type_registry[dst[5]].size;
    uint32_t dst_total = (uint32_t)dst_len * dst_esz;
    stack_ptr += 6;
    
    if (dst_total < 20 || dst_base + 20 > DATA_POOL_SIZE) {
        pushBool(false); return;
    }
    
    // 2. Снимаем данные
    if (stack_is_empty()) { pushBool(false); return; }
    uint8_t* top = &stack_mem[stack_ptr];
    const uint8_t* data = nullptr;
    uint16_t len = 0;
    
    if (top[0] == 0x0E || top[0] == 0x0D) {
        data = &top[2]; len = top[1];
    } else if (top[0] == 15) {
        len = top[1];
        uint16_t a = top[2] | (top[3] << 8);
        if (a + len <= DATA_POOL_SIZE) data = &data_pool[a];
    } else if (top[0] == 17 || top[0] == 20) {
        uint16_t base = top[1] | (top[2] << 8);
        uint16_t count = top[3] | (top[4] << 8);
        uint8_t esz = type_registry[top[5]].size;
        len = count * esz;
        if (base + len <= DATA_POOL_SIZE) data = &data_pool[base];
    }
    stack_ptr += elem_size(top);
    
    if (!data || len == 0) { pushBool(false); return; }
    
    // 3. SHA-1 ПРЯМО В ПРИЁМНИК — data_ptr НЕ МЕНЯЕТСЯ!
    mbedtls_sha1_context ctx;
    mbedtls_sha1_init(&ctx);
    mbedtls_sha1_starts(&ctx);
    mbedtls_sha1_update(&ctx, data, len);
    mbedtls_sha1_finish(&ctx, &data_pool[dst_base]);
    mbedtls_sha1_free(&ctx);
    
    pushBool(true);
}
// ============================================================
// === uuid5 : данные → STRING "xxxxxxxx-xxxx-5xxx-yxxx-..." ===
// SHA-1 буфер — локальный на C-стеке, приёмник НЕ нужен.
// На выходе: STRING (тег 0x0E) длиной 36 байт прямо на стеке HDL.
// ============================================================
void uuid5Func() {
    if (stack_is_empty()) { pushStringRaw(""); return; }
    uint8_t* top = &stack_mem[stack_ptr];

    // 1. Извлекаем сырые байты из элемента на стеке
    const uint8_t* data = nullptr;
    uint16_t len = 0;

    if (top[0] == 0x0E || top[0] == 0x0D) {          // STRING / NAME
        data = &top[2];
        len = top[1];
    } else if (top[0] == 15) {                       // $STRING
        len = top[1];
        uint16_t a = top[2] | (top[3] << 8);
        if (a + len <= DATA_POOL_SIZE) data = &data_pool[a];
    } else if (top[0] == 17 || top[0] == 20) {       // ARRAY / REF_ARR
        uint16_t base = top[1] | (top[2] << 8);
        uint16_t count = top[3] | (top[4] << 8);
        uint8_t esz = type_registry[top[5]].size;
        len = count * esz;
        if (base + len <= DATA_POOL_SIZE) data = &data_pool[base];
    } else {
        stack_ptr += elem_size(top);
        pushStringRaw("");
        return;
    }
    stack_ptr += elem_size(top);

    if (!data || len == 0) { pushStringRaw(""); return; }

    // 2. SHA-1 в ЛОКАЛЬНОМ буфере на C-стеке (не в data_pool!)
    uint8_t hash[20];
    mbedtls_sha1_context ctx;
    mbedtls_sha1_init(&ctx);
    mbedtls_sha1_starts(&ctx);
    mbedtls_sha1_update(&ctx, data, len);
    mbedtls_sha1_finish(&ctx, hash);
    mbedtls_sha1_free(&ctx);

    // 3. Версия 5 (старшие 4 бита байта 6) и вариант 10xx (байт 8)
    hash[6] = (hash[6] & 0x0F) | 0x50;
    hash[8] = (hash[8] & 0x3F) | 0x80;

    // 4. Форматируем UUID 8-4-4-4-12 в локальный буфер
    char buf[37];
    snprintf(buf, sizeof(buf),
        "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
        hash[0], hash[1], hash[2], hash[3],
        hash[4], hash[5],
        hash[6], hash[7],
        hash[8], hash[9],
        hash[10], hash[11], hash[12], hash[13], hash[14], hash[15]);

    // 5. Кладём как STRING (тег 0x0E) прямо на стек HDL
    pushStringRaw(buf);
}
void word_exec() {
    if (stack_is_empty()) {
        currentOutput->println("exec: stack empty");
        return;
    }
    uint8_t* top = &stack_mem[stack_ptr];
    uint8_t tag = top[0];
    char name[65];
    uint8_t len = 0;
    
    // STRING (0x0E) / NAME (0x0D)
    if (tag == 0x0E || tag == 0x0D) {
        len = top[1];
        if (len > 64) len = 64;
        memcpy(name, &top[2], len);
        name[len] = '\0';
        stack_ptr += elem_size(top);
    }
    // $STRING (15)
    else if (tag == 15) {
        len = top[1];
        uint16_t addr = top[2] | (top[3] << 8);
        if (addr + len <= DATA_POOL_SIZE) {
            if (len > 64) len = 64;
            memcpy(name, &data_pool[addr], len);
            name[len] = '\0';
        } else {
            name[0] = '\0';
        }
        stack_ptr += elem_size(top);
    }
    else {
        currentOutput->println("exec: expected STRING/NAME/$STRING");
        return;
    }
    
    if (len == 0) {
        currentOutput->println("exec: empty name");
        return;
    }
    
    // 🔑 Ищем слово в словаре
    uint16_t addr = dict_find(name);
    if (addr == 0xFFFF) {
        currentOutput->print("exec: word not found: ");
        currentOutput->println(name);
        return;
    }
    
    // 🔑 Прямой вызов — быстрее и чище
    exec_word(addr);
}
// === ДИАГНОСТИКА ПОТОКА ВЫВОДА ===
// 🔑 КРИТИЧНО: всегда выводит в Serial, независимо от currentOutput!
// Это аварийный канал — когда основной вывод «улетел», Serial остаётся
// единственным надёжным способом увидеть, что происходит.
void word_out_query() {
    Serial.println();
    Serial.println("=== out? DIAGNOSTIC ===");
    
    if (currentOutput == &Serial) {
        Serial.println("out: Serial");
    }
    else if (currentOutput == &g_outFile) {
        Serial.println("out: FILE (g_outFile)");
    }
    else if (currentOutput == &g_stream) {
        Serial.print("out: g_stream -> ");
        Serial.println(g_stream.targetName());
    }
    else {
        Serial.print("out: UNKNOWN @ 0x");
        Serial.println((uint32_t)(uintptr_t)currentOutput, HEX);
    }
    
    // Дополнительно: состояние g_stream
    Serial.print("g_stream.target = ");
    Serial.println(g_stream.targetName());
    
    // Состояние файла
    Serial.print("g_outFile open: ");
    Serial.println(g_outFile ? "YES" : "NO");
    
    Serial.println("=======================");
    Serial.println();
}
// Объявляем функцию из net.ino
extern void udp_finalize_streaming();

void word_out_restore() {
    uint32_t ptr = 0;
    if (!popUInt32(ptr)) return;
    
    // 🔑 КЛЮЧЕВОЕ: перед восстановлением канала — финализируем UDP-стриминг
    udp_finalize_streaming();
    
    // Закрываем файл ТОЛЬКО если он был активным каналом
    if (g_outFile && currentOutput == &g_outFile) {
        g_outFile.close();
    }
    
    currentOutput = (Print*)(uintptr_t)ptr;
}
void setup() {
  Serial.begin(115200);
  Serial.println(); delay(500);
      // 🔑 КРИТИЧНО: выделяем пулы в heap ПЕРЕД любым использованием
    if (!init_pools()) {
        Serial.println(F("FATAL: heap allocation failed for pools"));
        Serial.printf("Need: %u bytes, free: %u bytes\n",
            (unsigned)(STACK_SIZE + DICT_POOL_SIZE + DATA_POOL_SIZE + RSTACK_SIZE),
            (unsigned)ESP.getFreeHeap());
        while (true) { delay(1000); }  // зависаем — дальше работать нельзя
    }
    Serial.printf("pools allocated: %u bytes from heap (free: %u)\n",
        (unsigned)(STACK_SIZE + DICT_POOL_SIZE + DATA_POOL_SIZE + RSTACK_SIZE),
        (unsigned)ESP.getFreeHeap());
  for (int i = 0; i <= 255; i++) Serial.println();
  stack_clear();
  if (!FILESYSTEM.begin()) currentOutput->println("FS Mount Failed");
  // === БАЗОВЫЕ И СИСТЕМНЫЕ СЛОВА ===
  addInternalWord("lit", wordLit);               // Внутренний механизм: кладёт литерал (число/строку) из байт-кода на стек.
      // ==========================================================
    // === ИНИЦИАЛИЗАЦИЯ ПЕРЕМЕННОЙ ЯЗЫКА (lang) ===
    // ==========================================================
    // 1. Размещаем начальное значение "ru" в data_pool
    const char* init_lang = "ru";
    uint8_t lang_len = strlen(init_lang);
    uint16_t lang_data_addr = data_ptr;
    memcpy(&data_pool[data_ptr], init_lang, lang_len);
    data_ptr += lang_len;

    // 2. Формируем тело переменной как $STRING (тег 15)
    // Формат: [тег 15][длина][адрес_lo][адрес_hi]
    uint8_t lang_body[4] = {15, lang_len, (uint8_t)(lang_data_addr & 0xFF), (uint8_t)(lang_data_addr >> 8)};

    // 3. Формируем имя: NAME (тег 0x0D) -> [0x0D][len]['l']['a']['n']['g']
    uint8_t lang_name[6] = {0x0D, 4, 'l', 'a', 'n', 'g'}; 

    // 4. Создаем внутреннее слово-переменную "lang"
    // flags = 0x06 (0x02 = VAR | 0x04 = INTERNAL)
        create_internal_word(lang_name, lang_body, sizeof(lang_body), 0x06, choiceFunc);
  addInternalWord("abort", word_abort);          // Аварийная кнопка: мгновенно очищает стеки, останавливает задачи и возвращает вывод в Serial.
  addInternalWord("nop",  wordNop);              // Пустая операция. Используется как маркер конца цепочки (cord) или для выравнивания.
    addInternalWord("?", word_help);
  addInternalWord("words", word_words);          // Выводит список всех доступных слов в текущем контексте (фокусе внимания).
  addInternalWord("all", word_words_all);   // ← ДОБАВИТЬ
  addInternalWord("exec", word_exec);
  addInternalWord("sha1",  sha1Func);
  addInternalWord("uuid5", uuid5Func);
  addInternalWord("<--", word_checkpoint);
  addInternalWord("xxx", word_forget);
  addInternalWord("main", word_main);            // Переключает фокус внимания в корневой (глобальный) контекст.
  addInternalWord("focus",  word_cont);           // Создаёт новый контекст или переключает фокус внимания на существующий.
  addInternalWord("device", word_device);        // Создаёт новое слово-устройство (переменную состояния) с уникальным внутренним ID.
  addInternalWord("reset", word_reset);          // Полная программная перезагрузка микроконтроллера.
  addInternalWord("as", word_as);                // Явное связывание: создаёт алиас (синоним). R2L: берёт 'source', связывает с 'new_name'.
  executeLine("nop as var");                     // Трюк: делаем 'nop' доступным как псевдо-переменная для инициализации пустых цепочек.
  executeLine("focus as cont");  
  addInternalWord("const", word_const);          // Помечает последнее созданное слово как константу (запрет на изменение).
  executeLine("? as help"); 
  addInternalWord("print", word_print);
  // === УПРАВЛЕНИЕ ПОТОКОМ ИСПОЛНЕНИЯ ===
  addInternalWord("if", word_if);                // Ветвление: если верх стека истинен, исполняет тело, иначе перепрыгивает на адрес после '}'.
  addInternalWord("while", word_while);          // Цикл: проверяет условие на стеке. Если истинно, исполняет тело и возвращается к проверке.
  addInternalWord("goto",  word_goto);           // Безусловный переход к указанному адресу в байт-коде.
  addInternalWord("exit", word_exit);            // Досрочный выход из текущего скомпилированного слова (возврат по стеку вызовов).
 
  // === МАНИПУЛЯЦИЯ СТЕКОМ ===
  addInternalWord("->",    word_drop);           // Удаление верхнего элемента со стека (очистка мусора).
  addInternalWord("dup", word_dup);              // Дублирование верхнего элемента стека.
  addInternalWord("swap", word_swap);            // Меняет местами два верхних элемента стека (критично для R2L-построения цепочек).
  addInternalWord("check", word_check);          // Проверка: кладёт 1 (true), если стек пуст, иначе 0 (false).
  addInternalWord("oops",  stack_clear);         // Мягкая очистка стека данных (без остановки задач, в отличие от abort).

  // === ДИАГНОСТИКА И ОТЛАДКА ===
  addInternalWord("heap", word_heap);            // Возвращает количество свободной динамической памяти (ESP.getFreeHeap) на стек.
  addInternalWord("mem",  word_mem);             // Выводит статистику использования памяти: dict_pool, data_pool и heap.
  addInternalWord("`",     word_dict_dump);      // Дамп словаря: выводит сырые байты всех слов в памяти (для низкоуровневой отладки ядра).
  addInternalWord("hex", word_hexdump);          // Шестнадцатеричный дамп указанной области памяти словаря.
  addInternalWord("stack", printStack);          // Выводит текущее содержимое стека данных в человекочитаемом виде (с тегами типов).
  addInternalWord("locals", word_lwords);        // Выводит список локальных переменных текущего фрейма вызова.
  addInternalWord("body", word_body);            // Выводит внутреннее представление (байт-код) тела указанного слова. R2L: `word_name body`.

  // === КОМПИЛЯЦИЯ И СТРУКТУРЫ ДАННЫХ ===
  addInternalWord(":", word_colon);              // Начало компиляции: открывает режим записи нового слова в словарь.
  addInternalWord(";", word_semicolon);          // Конец компиляции: закрывает режим записи и связывает слово в списке.
  addInternalWord("cord", word_cord);            // Создаёт новую пустую "нить" (цепочку действий) с указанным именем.
  addInternalWord("bead", word_bead);            // Нанизывает "бусину" (действие) на существующую "нить" (cord).
  addInternalWord("pool>", word_pool_dump);      // Дамп области данных (data_pool), где хранятся массивы и строки.
  addInternalWord("view", word_view);            // Создаёт новое "представление" (view) существующего массива с другим типом данных, не копируя память.
  addInternalWord("meta", word_meta);
  create_internal_word_str("chip", getChipName(), 0x06, choiceFunc); // Создаёт константу 'chip', возвращающую название чипа (esp32, esp32s3 и т.д.).
  focusTo("main");
  // === КОНТЕКСТ: STREAMS (Перенаправление вывода) ===
  focusTo("streams");
  addInternalWord("out>file", word_out_file);    // Перенаправляет весь последующий вывод (print, stack и т.д.) в указанный файл.
  addInternalWord("out>serial",  word_out_serial);
  addInternalWord("out>save",    word_out_save);
  addInternalWord("out>restore", word_out_restore);
  addInternalWord("out?",        word_out_query); //
  focusTo("main");
  focusTo( "strings");
  addInternalWord("toUpper", []() { pushUInt8(1); caseConvertFunc(); });
  addInternalWord("toLower", []() { pushUInt8(0); caseConvertFunc(); });
  addInternalWord("find", word_find); 
  addMarkerWord("(-");
  addMarkerWord("-)");
  addInternalWord("between", word_between);
  addInternalWord("name", word_name);
  focusTo("main");
    // === КОНТЕКСТ: JSON (Экспорт/Импорт структур) ===
  focusTo("jsons");                     // Переключаем фокус внимания на контекст работы с JSON.
  addInternalWord("json>", word_json_export);    // Экспортирует все переменные текущего контекста в формат JSON.
  addInternalWord("json*>",     word_json_star_export);   // ← ДОБАВИТЬ
  addInternalWord("json>>", word_json_delta);    // Экспортирует только изменённые (dirty) переменные в JSON (оптимизация трафика).
  addInternalWord("json>serial", word_json_export_serial); // Принудительно выводит JSON-дампа контекста в Serial (игнорируя перенаправление).
  addInternalWord("json>file", word_json_export_file);     // Сохраняет JSON-дампа контекста в указанный файл.
  addInternalWord("json-set", jsonSetWord);      // Добавляет пару "ключ:значение" во внутренний буфер сборки JSON.
  addInternalWord("json>var", jsonToVarWord);    // Парсит накопленный JSON-буфер и создаёт из него переменные в текущем контексте.
  focusTo("main");

  // === КОНТЕКСТ: TIMES (Время и планирование) ===
  focusTo("times");                     // Переключаем фокус внимания на контекст времени и планирования.
  addInternalWord("delayMicroseconds", delayMicrosecondsWord); // Аппаратная микросекундная задержка (только для критических таймингов, не для логики).
  addInternalWord("millis", word_millis);        // Возвращает время в мс с момента запуска (основа для неблокирующих проверок вместо delay).
  addInternalWord("+task", word_add_task);       // Планирует фоновое выполнение слова с заданным интервалом (не блокирует основной поток).
  addInternalWord("-task", word_remove_task);    // Отменяет выполнение ранее запланированной фоновой задачи.
  addInternalWord("+loop", word_add_loop);       // Запускает слово в непрерывном фоновом цикле (интервал = 0).
  addInternalWord("-loop", word_remove_loop);    // Останавливает непрерывный фоновый цикл.
  addInternalWord("__schedule_task__", word_schedule_task_runtime); // Внутренний механизм: регистрация задачи из скомпилированного байт-кода.
  addInternalWord("__remove_task__", word_remove_task_runtime);     // Внутренний механизм: удаление задачи из скомпилированного байт-кода.
  addInternalWord("__local__", word_local_stub); // Внутренний резолвер: создаёт или извлекает локальную переменную внутри слова.
  focusTo("main");

  // === УТИЛИТЫ ДАННЫХ ===
  addInternalWord("len", lenWord);               // Измеряет длину строки или массива на вершине стека и удаляет исходный элемент.
  addInternalWord("len!", lenPeekWord);          // Измеряет длину строки или массива, оставляя исходный элемент на стеке.
  addInternalWord("copy", word_copy);            // Копирует данные из источника в приёмник (поддерживает скаляры и циклические массивы).
  addInternalWord("rgb2grb", word_rgb2grb);      // Преобразует порядок байт цвета из RGB в GRB (для лент WS2812 и подобных).
  addInternalWord("rgb2wrgb", word_rgb2wrgb);    // Преобразует RGB в WRGB (SK6812), вычисляя белый канал как min(R, G, B).
  addInternalWord("part", word_part);            // Создаёт срез (представление) массива: part массив смещение длина. Возвращает ссылку на фрагмент без копирования памяти.
  addInternalWord("part$", word_part_str);       // Извлекает часть байтового массива как строку: part$ массив смещение длина. Возвращает строку ($STRING) из указанного диапазона и признак длинна больше 255.
  focusTo("main");

  // === КОНТЕКСТ: TYPES (Преобразование типов) ===
  executeLine("types cont");                     // Переключаем фокус внимания на контекст преобразования типов данных.
  addMarkerWord("u8"); addMarkerWord("u24"); addMarkerWord("u32"); addMarkerWord("u16"); // Маркеры типов для явного указания формата.
  addMarkerWord("i8"); addMarkerWord("i16"); addMarkerWord("i32"); addMarkerWord("f");    
  addMarkerWord("$S");                           // Разрешаем использование $S (ссылка на строку) как типа элемента массива.
  addMarkerWord("["); addMarkerWord("]");        // Маркеры начала и конца массива.
  addMarkerWord("array"); addMarkerWord("@");    // Маркеры для работы с массивами и ссылками.
  addInternalWord("#", word_addr_of);            // Оператор адреса: кладёт адрес (ADDR) указанного слова на стек. Берёт имя из R2L-потока (REPL) или читает следующий адрес (в байт-коде).
  addInternalWord("toChar", word_num_to_char);   // ПРЕОБРАЗОВАТЕЛЬ: берёт числовой код со стека и превращает его в строку из 1 символа.
  addInternalWord("toNum",  word_char_to_num);   // ПРЕОБРАЗОВАТЕЛЬ: берёт строку/символ со стека и превращает его в числовой ASCII-код.
  addInternalWord("toInt", word_toInt);          // ПРЕОБРАЗОВАТЕЛЬ: парсит текстовую строку (напр. "42") и кладёт на стек как число (INT32).
  addInternalWord("toStr", word_toStr);          // Преобразует числовое значение (целое или float) в текстовую строку (STRING).
  addMarkerWord("word");                         // Маркер типа «word» (ссылка на слово). Используется для объявления массивов, хранящих адреса других слов.
  focusTo("main");



  // === КОНТЕКСТ: MATH (Математика и логика) ===
  focusTo("math");                      // Переключаем фокус внимания на математические операции.
  addInternalWord("randRange", word_randRange);  // Генерирует случайное число в диапазоне [min, max]. R2L: `randRange min max`.
  addInternalWord("!", wordNot);                 // Логическое НЕ: инвертирует числовое значение на стеке (0 → 1, ненулевое → 0).
  addMarkerWord("-"); addMarkerWord("+"); addMarkerWord("="); addMarkerWord("/"); addMarkerWord("*"); // Математические операторы.
  addMarkerWord("%"); addMarkerWord("^"); addMarkerWord("<<"); addMarkerWord(">>");                 // Битовые операторы.
  addMarkerWord("+="); addMarkerWord("-="); addMarkerWord("*="); addMarkerWord("/=");               // Операторы присваивания с действием.
  addMarkerWord("&"); addMarkerWord("|");                                                           // Битовые И и ИЛИ.
  addMarkerWord(")");                            // Закрывающая скобка для группировки операций.
  addInternalWord("(", word_open_paren);         // Открывающая скобка: инициирует свёртку выражения внутри скобок до получения результата.
  focusTo("main");

  // === КОНТЕКСТ: LOGICS (Сравнение) ===
  focusTo("logics");                    // Переключаем фокус внимания на логические операции сравнения.
  addMarkerWord("=="); addMarkerWord("!="); addMarkerWord("<"); addMarkerWord(">");         // Операторы сравнения (возвращают 1 или 0).
  addMarkerWord("<="); addMarkerWord(">=");                                                   // Операторы нестрогого сравнения.
  focusTo("main");

  // === КОНТЕКСТ: FS / IO (Файловая система и вывод) ===
  focusTo("fs");                        // (Примечание: здесь логичнее было бы "fs" или "io", но сохраняем структуру исходника).
  addInternalWord("ls", word_ls);                // Выводит список файлов и директорий в текущем пути файловой системы.
  executeLine("ls as dir");                      // Создаём удобный алиас: теперь 'dir' делает то же самое, что и 'ls'.
  addInternalWord("cd", word_cd);                // Меняет текущую рабочую директорию файловой системы.
  addInternalWord("type", word_type);            // Выводит содержимое текстового файла на экран (аналог Unix 'cat' или 'type').
  executeLine("type as cat"); 
  addInternalWord("exists", word_exists);
  addInternalWord("fs.size", word_fs_size);
  addInternalWord("load", word_load);            // Загружает и исполняет команды из указанного .wrd файла построчно (обучение).
  addInternalWord("load?", word_load_query);
  addInternalWord( "save.core", word_save);      // Сохраняет полный снимок состояния (словарь, стеки, данные) в файл (фиксация навыка).
  addInternalWord("restore.core", word_restore);      // Восстанавливает состояние системы из ранее сохранённого файла снимка.
  // addInternalWord("serial",     word_serial);      // Создаёт/открывает: serial uart 1 115200
  // addInternalWord("serialset",  word_serialset);   // Перенастраивает: serialset uart 1 115200 16 17
  // addInternalWord("serialstop", word_serialstop);  // Останавливает: serialstop uart
  addInternalWord("open",   word_open);     // cfg open file config.txt 1
  addInternalWord("write",   word_write);
  create_self_address_word("file");




  gpioInit();
  ethInit(); 
  wifiInit();
  webInit();
  udpInit();
  tcpInit();
  rmtModuleInit();
  i2sInit();
  i2cInit();
#if ENABLE_TERM_LAYER
  addInternalWord("term", word_term);
#endif
// === ЗАЩИТА ОТ BOOT-LOOP ===
#if defined(ESP32)
esp_reset_reason_t reason = esp_reset_reason();
Serial.println(reason);
if (!(reason == 5 || reason == 4))crashCounter = 0;
Serial.println(crashCounter);
crashCounter++;
#endif

// 🔒 Безопасная загрузка стартового файла
if (FILESYSTEM.exists("/startup.wrd")) {
#if defined(ESP32)
    if (crashCounter > 5) {
        currentOutput->println("⚠️ boot: 5+ crashes, skipping startup.wrd");
    } else {
         executeLine("load startup.wrd main");
    }
#else
    executeLine("load startup.wrd main");
#endif
}
  //executeLine("load ex/net/net.words main");
  printStack();
  printActiveTasks(); // ← СПИСОК ЗАДАЧ
  word_prompt();

}

void loop() {
  // 1. Кооперативное исполнение фоновых задач
  process_tasks(false);

  // 2. Обработка Serial (ваш оригинальный код)
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
        printActiveTasks();
        word_prompt();
      }
      idx = 0;
    } else if (c >= 32 && idx < 255) {
      line_buf[idx++] = c;
    }
  }



// 3. ✅ ДЕТЕРМИНИРОВАННАЯ обработка WebSocket
if (ws_cmd_ready) {
    ws_cmd_ready = false;
    
    // 🔑 Временно переключаем currentOutput на g_stream
    Print* saved = currentOutput;
    currentOutput = &g_stream;
    
    currentOutput->println();
    currentOutput->print(ws_cmd); 
    currentOutput->println();
    executeLine(ws_cmd);
    currentOutput->println();
    printStack();
    printActiveTasks();
    word_prompt();
    g_stream.flush();
    
    // 🔑 Восстанавливаем currentOutput (обычно обратно в Serial)
    currentOutput = saved;
    
    // ❌ НЕ вызываем g_stream.detach() — WS клиент всё ещё подключен!
}
  // 4. Обслуживание библиотеки WebSocket (пинг/понг, разрывы)
  // ⚠️ ВАЖНО: Это должно вызываться здесь, а не через +loop в startup.wrd
  wsServer.loop();
}
