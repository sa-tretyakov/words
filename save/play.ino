// d стандартная длительность
// o основная октава
// b скорость четвертных нот в минуту
// . Точка, помещённая рядом с символом ноты или паузы, увеличивает длительность. 
//Одна точка увеличивает длительность на половину её значения. (!код не поддерживает несколько точек)
// # добавить полтона
// 8a6 первая цифра длительность ноты если нет, то обычная длительность
// Число за нотой - актава
// , разделитель нот
#include <FS.h>                      //Содержится в пакете
#define TONE_PIN 5

#define NOTE_C4  262
#define NOTE_CS4 277
#define NOTE_D4  294
#define NOTE_DS4 311
#define NOTE_E4  330
#define NOTE_F4  349
#define NOTE_FS4 370
#define NOTE_G4  392
#define NOTE_GS4 415
#define NOTE_A4  440
#define NOTE_AS4 466
#define NOTE_B4  494
#define NOTE_C5  523
#define NOTE_CS5 554
#define NOTE_D5  587
#define NOTE_DS5 622
#define NOTE_E5  659
#define NOTE_F5  698
#define NOTE_FS5 740
#define NOTE_G5  784
#define NOTE_GS5 831
#define NOTE_A5  880
#define NOTE_AS5 932
#define NOTE_B5  988
#define NOTE_C6  1047
#define NOTE_CS6 1109
#define NOTE_D6  1175
#define NOTE_DS6 1245
#define NOTE_E6  1319
#define NOTE_F6  1397
#define NOTE_FS6 1480
#define NOTE_G6  1568
#define NOTE_GS6 1661
#define NOTE_A6  1760
#define NOTE_AS6 1865
#define NOTE_B6  1976
#define NOTE_C7  2093
#define NOTE_CS7 2217
#define NOTE_D7  2349
#define NOTE_DS7 2489
#define NOTE_E7  2637
#define NOTE_F7  2794
#define NOTE_FS7 2960
#define NOTE_G7  3136
#define NOTE_GS7 3322
#define NOTE_A7  3520
#define NOTE_AS7 3729
#define NOTE_B7  3951

int notes[] = { 0,
NOTE_C4, NOTE_CS4, NOTE_D4, NOTE_DS4, NOTE_E4, NOTE_F4, NOTE_FS4, NOTE_G4, NOTE_GS4, NOTE_A4, NOTE_AS4, NOTE_B4,
NOTE_C5, NOTE_CS5, NOTE_D5, NOTE_DS5, NOTE_E5, NOTE_F5, NOTE_FS5, NOTE_G5, NOTE_GS5, NOTE_A5, NOTE_AS5, NOTE_B5,
NOTE_C6, NOTE_CS6, NOTE_D6, NOTE_DS6, NOTE_E6, NOTE_F6, NOTE_FS6, NOTE_G6, NOTE_GS6, NOTE_A6, NOTE_AS6, NOTE_B6,
NOTE_C7, NOTE_CS7, NOTE_D7, NOTE_DS7, NOTE_E7, NOTE_F7, NOTE_FS7, NOTE_G7, NOTE_GS7, NOTE_A7, NOTE_AS7, NOTE_B7
};
//String song = "The Simpsons:d=4,o=5,b=160:c.6,e6,f#6,8a6,g.6,e6,c6,8a,8f#,8f#,8f#,2g,8p,8p,8f#,8f#,8f#,8g,a#.,8c6,8c6,8c6,c6";
//String song = "Indiana:d=4,o=5,b=250:e,8p,8f,8g,8p,1c6,8p.,d,8p,8e,1f,p.,g,8p,8a,8b,8p,1f6,p,a,8p,8b,2c6,2d6,2e6,e,8p,8f,8g,8p,1c6,p,d6,8p,8e6,1f.6,g,8p,8g,e.6,8p,d6,8p,8g,e.6,8p,d6,8p,8g,f.6,8p,e6,8p,8d6,2c6";
//String song = "Entertainer:d=4,o=5,b=140:8d,8d#,8e,c6,8e,c6,8e,2c.6,8c6,8d6,8d#6,8e6,8c6,8d6,e6,8b,d6,2c6,p,8d,8d#,8e,c6,8e,c6,8e,2c.6,8p,8a,8g,8f#,8a,8c6,e6,8d6,8c6,8a,2d6";
//String song  = "Looney:d=4,o=5,b=140:32p,c6,8f6,8e6,8d6,8c6,a.,8c6,8f6,8e6,8d6,8d#6,e.6,8e6,8e6,8c6,8d6,8c6,8e6,8c6,8d6,8a,8c6,8g,8a#,8a,8f";
//String song  = "Bond:d=4,o=5,b=80:32p,16c#6,32d#6,32d#6,16d#6,8d#6,16c#6,16c#6,16c#6,16c#6,32e6,32e6,16e6,8e6,16d#6,16d#6,16d#6,16c#6,32d#6,32d#6,16d#6,8d#6,16c#6,16c#6,16c#6,16c#6,32e6,32e6,16e6,8e6,16d#6,16d6,16c#6,16c#7,c.7,16g#6,16f#6,g#.6";
//String song   = "MASH:d=8,o=5,b=140:4a,4g,f#,g,p,f#,p,g,p,f#,p,2e.,p,f#,e,4f#,e,f#,p,e,p,4d.,p,f#,4e,d,e,p,d,p,e,p,d,p,2c#.,p,d,c#,4d,c#,d,p,e,p,4f#,p,a,p,4b,a,b,p,a,p,b,p,2a.,4p,a,b,a,4b,a,b,p,2a.,a,4f#,a,b,p,d6,p,4e.6,d6,b,p,a,p,2b";
//String song   = "StarWars:d=4,o=5,b=45:32p,32f#,32f#,32f#,8b.,8f#.6,32e6,32d#6,32c#6,8b.6,16f#.6,32e6,32d#6,32c#6,8b.6,16f#.6,32e6,32d#6,32e6,8c#.6,32f#,32f#,32f#,8b.,8f#.6,32e6,32d#6,32c#6,8b.6,16f#.6,32e6,32d#6,32c#6,8b.6,16f#.6,32e6,32d#6,32e6,8c#6";
//String song   = "GoodBad:d=4,o=5,b=56:32p,32a#,32d#6,32a#,32d#6,8a#.,16f#.,16g#.,d#,32a#,32d#6,32a#,32d#6,8a#.,16f#.,16g#.,c#6,32a#,32d#6,32a#,32d#6,8a#.,16f#.,32f.,32d#.,c#,32a#,32d#6,32a#,32d#6,8a#.,16g#.,d#";
//String song   = "TopGun:d=4,o=4,b=31:32p,16c#,16g#,16g#,32f#,32f,32f#,32f,16d#,16d#,32c#,32d#,16f,32d#,32f,16f#,32f,32c#,16f,d#,16c#,16g#,16g#,32f#,32f,32f#,32f,16d#,16d#,32c#,32d#,16f,32d#,32f,16f#,32f,32c#,g#";
//String song   = "A-Team:d=8,o=5,b=125:4d#6,a#,2d#6,16p,g#,4a#,4d#.,p,16g,16a#,d#6,a#,f6,2d#6,16p,c#.6,16c6,16a#,g#.,2a#";
//String song   = "Flinstones:d=4,o=5,b=40:32p,16f6,16a#,16a#6,32g6,16f6,16a#.,16f6,32d#6,32d6,32d6,32d#6,32f6,16a#,16c6,d6,16f6,16a#.,16a#6,32g6,16f6,16a#.,32f6,32f6,32d#6,32d6,32d6,32d#6,32f6,16a#,16c6,a#,16a6,16d.6,16a#6,32a6,32a6,32g6,32f#6,32a6,8g6,16g6,16c.6,32a6,32a6,32g6,32g6,32f6,32e6,32g6,8f6,16f6,16a#.,16a#6,32g6,16f6,16a#.,16f6,32d#6,32d6,32d6,32d#6,32f6,16a#,16c.6,32d6,32d#6,32f6,16a#,16c.6,32d6,32d#6,32f6,16a#6,16c7,8a#.6";
//String song   = "Jeopardy:d=4,o=6,b=125:c,f,c,f5,c,f,2c,c,f,c,f,a.,8g,8f,8e,8d,8c#,c,f,c,f5,c,f,2c,f.,8d,c,a#5,a5,g5,f5,p,d#,g#,d#,g#5,d#,g#,2d#,d#,g#,d#,g#,c.7,8a#,8g#,8g,8f,8e,d#,g#,d#,g#5,d#,g#,2d#,g#.,8f,d#,c#,c,p,a#5,p,g#.5,d#,g#";
String song   = "MahnaMahna:d=16,o=6,b=125:c#,c.,b5,8a#.5,8f.,4g#,a#,g.,4d#,8p,c#,c.,b5,8a#.5,8f.,g#.,8a#.,4g,8p,c#,c.,b5,8a#.5,8f.,4g#,f,g.,8d#.,f,g.,8d#.,f,8g,8d#.,f,8g,d#,8c,a#5,8d#.,8d#.,4d#,8d#.";
//String song   = "MissionImp:d=16,o=6,b=95:32d,32d#,32d,32d#,32d,32d#,32d,32d#,32d,32d,32d#,32e,32f,32f#,32g,g,8p,g,8p,a#,p,c7,p,g,8p,g,8p,f,p,f#,p,g,8p,g,8p,a#,p,c7,p,g,8p,g,8p,f,p,f#,p,a#,g,2d,32p,a#,g,2c#,32p,a#,g,2c,a#5,8c,2p,32p,a#5,g5,2f#,32p,a#5,g5,2f,32p,a#5,g5,2e,d#,8d";
//String song  = "KnightRider:d=4,o=5,b=125:16e,16p,16f,16e,16e,16p,16e,16e,16f,16e,16e,16e,16d#,16e,16e,16e,16e,16p,16f,16e,16e,16p,16f,16e,16f,16e,16e,16e,16d#,16e,16e,16e,16d,16p,16e,16d,16d,16p,16e,16d,16e,16d,16d,16d,16c,16d,16d,16d,16d,16p,16e,16d,16d,16p,16e,16d,16e,16d,16d,16d,16c,16d,16d,16d";
//String song   = "Entertainer:d=4,o=5,b=140:8d,8d#,8e,c6,8e,c6,8e,2c.6,8c6,8d6,8d#6,8e6,8c6,8d6,e6,8b,d6,2c6,p,8d,8d#,8e,c6,8e,c6,8e,2c.6,8p,8a,8g,8f#,8a,8c6,e6,8d6,8c6,8a,2d6";

byte default_dur = 4;
byte default_oct = 1;
int bpm = 63;
  long wholenote;
  long duration;
  byte scale;
  
void setup() {
  Serial.begin(115200);  
  Serial.println(); 
  SPIFFS.begin();
  //song = readFile("Simpsons.txt", 4096); // Раскоментировать если хотим сыграть из файла, загрузите  SPIFFS
  if (song!= "Failed" && song!= "Large"){    
  getMuzicPar(); 
   while (song != "")
 {
  playNote();
  }   
  }
  else Serial.println("Bad File");  
}

void loop() {
 
}

void getMuzicPar(){
  Serial.println(selectToMarker(song, ":")); // Название компазиции
  song = deleteBeforeDelimiter(song, ":");  // Отрезаем название
  String title = selectToMarker(song, ":"); // Выделяем заголовок
  song = deleteBeforeDelimiter(song, ":");  // Оставить только ноты
  String first = selectToMarker(title, ","); // первый параметр
  title = deleteBeforeDelimiter(title, ","); // отбросить
  String second = selectToMarker(title, ","); // второй параметр
  title = deleteBeforeDelimiter(title, ","); // отбросить теперь тут третий параметр
  first = deleteBeforeDelimiter(first, "=");
  byte d =  first.toInt(); // получаем длительность
  if(d > 0) default_dur = d;
  Serial.print("ddur: ");
  Serial.println(first);
  second = deleteBeforeDelimiter(second, "=");
  default_oct = second.toInt(); // получаем октаву
  Serial.print("doct: ");
  Serial.println(second);
  title = deleteBeforeDelimiter(title, "=");
  bpm = title.toInt();
  wholenote = (60 * 1000L / bpm) * 4;  // this is the time for whole note (in milliseconds)
  Serial.print("bpm: ");
  Serial.println(title);
  Serial.print("wn: "); Serial.println(wholenote, 10);
  }
void playNote(){  
  String note = selectToMarker(song, ","); // Выделяем ноту из строки
  int8_t n=-1; // позиция имени ноты
  uint8_t nn=0; // Позиция ноты в массиве тонов   
  String nS; // Символьное обозначение ноты
  // определяем позицию ноты в массиве
  if (n ==-1) {nS = "c"; n = note.indexOf(nS);nn=1;}
  if (n ==-1) {nS = "d";n = note.indexOf(nS);nn=3;}
  if (n ==-1) {nS = "e";n = note.indexOf(nS);nn=5;}
  if (n ==-1) {nS = "f";n = note.indexOf(nS);nn=6;}
  if (n ==-1) {nS = "g";n = note.indexOf(nS);nn=8;}
  if (n ==-1) {nS = "a";n = note.indexOf(nS);nn=10;}
  if (n ==-1) {nS = "b";n = note.indexOf(nS);nn=12;}
  if (n ==-1) {nS = "p";n = note.indexOf(nS);nn=0;}
  // если перед нотой есть число, то это длительность
  if (n>0){
    int num = selectToMarker(note,nS).toInt(); // Вделяем длительность в виде числа
    if(num) duration = wholenote / num;        // Переводим в ms
    }
    else duration = wholenote / default_dur;  // Если длительность не указана, то устанавливаем длительность по умолчанию
   // проверяем дополнительные символы для ноты
   if (note.indexOf(".")!=-1) duration += duration/2; // если есть точка то увеличиваем длительность на четверть
   if (note.indexOf("#")!=-1) nn++;                   // если есть # указываем на ноту на полтона выше
   // проверяем признак октавы
   scale = note.substring(note.length()-1,note.length()).toInt(); // выделяем последний символ и переводим в int это будет октава
   // если получили 0
   if (scale==0){ 
    scale = default_oct; // октава по умолчанию
    }
    // получим частоту ноты
    int toneF = notes[(scale - 4) * 12 + nn];
    // теперь есть все данные ноты scale = октава, nn = номер ноты в массиве, duration = длительность, toneF = частота ноты
   if (nn!=0){ //если номер ноты не 0 играем ноту, иначе делаем паузу
      Serial.print("Playing: ");
      Serial.print(scale, 10); Serial.print(' ');
      Serial.print(nn, 10); Serial.print(" (");
      Serial.print(toneF, 10);
      Serial.print(") ");
      Serial.println(duration, 10);
  tone(TONE_PIN, toneF);
  delay(duration);
  noTone(TONE_PIN);
   }
   else{
    Serial.print("Pausing: ");
      Serial.println(duration, 10);
      delay(duration); 
    }
   // Отрезаем от строки проигранную ноту. 
  int p = note.length();
  song = song.substring(p + 1);
  }

  // --------------------Выделяем строку до маркера
String selectToMarker (String str, String found) {
  int p = str.indexOf(found);
  return str.substring(0, p);
}

//----------------------Удаляем все до символа разделителя
String deleteBeforeDelimiter(String str, String found) {
  int p = str.indexOf(found) + found.length();
  return str.substring(p);
}

// ------------- Чтение файла в строку
String readFile(String fileName, size_t len ) {
  File configFile = SPIFFS.open("/" + fileName, "r");
  if (!configFile) {
    return "Failed";
  }
  size_t size = configFile.size();
  if (size > len) {
    configFile.close();
    return "Large";
  }
  String temp = configFile.readString();
  configFile.close();
  return temp;
}
