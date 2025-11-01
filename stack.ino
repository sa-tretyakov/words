void gotoFunc(uint16_t addr) {
  // Читаем смещение со стека (int32)
  if (stackTop < 2) return;
  uint8_t len = stack[stackTop - 2];
  uint8_t type = stack[stackTop - 1];
  if (type != TYPE_INT || len != 4) {
    dropTop(0);
    return;
  }
  int32_t offset;
  memcpy(&offset, &stack[stackTop - 2 - 4], 4);
  dropTop(0);

  // Устанавливаем глобальный offset для executeAt
  jumpOffset = offset;
  shouldJump = true;
}
