#include "driver/rmt.h"
// ГЛОБАЛЬНЫЕ ПЕРЕМЕННЫЕ RTM (вставить в самый верх файла RTM.ino)
bool rtmInstalled[8] = {false};          // <-- ВОТ ОНО. Флаг инициализации канала
uint16_t rmt_proto_addr[8] = {0};        // Адрес массива протокола для прерывания
struct RmtBinding {                      // Привязка массивов к каналу
    uint16_t proto_addr;
    uint16_t hdr_addr, hdr_len;
    uint16_t ftr_addr, ftr_len;
    uint16_t data_addr, data_len;
    bool ready;
};
RmtBinding rmt_bind[8] = {0};
static volatile uint8_t active_rtm_ch = 0;


void rtmInit() {
    String tmp = "cont rtm";
    executeLine(tmp);
    
    addInternalWord("rtmInit", rtmInitFunc);
    addInternalWord("rtmClk", rtmClkFunc);
    addInternalWord("rtmMem", rtmMemFunc);
    addInternalWord("rtmCarrier", rtmCarrierFunc);
    addInternalWord("rtmIdle", rtmIdleFunc);
    addInternalWord("rtmLoop", rtmLoopFunc);
    addInternalWord("rtmFilter", rtmFilterFunc);
    addInternalWord("rtmDeinit", rtmDeinitFunc);
    addInternalWord("rtmWrite", rtmWriteFunc);
    addInternalWord("rtmAvailable", rtmAvailableFunc);
    addInternalWord("rtmRead", rtmReadFunc);
    rtmAsyncInit();
    
    tmp = "main";
    executeLine(tmp);
}
// ========================
// RTM: Универсальная асинхронная отправка
// ========================


// --- FORWARD DECLARATION ---
static void IRAM_ATTR translator_callback(const void* src, rmt_item32_t* dest, size_t src_size,
                                          size_t wanted_num, size_t* translated_size, size_t* item_num);

// --- 1. nullArrayFunc: возвращает ADDRINFO(0,0,u8) ---
void nullArrayFunc(uint16_t addr) {
    if (isStackOverflow(7)) { handleStackOverflow(); return; }
    stack[stackTop++] = 0x00;  // addr lo
    stack[stackTop++] = 0x00;  // addr hi
    stack[stackTop++] = 0x00;  // len lo
    stack[stackTop++] = 0x00;  // len hi
    stack[stackTop++] = TYPE_UINT8; // elemType
    stack[stackTop++] = 5;     // len metadata
    stack[stackTop++] = TYPE_ADDRINFO;
}

// --- 2. Вспомогательная: снять ADDRINFO со стека ---
bool popAddrInfo(uint16_t* out_addr, uint16_t* out_len) {
    if (stackTop < 2) return false;
    uint8_t len_meta, type;
    popMetadata(len_meta, type);
    if (type != TYPE_ADDRINFO || len_meta != 5) { stackTop -= len_meta; return false; }
    *out_addr = stack[stackTop - 5] | (stack[stackTop - 4] << 8);
    *out_len  = stack[stackTop - 3] | (stack[stackTop - 2] << 8);
    stackTop -= 5;
    return true;
}

// --- 3. Сеттеры привязок (исправленный порядок pop) ---

// ========================
// ВСПОМОГАТЕЛЬНАЯ: привязка канала + ADDRINFO
// ========================
static bool bindChannelAddr(uint8_t channel, uint16_t addr, uint16_t len,
                           uint16_t* store_addr, uint16_t* store_len,
                           bool mark_ready = false) {
    if (channel >= 8) return false;
    *store_addr = addr;
    *store_len = len;
    if (mark_ready) {
        // ready = true только если уже есть proto_addr и data_addr
        rmt_bind[channel].ready = (rmt_bind[channel].proto_addr != 0 && 
                                   rmt_bind[channel].data_addr != 0);
    }
    return true;
}

// ========================
// СЕТТЕРЫ (по 5 строк каждый)
// ========================

// rtmSetProto: канал ADDRINFO → success
void rtmSetProtoFunc(uint16_t addr) {
    uint8_t ch; uint16_t a, l;
    if (!popAsUInt8(&ch) || ch >= 8) { pushBool(false); return; }
    if (!popAddrInfo(&a, &l)) { pushBool(false); return; }
    rmt_bind[ch].proto_addr = a;
    rmt_proto_addr[ch] = a;  // для прерывания
    pushBool(true);
}

// rtmSetHeader: канал ADDRINFO → success
void rtmSetHeaderFunc(uint16_t addr) {
    uint8_t ch; uint16_t a, l;
    if (!popAsUInt8(&ch) || ch >= 8) { pushBool(false); return; }
    if (!popAddrInfo(&a, &l)) { pushBool(false); return; }
    pushBool(bindChannelAddr(ch, a, l, &rmt_bind[ch].hdr_addr, &rmt_bind[ch].hdr_len));
}

// rtmSetFooter: канал ADDRINFO → success
void rtmSetFooterFunc(uint16_t addr) {
    uint8_t ch; uint16_t a, l;
    if (!popAsUInt8(&ch) || ch >= 8) { pushBool(false); return; }
    if (!popAddrInfo(&a, &l)) { pushBool(false); return; }
    pushBool(bindChannelAddr(ch, a, l, &rmt_bind[ch].ftr_addr, &rmt_bind[ch].ftr_len));
}

// rtmSetData: канал ADDRINFO → success
void rtmSetDataFunc(uint16_t addr) {
    uint8_t ch; uint16_t a, l;
    if (!popAsUInt8(&ch) || ch >= 8) { pushBool(false); return; }
    if (!popAddrInfo(&a, &l)) { pushBool(false); return; }
    pushBool(bindChannelAddr(ch, a, l, &rmt_bind[ch].data_addr, &rmt_bind[ch].data_len, true));
}

// rtmSendBegin: канал → success
// Использует адреса/длины, сохранённые через rtmSet*
void rtmSendBeginFunc(uint16_t addr) {
    uint8_t ch;
    if (!popAsUInt8(&ch) || ch >= 8 || !rtmInstalled[ch]) { pushBool(false); return; }
    
    RmtBinding* b = &rmt_bind[ch];
    if (!b->ready) { pushBool(false); return; }

    // 1. Заголовок (если есть)
    if (b->hdr_addr != 0 && b->hdr_len > 0) {
        rmt_write_items((rmt_channel_t)ch, (rmt_item32_t*)&dataPool[b->hdr_addr], b->hdr_len, false);
    }
    
    // 2. Данные (транслятор: байты → импульсы на лету)
    rmt_proto_addr[ch] = b->proto_addr;  // контекст для прерывания
    rmt_translator_init((rmt_channel_t)ch, translator_callback);
    rmt_write_sample((rmt_channel_t)ch, &dataPool[b->data_addr], b->data_len, false);
    
    // 3. Завершение (если есть)
    if (b->ftr_addr != 0 && b->ftr_len > 0) {
        rmt_write_items((rmt_channel_t)ch, (rmt_item32_t*)&dataPool[b->ftr_addr], b->ftr_len, false);
    }
    
    pushBool(true);  // вернули управление мгновенно
}



// --- 4. translator_callback: байты → импульсы (в прерывании) ---
static void IRAM_ATTR translator_callback(const void* src, rmt_item32_t* dest, size_t src_size,
                                          size_t wanted_num, size_t* translated_size, size_t* item_num) {
    const uint8_t* bytes = (const uint8_t*)src;
    size_t processed = 0, created = 0;
    
    uint8_t ch = active_rtm_ch;
    uint16_t* tpl = (uint16_t*)&dataPool[rmt_proto_addr[ch]]; // 8 u16

    while (processed < src_size && created < wanted_num) {
        uint8_t b = bytes[processed];
        for (int i = 7; i >= 0 && created < wanted_num; i--) {
            uint16_t* t = (b & (1 << i)) ? &tpl[4] : &tpl[0];
            dest[created].level0 = t[0]; dest[created].duration0 = t[1];
            dest[created].level1 = t[2]; dest[created].duration1 = t[3];
            created++;
        }
        processed++;
    }
    *translated_size = processed;
    *item_num = created;
}

void rtmAllBeginFunc(uint16_t addr) {
    bool started = false;
    for (uint8_t ch = 0; ch < 8; ch++) {
        if (rtmInstalled[ch] && rmt_bind[ch].ready) {
            RmtBinding* b = &rmt_bind[ch];
            
            if (b->hdr_addr != 0 && b->hdr_len > 0) {
                rmt_write_items((rmt_channel_t)ch, (rmt_item32_t*)&dataPool[b->hdr_addr], b->hdr_len, false);
            }
            
            active_rtm_ch = ch;  // ← КРИТИЧНО: колбэк узнаёт свой канал
            rmt_translator_init((rmt_channel_t)ch, translator_callback);
            rmt_write_sample((rmt_channel_t)ch, &dataPool[b->data_addr], b->data_len, false);
            
            if (b->ftr_addr != 0 && b->ftr_len > 0) {
                rmt_write_items((rmt_channel_t)ch, (rmt_item32_t*)&dataPool[b->ftr_addr], b->ftr_len, false);
            }
            started = true;
        }
    }
    // pushBool(started);
}
// --- 6. Регистрация слов ---
void rtmAsyncInit() {
    addInternalWord("nullArray", nullArrayFunc);
    addInternalWord("rtmSetProto", rtmSetProtoFunc);
    addInternalWord("rtmSetHeader", rtmSetHeaderFunc);
    addInternalWord("rtmSetFooter", rtmSetFooterFunc);
    addInternalWord("rtmSetData", rtmSetDataFunc);
    addInternalWord("rtmSendBegin", rtmSendBeginFunc);
    addInternalWord("rtmAllBegin", rtmAllBeginFunc);
}

// rtmInit channel gpio mode
// mode: 0=TX, 1=RX
void rtmInitFunc(uint16_t addr) {
    uint8_t channel; 
    if (!popAsUInt8(&channel) || channel >= 8) { pushBool(false); return; }
    
    uint8_t gpio; 
    if (!popAsUInt8(&gpio)) { pushBool(false); return; }

    uint8_t mode; 
    if (!popAsUInt8(&mode) || mode > 1) { pushBool(false); return; }
  
    
    if (rtmInstalled[channel]) {
        outputStream->println("⚠️ rtmInit: channel already installed");
        pushBool(false); return;
    }

    rmt_config_t config = {};
    config.rmt_mode = (mode == 0) ? RMT_MODE_TX : RMT_MODE_RX;
    config.channel  = (rmt_channel_t)channel;
    config.gpio_num = (gpio_num_t)gpio;
    config.clk_div  = 80;          // Дефолт: 1 тик = 1 мкс
    config.mem_block_num = 1;

    if (config.rmt_mode == RMT_MODE_TX) {
        config.tx_config.idle_level = RMT_IDLE_LEVEL_LOW;
        config.tx_config.idle_output_en = true;
    } else {
        config.rx_config.filter_en = false;
        config.rx_config.idle_threshold = 10000;
    }

    esp_err_t err = rmt_config(&config);
    if (err == ESP_OK) {
        err = rmt_driver_install(config.channel, (config.rmt_mode == RMT_MODE_RX) ? 1024 : 0, 0);
    }

    if (err == ESP_OK) rtmInstalled[channel] = true;
    pushBool(err == ESP_OK);
}

// rtmClk channel divider
// Частота тика = 80 МГц / divider
void rtmClkFunc(uint16_t addr) {    
    uint8_t channel; 
    if (!popAsUInt8(&channel) || channel >= 8 || !rtmInstalled[channel]) { pushBool(false); return; }
    
    uint8_t div; 
    if (!popAsUInt8(&div) || div == 0) { pushBool(false); return; }    
    
    pushBool(rmt_set_clk_div((rmt_channel_t)channel, div) == ESP_OK);
}

// rtmMem channel blocks
// 1 блок = 64 импульса. Макс 8.
void rtmMemFunc(uint16_t addr) {
    uint8_t blocks; 
    if (!popAsUInt8(&blocks) || blocks == 0 || blocks > 8) { pushBool(false); return; }
    
    uint8_t channel; 
    if (!popAsUInt8(&channel) || channel >= 8 || !rtmInstalled[channel]) { pushBool(false); return; }
    
    pushBool(rmt_set_mem_block_num((rmt_channel_t)channel, blocks) == ESP_OK);
}

// rtmCarrier channel enable freq duty level
// duty: 1..100 (%), level: 0=LOW, 1=HIGH
// rtmCarrier channel enable freq duty level → success
void rtmCarrierFunc(uint16_t addr) {
    uint8_t channel; 
    if (!popAsUInt8(&channel) || channel >= 8 || !rtmInstalled[channel]) { 
        pushBool(false); return; 
    }
    
    uint8_t enable; 
    if (!popAsUInt8(&enable) || enable > 1) { pushBool(false); return; }

    // Читаем остальные параметры в любом случае (чтобы очистить стек)
    int32_t freqVal = 0, dutyVal = 0;
    uint8_t levelVal = 0;
    
    popInt32FromAny(&freqVal);   // частота (может быть 0, если enable=0)
    popInt32FromAny(&dutyVal);   // скважность
    popAsUInt8(&levelVal);       // уровень

    // Валидируем параметры ТОЛЬКО если несущая включена
    if (enable) {
        if (freqVal < 100 || freqVal > 1000000) { 
            outputStream->println("⚠️ rtmCarrier: freq must be 100..1000000 Hz");
            pushBool(false); return; 
        }
        if (dutyVal < 1 || dutyVal > 100) { 
            outputStream->println("⚠️ rtmCarrier: duty must be 1..100 %");
            pushBool(false); return; 
        }
    }

    rmt_carrier_level_t level = (levelVal == 0) ? RMT_CARRIER_LEVEL_LOW : RMT_CARRIER_LEVEL_HIGH;
    esp_err_t err = rmt_set_tx_carrier((rmt_channel_t)channel, enable, (uint32_t)freqVal, (uint8_t)dutyVal, level);
    pushBool(err == ESP_OK);
}

// rtmIdle channel enable level
// Уровень на пине в простое (TX)
void rtmIdleFunc(uint16_t addr) {
    uint8_t channel; 
    if (!popAsUInt8(&channel) || channel >= 8 || !rtmInstalled[channel]) { pushBool(false); return; }
    
    uint8_t enable; 
    if (!popAsUInt8(&enable) || enable > 1) { pushBool(false); return; }

    uint8_t levelVal; 
    if (!popAsUInt8(&levelVal) || levelVal > 1) { pushBool(false); return; }    

    rmt_idle_level_t level = (levelVal == 0) ? RMT_IDLE_LEVEL_LOW : RMT_IDLE_LEVEL_HIGH;
    pushBool(rmt_set_idle_level((rmt_channel_t)channel, enable, level) == ESP_OK);
}

// rtmLoop channel enable
// Бесконечный повтор переданных данных (TX)
void rtmLoopFunc(uint16_t addr) {
    uint8_t enable; 
    if (!popAsUInt8(&enable) || enable > 1) { pushBool(false); return; }
    
    uint8_t channel; 
    if (!popAsUInt8(&channel) || channel >= 8 || !rtmInstalled[channel]) { pushBool(false); return; }
    
    pushBool(rmt_set_tx_loop_mode((rmt_channel_t)channel, enable) == ESP_OK);
}

// rtmFilter channel enable threshold
// Фильтр коротких импульсов (RX). Порог в тиках.
void rtmFilterFunc(uint16_t addr) {
    uint8_t thresh; 
    if (!popAsUInt8(&thresh)) { pushBool(false); return; }
    
    uint8_t enable; 
    if (!popAsUInt8(&enable) || enable > 1) { pushBool(false); return; }
    
    uint8_t channel; 
    if (!popAsUInt8(&channel) || channel >= 8 || !rtmInstalled[channel]) { pushBool(false); return; }
    
    pushBool(rmt_set_rx_filter((rmt_channel_t)channel, enable, thresh) == ESP_OK);
}

// rtmDeinit channel
void rtmDeinitFunc(uint16_t addr) {
    uint8_t channel; 
    if (!popAsUInt8(&channel) || channel >= 8) { pushBool(false); return; }
    
    if (!rtmInstalled[channel]) { pushBool(false); return; }
    
    esp_err_t err = rmt_driver_uninstall((rmt_channel_t)channel);
    if (err == ESP_OK) rtmInstalled[channel] = false;
    pushBool(err == ESP_OK);
}


// rtmWriteFunc: канал массив(ADDRINFO) → true/false
void rtmWriteFunc(uint16_t addr) {
  // Отладка: покажем, что на стеке
  outputStream->printf("DEBUG rtmWrite: stackTop=%d\n", stackTop);
  
  // 1. Снимаем КАНАЛ
  uint8_t channel;
  if (!popAsUInt8(&channel)) {
    outputStream->println("DEBUG: pop channel FAILED");
    pushBool(false); return;
  }
  outputStream->printf("DEBUG: channel=%u\n", channel);

  // 2. Снимаем СТРУКТУРУ АДРЕСА
  uint8_t type, len;
  const uint8_t* data;
  
  if (!peekStackTop(&type, &len, &data)) {
    outputStream->println("DEBUG: peekStackTop FAILED");
    pushBool(false); return;
  }
  outputStream->printf("DEBUG: type=%u len=%u\n", type, len);
  
  if (type != TYPE_ADDRINFO || len != 5) {
    outputStream->printf("DEBUG: EXPECTED TYPE_ADDRINFO(5) GOT type=%u len=%u\n", type, len);
    dropTop(0);
    pushBool(false); 
    return;
  }
  dropTop(0);

  uint16_t dataAddr = data[0] | (data[1] << 8);
  uint16_t bufLen   = data[2] | (data[3] << 8);
  uint8_t elemType  = data[4];
  
  outputStream->printf("DEBUG: addr=%u len=%u elemType=%u\n", dataAddr, bufLen, elemType);

  // Проверки
  if (channel >= 8 || !rtmInstalled[channel]) {
    outputStream->println("DEBUG: channel invalid or not installed");
    pushBool(false); return;
  }
  if (dataAddr >= DATA_POOL_SIZE || dataAddr + bufLen > DATA_POOL_SIZE) {
    outputStream->println("DEBUG: dataPool bounds error");
    pushBool(false); return;
  }

  // Отправка
  esp_err_t err = rmt_write_sample((rmt_channel_t)channel, &dataPool[dataAddr], bufLen, true);
  outputStream->printf("DEBUG: rmt_write_sample result=%d\n", err);
  pushBool(err == ESP_OK);
}



// rtmAvailableFunc: канал → кол-во_элементов (0 если нет данных)
void rtmAvailableFunc(uint16_t addr) {
  uint8_t channel;
  if (!popAsUInt8(&channel) || channel >= 8 || !rtmInstalled[channel]) {
    pushUInt16(0); return;
  }

  RingbufHandle_t rb;
  esp_err_t err = rmt_get_ringbuf_handle((rmt_channel_t)channel, &rb);
  if (err != ESP_OK || !rb) { pushUInt16(0); return; }

  size_t len = 0;
  rmt_item32_t* item = (rmt_item32_t*)xRingbufferReceive(rb, &len, 0);
  if (item) {
    vRingbufferReturnItem(rb, (void*)item); // сразу освобождаем буфер
    pushUInt16((uint16_t)(len / 4));        // 1 item = 4 байта
  } else {
    pushUInt16(0);
  }
}

// rtmReadFunc: канал адрес макс_длина → скопировано_байт (0 если нет данных)
void rtmReadFunc(uint16_t addr) {
  uint16_t maxLen = popUInt16();       // 1. Макс. длина буфера
  uint16_t dataAddr = popUInt16();     // 2. Адрес в dataPool
  uint8_t channel;
  if (!popAsUInt8(&channel) || channel >= 8 || !rtmInstalled[channel]) {
    pushUInt16(0); return;
  }

  if (dataAddr >= DATA_POOL_SIZE || dataAddr + maxLen > DATA_POOL_SIZE) {
    pushUInt16(0); return;
  }

  RingbufHandle_t rb;
  esp_err_t err = rmt_get_ringbuf_handle((rmt_channel_t)channel, &rb);
  if (err != ESP_OK || !rb) { pushUInt16(0); return; }

  size_t len = 0;
  rmt_item32_t* item = (rmt_item32_t*)xRingbufferReceive(rb, &len, 0);
  if (!item) { pushUInt16(0); return; }

  // Копируем сырые импульсы в dataPool
  size_t toCopy = (len > maxLen) ? maxLen : len;
  memcpy(&dataPool[dataAddr], item, toCopy);
  vRingbufferReturnItem(rb, (void*)item);
  pushUInt16((uint16_t)toCopy);
}
