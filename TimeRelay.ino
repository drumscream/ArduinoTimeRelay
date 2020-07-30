/* адресация в EEPROM:
 *  байт 0 - часы включения реле
 *  байт 1 - минуты включения реле
 *  байт 2 - часы выключения реле
 *  байт 3 - минуты выключения реле
 */
#include "pitches.h"
#include <TM1637Display.h>
#include <RTClib.h>
#include <Time.h>
#include <OneWire.h>
#include <Wire.h>
#include <Bounce2.h>
#include <EEPROM.h>

const int speaker = 11; // пин пищалки
const int RELAY = 10; // пин управления реле
const int LED_INFO = 9; // пин светодиода
const int LED_RELAY_ON_EDIT = 3; // пин светодиода установки времени включения
const int LED_RELAY_OFF_EDIT = 4; // пин светодиода установки времени выключения
const int BUTTON_1 = 7; // пин кнопки 1 (инкрементирование цифр)
const int BUTTON_2 = 8; // пин кнопки 2 (кнопка установки часов, минут, или сохранения настроек)
const int BUTTON_SET_ON_TIME = 5; // пин кнопки установки времени включения реле
const int BUTTON_SET_OFF_TIME = 6; // пин кнопки установки времени выключения реле

// Создаем объекты
Bounce debouncer1 = Bounce();
Bounce debouncer2 = Bounce();
Bounce debouncer3 = Bounce();
Bounce debouncer4 = Bounce();
RTC_DS1307 RTC;

byte buttonState1; // состояние кнопки листания цифр
byte buttonState2; // состояние кнопки установки часов
byte buttonState3; // состояние кнопки редактирования времени включения
byte buttonState4; // состояние кнопки редактирования времени выключения
unsigned long buttonPressTimeStamp; // таймштамп для первого зажатия (время первичного нажатия на кнопку)
unsigned long buttonPressTimeStamp2; // таймштамп для второго зажатия (время для отсчёта длительного удержания нажатия на кнопку)
unsigned long ClockModeTimeoutTimeStamp; // таймштамп для таймаута редактирования
byte Hours = 0;
byte Minutes = 0;
byte ClockMode = 0; //0 - отсутствие редактирования, 1 - редактирование часов, 2 - редактирование минут
byte Relay_On_Hours = 21; // Значения по умолчанию
byte Relay_On_Minutes = 0;
byte Relay_Off_Hours = 8;
byte Relay_Off_Minutes = 0;

boolean EditModeOnTime = false; // режим редактирования времени включения
boolean EditModeOffTime = false; // режим редактирования времени выключения
boolean LastRelayStatus = false; // последний измеренный статус состояния реле, чтобы пиликать при изменениях состояния (выкл по умолчанию)

void setup() {
  Serial.begin(9600);
  Wire.begin();
  RTC.begin();
  if (!RTC.isrunning()) {
    Serial.println("RTC is NOT running!");
    RTC.adjust(DateTime(__DATE__, __TIME__)); // настройка даты и времени часов
  }
  pinMode(RELAY, OUTPUT);
  pinMode(LED_INFO, OUTPUT);
  pinMode(LED_RELAY_ON_EDIT, OUTPUT);
  pinMode(LED_RELAY_OFF_EDIT, OUTPUT);
  digitalWrite(RELAY, LOW);
  digitalWrite(LED_INFO, LOW);
  digitalWrite(LED_RELAY_ON_EDIT, LOW);
  digitalWrite(LED_RELAY_OFF_EDIT, LOW);
  
  pinMode(BUTTON_1, INPUT);
  pinMode(BUTTON_1, INPUT_PULLUP);
  debouncer1.attach(BUTTON_1); // Даем бибилотеке знать, к какому пину мы подключили кнопку
  debouncer1.interval(25); // Интервал, в течение которого мы не буем получать значения с пина
  
  pinMode(BUTTON_2, INPUT);
  pinMode(BUTTON_2, INPUT_PULLUP);
  debouncer2.attach(BUTTON_2);
  debouncer2.interval(25);
  
  pinMode(BUTTON_SET_ON_TIME, INPUT);
  pinMode(BUTTON_SET_ON_TIME, INPUT_PULLUP);
  debouncer3.attach(BUTTON_SET_ON_TIME);
  debouncer3.interval(25);
  
  pinMode(BUTTON_SET_OFF_TIME, INPUT);
  pinMode(BUTTON_SET_OFF_TIME, INPUT_PULLUP);
  debouncer4.attach(BUTTON_SET_OFF_TIME); // Даем бибилотеке знать, к какому пину мы подключили кнопку
  debouncer4.interval(25); // Интервал, в течение которого мы не буем получать значения с пина

  // читаем значения часов-минут включения-выключения из памяти
  EEPROM.get(0, Relay_On_Hours);
  if((Relay_On_Hours < 0) || (Relay_On_Hours > 23)) { Relay_On_Hours = 21; }
  EEPROM.get(1, Relay_On_Minutes);
  if((Relay_On_Minutes < 0) || (Relay_On_Minutes > 59)) { Relay_On_Minutes = 0; }
  EEPROM.get(2, Relay_Off_Hours);
  if((Relay_Off_Hours < 0) || (Relay_Off_Hours > 23)) { Relay_Off_Hours = 8; }
  EEPROM.get(3, Relay_Off_Minutes);
  if((Relay_Off_Minutes < 0) || (Relay_Off_Minutes > 59)) { Relay_Off_Minutes = 0; }
}

void loop() {
  DateTime now = RTC.now();
  unsigned int curMinutes = now.hour()*60 + now.minute(); // текущее значение минут с начала суток
  boolean CurrentRelayStatus; // Текущий статус состояния реле
  if(curMinutes >= (Relay_Off_Hours*60 + Relay_Off_Minutes) && curMinutes <= (Relay_On_Hours*60 + Relay_On_Minutes)) { // если попадаем между временем выключения и включения (переводим всё в минуты и сравниваем)
    digitalWrite(RELAY, LOW); // то выключаем реле
    digitalWrite(LED_INFO, LOW);
    CurrentRelayStatus = false;
  } else {
    digitalWrite(RELAY, HIGH); // или включаем реле
    digitalWrite(LED_INFO, HIGH);
    CurrentRelayStatus = true;
  }
  if(LastRelayStatus != CurrentRelayStatus) { // если меняется состояние реле с момента последней проверки, то пиликаем
    led_flash();
    CurrentRelayStatus = LastRelayStatus;
  }
  Serial.print(now.hour(), DEC);
  Serial.print(':');
  Serial.print(now.minute(), DEC);
  
  boolean changed1 = debouncer1.update(); // Изменение состояния кнопки 1
  boolean changed2 = debouncer2.update(); // Изменение состояния кнопки 2
  boolean changed3 = debouncer3.update(); // Изменение состояния кнопки 3
  boolean changed4 = debouncer4.update(); // Изменение состояния кнопки 4

/*----------------- Обработка первой кнопки -------------------*/  
  if (changed1) {
    if (debouncer1.read() == HIGH) { // получаем значение первой кнопки, кнопка отпущена
      digitalWrite(LED_INFO, LOW);
   
      buttonState1 = 0;
 //     Serial.println("Button released (state 0)");
    } else { // кнопка нажата
      digitalWrite(LED_INFO, HIGH);
      buttonState1 = 1;
        switch(ClockMode) {
          case 0:
            Serial.println("Normal mode");
            break;
          case 1:
            Hours++;
            if (Hours > 23) { Hours=0; }
            Serial.print("Hours=");
            Serial.println(Hours);
            break;
          case 2:
            Minutes++;
            if (Minutes > 59) { Minutes=0; }
            Serial.print("Minutes=");
            Serial.println(Minutes);
            break;
        }
      buttonPressTimeStamp = millis();
      buttonPressTimeStamp2 = millis();
      ClockModeTimeoutTimeStamp = millis();
   }
  }
  if (buttonState1 == 1) { // кнопка зажата
    if (millis() - buttonPressTimeStamp >= 700) { // время в течении которого при зажатии кнопки не будет быстрого инкремента
      if (millis() - buttonPressTimeStamp2 >= 150) { // время между инкрементами при быстром листании
        buttonPressTimeStamp2 = millis();
        digitalWrite(LED_INFO, HIGH);
        switch(ClockMode) {
          case 0:
            Serial.println("Normal mode");
            break;
          case 1:
            Hours++;
            if (Hours > 23) { Hours=0; }
            Serial.print("Hours=");
            Serial.println(Hours);
            break;
          case 2:
            Minutes++;
            if (Minutes > 59) { Minutes=0; }
            Serial.print("Minutes=");
            Serial.println(Minutes);
            break;
        }
        ClockModeTimeoutTimeStamp = millis(); // сбрасываем таймер таймаута
      }
    }
  }
/*-------------------------------------------------------------*/

/*----------------- Обработка второй кнопки -------------------*/  
  if (changed2) {
    if (debouncer2.read() == HIGH) { // получаем значение второй кнопки
      digitalWrite(LED_INFO, LOW);
   
      buttonState2 = 0;
 //     Serial.println("Button released (state 0)");
    } else {
      digitalWrite(LED_INFO, HIGH);
      buttonState2 = 1;
      ClockMode++;
      if(ClockMode > 2) { ClockMode = 0; } // циклически переключаем режимы (редактирование часов, редактирование минут, выход из режима редактирования и сохранение настроек)
      switch(ClockMode) {
        case 0:
          if(EditModeOnTime) {
            Relay_On_Hours = Hours;
            Relay_On_Minutes = Minutes;
            EEPROM.put(0, Relay_On_Hours);
            EEPROM.put(1, Relay_On_Minutes);
            Serial.print("Set ON time: ");
            Serial.print(Relay_On_Hours);
            Serial.print(":");
            Serial.println(Relay_On_Minutes);
            EditModeOnTime = false;
            audio_relay_on_off_setup(LED_RELAY_ON_EDIT);
          } else if(EditModeOffTime) {
            Relay_Off_Hours = Hours;
            Relay_Off_Minutes = Minutes;
            EEPROM.put(2, Relay_Off_Hours);
            EEPROM.put(3, Relay_Off_Minutes);
            Serial.print("Set OFF time: ");
            Serial.print(Relay_Off_Hours);
            Serial.print(":");
            Serial.println(Relay_Off_Minutes);
            EditModeOffTime = false;
            audio_relay_on_off_setup(LED_RELAY_OFF_EDIT);
          } else {
            // здесь записать часы и минуты в DS2321
            RTC.adjust(DateTime(2020, 1, 1, Hours, Minutes, 0)); // Т.к. дата не важна, устанавливаем 1 января 2020 г., дальше часы, минуты и 0 секунд
            Serial.print("Set CLOCK time: ");
            Serial.print(Hours);
            Serial.print(":");
            Serial.println(Minutes);
            led_flash(); // мигаем/пищим, сигнализируя сохранение
          }
          Serial.println("Switch to normal mode");
          break;
        case 1:
          Hours = now.hour(); // для того, чтобы стартовать промотку часов с текущих часов, а не с, возможно, устаревшего значения часов в памяти ардуино
          Serial.println("Switch to hours");
          break;
        case 2:
          Minutes = now.minute(); // для того, чтобы стартовать промотку минут с текущих минут, а не с, возможно, устаревшего значения минут в памяти ардуино
          Serial.println("Switch to minutes");
          break;
      }
      buttonPressTimeStamp = millis();
      buttonPressTimeStamp2 = millis();
      ClockModeTimeoutTimeStamp = millis();
    }
  }
/*-------------------------------------------------------------*/

/*---- Обработка кнопки установки времени включения реле ------*/
  if (changed3) {
    if (debouncer3.read() == HIGH) { // получаем значение третьей кнопки
 //     Serial.println("Button 3 released");
    } else {
      EditModeOnTime = true;
      digitalWrite(LED_RELAY_ON_EDIT, HIGH);
      EditModeOffTime = false;
      digitalWrite(LED_RELAY_OFF_EDIT, LOW);
      EEPROM.get(0, Hours); // сразу присвоим значение часов и минут, 
      EEPROM.get(1, Minutes); // чтобы вывести на экрна и листать цифры уже от этих значений
      ClockMode = 1; // сразу редактируем часы
      ClockModeTimeoutTimeStamp = millis(); // сбрасываем таймер тайм аута
    }
  }
/*-------------------------------------------------------------*/

/*---- Обработка кнопки установки времени выключения реле ------*/
  if (changed4) {
    if (debouncer4.read() == HIGH) { // получаем значение третьей кнопки
 //     Serial.println("Button 4 released");
    } else {
      EditModeOffTime = true;
      digitalWrite(LED_RELAY_OFF_EDIT, HIGH);
      EditModeOnTime = false;
      digitalWrite(LED_RELAY_ON_EDIT, LOW);
      EEPROM.get(2, Hours);
      EEPROM.get(3, Minutes);
      ClockMode = 1;
      ClockModeTimeoutTimeStamp = millis();
    }
  }
/*-------------------------------------------------------------*/

  // Выход из режима редактирования по таймауту
  if(ClockMode != 0) {
    if(millis() - ClockModeTimeoutTimeStamp >= 3000) { // 3 секунды
      ClockMode = 0;
      EditModeOnTime = false;
      EditModeOffTime = false;
      digitalWrite(LED_RELAY_ON_EDIT, LOW);
      digitalWrite(LED_RELAY_OFF_EDIT, LOW);
      Serial.println("Switch to normal mode by timeout");
    }
  }
}
void audio_relay_on_off_setup(byte led) { // мигаем и пиликаем при сохранении настроек выключения/выключения реле
  digitalWrite(led, LOW);
  delay(200);
  digitalWrite(led, HIGH);
  tone(speaker, NOTE_G5, 100);
  delay(100);
  digitalWrite(led, LOW);
  delay(200);
  digitalWrite(led, HIGH);
  tone(speaker, NOTE_G5, 100);
  delay(100);
  digitalWrite(led, LOW);
  delay(200);
}

void audio() {
  tone(speaker, NOTE_E5, 100);
  delay(300);
  tone(speaker, NOTE_E5, 100);
  delay(300);
  tone(speaker, NOTE_E5, 100);
  delay(300);
}

void led_flash() {
  digitalWrite(LED_INFO, HIGH);
  tone(speaker, NOTE_B5, 50);
  delay(50);
  digitalWrite(LED_INFO, LOW);
  delay(100);
  digitalWrite(LED_INFO, HIGH);
  tone(speaker, NOTE_B5, 50);
  delay(50);
  digitalWrite(LED_INFO, LOW);
  delay(100);
  digitalWrite(LED_INFO, HIGH);
  tone(speaker, NOTE_B5, 50);
  delay(50);
  digitalWrite(LED_INFO, LOW);
  delay(100);
}
