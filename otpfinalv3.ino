#include <WiFi.h>
#include <HTTPClient.h>
#include <SPI.h>
#include <MFRC522.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include "MC_Keypad_I2C.h"

// WiFi
const char* ssid = "daffa";
const char* password = "12345678";

// Telegram
const char* botToken = "7774550900:AAGToHbXxSuRHFFTvumKsh9b804ZpCT5ulI";
const char* groupChatID = "-4918317267";

// RFID
#define SS_PIN 10
#define RST_PIN 14
#define SCK_PIN 12
#define MISO_PIN 13
#define MOSI_PIN 11
MFRC522 rfid(SS_PIN, RST_PIN);

// Buzzer
const int buzzerPin = 21;

// LCD & Keypad
LiquidCrystal_I2C lcd(0x27, 16, 2);
KeypadI2C keypad(0x21);
int lcdCol = 0;

// OTP State
String expectedOTP = "";
String inputOTP = "";
bool waitingForOTP = false;
unsigned long otpStartTime = 0;
const unsigned long otpTimeout = 30000;

// Mahasiswa data
struct Mahasiswa {
  String uid;
  String chatID;
  String nama;
  String nim;
};

Mahasiswa daftarMahasiswa[] = {
  {"32383402", "7900493277", "M. Daffa Muis", "11210910000030"},
  {"EABA4005", "6611732419", "M. RAFI HASAN", "11220910000087"}
};

const int jumlahMahasiswa = sizeof(daftarMahasiswa) / sizeof(Mahasiswa);

String currentStudentUID = "";
String currentStudentChatID = "";
String currentStudentNama = "";
String currentStudentNIM = "";

void setup() {
  Serial.begin(115200);

  // WiFi
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi Connected!");

  // SPI & RFID
  SPI.begin(SCK_PIN, MISO_PIN, MOSI_PIN, SS_PIN);
  rfid.PCD_Init();
  Serial.println("Tempelkan kartu RFID...");

  // LCD
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("Sistem Siap");

  // Keypad
  keypad.begin();

  // Telegram Notifikasi
  sendTelegramMessage(groupChatID, "✅ IoT Absensi Siap Digunakan!");
}

void loop() {
  if (!waitingForOTP && rfid.PICC_IsNewCardPresent() && rfid.PICC_ReadCardSerial()) {
    String uidString = "";
    for (byte i = 0; i < rfid.uid.size; i++) {
      byte b = rfid.uid.uidByte[i];
      if (b < 0x10) uidString += "0";
      uidString += String(b, HEX);
    }
    uidString.toUpperCase();
    Serial.print("UID: ");
    Serial.println(uidString);

    bool found = false;
    for (int i = 0; i < jumlahMahasiswa; i++) {
      if (uidString == daftarMahasiswa[i].uid) {
        expectedOTP = String(random(100000, 999999));
        currentStudentUID = daftarMahasiswa[i].uid;
        currentStudentChatID = daftarMahasiswa[i].chatID;
        currentStudentNama = daftarMahasiswa[i].nama;
        currentStudentNIM = daftarMahasiswa[i].nim;

        sendTelegramMessage(currentStudentChatID.c_str(), "OTP Anda: " + expectedOTP);
        lcd.clear();
        lcd.print(currentStudentNama);
        lcd.setCursor(0, 1);
        lcd.print("Masukkan OTP");
        waitingForOTP = true;
        otpStartTime = millis();
        lcdCol = 0;
        inputOTP = "";
        beepBuzzer();
        found = true;
        break;
      }
    }

    if (!found) {
      lcd.clear();
      lcd.print("Kartu tidak");
      lcd.setCursor(0, 1);
      lcd.print("dikenal!");
      delay(2000);
      lcd.clear();
      lcd.print("Tempelkan kartu");
    }

    rfid.PICC_HaltA();
  }

  // Timeout OTP
  if (waitingForOTP && millis() - otpStartTime > otpTimeout) {
    lcd.clear();
    lcd.print("OTP kadaluarsa");
    delay(2000);
    resetState();
  }

  // OTP Input dari Keypad
  if (waitingForOTP) {
    char key = keypad.getKey();
    if (key) {
      String val;

      switch (key) {
        case 'A': val = "4"; break;
        case 'C': val = "5"; break;
        case 'D': val = "6"; break;
        case '#':  // ENTER
          if (inputOTP == expectedOTP) {
            lcd.clear();
            lcd.print("OTP Benar!");
            String message = "Absensi berhasil: " + currentStudentNama + " (" + currentStudentNIM + ")";
            sendTelegramMessage(groupChatID, message);

            // Kirim ke Google Sheet
            sendToGoogleSheet(currentStudentUID, currentStudentNama, currentStudentNIM);
          } else {
            lcd.clear();
            lcd.print("OTP Salah!");
          }
          delay(2000);
          resetState();
          return;

        case '*': // DELETE
          if (inputOTP.length() > 0) {
            inputOTP.remove(inputOTP.length() - 1);
            lcdCol--;
            if (lcdCol < 0) lcdCol = 0;
            lcd.setCursor(lcdCol, 1);
            lcd.print(" ");
            lcd.setCursor(lcdCol, 1);
          }
          return;

        default:
          val = String(key);
          break;
      }

      inputOTP += val;
      lcd.setCursor(lcdCol, 1);
      lcd.print(val);
      lcdCol += val.length();
      if (lcdCol >= 16) lcdCol = 15;  // Prevent overflow
    }
  }
}

void sendTelegramMessage(const char* chat_id, String message) {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    String url = "https://api.telegram.org/bot" + String(botToken) +
                 "/sendMessage?chat_id=" + String(chat_id) +
                 "&text=" + message;
    http.begin(url);
    int httpResponseCode = http.GET();
    if (httpResponseCode > 0) {
      Serial.println("Telegram: " + message);
    } else {
      Serial.print("Error kirim: ");
      Serial.println(httpResponseCode);
    }
    http.end();
  } else {
    Serial.println("WiFi tidak terhubung.");
  }
}

void beepBuzzer() {
  pinMode(buzzerPin, OUTPUT);
  digitalWrite(buzzerPin, HIGH);
  delay(200);
  pinMode(buzzerPin, INPUT);
}

void resetState() {
  lcd.clear();
  lcd.print("Tempelkan kartu");
  expectedOTP = "";
  currentStudentUID = "";
  currentStudentChatID = "";
  currentStudentNama = "";
  currentStudentNIM = "";
  inputOTP = "";
  waitingForOTP = false;
  lcdCol = 0;
}

void sendToGoogleSheet(String uid, String nama, String nim) {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    String url = "https://script.google.com/macros/s/AKfycbybE4JSxMyl_j3w4DBAyfs2-dTiaBySMJBVE3VxeF6rLPY_IoFJlU0pfBPUA9JERTC6/exec"; // Ganti dengan URL kamu

    http.begin(url);
    http.addHeader("Content-Type", "application/json");

    String jsonData = "{\"uid\":\"" + uid + "\",\"nama\":\"" + nama + "\",\"nim\":\"" + nim + "\"}";
    int httpResponseCode = http.POST(jsonData);

    if (httpResponseCode > 0) {
      Serial.println("✅ Data disimpan ke Google Sheet.");
    } else {
      Serial.print("❌ Gagal kirim: ");
      Serial.println(httpResponseCode);
    }
    http.end();
  }
}

