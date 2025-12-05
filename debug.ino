void debugInit() {
  String tmp = "cont debug";
  executeLine(tmp);
  addInternalWord("pool", dumpDataPoolWord);
  addInternalWord("body", bodyWord);
  addInternalWord("seetime", seetimeWord);
   tmp = "main";
  executeLine(tmp);
}

void dumpDataPoolWord(uint16_t addr) {
  outputStream->printf("dataPool (used %u/%u):\n", dataPoolPtr, DATA_POOL_SIZE);
  
  // Заголовок: смещения 0-F
  outputStream->print("     ");
  for (int i = 0; i < 16; i++) {
    outputStream->printf(" %02X", i);
  }
  outputStream->print("  | ASCII\n");  // ← добавлена колонка заголовка

  for (uint16_t i = 0; i < dataPoolPtr; i += 16) {
    // Смещение
    outputStream->printf("%04X:", i);

    // Байты в hex
    for (int j = 0; j < 16; j++) {
      if (j == 8) outputStream->print(" "); // визуальный разделитель
      if (i + j < dataPoolPtr) {
        outputStream->printf(" %02X", dataPool[i + j]);
      } else {
        outputStream->print("   ");
      }
    }

    // ASCII колонка
    outputStream->print("  | ");
    for (int j = 0; j < 16; j++) {
      if (i + j < dataPoolPtr) {
        uint8_t c = dataPool[i + j];
        if (c >= 32 && c <= 126) {
          outputStream->print((char)c);
        } else {
          outputStream->print('.');
        }
      } else {
        outputStream->print(' ');
      }
    }
    outputStream->println();
  }
  outputStream->println();
}
