void ioInit() {
  String tmp = "cont io";
  executeLine(tmp);
  addInternalWord(".", printTop);
  addInternalWord("print", printTop);
  addInternalWord("CR", [](uint16_t) { pushString("\r"); });
  addInternalWord("LF", [](uint16_t) { pushString("\n"); });
  addInternalWord("CRLF", [](uint16_t) { pushString("\r\n"); });
 tmp = "main";
  executeLine(tmp);
}
