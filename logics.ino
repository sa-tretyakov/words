void logicsInit() {
  String tmp = "cont logics";
  executeLine(tmp);
  addMarkerWord("if");
  addInternalWord("while", whileFunc);
  addrWhile = findWordAddress("while");
    addMarkerWord("{");
  addMarkerWord("}");
  

  addMarkerWord("==");
  addMarkerWord("!=");
  addMarkerWord("<");
  addMarkerWord(">");
  addMarkerWord("<=");
  addMarkerWord(">=");
  addInternalWord("not", notWord);  
  addInternalWord("true", [](uint16_t) { pushBool(true); });
  addInternalWord("false", [](uint16_t) { pushBool(false); });
    addMarkerWord("end");


 tmp = "main";
  executeLine(tmp);
}
