//Termosztát figyelő - Küldő

#include "avr/wdt.h"   //Watchdog
#include "avr/boot.h"  //Watchdog
#include <SPI.h>       //nRF24L01+ kommunikációs protokol
#include <RF24.h>      //nRF24L01+
#include "printf.h"

const uint8_t WirelessChannel[7] = { "Kazan1" };  //Kommunikációs csatorna azonosító, egyeznie kell a Küldő és Fogadó között

#define Thermostat_COOL_PIN 2  //Pin, amely figyeli a termosztát HŰTÉS jelét (AC jelet optocsatolós izolációs modullal észlelve)
#define Thermostat_HEAT_PIN 3  //Pin, amely figyeli a termosztát FŰTÉS jelét (AC jelet optocsatolós izolációs modullal észlelve)
#define CSN_PIN 9              //nRF24L01+ portok Arduino Nano vagy RF-Nano számára
#define CE_PIN 10              //nRF24L01+ portok Arduino Nano vagy RF-Nano számára
RF24 Wireless(CE_PIN, CSN_PIN);

//Változók és konstansok a vezérlő üzenetek időzítéséhez
unsigned long LastMessageSent = 0;           //Mikor lett az utolsó üzenet elküldve
const unsigned long MessageInterval = 5000;  //Vezérlőüzenet küldése 5 másodpercenként
const uint8_t RetryDelay = 5;                //Mennyi ideig várjon az egyes újrapróbálkozások között, 250us többszöröse, max 15. 0 azt jelenti 250us, 15 azt jelenti 4000us (4ms).
const uint8_t RetryCount = 15;               //Hány újrapróbálkozás után adja fel, max 15
bool ThermostateCurrentCoolState = false;    //Aktuális beolvasott HŰTÉSI jel
bool ThermostateCurrentHeatState = false;    //Aktuális beolvasott FŰTÉSI jel
bool ThermostateConfirmedState = false;      //Visszaigazolt jel
bool ChangeDetected = true;                  //true: A termosztát állapotában változást észlelt
bool ConnectionLost = true;                  //true: ha a vevő nem válaszol

struct commandTemplate  //Max 32 byte. A küldő által küldött parancs sablonja. A küldőnek és a fogadónak is ismernie kell ezt a struktúrát
{
  bool FancoilNewState;
};
struct commandTemplate Command = { 0 };  //Az elküldendő parancs van itt tárolva

struct responseTemplate  //Max 32 byte. A fogadó által visszaküldött válasz sablonja. A küldőnek és a fogadónak is ismernie kell ezt a struktúrát
{
  bool FancoilState;
};
struct responseTemplate Response;  //A vevőtől kapott válasz itt lesz tárolva, a vevő aktuális állapotát képviseli

void setup() {
  Serial.begin(115200);
  Serial.println(F("******************************************"));
  Serial.println(F("Termosztát figyelő - Küldő indítása"));
  Serial.print(F("Watchdog indítása..."));
  wdt_enable(WDTO_8S);  //Watchdog 8 másodperces időtúllépésre állítva (leghosszabb intervallum)
  boot_rww_enable();
  Serial.println(F("kész"));

  Serial.print(F("Vezeték nélküli küldő beállítása..."));
  if (!Wireless.begin()) {
    Serial.println(F("a rádió hardver nem válaszol!!"));
    while (1) {}  // végtelen ciklusban tartás
  }
  Wireless.setPALevel(RF24_PA_LOW);
  Wireless.setDataRate(RF24_250KBPS);
  Wireless.enableDynamicPayloads();
  Wireless.enableAckPayload();
  Wireless.setRetries(RetryDelay, RetryCount);
  Wireless.stopListening();
  Wireless.openWritingPipe(WirelessChannel);
  Serial.println(F("kész"));
  printf_begin();
  Wireless.printPrettyDetails();

  pinMode(Thermostat_COOL_PIN, INPUT_PULLUP);
  pinMode(Thermostat_HEAT_PIN, INPUT_PULLUP);
  Serial.println(F(""));
  Serial.println(F("Termosztát figyelő - Küldő fut"));
  readThermostat(true);
}

void loop() {
  readThermostat(false);
  if (ChangeDetected)
    sendCommand();
  delay(1000);  //Várakozás 1 másodpercig
  wdt_reset();
}

void readThermostat(bool ForceSync) {  // hogy lásd, új adatok kerülnek elküldésre
  ThermostateCurrentCoolState = !digitalRead(Thermostat_COOL_PIN);
  ThermostateCurrentHeatState = !digitalRead(Thermostat_HEAT_PIN);
  Command.FancoilNewState = ThermostateCurrentCoolState || ThermostateCurrentHeatState;
  Serial.print("Termosztát HŰTÉS: ");
  Serial.print(ThermostateCurrentCoolState ? "BE, " : "KI, ");
  Serial.print("Termosztát FŰTÉS: ");
  Serial.println(ThermostateCurrentHeatState ? "BE" : "KI");
  if (ForceSync || (ThermostateCurrentCoolState || ThermostateCurrentHeatState) != ThermostateConfirmedState || millis() - LastMessageSent >= MessageInterval) {
    ChangeDetected = true;
  }
}

void sendCommand() {
  Serial.print(F("Parancs küldése és válasz várása..."));
  bool Result = Wireless.write(&Command, sizeof(Command));

  Serial.print(F("Adat küldve, "));
  if (Result) {
    if (Wireless.isAckPayloadAvailable()) {
      ConnectionLost = false;
      if (Response.FancoilState == (ThermostateCurrentCoolState || ThermostateCurrentHeatState)) {
        ChangeDetected = false;
        ThermostateConfirmedState = Response.FancoilState;
      }
      Wireless.read(&Response, sizeof(Response));
      Serial.print(F("visszaigazolás megkapva ["));
      Serial.print(sizeof(Response));
      Serial.print(F(" bájt]: "));
      Serial.print(F("FancoilState: "));
      Serial.print(Response.FancoilState);
      Serial.println();
    } else {
      Serial.println(F("visszaigazolás megkapva adat nélkül."));  //Abnormális állapot, válasz adatnak kellene lennie
      ConnectionLost = true;
    }
  } else {
    Serial.println(F("nincs válasz."));
    ConnectionLost = true;
  }
  LastMessageSent = millis();
}