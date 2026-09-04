// === peripherals.ino — RMT + I2S периферия ===
// Работает на: ESP32, S3, C3, C5, C6, H2
// НЕ работает на: ESP8266 (нет аппаратной поддержки RMT/I2S)
//
// 🔒 Условная компиляция: на ESP8266 файл становится пустым,
// но функции rmtModuleInit() и i2sInit() остаются доступными (ничего не делают).

#if defined(ESP32)
// ============================================================
// === ВЕСЬ КОД RMT + I2S — ТОЛЬКО ДЛЯ ESP32 ===
// ============================================================

#include "driver/rmt_tx.h"
#include "driver/rmt_rx.h"
// 🔧 FIX: rmt_symbol_word → rmt_symbol_word_t (ESP-IDF 5.x)
using rmt_symbol_word = rmt_symbol_word_t;

// === ГЛОБАЛЬНЫЕ ПЕРЕМЕННЫЕ RMT ===
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

// 🔧 ДИНАМИЧЕСКОЕ ВЫДЕЛЕНИЕ: массив указателей вместо статических массивов
static RmtBinding* rmt_bind[8] = {nullptr};

// === Хэндлы каналов и энкодеров ===
static rmt_channel_handle_t rmt_tx_chan[8]   = {};
static rmt_channel_handle_t rmt_rx_chan[8]   = {};
static rmt_encoder_handle_t rmt_bytes_enc[8] = {};
static rmt_encoder_handle_t rmt_copy_enc[8]  = {};

// === RX буферы и состояние ===
#define RMT_RX_BUF_SYMBOLS 128
static rmt_symbol_word_t* rmt_rx_buf[8] = {nullptr};  // ← указатели вместо массивов
static size_t rmt_rx_received[8] = {};
static bool   rmt_rx_ready[8]    = {};

// === RX callback ===
static bool IRAM_ATTR rmt_rx_done_cb(rmt_channel_handle_t chan,
    const rmt_rx_done_event_data_t *edata, void *) {
    for (int i = 0; i < 8; i++) {
        if (rmt_rx_chan[i] == chan && rmt_rx_buf[i]) {  // ← проверка на nullptr
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

// === NULL ARRAY ===
void nullArrayFunc() {
    uint8_t out[6] = {17, 0, 0, 0, 0, 4};
    stack_push(out, 6);
}

// === Пересоздание bytes-encoder ===
static void _rebuild_bytes_encoder(uint8_t ch) {
    if (!rmt_bind[ch]) return;  // ← проверка на nullptr
    if (rmt_bytes_enc[ch]) {
        rmt_del_encoder(rmt_bytes_enc[ch]);
        rmt_bytes_enc[ch] = nullptr;
    }
    uint16_t a = rmt_bind[ch]->proto_addr;
    if (a == 0 || a + 8 > DATA_POOL_SIZE) return;
    rmt_bytes_encoder_config_t enc_cfg = {};
    rmt_symbol_word_t* tpl = (rmt_symbol_word_t*)&data_pool[a];
    enc_cfg.bit0 = tpl[0];
    enc_cfg.bit1 = tpl[1];
    enc_cfg.flags.msb_first = 1;
    rmt_new_bytes_encoder(&enc_cfg, &rmt_bytes_enc[ch]);
}

// === СЕТТЕРЫ ===
void rtmSetProtoFunc() {
    uint8_t ch; uint16_t a, l;
    if (!popUInt8(ch) || ch >= 8 || !popAddrInfo(a, l)) { pushBool(false); return; }
    if (!rmt_bind[ch]) { pushBool(false); return; }  // ← проверка
    rmt_bind[ch]->proto_addr = a; rmt_bind[ch]->proto_len = l;
    rmt_proto_addr[ch] = a;
    if (rtmInstalled[ch]) _rebuild_bytes_encoder(ch);
    pushBool(true);
}

void rtmSetHeaderFunc() {
    uint8_t ch; uint16_t a, l;
    if (!popUInt8(ch) || ch >= 8 || !popAddrInfo(a, l)) { pushBool(false); return; }
    if (!rmt_bind[ch]) { pushBool(false); return; }  // ← проверка
    rmt_bind[ch]->hdr_addr = a; rmt_bind[ch]->hdr_len = l;
    pushBool(true);
}

void rtmSetFooterFunc() {
    uint8_t ch; uint16_t a, l;
    if (!popUInt8(ch) || ch >= 8 || !popAddrInfo(a, l)) { pushBool(false); return; }
    if (!rmt_bind[ch]) { pushBool(false); return; }  // ← проверка
    rmt_bind[ch]->ftr_addr = a; rmt_bind[ch]->ftr_len = l;
    pushBool(true);
}

void rtmSetDataFunc() {
    uint8_t ch;
    if (!popUInt8(ch) || ch >= 8) { pushBool(false); return; }
    if (!rmt_bind[ch]) { pushBool(false); return; }  // ← проверка
    uint8_t buf[8];
    uint16_t sz = stack_pop(buf, sizeof(buf));
    if (sz < 2) { pushBool(false); return; }
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
    if (tag != 17 && tag != 20) { pushBool(false); return; }
    uint16_t a = hdr[1] | (hdr[2] << 8);
    uint16_t l = hdr[3] | (hdr[4] << 8);
    uint8_t tp = hdr[5];
    uint8_t esz = type_registry[tp].size;
    uint32_t total_bytes = (uint32_t)l * esz;
    rmt_bind[ch]->data_addr = a;
    rmt_bind[ch]->data_len  = (uint16_t)total_bytes;
    rmt_bind[ch]->ready     = (rmt_bind[ch]->proto_addr != 0);
    pushBool(true);
}

// === ОТПРАВКА ===
void rtmSendBeginFunc() {
    uint8_t ch;
    if (!popUInt8(ch) || ch >= 8 || !rtmInstalled[ch] || !rmt_bind[ch] || !rmt_bind[ch]->ready) {
        pushBool(false); return;
    }
    if (!rmt_tx_chan[ch] || !rmt_bytes_enc[ch]) { pushBool(false); return; }
    RmtBinding* b = rmt_bind[ch];
    rmt_transmit_config_t tx_cfg = {};
    tx_cfg.loop_count = 0;
    esp_err_t err = ESP_OK;
    if (b->hdr_addr && b->hdr_len && rmt_copy_enc[ch]) {
        err = rmt_transmit(rmt_tx_chan[ch], rmt_copy_enc[ch],
            &data_pool[b->hdr_addr],
            b->hdr_len * sizeof(rmt_symbol_word_t), &tx_cfg);
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
            b->ftr_len * sizeof(rmt_symbol_word_t), &tx_cfg);
        if (err == ESP_OK) rmt_tx_wait_all_done(rmt_tx_chan[ch], pdMS_TO_TICKS(1000));
    }
    pushBool(err == ESP_OK);
}

void rtmAllBeginFunc() {
    bool started = false;
    for (uint8_t ch = 0; ch < 8; ch++) {
        if (!rtmInstalled[ch] || !rmt_bind[ch] || !rmt_bind[ch]->ready ||
            !rmt_tx_chan[ch] || !rmt_bytes_enc[ch]) continue;
        RmtBinding* b = rmt_bind[ch];
        rmt_transmit_config_t tx_cfg = {};
        tx_cfg.loop_count = 0;
        esp_err_t err = ESP_OK;
        if (b->hdr_addr && b->hdr_len && rmt_copy_enc[ch]) {
            err = rmt_transmit(rmt_tx_chan[ch], rmt_copy_enc[ch],
                &data_pool[b->hdr_addr],
                b->hdr_len * sizeof(rmt_symbol_word_t), &tx_cfg);
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
                b->ftr_len * sizeof(rmt_symbol_word_t), &tx_cfg);
            if (err == ESP_OK) rmt_tx_wait_all_done(rmt_tx_chan[ch], pdMS_TO_TICKS(1000));
        }
        if (err == ESP_OK) started = true;
    }
    pushBool(started);
}

// === КОНФИГУРАЦИЯ ===
void rtmInitFunc() {
    uint8_t ch, gpio, mode;
    if (!popUInt8(ch) || ch >= 8 || !popUInt8(gpio) ||
        !popUInt8(mode) || mode > 1) { pushBool(false); return; }
    if (rtmInstalled[ch]) { pushBool(false); return; }
    // 🔧 ДИНАМИЧЕСКОЕ ВЫДЕЛЕНИЕ: создаём binding только при необходимости
    if (!rmt_bind[ch]) {
        rmt_bind[ch] = (RmtBinding*)malloc(sizeof(RmtBinding));
        if (!rmt_bind[ch]) { pushBool(false); return; }
        *rmt_bind[ch] = RmtBinding();  // Инициализация по умолчанию
    }
    esp_err_t err = ESP_FAIL;
    if (mode == 0) {
        rmt_tx_channel_config_t tx_cfg = {};
        tx_cfg.gpio_num            = (gpio_num_t)gpio;
        tx_cfg.clk_src             = RMT_CLK_SRC_DEFAULT;
        tx_cfg.resolution_hz       = rmt_bind[ch]->resolution_hz;
        tx_cfg.mem_block_symbols   = rmt_bind[ch]->mem_block_symbols;
        tx_cfg.trans_queue_depth   = 4;
        tx_cfg.flags.with_dma      = false;
        err = rmt_new_tx_channel(&tx_cfg, &rmt_tx_chan[ch]);
        if (err == ESP_OK) err = rmt_enable(rmt_tx_chan[ch]);
        if (err == ESP_OK) {
            rmt_copy_encoder_config_t copy_cfg = {};
            err = rmt_new_copy_encoder(&copy_cfg, &rmt_copy_enc[ch]);
        }
    } else {
        // 🔧 ДИНАМИЧЕСКОЕ ВЫДЕЛЕНИЕ: RX-буфер только для RX-режима
        if (!rmt_rx_buf[ch]) {
            rmt_rx_buf[ch] = (rmt_symbol_word_t*)malloc(RMT_RX_BUF_SYMBOLS * sizeof(rmt_symbol_word_t));
            if (!rmt_rx_buf[ch]) { pushBool(false); return; }
        }
        rmt_rx_channel_config_t rx_cfg = {};
        rx_cfg.gpio_num            = (gpio_num_t)gpio;
        rx_cfg.clk_src             = RMT_CLK_SRC_DEFAULT;
        rx_cfg.resolution_hz       = rmt_bind[ch]->resolution_hz;
        rx_cfg.mem_block_symbols   = rmt_bind[ch]->mem_block_symbols;
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
                RMT_RX_BUF_SYMBOLS * sizeof(rmt_symbol_word_t), &rxcfg);
        }
    }
    if (err == ESP_OK) rtmInstalled[ch] = true;
    pushBool(err == ESP_OK);
}

void rtmClkFunc() {
    uint8_t ch, div;
    if (!popUInt8(ch) || ch >= 8 || !rtmInstalled[ch] || !rmt_bind[ch] ||
        !popUInt8(div) || div == 0) { pushBool(false); return; }
    uint32_t new_res = 80000000UL / div;
    rmt_bind[ch]->resolution_hz = new_res;
    pushBool(true);
}

void rtmMemFunc() {
    uint8_t blocks, ch;
    if (!popUInt8(blocks) || blocks == 0 || blocks > 8 ||
        !popUInt8(ch) || ch >= 8 || !rtmInstalled[ch] || !rmt_bind[ch]) { pushBool(false); return; }
    rmt_bind[ch]->mem_block_symbols = (uint32_t)blocks * 48;
    pushBool(true);
}

void rtmCarrierFunc() {
    uint8_t ch, enable, levelVal = 0;
    if (!popUInt8(ch) || ch >= 8 || !rtmInstalled[ch] ||
        !popUInt8(enable) || enable > 1) { pushBool(false); return; }
    uint8_t buf[8];
    int32_t freq = 0, duty = 0;
    if (!stack_is_empty()) { uint16_t s = stack_pop(buf, sizeof(buf));
        if (s >= 2) { uint32_t v=0; for(int i=0;i<s-1 && i<4;i++) v|=buf[1+i]<<(i*8); freq=v; } }
    if (!stack_is_empty()) { uint16_t s = stack_pop(buf, sizeof(buf));
        if (s >= 2) { uint32_t v=0; for(int i=0;i<s-1 && i<4;i++) v|=buf[1+i]<<(i*8); duty=v; } }
    if (!stack_is_empty()) { uint16_t s = stack_pop(buf, sizeof(buf));
        if (s >= 2) levelVal = buf[1]; }
    if (enable && (freq < 100 || freq > 1000000 || duty < 1 || duty > 100)) {
        pushBool(false); return;
    }
    if (!rmt_tx_chan[ch]) { pushBool(false); return; }
    if (enable) {
        rmt_carrier_config_t car_cfg = {};
        car_cfg.frequency_hz = freq;
        car_cfg.duty_cycle   = (float)duty / 100.0f;
        car_cfg.flags.polarity_active_low = (levelVal == 0) ? 1 : 0;
        car_cfg.flags.always_on = 1;
        pushBool(rmt_apply_carrier(rmt_tx_chan[ch], &car_cfg) == ESP_OK);
    } else {
        pushBool(rmt_apply_carrier(rmt_tx_chan[ch], nullptr) == ESP_OK);
    }
}

void rtmIdleFunc() {
    uint8_t ch, enable, levelVal;
    if (!popUInt8(ch) || ch >= 8 || !rtmInstalled[ch] ||
        !popUInt8(enable) || enable > 1 || !popUInt8(levelVal) || levelVal > 1) {
        pushBool(false); return;
    }
    pushBool(true);
}

void rtmLoopFunc() {
    uint8_t enable, ch;
    if (!popUInt8(enable) || enable > 1 || !popUInt8(ch) ||
        ch >= 8 || !rtmInstalled[ch]) { pushBool(false); return; }
    pushBool(true);
}

void rtmFilterFunc() {
    uint8_t thresh, enable, ch;
    if (!popUInt8(thresh) || !popUInt8(enable) || enable > 1 ||
        !popUInt8(ch) || ch >= 8 || !rtmInstalled[ch]) { pushBool(false); return; }
    pushBool(true);
}

void rtmDeinitFunc() {
    uint8_t ch;
    if (!popUInt8(ch) || ch >= 8 || !rtmInstalled[ch]) { pushBool(false); return; }
    esp_err_t err = ESP_OK;
    if (rmt_tx_chan[ch])   { if (rmt_del_channel(rmt_tx_chan[ch])   == ESP_OK) rmt_tx_chan[ch]   = nullptr; else err = ESP_FAIL; }
    if (rmt_rx_chan[ch])   { if (rmt_del_channel(rmt_rx_chan[ch])   == ESP_OK) rmt_rx_chan[ch]   = nullptr; else err = ESP_FAIL; }
    if (rmt_bytes_enc[ch]) { if (rmt_del_encoder(rmt_bytes_enc[ch]) == ESP_OK) rmt_bytes_enc[ch] = nullptr; else err = ESP_FAIL; }
    if (rmt_copy_enc[ch])  { if (rmt_del_encoder(rmt_copy_enc[ch])  == ESP_OK) rmt_copy_enc[ch]  = nullptr; else err = ESP_FAIL; }
    // 🔧 ОСВОБОЖДЕНИЕ ДИНАМИЧЕСКОЙ ПАМЯТИ
    if (rmt_rx_buf[ch]) { free(rmt_rx_buf[ch]); rmt_rx_buf[ch] = nullptr; }
    if (rmt_bind[ch]) { free(rmt_bind[ch]); rmt_bind[ch] = nullptr; }
    if (err == ESP_OK) rtmInstalled[ch] = false;
    pushBool(err == ESP_OK);
}

// === ЧТЕНИЕ / ЗАПИСЬ ===
void rtmWriteFunc() {
    uint8_t ch; uint16_t a, l;
    if (!popUInt8(ch) || ch >= 8 || !rtmInstalled[ch] || !popAddrInfo(a, l)) {
        pushBool(false); return;
    }
    if (a >= DATA_POOL_SIZE || a + l > DATA_POOL_SIZE ||
        !rmt_tx_chan[ch] || !rmt_bytes_enc[ch]) { pushBool(false); return; }
    rmt_transmit_config_t tx_cfg = {};
    tx_cfg.loop_count = 0;
    esp_err_t err = rmt_transmit(rmt_tx_chan[ch], rmt_bytes_enc[ch], &data_pool[a], l, &tx_cfg);
    if (err == ESP_OK) rmt_tx_wait_all_done(rmt_tx_chan[ch], pdMS_TO_TICKS(1000));
    pushBool(err == ESP_OK);
}

void rtmAvailableFunc() {
    uint8_t ch;
    if (!popUInt8(ch) || ch >= 8 || !rtmInstalled[ch]) { pushUInt16(0); return; }
    pushUInt16(rmt_rx_ready[ch] ? (uint16_t)rmt_rx_received[ch] : 0);
}

void rtmReadFunc() {
    uint16_t maxLen, daddr;
    uint8_t ch;
    if (!popUInt16(maxLen) || !popUInt16(daddr) || !popUInt8(ch) ||
        ch >= 8 || !rtmInstalled[ch]) { pushUInt16(0); return; }
    if (daddr >= DATA_POOL_SIZE || daddr + maxLen > DATA_POOL_SIZE) { pushUInt16(0); return; }
    if (!rmt_rx_ready[ch] || !rmt_rx_buf[ch]) { pushUInt16(0); return; }  // ← проверка
    size_t avail_bytes = rmt_rx_received[ch] * sizeof(rmt_symbol_word_t);
    size_t toCopy = (avail_bytes > maxLen) ? maxLen : avail_bytes;
    memcpy(&data_pool[daddr], rmt_rx_buf[ch], toCopy);
    rmt_rx_ready[ch] = false;
    if (rmt_rx_chan[ch]) {
        rmt_receive_config_t rxcfg = {};
        rxcfg.signal_range_min_ns = 0;
        rxcfg.signal_range_max_ns = 10000000;
        rmt_receive(rmt_rx_chan[ch], rmt_rx_buf[ch],
            RMT_RX_BUF_SYMBOLS * sizeof(rmt_symbol_word_t), &rxcfg);
    }
    pushUInt16(toCopy);
}

// ============================================================
// === I2S ПЕРИФЕРИЯ ===
// ============================================================
#include "driver/i2s.h"

// === ГЛОБАЛЬНЫЕ ПЕРЕМЕННЫЕ I2S ===
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
// =========================================================
void i2sInitFunc() {
    uint8_t ch, mode, bits;
    uint32_t rate;
    if (!popUInt8(ch) || ch > 1) { pushBool(false); return; }
    if (!popUInt8(mode) || mode > 4) { pushBool(false); return; }
    if (stack_is_empty()) { pushBool(false); return; }
    uint8_t buf[8]; uint16_t sz = stack_pop(buf, sizeof(buf));
    if (sz < 2 || buf[0] < 4 || buf[0] > 11) { pushBool(false); return; }
    rate = 0;
    for (uint16_t k = 0; k < sz - 1 && k < 4; k++) rate |= (uint32_t)buf[1 + k] << (k * 8);
    if (rate == 0) { pushBool(false); return; }
    if (!popUInt8(bits)) { pushBool(false); return; }
    if (i2sInstalled[ch]) { pushBool(false); return; }
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
        pushBool(false); return;
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
        i2s_bind[ch].rate     = rate;
    }
    pushBool(err == ESP_OK);
}

// =========================================================
// i2s.Pins : channel bck ws dout din → BOOL
// =========================================================
void i2sPinsFunc() {
    uint8_t ch;
    if (!popUInt8(ch) || ch > 1 || !i2sInstalled[ch]) { pushBool(false); return; }
    int pins[4];
    for (int i = 0; i < 4; i++) {
        if (stack_is_empty()) { pushBool(false); return; }
        uint8_t buf[8]; uint16_t sz = stack_pop(buf, sizeof(buf));
        if (sz < 2 || buf[0] < 4 || buf[0] > 11) { pushBool(false); return; }
        int32_t v = 0;
        for (uint16_t k = 0; k < sz - 1 && k < 4; k++) v |= (int32_t)buf[1 + k] << (k * 8);
        if (buf[0] == 5 || buf[0] == 7 || buf[0] == 10) {
            if (sz - 1 == 1 && (buf[1] & 0x80)) v |= 0xFFFFFF00;
            if (sz - 1 == 2 && (buf[2] & 0x80)) v |= 0xFFFF0000;
            if (sz - 1 == 3 && (buf[3] & 0x80)) v |= 0xFF000000;
        }
        pins[i] = (int)v;
    }
    i2s_pin_config_t pin_cfg = {
        .bck_io_num   = pins[3],
        .ws_io_num    = pins[2],
        .data_out_num = pins[1],
        .data_in_num  = pins[0]
    };
    pushBool(i2s_set_pin((i2s_port_t)ch, &pin_cfg) == ESP_OK);
}

// =========================================================
// i2s.SetData : channel array → BOOL
// =========================================================
void i2sSetDataFunc() {
    uint8_t ch;
    if (!popUInt8(ch) || ch > 1) { pushBool(false); return; }
    uint8_t buf[8];
    uint16_t sz = stack_pop(buf, sizeof(buf));
    if (sz < 2) { pushBool(false); return; }
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
    if (tag != 17 && tag != 20) { pushBool(false); return; }
    uint16_t a = hdr[1] | (hdr[2] << 8);
    uint16_t l = hdr[3] | (hdr[4] << 8);
    uint8_t  tp = hdr[5];
    uint8_t  esz = type_registry[tp].size;
    uint32_t total_bytes = (uint32_t)l * esz;
    i2s_bind[ch].data_addr = a;
    i2s_bind[ch].data_len  = (uint16_t)total_bytes;
    i2s_bind[ch].ready     = true;
    pushBool(true);
}

// =========================================================
// i2s.Write : channel → written_bytes
// =========================================================
void i2sWriteFunc() {
    uint8_t ch;
    if (!popUInt8(ch) || ch > 1 || !i2sInstalled[ch] || !i2s_bind[ch].ready) {
        pushUInt16(0); return;
    }
    size_t written = 0;
    i2s_write((i2s_port_t)ch,
        &data_pool[i2s_bind[ch].data_addr],
        i2s_bind[ch].data_len,
        &written,
        portMAX_DELAY);
    pushUInt16((uint16_t)written);
}

// =========================================================
// i2s.WriteRaw : channel ADDRINFO → written_bytes
// =========================================================
void i2sWriteRawFunc() {
    uint8_t ch;
    if (!popUInt8(ch) || ch > 1 || !i2sInstalled[ch]) { pushUInt16(0); return; }
    uint16_t a, l;
    if (!popAddrInfo(a, l)) { pushUInt16(0); return; }
    if (a + l > DATA_POOL_SIZE) { pushUInt16(0); return; }
    size_t written = 0;
    i2s_write((i2s_port_t)ch, &data_pool[a], l, &written, portMAX_DELAY);
    pushUInt16((uint16_t)written);
}

// =========================================================
// i2s.Read : channel → read_bytes (блокирующее)
// =========================================================
void i2sReadFunc() {
    uint8_t ch;
    if (!popUInt8(ch) || ch > 1 || !i2sInstalled[ch] || !i2s_bind[ch].ready) {
        pushUInt16(0); return;
    }
    size_t read_bytes = 0;
    i2s_read((i2s_port_t)ch,
        &data_pool[i2s_bind[ch].data_addr],
        i2s_bind[ch].data_len,
        &read_bytes,
        portMAX_DELAY);
    pushUInt16((uint16_t)read_bytes);
}

// =========================================================
// 🆕 i2s.ReadNB : channel [timeout_ms] → read_bytes
// =========================================================
void i2sReadNBFunc() {
    uint8_t ch;
    if (!popUInt8(ch) || ch > 1 || !i2sInstalled[ch] || !i2s_bind[ch].ready) {
        pushUInt16(0); return;
    }
    uint32_t timeout_ms = 0;
    if (!stack_is_empty()) {
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
    pushUInt16((uint16_t)read_bytes);
}

// =========================================================
// i2s.Zero : channel → BOOL
// =========================================================
void i2sZeroFunc() {
    uint8_t ch;
    if (!popUInt8(ch) || ch > 1 || !i2sInstalled[ch]) { pushBool(false); return; }
    pushBool(i2s_zero_dma_buffer((i2s_port_t)ch) == ESP_OK);
}

// =========================================================
// i2s.Start : channel → BOOL
// =========================================================
void i2sStartFunc() {
    uint8_t ch;
    if (!popUInt8(ch) || ch > 1 || !i2sInstalled[ch]) { pushBool(false); return; }
    pushBool(i2s_start((i2s_port_t)ch) == ESP_OK);
}

// =========================================================
// i2s.Stop : channel → BOOL
// =========================================================
void i2sStopFunc() {
    uint8_t ch;
    if (!popUInt8(ch) || ch > 1 || !i2sInstalled[ch]) { pushBool(false); return; }
    pushBool(i2s_stop((i2s_port_t)ch) == ESP_OK);
}

// =========================================================
// i2s.Rate : channel rate → BOOL
// =========================================================
void i2sRateFunc() {
    uint8_t ch;
    if (!popUInt8(ch) || ch > 1 || !i2sInstalled[ch]) { pushBool(false); return; }
    if (stack_is_empty()) { pushBool(false); return; }
    uint8_t buf[8]; uint16_t sz = stack_pop(buf, sizeof(buf));
    if (sz < 2 || buf[0] < 4 || buf[0] > 11) { pushBool(false); return; }
    uint32_t rate = 0;
    for (uint16_t k = 0; k < sz - 1 && k < 4; k++) rate |= (uint32_t)buf[1 + k] << (k * 8);
    esp_err_t err = i2s_set_sample_rates((i2s_port_t)ch, rate);
    if (err == ESP_OK) {
        i2s_bind[ch].rate = rate;
    }
    pushBool(err == ESP_OK);
}

// =========================================================
// i2s.Deinit : channel → BOOL
// =========================================================
void i2sDeinitFunc() {
    uint8_t ch;
    if (!popUInt8(ch) || ch > 1 || !i2sInstalled[ch]) { pushBool(false); return; }
    esp_err_t err = i2s_driver_uninstall((i2s_port_t)ch);
    if (err == ESP_OK) {
        i2sInstalled[ch] = false;
        i2s_bind[ch].ready = false;
    }
    pushBool(err == ESP_OK);
}

// =========================================================
// i2s.Available : channel → bytes_capacity
// =========================================================
void i2sAvailableFunc() {
    uint8_t ch;
    if (!popUInt8(ch) || ch > 1 || !i2sInstalled[ch]) { pushUInt32(0); return; }
    size_t avail = i2s_bind[ch].ready ? i2s_bind[ch].data_len : 0;
    pushUInt32((uint32_t)avail);
}

// =========================================================
// i2s.Pdm : channel → BOOL
// =========================================================
void i2sPdmFunc() {
    uint8_t ch;
    if (!popUInt8(ch) || ch > 1 || !i2sInstalled[ch]) {
        pushBool(false); return;
    }
#if CONFIG_IDF_TARGET_ESP32
    if (ch != 0) {
        currentOutput->println("i2s.Pdm: internal DAC only on port 0");
        pushBool(false); return;
    }
    pushBool(i2s_set_dac_mode(I2S_DAC_CHANNEL_BOTH_EN) == ESP_OK);
#elif CONFIG_IDF_TARGET_ESP32S3
    esp_err_t err = i2s_set_pdm_rx_down_sample((i2s_port_t)ch, I2S_PDM_DSR_8S);
    if (err != ESP_OK) {
        currentOutput->println("i2s.Pdm: use i2s.Init with mode=4 (PDM RX)");
    }
    pushBool(err == ESP_OK);
#else
    currentOutput->println("i2s.Pdm: not supported on this chip");
    pushBool(false);
#endif
}

// =========================================================
// i2s.Format : channel fmt → BOOL
// =========================================================
void i2sFormatFunc() {
    uint8_t ch, fmt;
    if (!popUInt8(ch) || ch > 1 || !i2sInstalled[ch]) { pushBool(false); return; }
    if (!popUInt8(fmt) || fmt > 5) { pushBool(false); return; }
    int ch_mode;
    uint8_t new_channels = 2;
    switch (fmt) {
        case 0: ch_mode = (int)I2S_CHANNEL_STEREO; new_channels = 2; break;
        case 1: ch_mode = (int)I2S_CHANNEL_MONO;   new_channels = 1; break;
        case 2: ch_mode = (int)I2S_CHANNEL_MONO;   new_channels = 1; break;
        case 3: ch_mode = (int)I2S_CHANNEL_MONO;   new_channels = 1; break;
        case 4: ch_mode = (int)I2S_CHANNEL_MONO;   new_channels = 1; break;
        case 5: ch_mode = (int)I2S_CHANNEL_STEREO; new_channels = 2; break;
        default: pushBool(false); return;
    }
    esp_err_t err = i2s_set_clk((i2s_port_t)ch,
        i2s_bind[ch].rate,
        (uint32_t)_i2s_bps_code(i2s_bind[ch].bps),
        (i2s_channel_t)ch_mode);
    if (err == ESP_OK) {
        i2s_bind[ch].channels = new_channels;
    }
    pushBool(err == ESP_OK);
}

// =========================================================
// i2s.Mono : channel side → BOOL
// =========================================================
void i2sMonoFunc() {
    uint8_t ch, side;
    if (!popUInt8(ch) || ch > 1 || !i2sInstalled[ch]) { pushBool(false); return; }
    if (!popUInt8(side) || side > 1) { pushBool(false); return; }
    esp_err_t err = i2s_set_clk((i2s_port_t)ch,
        i2s_bind[ch].rate,
        (uint32_t)_i2s_bps_code(i2s_bind[ch].bps),
        I2S_CHANNEL_MONO);
    if (err == ESP_OK) {
        i2s_bind[ch].channels = 1;
    }
    pushBool(err == ESP_OK);
}

// =========================================================
// i2s.Samples : channel → u32
// =========================================================
void i2sSamplesFunc() {
    uint8_t ch;
    if (!popUInt8(ch) || ch > 1 || !i2sInstalled[ch] || !i2s_bind[ch].ready) {
        pushUInt32(0); return;
    }
    I2sBinding* b = &i2s_bind[ch];
    uint8_t bytes_per_sample = b->bps / 8;
    if (bytes_per_sample == 0 || b->channels == 0) {
        pushUInt32(0); return;
    }
    uint32_t frame_size = (uint32_t)bytes_per_sample * b->channels;
    uint32_t samples = (uint32_t)b->data_len / frame_size;
    pushUInt32(samples);
}

#endif // ESP32 — конец блока RMT + I2S кода

// ============================================================
// === РЕГИСТРАЦИЯ СЛОВ — ВСЕГДА ДОСТУПНА ===
// ============================================================
// На ESP8266 функции существуют, но ничего не делают.
// Это позволяет вызывать rmtModuleInit() и i2sInit() из setup() без условной компиляции.

void rmtModuleInit() {
#if defined(ESP32)
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
#endif
    // На ESP8266 — просто выходим, ничего не регистрируя.
}

void i2sInit() {
#if defined(ESP32)
    executeLine("i2s cont");
    addInternalWord("i2s.Init",       i2sInitFunc);
    addInternalWord("i2s.Pins",       i2sPinsFunc);
    addInternalWord("i2s.SetData",    i2sSetDataFunc);
    addInternalWord("i2s.Write",      i2sWriteFunc);
    addInternalWord("i2s.WriteRaw",   i2sWriteRawFunc);
    addInternalWord("i2s.Read",       i2sReadFunc);
    addInternalWord("i2s.ReadNB",     i2sReadNBFunc);
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
    executeLine("main");
#endif
    // На ESP8266 — просто выходим, ничего не регистрируя.
}

// === bt.ino — BLE (Bluetooth Low Energy) с NUS-сервисом ===
// Работает на: ESP32, S3, C3, C5, C6, H2
// НЕ работает на: ESP32-S2 (нет радиомодуля), ESP8266 (нет Bluetooth)
//
// 🔒 Условная компиляция: на неподдерживаемых платформах файл становится пустым,
// но функция btInit() остаётся доступной (ничего не делает).

// === ОПРЕДЕЛЕНИЕ НАЛИЧИЯ BLE ===
// Если HDL_ENABLE_BLE не задан в words.ino, #if посчитает его равным 0 (выключено).
#if HDL_ENABLE_BLE
    #if !defined(ESP8266) && !defined(CONFIG_IDF_TARGET_ESP32S2)
        #define HDL_HAS_BLE 1
    #else
        #define HDL_HAS_BLE 0
    #endif
#else
    #define HDL_HAS_BLE 0
#endif

#if HDL_HAS_BLE
// ============================================================
// === ВЕСЬ КОД BLE — ТОЛЬКО ДЛЯ ПОДДЕРЖИВАЕМЫХ ПЛАТФОРМ ===
// ============================================================

#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

#define MAX_BT_CHANNELS 4

struct BtChannel {
    bool installed = false;
    bool connected = false;
    BLEService* pService = nullptr;
    BLECharacteristic* pTxCharacteristic = nullptr;
    BLECharacteristic* pRxCharacteristic = nullptr;
    
    uint8_t* rx_buf = nullptr;
    volatile uint16_t rx_head = 0;
    volatile uint16_t rx_tail = 0;
    volatile uint16_t rx_count = 0;
    uint16_t rx_buf_size = 512;
};

static BtChannel bt_channels[MAX_BT_CHANNELS];
static BLEServer* pGlobalServer = nullptr;
static bool ble_global_initialized = false;

// === CALLBACK: Запись в RX-характеристику ===
class MyRxCallbacks : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic* pCharacteristic) override {
        int ch = -1;
        for (int i = 0; i < MAX_BT_CHANNELS; i++) {
            if (bt_channels[i].installed && bt_channels[i].pRxCharacteristic == pCharacteristic) {
                ch = i; break;
            }
        }
        if (ch == -1) return;
        
        String rxValue = pCharacteristic->getValue();
        if (rxValue.length() > 0) {
            for (size_t i = 0; i < rxValue.length(); i++) {
                if (bt_channels[ch].rx_count < bt_channels[ch].rx_buf_size) {
                    bt_channels[ch].rx_buf[bt_channels[ch].rx_head] = (uint8_t)rxValue[i];
                    bt_channels[ch].rx_head = (bt_channels[ch].rx_head + 1) % bt_channels[ch].rx_buf_size;
                    bt_channels[ch].rx_count++;
                } else break;
            }
        }
    }
};

// === CALLBACK: Подключение/отключение ===
class MyServerCallbacks : public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) override {
        for (int i = 0; i < MAX_BT_CHANNELS; i++) {
            if (bt_channels[i].installed) bt_channels[i].connected = true;
        }
        if (currentOutput) currentOutput->println("BLE: client connected");
    }
    void onDisconnect(BLEServer* pServer) override {
        for (int i = 0; i < MAX_BT_CHANNELS; i++) {
            if (bt_channels[i].installed) bt_channels[i].connected = false;
        }
        if (currentOutput) currentOutput->println("BLE: client disconnected");
        pServer->startAdvertising();
    }
};

void btInitFunc() {
    uint8_t ch; String name;
    if (!popUInt8(ch) || ch >= MAX_BT_CHANNELS || !popString(name)) { pushBool(false); return; }
    if (bt_channels[ch].installed) { pushBool(false); return; }
    
    if (!ble_global_initialized) {
        if (!BLEDevice::init(name.c_str())) { pushBool(false); return; }
        pGlobalServer = BLEDevice::createServer();
        if (!pGlobalServer) { pushBool(false); return; }
        pGlobalServer->setCallbacks(new MyServerCallbacks());
        BLEAdvertising* pAdvertising = BLEDevice::getAdvertising();
        pAdvertising->setScanResponse(true);
        pAdvertising->setMinPreferred(0x06);
        pAdvertising->setMinPreferred(0x12);
        ble_global_initialized = true;
    }
    
    if (!bt_channels[ch].rx_buf) {
        bt_channels[ch].rx_buf = (uint8_t*)malloc(512);
        if (!bt_channels[ch].rx_buf) { pushBool(false); return; }
    }
    
    char svc_uuid[37], rx_uuid[37], tx_uuid[37];
    if (ch == 0) {
        strcpy(svc_uuid, "6E400001-B5A3-F393-E0A9-E50E24DCCA9E");
        strcpy(rx_uuid,  "6E400002-B5A3-F393-E0A9-E50E24DCCA9E");
        strcpy(tx_uuid,  "6E400003-B5A3-F393-E0A9-E50E24DCCA9E");
    } else {
        snprintf(svc_uuid, sizeof(svc_uuid), "6E400001-B5A3-F393-E0A9-E50E24DCCA9%X", ch);
        snprintf(rx_uuid,  sizeof(rx_uuid),  "6E400002-B5A3-F393-E0A9-E50E24DCCA9%X", ch);
        snprintf(tx_uuid,  sizeof(tx_uuid),  "6E400003-B5A3-F393-E0A9-E50E24DCCA9%X", ch);
    }
    
    BLEService* pService = pGlobalServer->createService(svc_uuid);
    if (!pService) { pushBool(false); return; }
    
    bt_channels[ch].pTxCharacteristic = pService->createCharacteristic(
        tx_uuid, BLECharacteristic::PROPERTY_NOTIFY);
    bt_channels[ch].pTxCharacteristic->addDescriptor(new BLE2902());
    
    bt_channels[ch].pRxCharacteristic = pService->createCharacteristic(
        rx_uuid, BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);
    bt_channels[ch].pRxCharacteristic->setCallbacks(new MyRxCallbacks());
    
    pService->start();
    BLEDevice::getAdvertising()->addServiceUUID(svc_uuid);
    BLEDevice::startAdvertising();
    
    bt_channels[ch].installed = true;
    if (currentOutput) currentOutput->printf("BLE channel %d started: %s\n", ch, name.c_str());
    pushBool(true);
}

void btAvailableFunc() {
    uint8_t ch;
    if (!popUInt8(ch) || ch >= MAX_BT_CHANNELS) { pushUInt16(0); return; }
    if (!bt_channels[ch].installed) { pushUInt16(0); return; }
    pushUInt16(bt_channels[ch].rx_count);
}

void btReadFunc() {
    uint8_t ch; uint16_t base = 0, max_len = 0;
    if (!popUInt8(ch) || ch >= MAX_BT_CHANNELS) { pushUInt16(0); return; }
    if (!popAddrInfo(base, max_len)) { pushUInt16(0); return; }
    if (!bt_channels[ch].installed || base + max_len > DATA_POOL_SIZE || bt_channels[ch].rx_count == 0) { pushUInt16(0); return; }
    
    uint16_t to_read = (bt_channels[ch].rx_count > max_len) ? max_len : bt_channels[ch].rx_count;
    for (uint16_t i = 0; i < to_read; i++) {
        data_pool[base + i] = bt_channels[ch].rx_buf[bt_channels[ch].rx_tail];
        bt_channels[ch].rx_tail = (bt_channels[ch].rx_tail + 1) % bt_channels[ch].rx_buf_size;
        bt_channels[ch].rx_count--;
    }
    pushUInt16(to_read);
}

void btSendFunc() {
    uint8_t ch;
    if (!popUInt8(ch) || ch >= MAX_BT_CHANNELS) { pushBool(false); return; }
    if (!bt_channels[ch].installed || !bt_channels[ch].connected || !bt_channels[ch].pTxCharacteristic) {
        if (!stack_is_empty()) stack_ptr += elem_size(&stack_mem[stack_ptr]);
        pushBool(false); return;
    }
    if (stack_is_empty()) { pushBool(false); return; }
    
    uint8_t* data_ptr = &stack_mem[stack_ptr];
    uint8_t tag = data_ptr[0];
    uint16_t data_sz = elem_size(data_ptr);
    bool success = false;
    String txValue;
    
    if (tag == 0x0E || tag == 0x0D) {
        uint8_t len = data_ptr[1];
        txValue = String((char*)&data_ptr[2], len);
        success = true;
    } else if (tag == 15) {
        uint8_t len = data_ptr[1];
        uint16_t addr = data_ptr[2] | (data_ptr[3] << 8);
        if (addr + len <= DATA_POOL_SIZE) {
            txValue = String((char*)&data_pool[addr], len);
            success = true;
        }
    } else if (tag == 17 || tag == 20) {
        uint16_t base = data_ptr[1] | (data_ptr[2] << 8);
        uint16_t len  = data_ptr[3] | (data_ptr[4] << 8);
        uint8_t esz   = type_registry[data_ptr[5]].size;
        uint32_t total = (uint32_t)len * esz;
        if (base + total <= DATA_POOL_SIZE) {
            txValue = String((char*)&data_pool[base], total);
            success = true;
        }
    } else if (tag >= 4 && tag <= 11) {
        uint16_t payload_len = data_sz - 1;
        if (payload_len > 0) {
            txValue = String((char*)&data_ptr[1], payload_len);
            success = true;
        }
    }
    stack_ptr += data_sz;
    
    if (success && txValue.length() > 0) {
        bt_channels[ch].pTxCharacteristic->setValue(txValue);
        bt_channels[ch].pTxCharacteristic->notify();
    }
    pushBool(success);
}

void btConnectedFunc() {
    uint8_t ch;
    if (!popUInt8(ch) || ch >= MAX_BT_CHANNELS) { pushBool(false); return; }
    if (!bt_channels[ch].installed) { pushBool(false); return; }
    pushBool(bt_channels[ch].connected);
}

void btDeinitFunc() {
    uint8_t ch;
    if (!popUInt8(ch) || ch >= MAX_BT_CHANNELS) { pushBool(false); return; }
    if (!bt_channels[ch].installed) { pushBool(false); return; }
    if (bt_channels[ch].rx_buf) { free(bt_channels[ch].rx_buf); bt_channels[ch].rx_buf = nullptr; }
    bt_channels[ch].installed = false;
    bt_channels[ch].connected = false;
    bt_channels[ch].pService = nullptr;
    bt_channels[ch].pTxCharacteristic = nullptr;
    bt_channels[ch].pRxCharacteristic = nullptr;
    bt_channels[ch].rx_head = bt_channels[ch].rx_tail = bt_channels[ch].rx_count = 0;
    pushBool(true);
}

void btMacFunc() {
    uint8_t ch;
    if (!popUInt8(ch) || ch >= MAX_BT_CHANNELS || !bt_channels[ch].installed) {
        pushStringRaw("00:00:00:00:00:00"); return;
    }
    String macStr = BLEDevice::getAddress().toString().c_str();
    pushStringRaw(macStr.c_str());
}

void btRssiFunc() {
    uint8_t ch;
    if (!popUInt8(ch) || ch >= MAX_BT_CHANNELS) { pushInt32(-1000); return; }
    if (!bt_channels[ch].installed || !bt_channels[ch].connected || !pGlobalServer) {
        pushInt32(-1000); return;
    }
    int rssi = pGlobalServer->getConnectedCount() > 0 ? -50 : -1000;
    pushInt32(rssi);
}

#endif // HDL_HAS_BLE — конец блока BLE-кода
// === МОСТ ДЛЯ HDLStream ===
bool bt_stream_write(uint8_t ch, const uint8_t* data, size_t len) {
#if HDL_HAS_BLE
    if (ch >= MAX_BT_CHANNELS || !bt_channels[ch].installed ||
        !bt_channels[ch].connected || !bt_channels[ch].pTxCharacteristic) {
        return false;
    }
    size_t offset = 0;
    while (offset < len) {
        size_t chunk = (len - offset > 20) ? 20 : (len - offset);
        bt_channels[ch].pTxCharacteristic->setValue((uint8_t*)(data + offset), chunk);
        bt_channels[ch].pTxCharacteristic->notify();
        offset += chunk;
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    return true;
#else
    (void)ch; (void)data; (void)len;
    return false;
#endif
}

// ============================================================
// === РЕГИСТРАЦИЯ СЛОВ — ВСЕГДА ДОСТУПНА ===
// ============================================================
// На неподдерживаемых платформах функция существует, но ничего не делает.
// Это позволяет вызывать btInit() из setup() без условной компиляции.
void btInit() {
#if HDL_HAS_BLE
    focusTo("bt");
    addInternalWord("bt.Init",      btInitFunc);
    addInternalWord("bt.Available", btAvailableFunc);
    addInternalWord("bt.Read",      btReadFunc);
    addInternalWord("bt.Send",      btSendFunc);
    addInternalWord("bt.Connected", btConnectedFunc);
    addInternalWord("bt.Deinit",    btDeinitFunc);
    addInternalWord("bt.Mac",       btMacFunc);
    addInternalWord("bt.RSSI",      btRssiFunc);
    focusTo("main");
#endif
    // На ESP8266 / ESP32-S2 — просто выходим, ничего не регистрируя.
    // Слова bt.* не будут доступны в словаре, но сборка пройдёт успешно.
}
