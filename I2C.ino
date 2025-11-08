#include <Wire.h>
void i2cInit() {
     String tmp = "cont i2c";
   executeLine(tmp);
addInternalWord("i2cBegin", i2cBeginFunc,currentContext);
addInternalWord("i2cWrite", i2cWriteFunc,currentContext);
addInternalWord("i2cWriteReg", i2cWriteRegFunc,currentContext);
addInternalWord("i2cReadReg", i2cReadRegFunc,currentContext);
addInternalWord("i2cBeginPins", i2cBeginPinsFunc,currentContext);

}
void i2cBeginPinsFunc(uint16_t addr) {
  if (stackTop < 2) { pushBool(false); return; }
  uint8_t scl = popUInt8();
  if (stackTop < 2) { pushBool(false); return; }
  uint8_t sda = popUInt8();
  
  Wire.begin(sda, scl);
  pushBool(true);
}
void i2cBeginFunc(uint16_t addr) {
  Wire.begin();
  pushBool(true);
}

void i2cWriteFunc(uint16_t addr) {
  uint8_t devAddr, len;
  
  // Читаем адрес и длину с автоматическим преобразованием
  if (!popAsUInt8(&devAddr) || !popAsUInt8(&len)) {
    pushBool(false);
    return;
  }
Serial.print("Adress ");
Serial.println(devAddr,HEX);
  if (len == 0 || len > 32) {
    pushBool(false);
    return;
  }
Serial.print("Len ");
Serial.println(len);
  // Читаем байты данных
  uint8_t buffer[32];
  for (int i = len - 1; i >= 0; i--) {
    if (!popAsUInt8(&buffer[i])) {
      pushBool(false);
      return;
    }
  }

  // Отправляем
  Wire.beginTransmission(devAddr);
  for (uint8_t i = 0; i < len; i++) {
    Wire.write(buffer[i]);
  }
  pushBool(Wire.endTransmission() == 0);
}

void i2cWriteRegFunc(uint16_t addr) {
  if (stackTop < 2) { pushBool(false); return; }
  uint8_t reg = popUInt8();
  if (stackTop < 2) { pushBool(false); return; }
  uint8_t i2cAddr = popUInt8();

  uint8_t buffer[32];
  int count = 0;
  while (stackTop >= 2 && stack[stackTop - 1] == TYPE_UINT8 && count < 32) {
    buffer[count++] = popUInt8();
  }

  Wire.beginTransmission(i2cAddr);
  Wire.write(reg);
  for (int i = count - 1; i >= 0; i--) {
    Wire.write(buffer[i]);
  }
  bool ok = (Wire.endTransmission() == 0);
  pushBool(ok);
}

void i2cReadRegFunc(uint16_t addr) {
  if (stackTop < 2) { pushBool(false); return; }
  uint8_t len = popUInt8();
  if (len == 0 || len > 32) { pushBool(false); return; }
  if (stackTop < 2) { pushBool(false); return; }
  uint8_t reg = popUInt8();
  if (stackTop < 2) { pushBool(false); return; }
  uint8_t i2cAddr = popUInt8();

  // Записываем регистр
  Wire.beginTransmission(i2cAddr);
  Wire.write(reg);
  if (Wire.endTransmission() != 0) {
    pushBool(false);
    return;
  }

  // Читаем данные
  Wire.requestFrom(i2cAddr, (int)len);
  if (Wire.available() != len) {
    pushBool(false);
    return;
  }

  uint8_t data[32];
  for (uint8_t i = 0; i < len; i++) {
    data[i] = Wire.read();
  }

  // Кладём на стек в порядке: [d0][d1]...[dN-1] (как массив)
  for (uint8_t i = 0; i < len; i++) {
    pushUInt8(data[i]);
  }
  pushBool(true); // success flag at bottom
}
