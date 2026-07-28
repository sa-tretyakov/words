#include "driver/rmt_tx.h"
#include "driver/rmt_rx.h"

// 🔧 FIX: rmt_symbol_word → rmt_symbol_word_t (ESP-IDF 5.x)
using rmt_symbol_word = rmt_symbol_word_t;

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
    uint32_t resolution_hz = 10000000;   // 10 MHz по умолчанию
    uint32_t mem_block_symbols = 64;
};
static RmtBinding rmt_bind[8];

// === Хэндлы каналов и энкодеров ===
static rmt_channel_handle_t rmt_tx_chan[8]   = {};
static rmt_channel_handle_t rmt_rx_chan[8]   = {};
static rmt_encoder_handle_t rmt_bytes_enc[8] = {};
static rmt_encoder_handle_t rmt_copy_enc[8]  = {};

// === RX буферы и состояние ===
#define RMT_RX_BUF_SYMBOLS 128
static rmt_symbol_word_t rmt_rx_buf[8][RMT_RX_BUF_SYMBOLS]; // 🔧 FIX: rmt_symbol_word_t
static size_t rmt_rx_received[8] = {};
static bool   rmt_rx_ready[8]    = {};

// === RX callback ===
static bool IRAM_ATTR rmt_rx_done_cb(rmt_channel_handle_t chan,
                                     const rmt_rx_done_event_data_t *edata, void *) {
    for (int i = 0; i < 8; i++) {
        if (rmt_rx_chan[i] == chan) {
            size_t n = edata->num_symbols;
            if (n > RMT_RX_BUF_SYMBOLS) n = RMT_RX_BUF_SYMBOLS;
            memcpy(rmt_rx_buf[i], edata->received_symbols, n * sizeof(rmt_symbol_word_t));
            rmt_rx_received[i] = n;
            rmt_rx_ready[i]    = true;
            break;
        }
    }
    return false;
}

// === ВСПОМОГАТЕЛЬНЫЕ ===
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
    uint8_t out[6] = {17, 0, 0, 0, 0, 4};
    stack_push(out, 6);
}

// === Пересоздание bytes-encoder ===
static void _rebuild_bytes_encoder(uint8_t ch) {
    if (rmt_bytes_enc[ch]) {
        rmt_del_encoder(rmt_bytes_enc[ch]);
        rmt_bytes_enc[ch] = nullptr;
    }
    uint16_t a = rmt_bind[ch].proto_addr;
    if (a == 0 || a + 8 > DATA_POOL_SIZE) return;
    rmt_bytes_encoder_config_t enc_cfg = {};
    rmt_symbol_word_t* tpl = (rmt_symbol_word_t*)&data_pool[a]; // 🔧 FIX
    enc_cfg.bit0 = tpl[0];
    enc_cfg.bit1 = tpl[1];
    enc_cfg.flags.msb_first = 1;
    rmt_new_bytes_encoder(&enc_cfg, &rmt_bytes_enc[ch]);
}

// === СЕТТЕРЫ ===
void rtmSetProtoFunc() {
    uint8_t ch; uint16_t a, l;
    if (!_popUInt8(&ch) || ch >= 8 || !_popAddrInfo(&a, &l)) { _pushBool(false); return; }
    rmt_bind[ch].proto_addr = a; rmt_bind[ch].proto_len = l;
    rmt_proto_addr[ch] = a;
    if (rtmInstalled[ch]) _rebuild_bytes_encoder(ch);
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
    if (!_popUInt8(&ch) || ch >= 8) { _pushBool(false); return; }
    uint8_t buf[8];
    uint16_t sz = stack_pop(buf, sizeof(buf));
    if (sz < 2) { _pushBool(false); return; }
    uint8_t tag = buf[0];
    const uint8_t* hdr = buf;
    if (tag == 0x12 && sz == 3) {
        uint16_t var_addr = buf[1] | (buf[2] << 8);
        if (var_addr < dict_ptr) {
            uint8_t vn = dict_pool[var_addr + 2];
            uint16_t body = var_addr + 5 + vn;
            tag = dict_pool[body];
            hdr = &dict_pool[body];
        }
    }
    if (tag != 17 && tag != 20) { _pushBool(false); return; }
    a = hdr[1] | (hdr[2] << 8);
    l = hdr[3] | (hdr[4] << 8);
    uint8_t tp = hdr[5];
    uint8_t esz = type_registry[tp].size;
    uint32_t total_bytes = (uint32_t)l * esz;
    rmt_bind[ch].data_addr = a;
    rmt_bind[ch].data_len  = (uint16_t)total_bytes;
    rmt_bind[ch].ready     = (rmt_bind[ch].proto_addr != 0);
    _pushBool(true);
}

// === ОТПРАВКА ===
void rtmSendBeginFunc() {
    uint8_t ch;
    if (!_popUInt8(&ch) || ch >= 8 || !rtmInstalled[ch] || !rmt_bind[ch].ready) {
        _pushBool(false); return;
    }
    if (!rmt_tx_chan[ch] || !rmt_bytes_enc[ch]) { _pushBool(false); return; }

    RmtBinding* b = &rmt_bind[ch];
    rmt_transmit_config_t tx_cfg = {};
    tx_cfg.loop_count = 0;
    esp_err_t err = ESP_OK;

    if (b->hdr_addr && b->hdr_len && rmt_copy_enc[ch]) {
        err = rmt_transmit(rmt_tx_chan[ch], rmt_copy_enc[ch],
                           &data_pool[b->hdr_addr],
                           b->hdr_len * sizeof(rmt_symbol_word_t), &tx_cfg); // 🔧 FIX
        if (err == ESP_OK) rmt_tx_wait_all_done(rmt_tx_chan[ch], pdMS_TO_TICKS(1000));
    }
    if (err == ESP_OK) {
        err = rmt_transmit(rmt_tx_chan[ch], rmt_bytes_enc[ch],
                           &data_pool[b->data_addr], b->data_len, &tx_cfg);
        if (err == ESP_OK) rmt_tx_wait_all_done(rmt_tx_chan[ch], pdMS_TO_TICKS(1000));
    }
    if (err == ESP_OK && b->ftr_addr && b->ftr_len && rmt_copy_enc[ch]) {
        err = rmt_transmit(rmt_tx_chan[ch], rmt_copy_enc[ch],
                           &data_pool[b->ftr_addr],
                           b->ftr_len * sizeof(rmt_symbol_word_t), &tx_cfg); // 🔧 FIX
        if (err == ESP_OK) rmt_tx_wait_all_done(rmt_tx_chan[ch], pdMS_TO_TICKS(1000));
    }
    _pushBool(err == ESP_OK);
}

void rtmAllBeginFunc() {
    bool started = false;
    for (uint8_t ch = 0; ch < 8; ch++) {
        if (!rtmInstalled[ch] || !rmt_bind[ch].ready ||
            !rmt_tx_chan[ch] || !rmt_bytes_enc[ch]) continue;

        RmtBinding* b = &rmt_bind[ch];
        rmt_transmit_config_t tx_cfg = {};
        tx_cfg.loop_count = 0;
        esp_err_t err = ESP_OK;

        if (b->hdr_addr && b->hdr_len && rmt_copy_enc[ch]) {
            err = rmt_transmit(rmt_tx_chan[ch], rmt_copy_enc[ch],
                               &data_pool[b->hdr_addr],
                               b->hdr_len * sizeof(rmt_symbol_word_t), &tx_cfg); // 🔧 FIX
            if (err == ESP_OK) rmt_tx_wait_all_done(rmt_tx_chan[ch], pdMS_TO_TICKS(1000));
        }
        if (err == ESP_OK) {
            err = rmt_transmit(rmt_tx_chan[ch], rmt_bytes_enc[ch],
                               &data_pool[b->data_addr], b->data_len, &tx_cfg);
            if (err == ESP_OK) rmt_tx_wait_all_done(rmt_tx_chan[ch], pdMS_TO_TICKS(1000));
        }
        if (err == ESP_OK && b->ftr_addr && b->ftr_len && rmt_copy_enc[ch]) {
            err = rmt_transmit(rmt_tx_chan[ch], rmt_copy_enc[ch],
                               &data_pool[b->ftr_addr],
                               b->ftr_len * sizeof(rmt_symbol_word_t), &tx_cfg); // 🔧 FIX
            if (err == ESP_OK) rmt_tx_wait_all_done(rmt_tx_chan[ch], pdMS_TO_TICKS(1000));
        }
        if (err == ESP_OK) started = true;
    }
    _pushBool(started);
}

// === КОНФИГУРАЦИЯ ===
void rtmInitFunc() {
    uint8_t ch, gpio, mode;
    if (!_popUInt8(&ch) || ch >= 8 || !_popUInt8(&gpio) ||
        !_popUInt8(&mode) || mode > 1) { _pushBool(false); return; }
    if (rtmInstalled[ch]) { _pushBool(false); return; }

    esp_err_t err = ESP_FAIL;
    if (mode == 0) {
        rmt_tx_channel_config_t tx_cfg = {};
        tx_cfg.gpio_num            = (gpio_num_t)gpio;
        tx_cfg.clk_src             = RMT_CLK_SRC_DEFAULT;
        tx_cfg.resolution_hz       = rmt_bind[ch].resolution_hz;
        tx_cfg.mem_block_symbols   = rmt_bind[ch].mem_block_symbols;
        tx_cfg.trans_queue_depth   = 4;
        tx_cfg.flags.with_dma      = false;
        err = rmt_new_tx_channel(&tx_cfg, &rmt_tx_chan[ch]);
        if (err == ESP_OK) err = rmt_enable(rmt_tx_chan[ch]);
        if (err == ESP_OK) {
            rmt_copy_encoder_config_t copy_cfg = {};
            err = rmt_new_copy_encoder(&copy_cfg, &rmt_copy_enc[ch]);
        }
    } else {
        rmt_rx_channel_config_t rx_cfg = {};
        rx_cfg.gpio_num            = (gpio_num_t)gpio;
        rx_cfg.clk_src             = RMT_CLK_SRC_DEFAULT;
        rx_cfg.resolution_hz       = rmt_bind[ch].resolution_hz;
        rx_cfg.mem_block_symbols   = rmt_bind[ch].mem_block_symbols;
        rx_cfg.flags.with_dma      = false;
        err = rmt_new_rx_channel(&rx_cfg, &rmt_rx_chan[ch]);
        if (err == ESP_OK) err = rmt_enable(rmt_rx_chan[ch]);
        if (err == ESP_OK) {
            rmt_rx_event_callbacks_t cbs = {};
            cbs.on_recv_done = rmt_rx_done_cb;
            rmt_rx_register_event_callbacks(rmt_rx_chan[ch], &cbs, nullptr);
            rmt_receive_config_t rxcfg = {};
            rxcfg.signal_range_min_ns = 0;
            rxcfg.signal_range_max_ns = 10000000;
            rmt_receive(rmt_rx_chan[ch], rmt_rx_buf[ch],
                        sizeof(rmt_rx_buf[ch]), &rxcfg);
        }
    }
    if (err == ESP_OK) rtmInstalled[ch] = true;
    _pushBool(err == ESP_OK);
}

void rtmClkFunc() {
    uint8_t ch, div;
    if (!_popUInt8(&ch) || ch >= 8 || !rtmInstalled[ch] ||
        !_popUInt8(&div) || div == 0) { _pushBool(false); return; }
    uint32_t new_res = 80000000UL / div;
    rmt_bind[ch].resolution_hz = new_res;
    _pushBool(true);
}

void rtmMemFunc() {
    uint8_t blocks, ch;
    if (!_popUInt8(&blocks) || blocks == 0 || blocks > 8 ||
        !_popUInt8(&ch) || ch >= 8 || !rtmInstalled[ch]) { _pushBool(false); return; }
    rmt_bind[ch].mem_block_symbols = (uint32_t)blocks * 48;
    _pushBool(true);
}

void rtmCarrierFunc() {
    uint8_t ch, enable, levelVal = 0;
    if (!_popUInt8(&ch) || ch >= 8 || !rtmInstalled[ch] ||
        !_popUInt8(&enable) || enable > 1) { _pushBool(false); return; }
    uint8_t buf[8];
    int32_t freq = 0, duty = 0;
    if (!stack_is_empty()) { uint16_t s = stack_pop(buf, sizeof(buf));
        if (s >= 2) { uint32_t v=0; for(int i=0;i<s-1 && i<4;i++) v|=buf[1+i]<<(i*8); freq=v; } }
    if (!stack_is_empty()) { uint16_t s = stack_pop(buf, sizeof(buf));
        if (s >= 2) { uint32_t v=0; for(int i=0;i<s-1 && i<4;i++) v|=buf[1+i]<<(i*8); duty=v; } }
    if (!stack_is_empty()) { uint16_t s = stack_pop(buf, sizeof(buf));
        if (s >= 2) levelVal = buf[1]; }
    if (enable && (freq < 100 || freq > 1000000 || duty < 1 || duty > 100)) {
        _pushBool(false); return;
    }
    if (!rmt_tx_chan[ch]) { _pushBool(false); return; }

    if (enable) {
        rmt_carrier_config_t car_cfg = {};
        car_cfg.frequency_hz = freq;
        car_cfg.duty_cycle   = (float)duty / 100.0f;
        car_cfg.flags.polarity_active_low = (levelVal == 0) ? 1 : 0;
        car_cfg.flags.always_on = 1;
        _pushBool(rmt_apply_carrier(rmt_tx_chan[ch], &car_cfg) == ESP_OK);
    } else {
        _pushBool(rmt_apply_carrier(rmt_tx_chan[ch], nullptr) == ESP_OK);
    }
}

void rtmIdleFunc() {
    uint8_t ch, enable, levelVal;
    if (!_popUInt8(&ch) || ch >= 8 || !rtmInstalled[ch] ||
        !_popUInt8(&enable) || enable > 1 || !_popUInt8(&levelVal) || levelVal > 1) {
        _pushBool(false); return;
    }
    _pushBool(true);
}

void rtmLoopFunc() {
    uint8_t enable, ch;
    if (!_popUInt8(&enable) || enable > 1 || !_popUInt8(&ch) ||
        ch >= 8 || !rtmInstalled[ch]) { _pushBool(false); return; }
    _pushBool(true);
}

void rtmFilterFunc() {
    uint8_t thresh, enable, ch;
    if (!_popUInt8(&thresh) || !_popUInt8(&enable) || enable > 1 ||
        !_popUInt8(&ch) || ch >= 8 || !rtmInstalled[ch]) { _pushBool(false); return; }
    _pushBool(true);
}

void rtmDeinitFunc() {
    uint8_t ch;
    if (!_popUInt8(&ch) || ch >= 8 || !rtmInstalled[ch]) { _pushBool(false); return; }
    esp_err_t err = ESP_OK;
    if (rmt_tx_chan[ch])   { if (rmt_del_channel(rmt_tx_chan[ch])   == ESP_OK) rmt_tx_chan[ch]   = nullptr; else err = ESP_FAIL; }
    if (rmt_rx_chan[ch])   { if (rmt_del_channel(rmt_rx_chan[ch])   == ESP_OK) rmt_rx_chan[ch]   = nullptr; else err = ESP_FAIL; }
    if (rmt_bytes_enc[ch]) { if (rmt_del_encoder(rmt_bytes_enc[ch]) == ESP_OK) rmt_bytes_enc[ch] = nullptr; else err = ESP_FAIL; }
    if (rmt_copy_enc[ch])  { if (rmt_del_encoder(rmt_copy_enc[ch])  == ESP_OK) rmt_copy_enc[ch]  = nullptr; else err = ESP_FAIL; }
    if (err == ESP_OK) rtmInstalled[ch] = false;
    _pushBool(err == ESP_OK);
}

// === ЧТЕНИЕ / ЗАПИСЬ ===
void rtmWriteFunc() {
    uint8_t ch; uint16_t a, l;
    if (!_popUInt8(&ch) || ch >= 8 || !rtmInstalled[ch] || !_popAddrInfo(&a, &l)) {
        _pushBool(false); return;
    }
    if (a >= DATA_POOL_SIZE || a + l > DATA_POOL_SIZE ||
        !rmt_tx_chan[ch] || !rmt_bytes_enc[ch]) { _pushBool(false); return; }
    rmt_transmit_config_t tx_cfg = {};
    tx_cfg.loop_count = 0;
    esp_err_t err = rmt_transmit(rmt_tx_chan[ch], rmt_bytes_enc[ch], &data_pool[a], l, &tx_cfg);
    if (err == ESP_OK) rmt_tx_wait_all_done(rmt_tx_chan[ch], pdMS_TO_TICKS(1000));
    _pushBool(err == ESP_OK);
}

void rtmAvailableFunc() {
    uint8_t ch;
    if (!_popUInt8(&ch) || ch >= 8 || !rtmInstalled[ch]) { _pushUInt16(0); return; }
    _pushUInt16(rmt_rx_ready[ch] ? (uint16_t)rmt_rx_received[ch] : 0);
}

void rtmReadFunc() {
    uint16_t maxLen, daddr;
    uint8_t ch;
    if (!_popUInt16(&maxLen) || !_popUInt16(&daddr) || !_popUInt8(&ch) ||
        ch >= 8 || !rtmInstalled[ch]) { _pushUInt16(0); return; }
    if (daddr >= DATA_POOL_SIZE || daddr + maxLen > DATA_POOL_SIZE) { _pushUInt16(0); return; }
    if (!rmt_rx_ready[ch]) { _pushUInt16(0); return; }

    size_t avail_bytes = rmt_rx_received[ch] * sizeof(rmt_symbol_word_t); // 🔧 FIX
    size_t toCopy = (avail_bytes > maxLen) ? maxLen : avail_bytes;
    memcpy(&data_pool[daddr], rmt_rx_buf[ch], toCopy);
    rmt_rx_ready[ch] = false;

    if (rmt_rx_chan[ch]) {
        rmt_receive_config_t rxcfg = {};
        rxcfg.signal_range_min_ns = 0;
        rxcfg.signal_range_max_ns = 10000000;
        rmt_receive(rmt_rx_chan[ch], rmt_rx_buf[ch], sizeof(rmt_rx_buf[ch]), &rxcfg);
    }
    _pushUInt16(toCopy);
}

// 🔧 FIX: функция переименована, чтобы не конфликтовать с HAL rmtInit() из esp32-hal-rmt.h
void rmtModuleInit() {
    executeLine("rmt cont");
    addInternalWord("rmt.Init",      rtmInitFunc);
    addInternalWord("rmt.Clk",       rtmClkFunc);
    addInternalWord("rmt.Mem",       rtmMemFunc);
    addInternalWord("rmt.Carrier",   rtmCarrierFunc);
    addInternalWord("rmt.Idle",      rtmIdleFunc);
    addInternalWord("rmt.Loop",      rtmLoopFunc);
    addInternalWord("rmt.Filter",    rtmFilterFunc);
    addInternalWord("rmt.Deinit",    rtmDeinitFunc);
    addInternalWord("rmt.Write",     rtmWriteFunc);
    addInternalWord("rmt.Available", rtmAvailableFunc);
    addInternalWord("rmt.Read",      rtmReadFunc);
    addInternalWord("rmt.SetProto",  rtmSetProtoFunc);
    addInternalWord("rmt.SetHeader", rtmSetHeaderFunc);
    addInternalWord("rmt.SetFooter", rtmSetFooterFunc);
    addInternalWord("rmt.SetData",   rtmSetDataFunc);
    addInternalWord("rmt.SendBegin", rtmSendBeginFunc);
    addInternalWord("rmt.AllBegin",  rtmAllBeginFunc);
    addInternalWord("nullArray",     nullArrayFunc);
    executeLine("main");
}

#include "driver/i2s.h"

// === ГЛОБАЛЬНЫЕ ПЕРЕМЕННЫЕ ===
static bool i2sInstalled[2] = {false, false};

struct I2sBinding {
    uint16_t data_addr = 0;
    uint16_t data_len  = 0;
    uint8_t  bps       = 16;       // bits per sample (8/16/24/32)
    uint8_t  channels  = 2;        // 1 = mono, 2 = stereo
    uint32_t rate      = 44100;    // 🔑 сохраняем частоту для i2s_set_clk
    bool     ready     = false;
};
static I2sBinding i2s_bind[2];

// === ЛОКАЛЬНЫЕ ХЕЛПЕРЫ ===
static inline bool _i2s_popUInt8(uint8_t* out) {
    if (stack_is_empty()) return false;
    uint8_t buf[8];
    if (stack_pop(buf, sizeof(buf)) < 2) return false;
    *out = buf[1];
    return true;
}
static inline bool _i2s_popUInt16(uint16_t* out) {
    if (stack_is_empty()) return false;
    uint8_t buf[8];
    uint16_t sz = stack_pop(buf, sizeof(buf));
    if (sz < 3) return false;
    *out = buf[1] | (buf[2] << 8);
    return true;
}
static inline bool _i2s_popAddrInfo(uint16_t* out_addr, uint16_t* out_len) {
    if (stack_is_empty()) return false;
    uint8_t buf[8]; uint16_t sz = stack_pop(buf, sizeof(buf));
    if ((buf[0] != 17 && buf[0] != 20) || sz != 6) { stack_push(buf, sz); return false; }
    *out_addr = buf[1] | (buf[2] << 8);
    *out_len  = buf[3] | (buf[4] << 8);
    return true;
}
static inline void _i2s_pushBool(bool v) {
    uint8_t b[1] = {(uint8_t)v}; stack_push(b, 1);
}
static inline void _i2s_pushUInt16(uint16_t v) {
    uint8_t b[3] = {6, (uint8_t)(v & 0xFF), (uint8_t)(v >> 8)}; stack_push(b, 3);
}
static inline void _i2s_pushUInt32(uint32_t v) {
    uint8_t b[5] = {9};
    memcpy(&b[1], &v, 4);
    stack_push(b, 5);
}

// 🔧 Вспомогательная: получить числовой код bits_per_sample
static inline int _i2s_bps_code(uint8_t bps) {
    switch (bps) {
        case 8:  return (int)I2S_BITS_PER_SAMPLE_8BIT;
        case 16: return (int)I2S_BITS_PER_SAMPLE_16BIT;
        case 24: return (int)I2S_BITS_PER_SAMPLE_24BIT;
        case 32: return (int)I2S_BITS_PER_SAMPLE_32BIT;
        default: return (int)I2S_BITS_PER_SAMPLE_16BIT;
    }
}

// =========================================================
// i2s.Init : channel mode sample_rate bits → BOOL
//   channel    : 0 или 1
//   mode       : 0=TX, 1=RX, 2=TX+RX, 3=PDM TX (S3), 4=PDM RX (S3)
//   sample_rate: частота дискретизации
//   bits       : 8/16/24/32
// =========================================================
void i2sInitFunc() {
    uint8_t ch, mode, bits;
    uint32_t rate;

    if (!_i2s_popUInt8(&ch) || ch > 1) { _i2s_pushBool(false); return; }
    if (!_i2s_popUInt8(&mode) || mode > 4) { _i2s_pushBool(false); return; }

    if (stack_is_empty()) { _i2s_pushBool(false); return; }
    uint8_t buf[8]; uint16_t sz = stack_pop(buf, sizeof(buf));
    if (sz < 2 || buf[0] < 4 || buf[0] > 11) { _i2s_pushBool(false); return; }
    rate = 0;
    for (uint16_t k = 0; k < sz - 1 && k < 4; k++) rate |= (uint32_t)buf[1 + k] << (k * 8);
    if (rate == 0) { _i2s_pushBool(false); return; }

    if (!_i2s_popUInt8(&bits)) { _i2s_pushBool(false); return; }
    if (i2sInstalled[ch]) { _i2s_pushBool(false); return; }

    int bps_code = _i2s_bps_code(bits);

    i2s_config_t cfg = {};
    cfg.mode = (i2s_mode_t)(I2S_MODE_MASTER);

    if (mode == 0) {
        cfg.mode = (i2s_mode_t)(cfg.mode | I2S_MODE_TX);
    } else if (mode == 1) {
        cfg.mode = (i2s_mode_t)(cfg.mode | I2S_MODE_RX);
    } else if (mode == 2) {
        cfg.mode = (i2s_mode_t)(cfg.mode | I2S_MODE_TX | I2S_MODE_RX);
    }
#if CONFIG_IDF_TARGET_ESP32S3 || CONFIG_IDF_TARGET_ESP32S2
    else if (mode == 3) {
        cfg.mode = (i2s_mode_t)(cfg.mode | I2S_MODE_TX | I2S_MODE_PDM);
    } else if (mode == 4) {
        cfg.mode = (i2s_mode_t)(cfg.mode | I2S_MODE_RX | I2S_MODE_PDM);
    }
#endif
    else {
        _i2s_pushBool(false); return;
    }

    cfg.sample_rate          = rate;
    cfg.bits_per_sample      = (i2s_bits_per_sample_t)bps_code;
    cfg.channel_format       = I2S_CHANNEL_FMT_RIGHT_LEFT;
    cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
    cfg.dma_buf_count        = 4;
    cfg.dma_buf_len          = 256;
    cfg.use_apll             = false;
    cfg.intr_alloc_flags     = ESP_INTR_FLAG_LEVEL1;

    esp_err_t err = i2s_driver_install((i2s_port_t)ch, &cfg, 0, NULL);
    if (err == ESP_OK) {
        i2sInstalled[ch]      = true;
        i2s_bind[ch].ready    = false;
        i2s_bind[ch].bps      = bits;
        i2s_bind[ch].channels = 2;
        i2s_bind[ch].rate     = rate;   // 🔑 сохраняем частоту
    }
    _i2s_pushBool(err == ESP_OK);
}

// =========================================================
// i2s.Pins : channel bck ws dout din → BOOL
//   -1 = пин не используется
// =========================================================
void i2sPinsFunc() {
    uint8_t ch;
    if (!_i2s_popUInt8(&ch) || ch > 1 || !i2sInstalled[ch]) { _i2s_pushBool(false); return; }

    int pins[4];
    for (int i = 0; i < 4; i++) {
        if (stack_is_empty()) { _i2s_pushBool(false); return; }
        uint8_t buf[8]; uint16_t sz = stack_pop(buf, sizeof(buf));
        if (sz < 2 || buf[0] < 4 || buf[0] > 11) { _i2s_pushBool(false); return; }
        int32_t v = 0;
        for (uint16_t k = 0; k < sz - 1 && k < 4; k++) v |= (int32_t)buf[1 + k] << (k * 8);
        // знаковое расширение для -1
        if (buf[0] == 5 || buf[0] == 7 || buf[0] == 10) {
            if (sz - 1 == 1 && (buf[1] & 0x80)) v |= 0xFFFFFF00;
            if (sz - 1 == 2 && (buf[2] & 0x80)) v |= 0xFFFF0000;
            if (sz - 1 == 3 && (buf[3] & 0x80)) v |= 0xFF000000;
        }
        pins[i] = (int)v;
    }
    // R2L порядок: читаем din→dout→ws→bck, пользователю нужен bck ws dout din
    // 🔧 ИСПРАВЛЕНО: i2s_pin_config_t вместо i2s_pin_t
    i2s_pin_config_t pin_cfg = {
        .bck_io_num   = pins[3],
        .ws_io_num    = pins[2],
        .data_out_num = pins[1],
        .data_in_num  = pins[0]
    };
    _i2s_pushBool(i2s_set_pin((i2s_port_t)ch, &pin_cfg) == ESP_OK);
}

// =========================================================
// i2s.SetData : channel array → BOOL
// =========================================================
void i2sSetDataFunc() {
    uint8_t ch;
    if (!_i2s_popUInt8(&ch) || ch > 1) { _i2s_pushBool(false); return; }

    uint8_t buf[8];
    uint16_t sz = stack_pop(buf, sizeof(buf));
    if (sz < 2) { _i2s_pushBool(false); return; }

    uint8_t tag = buf[0];
    const uint8_t* hdr = buf;

    // Разыменование переменной
    if (tag == 0x12 && sz == 3) {
        uint16_t var_addr = buf[1] | (buf[2] << 8);
        if (var_addr < dict_ptr) {
            uint8_t vn = dict_pool[var_addr + 2];
            uint16_t body = var_addr + 5 + vn;
            tag = dict_pool[body];
            hdr = &dict_pool[body];
        }
    }
    if (tag != 17 && tag != 20) { _i2s_pushBool(false); return; }

    uint16_t a = hdr[1] | (hdr[2] << 8);
    uint16_t l = hdr[3] | (hdr[4] << 8);
    uint8_t  tp = hdr[5];
    uint8_t  esz = type_registry[tp].size;
    uint32_t total_bytes = (uint32_t)l * esz;

    i2s_bind[ch].data_addr = a;
    i2s_bind[ch].data_len  = (uint16_t)total_bytes;
    i2s_bind[ch].ready     = true;
    _i2s_pushBool(true);
}

// =========================================================
// i2s.Write : channel → written_bytes
// =========================================================
void i2sWriteFunc() {
    uint8_t ch;
    if (!_i2s_popUInt8(&ch) || ch > 1 || !i2sInstalled[ch] || !i2s_bind[ch].ready) {
        _i2s_pushUInt16(0); return;
    }
    size_t written = 0;
    i2s_write((i2s_port_t)ch,
              &data_pool[i2s_bind[ch].data_addr],
              i2s_bind[ch].data_len,
              &written,
              portMAX_DELAY);
    _i2s_pushUInt16((uint16_t)written);
}

// =========================================================
// i2s.WriteRaw : channel ADDRINFO → written_bytes
// =========================================================
void i2sWriteRawFunc() {
    uint8_t ch;
    if (!_i2s_popUInt8(&ch) || ch > 1 || !i2sInstalled[ch]) { _i2s_pushUInt16(0); return; }
    uint16_t a, l;
    if (!_i2s_popAddrInfo(&a, &l)) { _i2s_pushUInt16(0); return; }
    if (a + l > DATA_POOL_SIZE) { _i2s_pushUInt16(0); return; }
    size_t written = 0;
    i2s_write((i2s_port_t)ch, &data_pool[a], l, &written, portMAX_DELAY);
    _i2s_pushUInt16((uint16_t)written);
}

// =========================================================
// i2s.Read : channel → read_bytes (блокирующее)
// =========================================================
void i2sReadFunc() {
    uint8_t ch;
    if (!_i2s_popUInt8(&ch) || ch > 1 || !i2sInstalled[ch] || !i2s_bind[ch].ready) {
        _i2s_pushUInt16(0); return;
    }
    size_t read_bytes = 0;
    i2s_read((i2s_port_t)ch,
             &data_pool[i2s_bind[ch].data_addr],
             i2s_bind[ch].data_len,
             &read_bytes,
             portMAX_DELAY);
    _i2s_pushUInt16((uint16_t)read_bytes);
}

// =========================================================
// 🆕 i2s.ReadNB : channel [timeout_ms] → read_bytes
//   Неблокирующее чтение. Без таймаута — мгновенно.
// =========================================================
void i2sReadNBFunc() {
    uint8_t ch;
    if (!_i2s_popUInt8(&ch) || ch > 1 || !i2sInstalled[ch] || !i2s_bind[ch].ready) {
        _i2s_pushUInt16(0); return;
    }

    uint32_t timeout_ms = 0;
    if (!stack_is_empty()) {
        // Подсматриваем вершину стека
        uint8_t* top = &stack_mem[stack_ptr];
        if (top[0] >= 4 && top[0] <= 11) {
            uint8_t buf[8];
            uint16_t sz = stack_pop(buf, sizeof(buf));
            if (sz >= 2) {
                for (uint16_t k = 0; k < sz - 1 && k < 4; k++) {
                    timeout_ms |= (uint32_t)buf[1 + k] << (k * 8);
                }
            }
        }
    }

    TickType_t ticks = (timeout_ms == 0) ? 0 : pdMS_TO_TICKS(timeout_ms);

    size_t read_bytes = 0;
    i2s_read((i2s_port_t)ch,
             &data_pool[i2s_bind[ch].data_addr],
             i2s_bind[ch].data_len,
             &read_bytes,
             ticks);
    _i2s_pushUInt16((uint16_t)read_bytes);
}

// =========================================================
// i2s.Zero : channel → BOOL
// =========================================================
void i2sZeroFunc() {
    uint8_t ch;
    if (!_i2s_popUInt8(&ch) || ch > 1 || !i2sInstalled[ch]) { _i2s_pushBool(false); return; }
    _i2s_pushBool(i2s_zero_dma_buffer((i2s_port_t)ch) == ESP_OK);
}

// =========================================================
// i2s.Start : channel → BOOL
// =========================================================
void i2sStartFunc() {
    uint8_t ch;
    if (!_i2s_popUInt8(&ch) || ch > 1 || !i2sInstalled[ch]) { _i2s_pushBool(false); return; }
    _i2s_pushBool(i2s_start((i2s_port_t)ch) == ESP_OK);
}

// =========================================================
// i2s.Stop : channel → BOOL
// =========================================================
void i2sStopFunc() {
    uint8_t ch;
    if (!_i2s_popUInt8(&ch) || ch > 1 || !i2sInstalled[ch]) { _i2s_pushBool(false); return; }
    _i2s_pushBool(i2s_stop((i2s_port_t)ch) == ESP_OK);
}

// =========================================================
// i2s.Rate : channel rate → BOOL
// =========================================================
void i2sRateFunc() {
    uint8_t ch;
    if (!_i2s_popUInt8(&ch) || ch > 1 || !i2sInstalled[ch]) { _i2s_pushBool(false); return; }
    if (stack_is_empty()) { _i2s_pushBool(false); return; }
    uint8_t buf[8]; uint16_t sz = stack_pop(buf, sizeof(buf));
    if (sz < 2 || buf[0] < 4 || buf[0] > 11) { _i2s_pushBool(false); return; }
    uint32_t rate = 0;
    for (uint16_t k = 0; k < sz - 1 && k < 4; k++) rate |= (uint32_t)buf[1 + k] << (k * 8);
    esp_err_t err = i2s_set_sample_rates((i2s_port_t)ch, rate);
    if (err == ESP_OK) {
        i2s_bind[ch].rate = rate;   // 🔑 обновляем сохранённую частоту
    }
    _i2s_pushBool(err == ESP_OK);
}

// =========================================================
// i2s.Deinit : channel → BOOL
// =========================================================
void i2sDeinitFunc() {
    uint8_t ch;
    if (!_i2s_popUInt8(&ch) || ch > 1 || !i2sInstalled[ch]) { _i2s_pushBool(false); return; }
    esp_err_t err = i2s_driver_uninstall((i2s_port_t)ch);
    if (err == ESP_OK) {
        i2sInstalled[ch] = false;
        i2s_bind[ch].ready = false;
    }
    _i2s_pushBool(err == ESP_OK);
}

// =========================================================
// i2s.Available : channel → bytes_capacity
// =========================================================
void i2sAvailableFunc() {
    uint8_t ch;
    if (!_i2s_popUInt8(&ch) || ch > 1 || !i2sInstalled[ch]) { _i2s_pushUInt32(0); return; }
    size_t avail = i2s_bind[ch].ready ? i2s_bind[ch].data_len : 0;
    _i2s_pushUInt32((uint32_t)avail);
}

// =========================================================
// i2s.Pdm : channel → BOOL
//   ESP32   : включает встроенный DAC (GPIO25/26)
//   ESP32-S3: настраивает PDM downsampling (mode=3/4)
// =========================================================
void i2sPdmFunc() {
    uint8_t ch;
    if (!_i2s_popUInt8(&ch) || ch > 1 || !i2sInstalled[ch]) {
        _i2s_pushBool(false); return;
    }

#if CONFIG_IDF_TARGET_ESP32
    if (ch != 0) {
        currentOutput->println("i2s.Pdm: internal DAC only on port 0");
        _i2s_pushBool(false); return;
    }
    _i2s_pushBool(i2s_set_dac_mode(I2S_DAC_CHANNEL_BOTH_EN) == ESP_OK);

#elif CONFIG_IDF_TARGET_ESP32S3 || CONFIG_IDF_TARGET_ESP32S2
    // 🔧 ИСПРАВЛЕНО: I2S_PDM_DSR_8S + только RX downsample
    esp_err_t err = i2s_set_pdm_rx_down_sample((i2s_port_t)ch, I2S_PDM_DSR_8S);
    if (err != ESP_OK) {
        currentOutput->println("i2s.Pdm: use i2s.Init with mode=4 (PDM RX)");
    }
    _i2s_pushBool(err == ESP_OK);

#else
    currentOutput->println("i2s.Pdm: not supported on this chip");
    _i2s_pushBool(false);
#endif
}

// =========================================================
// i2s.Format : channel fmt → BOOL
//   fmt: 0=STEREO, 1=LEFT, 2=RIGHT, 3=ALL_LEFT,
//        4=ALL_RIGHT, 5=SWAP
// =========================================================
void i2sFormatFunc() {
    uint8_t ch, fmt;
    if (!_i2s_popUInt8(&ch) || ch > 1 || !i2sInstalled[ch]) { _i2s_pushBool(false); return; }
    if (!_i2s_popUInt8(&fmt) || fmt > 5) { _i2s_pushBool(false); return; }

    int ch_mode;
    uint8_t new_channels = 2;
    switch (fmt) {
        case 0: ch_mode = (int)I2S_CHANNEL_STEREO; new_channels = 2; break;
        case 1: ch_mode = (int)I2S_CHANNEL_MONO;   new_channels = 1; break;
        case 2: ch_mode = (int)I2S_CHANNEL_MONO;   new_channels = 1; break;
        case 3: ch_mode = (int)I2S_CHANNEL_MONO;   new_channels = 1; break;
        case 4: ch_mode = (int)I2S_CHANNEL_MONO;   new_channels = 1; break;
        case 5: ch_mode = (int)I2S_CHANNEL_STEREO; new_channels = 2; break;
        default: _i2s_pushBool(false); return;
    }

    // 🔧 Передаём актуальные rate и bits (не 0!)
    esp_err_t err = i2s_set_clk((i2s_port_t)ch,
                                i2s_bind[ch].rate,
                                (uint32_t)_i2s_bps_code(i2s_bind[ch].bps),
                                (i2s_channel_t)ch_mode);
    if (err == ESP_OK) {
        i2s_bind[ch].channels = new_channels;
    }
    _i2s_pushBool(err == ESP_OK);
}

// =========================================================
// i2s.Mono : channel side → BOOL
//   side: 0 = left, 1 = right
// =========================================================
void i2sMonoFunc() {
    uint8_t ch, side;
    if (!_i2s_popUInt8(&ch) || ch > 1 || !i2sInstalled[ch]) { _i2s_pushBool(false); return; }
    if (!_i2s_popUInt8(&side) || side > 1) { _i2s_pushBool(false); return; }

    // 🔧 Передаём актуальные rate и bits (не 0!)
    esp_err_t err = i2s_set_clk((i2s_port_t)ch,
                                i2s_bind[ch].rate,
                                (uint32_t)_i2s_bps_code(i2s_bind[ch].bps),
                                I2S_CHANNEL_MONO);
    if (err == ESP_OK) {
        i2s_bind[ch].channels = 1;
    }
    _i2s_pushBool(err == ESP_OK);
}

// =========================================================
// i2s.Samples : channel → u32
//   Формула: data_len / (bps/8) / channels
// =========================================================
void i2sSamplesFunc() {
    uint8_t ch;
    if (!_i2s_popUInt8(&ch) || ch > 1 || !i2sInstalled[ch] || !i2s_bind[ch].ready) {
        _i2s_pushUInt32(0); return;
    }

    I2sBinding* b = &i2s_bind[ch];
    uint8_t bytes_per_sample = b->bps / 8;
    if (bytes_per_sample == 0 || b->channels == 0) {
        _i2s_pushUInt32(0); return;
    }
    uint32_t frame_size = (uint32_t)bytes_per_sample * b->channels;
    uint32_t samples = (uint32_t)b->data_len / frame_size;
    _i2s_pushUInt32(samples);
}

// =========================================================
// РЕГИСТРАЦИЯ
// =========================================================
void i2sInit() {
    executeLine("i2s cont");   // ✅ имя ПЕРЕД командой

    addInternalWord("i2s.Init",       i2sInitFunc);
    addInternalWord("i2s.Pins",       i2sPinsFunc);
    addInternalWord("i2s.SetData",    i2sSetDataFunc);
    addInternalWord("i2s.Write",      i2sWriteFunc);
    addInternalWord("i2s.WriteRaw",   i2sWriteRawFunc);
    addInternalWord("i2s.Read",       i2sReadFunc);
    addInternalWord("i2s.ReadNB",     i2sReadNBFunc);   // 🆕
    addInternalWord("i2s.Zero",       i2sZeroFunc);
    addInternalWord("i2s.Start",      i2sStartFunc);
    addInternalWord("i2s.Stop",       i2sStopFunc);
    addInternalWord("i2s.Rate",       i2sRateFunc);
    addInternalWord("i2s.Deinit",     i2sDeinitFunc);
    addInternalWord("i2s.Available",  i2sAvailableFunc);
    addInternalWord("i2s.Pdm",        i2sPdmFunc);
    addInternalWord("i2s.Format",     i2sFormatFunc);
    addInternalWord("i2s.Mono",       i2sMonoFunc);
    addInternalWord("i2s.Samples",    i2sSamplesFunc);

    executeLine("main");   // возврат в main контекст
}
