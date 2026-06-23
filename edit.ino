#if ENABLE_TERM_LAYER
// Перерисовка хвоста строки от текущей позиции курсора
void term_redraw() {
    currentOutput->print("\033[K");                     // 1. Очистить от курсора до конца строки
    uint8_t remaining = term_len - term_cur;
    if (remaining > 0) {
        currentOutput->write((const uint8_t*)&term_buf[term_cur], remaining); // 2. Напечатать хвост
        currentOutput->print("\033[");                  // 3. Вернуть курсор назад
        currentOutput->print(remaining);
        currentOutput->print("D");
    }
    currentOutput->flush();                             // ⚡ Мгновенная отправка UART
}

void handleTermChar(char c) {
    // ENTER
    if (c == '\r' || c == '\n') {
        currentOutput->println();
        term_buf[term_len] = '\0';
        if (term_len > 0) {
            executeLine(term_buf);
            printStack();
        }
        word_prompt();
        term_len = term_cur = 0;
        return;
    }

    // BACKSPACE (0x08) - удаляет символ СЛЕВА от курсора
    if (c == 0x08) {
        if (term_cur > 0) {
            term_cur--;
            memmove(&term_buf[term_cur], &term_buf[term_cur + 1], term_len - term_cur);
            term_len--;
            currentOutput->print("\033[D"); // Визуально сдвигаем курсор влево на 1
            term_redraw();                  // Перерисовываем хвост
        }
        return;
    }

    // DELETE (0x7F) - удаляет символ СПРАВА от курсора
    if (c == 0x7F) {
        if (term_cur < term_len) {
            memmove(&term_buf[term_cur], &term_buf[term_cur + 1], term_len - term_cur - 1);
            term_len--;
            term_redraw(); // Просто очищаем и перепечатываем хвост
        }
        return;
    }

    // Ctrl+L → очистка экрана
    if (c == 0x0C) {
        currentOutput->print("\033[2J\033[H");
        word_prompt();
        return;
    }

    // Ctrl+C → сброс стека
    if (c == 0x03) {
        stack_clear();
        //currentOutput->println("\r\nInterrupt.");
        currentOutput->println();
        term_len = term_cur = 0;
        word_prompt();
        return;
    }

    // Печатаемые символы
    if (c >= 32 && c < 127 && term_len < 255) {
        memmove(&term_buf[term_cur + 1], &term_buf[term_cur], term_len - term_cur);
        term_buf[term_cur] = c;
        term_len++;
        if (term_cur == term_len - 1) {
            currentOutput->write(c);      // Быстрое эхо при вводе в конец
        } else {
            term_redraw();                // Полная перерисовка при вставке в середину
        }
        term_cur++;
    }
}

void word_term() {
    g_termMode = !g_termMode;
    currentOutput->printf("terminal mode: %s\r\n", g_termMode ? "ON" : "OFF");
    if (g_termMode) {
        term_len = term_cur = 0;
        word_prompt();
    }
}
#endif
