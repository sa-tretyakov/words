void audioInit() {
  String tmp = "cont audio";
  executeLine(tmp);
  addInternalWord("tone", toneWord);
  addInternalWord("beep", beepWord);
  addInternalWord("noTone", noToneWord);
  addInternalWord("silence", noToneWord);
   tmp = "main";
  executeLine(tmp);
}

void toneWord(uint16_t addr) {
  // pin
  if (stackTop < 2) {
    Serial.println("⚠️ tone: пин ожидается");
    return;
  }
  uint8_t pin;
  if (!popAsUInt8(&pin)) {
    return;
  }
  // частота
  if (stackTop < 2) {
    Serial.println("⚠️ tone: частота ожидается");
    return;
  }
  int32_t freq;
  if (!popInt32FromAny(&freq)) {
    return;
  }
  if (freq < 1) freq = 1;
  if (freq > 65535) freq = 65535;

  ::tone(pin, (unsigned int)freq);
}

void beepWord(uint16_t addr) {
  // пин
  if (stackTop < 2) {
    Serial.println("⚠️ пикни: пин ожидается");
    return;
  }
  uint8_t pin = 0;
  if (!popAsUInt8(&pin)) return;
  // частота
  if (stackTop < 2) {
    Serial.println("⚠️ пикни: частота (Гц) ожидается");
    return;
  }
  int32_t freq = 0;
  if (!popInt32FromAny(&freq)) return;
  if (freq < 100) freq = 100;     // минимум 100 Гц (ниже — не слышно)
  if (freq > 10000) freq = 10000; // максимум 10 кГц
  // длительность
  if (stackTop < 2) {
    Serial.println("⚠️ пикни: длительность (мс) ожидается");
    return;
  }
  int32_t dur = 0;
  if (!popInt32FromAny(&dur)) return;
  if (dur < 0) dur = 0;
  if (dur > 10000) dur = 10000; // 10 сек максимум
  ::tone(pin, (unsigned int)freq, (unsigned long)dur);
}

void noToneWord(uint16_t addr) {
  if (stackTop < 2) {
    Serial.println("⚠️ noTone: pin expected");
    return;
  }
  uint8_t pin;
  if (!popAsUInt8(&pin)) {
    Serial.println("⚠️ noTone: invalid pin");
    return;
  }
  ::noTone(pin);
}
