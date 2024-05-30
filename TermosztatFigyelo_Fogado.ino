//Termosztát figyelő - Fogadó

#include "avr/wdt.h"   //Watchdog
#include "avr/boot.h"  //Watchdog
#include <SPI.h>       //nRF24L01+ kommunikációs protokol
#include <RF24.h>      //nRF24L01+
#include "printf.h"

const byte WirelessChannel[7] = { "Kazan1" };  //Kommunikációs csatorna azonosító, egyeznie kell a Küldő és Fogadó között
#define FANCOIL_PIN 2                          //Pin, ami vezérli a FanCoil-t
#define CSN_PIN 9                              //nRF24L01+ portok Arduino Nano vagy RF-Nano számára
#define CE_PIN 10                              //nRF24L01+ portok Arduino Nano vagy RF-Nano számára
RF24 Wireless(CE_PIN, CSN_PIN);

//Változók és konstansok a vezérlő üzenetek időzítéséhez
const unsigned long MessageTimeout = 60000;  //60 másodperces időtúllépés: Kapcsolja ki a fancoilt, ha az adó elhallgatott
unsigned long LastMessageTimestamp = 0;      //Mikor lett az utolsó üzenet elküldve

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
  Serial.println(F("Termosztát figyelő - Fogadó indítása"));
  Serial.print(F("Watchdog indítása..."));
  wdt_enable(WDTO_8S);  //Watchdog 8 másodperces időtúllépésre állítva (leghosszabb intervallum)
  boot_rww_enable();
  Serial.println(F("kész"));

  Serial.print(F("Vezeték nélküli vevő beállítása..."));
  if (!Wireless.begin()) {
    Serial.println(F("A vezeték nélküli hardver nem válaszol!!"));
    while (1) {}  // végtelen ciklusban tartás
  }
  Wireless.setPALevel(RF24_PA_LOW);
  Wireless.setDataRate(RF24_250KBPS);
  Wireless.enableDynamicPayloads();
  Wireless.enableAckPayload();
  Wireless.openReadingPipe(1, WirelessChannel);
  Wireless.startListening();
  Serial.println(F("kész, parancsra vár"));
  printf_begin();
  Wireless.printPrettyDetails();
  pinMode(FANCOIL_PIN, OUTPUT);
  Serial.println(F(""));
  Serial.println(F("Termosztát figyelő - Fogadó fut"));
}

void loop() {
  if (Wireless.available()) {  //Amikor parancsot kapunk
    LastMessageTimestamp = millis();
    Wireless.read(&Command, sizeof(Command));  //Töltse be a parancsot a Command változóba
    Serial.print(F("Parancs fogadva ["));
    Serial.print(sizeof(Command));
    Serial.println(F(" bájt]"));
    Serial.print(F("  FancoilNewState: "));
    Serial.print(Command.FancoilNewState);
    Serial.println();

    digitalWrite(FANCOIL_PIN, Command.FancoilNewState);
    Response.FancoilState = digitalRead(FANCOIL_PIN);
    Wireless.writeAckPayload(1, &Response, sizeof(Response));  // új visszaigazolás betöltése amit a következő parancs fogadásakor küld majd ki
  }
  delay(500);  //Várakozás 0.5 másodpercig
  if (millis() - LastMessageTimestamp >= MessageTimeout) {
    Serial.println("Szinkronizáció időtúllépés, Fancoil KIKAPCSOLÁSA");
    digitalWrite(FANCOIL_PIN, false);
    Response.FancoilState = digitalRead(FANCOIL_PIN);
    Wireless.writeAckPayload(1, &Response, sizeof(Response));  // új visszaigazolás betöltése amit a következő parancs fogadásakor küld majd ki
  }
  wdt_reset();
}