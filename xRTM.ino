#include "driver/rmt.h"

// === ГЛОБАЛЬНЫЕ ПЕРЕМЕННЫЕ ===
static bool rtmInstalled[8] = {false};
static uint16_t rmt_proto_addr[8] = {0};
static uint8_t active_rtm_ch = 0;

struct RmtBinding {
    uint16_t proto_addr = 0, proto_len = 0;
    uint16_t hdr_addr = 0, hdr_len = 0;
    uint16_t ftr_addr = 0, ftr_len = 0;
    uint16_t data_addr = 0, data_len = 0;
    bool ready = false;
};
static RmtBinding rmt_bind[8];

// === ВСПОМОГАТЕЛЬНЫЕ (работают напрямую с вашим стеком) ===
static inline bool _popUInt8(uint8_t* out) {
    if (stack_is_empty()) return false;
    uint8_t buf[8];
    if (stack_pop(buf, sizeof(buf)) < 2) return false;
    *out = buf[1];
    return true;
}

static inline bool _popUInt16(uint16_t* out) {
    if (stack_is_empty()) return false;
    uint8_t buf[8];
    uint16_t sz = stack_pop(buf, sizeof(buf));
    if (sz < 3) return false;
    *out = buf[1] | (buf[2] << 8);
    return true;
}

static inline bool _popAddrInfo(uint16_t* out_addr, uint16_t* out_len) {
    if (stack_is_empty()) return false;
    uint8_t buf[8]; uint16_t sz = stack_pop(buf, sizeof(buf));
    if ((buf[0] != 17 && buf[0] != 20) || sz != 6) { stack_push(buf, sz); return false; }
    *out_addr = buf[1] | (buf[2] << 8);
    *out_len  = buf[3] | (buf[4] << 8);
    return true;
}

static inline void _pushBool(bool v) {
    uint8_t b[1] = {(uint8_t)v}; stack_push(b, 1);
}
static inline void _pushUInt16(uint16_t v) {
    uint8_t b[3] = {6, (uint8_t)(v & 0xFF), (uint8_t)(v >> 8)}; stack_push(b, 3);
}

// === NULL ARRAY ===
void nullArrayFunc() {
    // [17][addr_lo][addr_hi][len_lo][len_hi][elemType=4]
    uint8_t out[6] = {17, 0, 0, 0, 0, 4};
    stack_push(out, 6);
}

// === СЕТТЕРЫ ===
void rtmSetProtoFunc() {
    uint8_t ch; uint16_t a, l;
    if (!_popUInt8(&ch) || ch >= 8 || !_popAddrInfo(&a, &l)) { _pushBool(false); return; }
    rmt_bind[ch].proto_addr = a; rmt_bind[ch].proto_len = l;
    rmt_proto_addr[ch] = a;
    _pushBool(true);
}
void rtmSetHeaderFunc() {
    uint8_t ch; uint16_t a, l;
    if (!_popUInt8(&ch) || ch >= 8 || !_popAddrInfo(&a, &l)) { _pushBool(false); return; }
    rmt_bind[ch].hdr_addr = a; rmt_bind[ch].hdr_len = l;
    _pushBool(true);
}
void rtmSetFooterFunc() {
    uint8_t ch; uint16_t a, l;
    if (!_popUInt8(&ch) || ch >= 8 || !_popAddrInfo(&a, &l)) { _pushBool(false); return; }
    rmt_bind[ch].ftr_addr = a; rmt_bind[ch].ftr_len = l;
    _pushBool(true);
}




void rtmSetDataFunc() {
    uint8_t ch; 
    uint16_t a, l;
    
    // 1. Снимаем канал
    if (!_popUInt8(&ch) || ch >= 8) { 
        _pushBool(false); return; 
    }
    
    // 2. Снимаем данные (может быть маркер 0x12 или прямой массив 17/20)
    uint8_t buf[8];
    uint16_t sz = stack_pop(buf, sizeof(buf));
    if (sz < 2) { 
        _pushBool(false); return; 
    }
    
    uint8_t tag = buf[0];
    const uint8_t* hdr = buf;
    
    // 🔹 РАЗЫМЕНОВАНИЕ ПЕРЕМЕННОЙ (локально, только для этого слова)
    if (tag == 0x12 && sz == 3) {
        uint16_t var_addr = buf[1] | (buf[2] << 8);
        if (var_addr < dict_ptr) {
            uint8_t vn = dict_pool[var_addr + 2];
            uint16_t body = var_addr + 5 + vn;
            tag = dict_pool[body];       // реальный тег (17 или 20)
            hdr = &dict_pool[body];      // переключаемся на тело переменной
        }
    }
    
    // 3. Проверяем, что внутри действительно массив/ссылка
    if (tag != 17 && tag != 20) { 
        _pushBool(false); return; 
    }
    
    // 4. Извлекаем метаданные массива
    a = hdr[1] | (hdr[2] << 8);  // base
    l = hdr[3] | (hdr[4] << 8);  // количество элементов
    uint8_t tp = hdr[5];         // тип элемента (4=u8, 8=u24, 9=u32...)
    
    // 🔹 СРАЗУ ПЕРЕСЧИТЫВАЕМ В БАЙТЫ
    uint8_t esz = type_registry[tp].size;
    uint32_t total_bytes = (uint32_t)l * esz;
    
    // 5. Сохраняем в привязку (data_len теперь хранит БАЙТЫ, а не элементы)
    rmt_bind[ch].data_addr = a; 
    rmt_bind[ch].data_len = (uint16_t)total_bytes; 
    rmt_bind[ch].ready = (rmt_bind[ch].proto_addr != 0);
    
    _pushBool(true);
}
// === TRANSLATOR CALLBACK ===
static void IRAM_ATTR _translator_cb(const void* src, rmt_item32_t* dest, size_t src_size,
                                     size_t wanted_num, size_t* translated_size, size_t* item_num) {
    const uint8_t* bytes = (const uint8_t*)src;
    size_t processed = 0, created = 0;
    uint8_t ch = active_rtm_ch;
    uint16_t* tpl = (uint16_t*)&data_pool[rmt_proto_addr[ch]];

    while (processed < src_size && created < wanted_num) {
        uint8_t b = bytes[processed++];
        for (int i = 7; i >= 0 && created < wanted_num; i--) {
            uint16_t* t = (b & (1 << i)) ? &tpl[4] : &tpl[0];
            dest[created].level0 = t[0]; dest[created].duration0 = t[1];
            dest[created].level1 = t[2]; dest[created].duration1 = t[3];
            created++;
        }
    }
    *translated_size = processed;
    *item_num = created;
}

// === ОТПРАВКА ===
void rtmSendBeginFunc() {
    uint8_t ch;
    if (!_popUInt8(&ch) || ch >= 8 || !rtmInstalled[ch] || !rmt_bind[ch].ready) { _pushBool(false); return; }
    RmtBinding* b = &rmt_bind[ch];

    if (b->hdr_addr && b->hdr_len)
        rmt_write_items((rmt_channel_t)ch, (rmt_item32_t*)&data_pool[b->hdr_addr], b->hdr_len, false);

    active_rtm_ch = ch;
    rmt_translator_init((rmt_channel_t)ch, _translator_cb);
    rmt_write_sample((rmt_channel_t)ch, &data_pool[b->data_addr], b->data_len, false);

    if (b->ftr_addr && b->ftr_len)
        rmt_write_items((rmt_channel_t)ch, (rmt_item32_t*)&data_pool[b->ftr_addr], b->ftr_len, false);

    _pushBool(true);
}

void rtmAllBeginFunc() {
    bool started = false;
    for (uint8_t ch = 0; ch < 8; ch++) {
        if (!rtmInstalled[ch] || !rmt_bind[ch].ready) continue;
        RmtBinding* b = &rmt_bind[ch];
        if (b->hdr_addr && b->hdr_len)
            rmt_write_items((rmt_channel_t)ch, (rmt_item32_t*)&data_pool[b->hdr_addr], b->hdr_len, false);
        active_rtm_ch = ch;
        rmt_translator_init((rmt_channel_t)ch, _translator_cb);
        rmt_write_sample((rmt_channel_t)ch, &data_pool[b->data_addr], b->data_len, false);
        if (b->ftr_addr && b->ftr_len)
            rmt_write_items((rmt_channel_t)ch, (rmt_item32_t*)&data_pool[b->ftr_addr], b->ftr_len, false);
        started = true;
    }
    _pushBool(started);
}

// === КОНФИГУРАЦИЯ ===
void rtmInitFunc() {
    uint8_t ch, gpio, mode;
    if (!_popUInt8(&ch) || ch >= 8 || !_popUInt8(&gpio) || !_popUInt8(&mode) || mode > 1) { _pushBool(false); return; }
    if (rtmInstalled[ch]) { _pushBool(false); return; }

    rmt_config_t cfg = {};
    cfg.rmt_mode = (mode == 0) ? RMT_MODE_TX : RMT_MODE_RX;
    cfg.channel = (rmt_channel_t)ch;
    cfg.gpio_num = (gpio_num_t)gpio;
    cfg.clk_div = 80;
    cfg.mem_block_num = 1;
    if (cfg.rmt_mode == RMT_MODE_TX) {
        cfg.tx_config.idle_level = RMT_IDLE_LEVEL_LOW;
        cfg.tx_config.idle_output_en = true;
    } else {
        cfg.rx_config.filter_en = false;
        cfg.rx_config.idle_threshold = 10000;
    }

    esp_err_t err = rmt_config(&cfg);
    if (err == ESP_OK) err = rmt_driver_install(cfg.channel, (cfg.rmt_mode == RMT_MODE_RX) ? 1024 : 0, 0);
    if (err == ESP_OK) rtmInstalled[ch] = true;
    _pushBool(err == ESP_OK);
}

void rtmClkFunc() {
    uint8_t ch, div;
    if (!_popUInt8(&ch) || ch >= 8 || !rtmInstalled[ch] || !_popUInt8(&div) || div == 0) { _pushBool(false); return; }
    _pushBool(rmt_set_clk_div((rmt_channel_t)ch, div) == ESP_OK);
}

void rtmMemFunc() {
    uint8_t blocks, ch;
    if (!_popUInt8(&blocks) || blocks == 0 || blocks > 8 || !_popUInt8(&ch) || ch >= 8 || !rtmInstalled[ch]) { _pushBool(false); return; }
    _pushBool(rmt_set_mem_block_num((rmt_channel_t)ch, blocks) == ESP_OK);
}

void rtmCarrierFunc() {
    uint8_t ch, enable, levelVal = 0;
    if (!_popUInt8(&ch) || ch >= 8 || !rtmInstalled[ch] || !_popUInt8(&enable) || enable > 1) { _pushBool(false); return; }

    uint8_t buf[8];
    int32_t freq = 0, duty = 0;
    // Снимаем параметры обязательно (чтобы очистить стек)
    if (!stack_is_empty()) { uint16_t s = stack_pop(buf, sizeof(buf)); if (s >= 2) { uint32_t v=0; for(int i=0;i<s-1 && i<4;i++) v|=buf[1+i]<<(i*8); freq=v; } }
    if (!stack_is_empty()) { uint16_t s = stack_pop(buf, sizeof(buf)); if (s >= 2) { uint32_t v=0; for(int i=0;i<s-1 && i<4;i++) v|=buf[1+i]<<(i*8); duty=v; } }
    if (!stack_is_empty()) { uint16_t s = stack_pop(buf, sizeof(buf)); if (s >= 2) levelVal = buf[1]; }

    if (enable && (freq < 100 || freq > 1000000 || duty < 1 || duty > 100)) { _pushBool(false); return; }
    rmt_carrier_level_t level = (levelVal == 0) ? RMT_CARRIER_LEVEL_LOW : RMT_CARRIER_LEVEL_HIGH;
    _pushBool(rmt_set_tx_carrier((rmt_channel_t)ch, enable, (uint32_t)freq, (uint8_t)duty, level) == ESP_OK);
}

void rtmIdleFunc() {
    uint8_t ch, enable, levelVal;
    if (!_popUInt8(&ch) || ch >= 8 || !rtmInstalled[ch] || !_popUInt8(&enable) || enable > 1 || !_popUInt8(&levelVal) || levelVal > 1) { _pushBool(false); return; }
    rmt_idle_level_t level = (levelVal == 0) ? RMT_IDLE_LEVEL_LOW : RMT_IDLE_LEVEL_HIGH;
    _pushBool(rmt_set_idle_level((rmt_channel_t)ch, enable, level) == ESP_OK);
}

void rtmLoopFunc() {
    uint8_t enable, ch;
    if (!_popUInt8(&enable) || enable > 1 || !_popUInt8(&ch) || ch >= 8 || !rtmInstalled[ch]) { _pushBool(false); return; }
    _pushBool(rmt_set_tx_loop_mode((rmt_channel_t)ch, enable) == ESP_OK);
}

void rtmFilterFunc() {
    uint8_t thresh, enable, ch;
    if (!_popUInt8(&thresh) || !_popUInt8(&enable) || enable > 1 || !_popUInt8(&ch) || ch >= 8 || !rtmInstalled[ch]) { _pushBool(false); return; }
    _pushBool(rmt_set_rx_filter((rmt_channel_t)ch, enable, thresh) == ESP_OK);
}

void rtmDeinitFunc() {
    uint8_t ch;
    if (!_popUInt8(&ch) || ch >= 8 || !rtmInstalled[ch]) { _pushBool(false); return; }
    esp_err_t err = rmt_driver_uninstall((rmt_channel_t)ch);
    if (err == ESP_OK) rtmInstalled[ch] = false;
    _pushBool(err == ESP_OK);
}

// === ЧТЕНИЕ / ЗАПИСЬ ===
void rtmWriteFunc() {
    uint8_t ch; uint16_t a, l;
    if (!_popUInt8(&ch) || ch >= 8 || !rtmInstalled[ch] || !_popAddrInfo(&a, &l)) { _pushBool(false); return; }
    if (a >= DATA_POOL_SIZE || a + l > DATA_POOL_SIZE) { _pushBool(false); return; }
    _pushBool(rmt_write_sample((rmt_channel_t)ch, &data_pool[a], l, true) == ESP_OK);
}

void rtmAvailableFunc() {
    uint8_t ch;
    if (!_popUInt8(&ch) || ch >= 8 || !rtmInstalled[ch]) { _pushUInt16(0); return; }
    RingbufHandle_t rb;
    if (rmt_get_ringbuf_handle((rmt_channel_t)ch, &rb) != ESP_OK || !rb) { _pushUInt16(0); return; }
    size_t len = 0;
    rmt_item32_t* item = (rmt_item32_t*)xRingbufferReceive(rb, &len, 0);
    if (item) { vRingbufferReturnItem(rb, item); _pushUInt16(len / 4); }
    else { _pushUInt16(0); }
}

void rtmReadFunc() {
    uint16_t maxLen, daddr;
    uint8_t ch;
    if (!_popUInt16(&maxLen) || !_popUInt16(&daddr) || !_popUInt8(&ch) || ch >= 8 || !rtmInstalled[ch]) { _pushUInt16(0); return; }
    if (daddr >= DATA_POOL_SIZE || daddr + maxLen > DATA_POOL_SIZE) { _pushUInt16(0); return; }
    RingbufHandle_t rb;
    if (rmt_get_ringbuf_handle((rmt_channel_t)ch, &rb) != ESP_OK || !rb) { _pushUInt16(0); return; }
    size_t len = 0;
    rmt_item32_t* item = (rmt_item32_t*)xRingbufferReceive(rb, &len, 0);
    if (!item) { _pushUInt16(0); return; }
    size_t toCopy = (len > maxLen) ? maxLen : len;
    memcpy(&data_pool[daddr], item, toCopy);
    vRingbufferReturnItem(rb, item);
    _pushUInt16(toCopy);
}


// === ИНИЦИАЛИЗАЦИЯ И РЕГИСТРАЦИЯ ===
void rtmInit() {
    executeLine("cont rtm");
    addInternalWord("rtm.Init", rtmInitFunc);
    addInternalWord("rtm.Clk", rtmClkFunc);
    addInternalWord("rtm.Mem", rtmMemFunc);
    addInternalWord("rtm.Carrier", rtmCarrierFunc);
    addInternalWord("rtm.Idle", rtmIdleFunc);
    addInternalWord("rtm.Loop", rtmLoopFunc);
    addInternalWord("rtm.Filter", rtmFilterFunc);
    addInternalWord("rtm.Deinit", rtmDeinitFunc);
    addInternalWord("rtm.Write", rtmWriteFunc);
    addInternalWord("rtm.Available", rtmAvailableFunc);
    addInternalWord("rtm.Read", rtmReadFunc);
    addInternalWord("rtm.SetProto", rtmSetProtoFunc);
    addInternalWord("rtm.SetHeader", rtmSetHeaderFunc);
    addInternalWord("rtm.SetFooter", rtmSetFooterFunc);
    addInternalWord("rtm.SetData", rtmSetDataFunc);
    addInternalWord("rtm.SendBegin", rtmSendBeginFunc);
    addInternalWord("rtm.AllBegin", rtmAllBeginFunc);
    addInternalWord("nullArray", nullArrayFunc);
    executeLine("main");
}
