void debugInit() {
  String tmp = "cont debug";
  executeLine(tmp);
  addInternalWord("pool", dumpDataPoolWord);
  addInternalWord("body", bodyWord);
  addInternalWord("seetime", seetimeWord);
   tmp = "main";
  executeLine(tmp);
}
