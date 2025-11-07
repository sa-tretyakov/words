/*
 * Этот код преобразует значение любого поддерживаемого типа данных в логическое условие (true/false) — то есть определяет, 
 * является ли значение «истинным» или «ложным». Он отвечает на вопрос: «Не равно ли это значение нулю?» 
 * bool condition = valueToBool(type, len, data);
 */
bool valueToBool(uint8_t type, uint8_t len, const uint8_t* data) {
  if (type == TYPE_BOOL && len == 1) return data[0] != 0;
  if (type == TYPE_INT && len == 4) { int32_t v; memcpy(&v, data, 4); return v != 0; }
  if (type == TYPE_UINT8 && len == 1) return data[0] != 0;
  if (type == TYPE_INT8 && len == 1) return (int8_t)data[0] != 0;
  if (type == TYPE_UINT16 && len == 2) { uint16_t v; memcpy(&v, data, 2); return v != 0; }
  if (type == TYPE_INT16 && len == 2) { int16_t v; memcpy(&v, data, 2); return v != 0; }
  if (type == TYPE_FLOAT && len == 4) { float v; memcpy(&v, data, 4); return v != 0.0f; }
  return false; // неизвестный тип → false
}
