/*
  AstroDue V5.9.5.1 - Firmware Ultimate RGBW (Digital)
  Plateforme : Arduino Due (SAM3X8E)
  Ecran : Nextion HMI (Serial1)
  RTC : DS3231 (I2C)
  CORRECTIONS :
  - Fix calcul Lune : Ajout termes Evection/Variation dans la boucle de scan.
  - LOGIQUE DYNAMIQUE : Aube/Crépuscule progressifs & Nuit selon phase lunaire.
  - COMMANDE SETDATE (Format: SETDATE 2025 6 21)
  - COMMANDE SETTIME (Format: SETTIME 18 30)
  - COMMANDE SETGPS DMS (Format: SETGPS LatD LatM LatS LngD LngM LngS UTC)
    -> Exemple Paris (UTC+1): SETGPS 48 51 23 2 21 07 1
  - Une Limite à 40% de la Puissance Totale a été Fixer pour Poisson Sensible Mais Peut être Modifie
    -> Par Exemple Pour 100% : const float MAX_POWER_AXO =1.0
  - L'intensite Lumineuse ne Pourra pas être Depasser, le Dimmer est Ajuster a Sont Max Limité
  - Passage au Multifichier
  - Amelioration Paralaxe pour Imprecision sur l'Equateur
*/
#include <Wire.h>
#include <RTClib.h>
#include <math.h>
#include <Adafruit_NeoPixel.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include "Globals.h"

// --- CONFIGURATION LED (3 FILS) ---
#define LED_PIN 5       // Pin Data
#define NUM_LEDS 48     // Nombre de LEDs sur le ruban
#define BRIGHTNESS 255  // Luminosité globale (0-255)

#define ONE_WIRE_BUS 6
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);
float currentTemp = 0.0;
unsigned long lastTempRequest = 0;

// --- CONFIGURATION EEPROM EXTERNE Sur RTC (I2C) ---
#define EEPROM_MIN_ADDR 0x50  // Adresse I2C minimale (souvent 0x50 pour les 24C32/24C256)
#define EEPROM_MAX_ADDR 0x57  // Adresse I2C maximale
#define EEPROM_MEM_START 0    // L'index de l'octet où l'on commence à écrire (0 = début)
byte foundEepromAddr = 0;
// Timers
unsigned long lastSecondLoop = 0;
unsigned long lastScreenRefresh = 0;
// --- 1. SCANNER L'ADRESSE EEPROM ---
byte scanEEPROM(byte startAddr, byte endAddr) {
  // On utilise uint16_t pour garantir que le test 'addr <= endAddr'
  // puisse devenir faux même si endAddr est égal à 255.
  for (uint16_t addr = startAddr; addr <= endAddr; addr++) {
    Wire.beginTransmission((byte)addr);
    if (Wire.endTransmission() == 0) {
      Serial.print(F("EEPROM trouvée à l'adresse : 0x"));
      Serial.println(addr, HEX);
      return (byte)addr;
    }
  }
  Serial.println(F("Erreur : Aucune EEPROM détectée."));
  return 0;
}

// --- SAUVEGARDER EEPROM (Lat, Lng, UTC) ---
void saveGPS(double lat, double lng, double utc) {
  // Si EEPROM pas Trouvé Retour
  if (foundEepromAddr == 0) return;
  const byte* pLat = reinterpret_cast<const byte*>(&lat);
  const byte* pLng = reinterpret_cast<const byte*>(&lng);
  const byte* pUtc = reinterpret_cast<const byte*>(&utc);
  byte magic = 66;  // Signature pour Validation Donnée
  // 1. Magic + Lat (Adresse 0)
  Wire.beginTransmission(foundEepromAddr);
  Wire.write(highByte(EEPROM_MEM_START));                             // Adresse mémoire MSB (16-bit)
  Wire.write(lowByte(EEPROM_MEM_START));                              // Adresse mémoire LSB
  Wire.write(magic);                                                  // 1 octet
  for (int i = 0; i < (int)sizeof(double); i++) Wire.write(pLat[i]);  // 8 octets
  if (Wire.endTransmission() != 0) {
    Serial.println(F("EEPROM Erreur : Échec écriture Lat"));
    return;
  }
  delay(10);
  // 2. Longitude (Adresse 9 : Magic + Lat)
  int adrLng = EEPROM_MEM_START + 1 + (int)sizeof(double);
  Wire.beginTransmission(foundEepromAddr);
  Wire.write(highByte(adrLng));
  Wire.write(lowByte(adrLng));
  for (int i = 0; i < (int)sizeof(double); i++) Wire.write(pLng[i]);
  if (Wire.endTransmission() != 0) {
    Serial.println(F("EEPROM Erreur : Échec écriture Long"));
    return;
  }
  delay(10);
  // 3. Offset UTC (Adresse 17 : Magic + Lat + Lng)
  int adrUtc = adrLng + (int)sizeof(double);
  Wire.beginTransmission(foundEepromAddr);
  Wire.write(highByte(adrUtc));
  Wire.write(lowByte(adrUtc));
  for (int i = 0; i < (int)sizeof(double); i++) Wire.write(pUtc[i]);
  if (Wire.endTransmission() != 0) {
    Serial.println(F("EEPROM Erreur : Échec écriture UTC"));
    return;
  }
  delay(10);
  Serial.println(F("GPS et UTC sauvegardés sur l'EEPROM externe."));
}

void loadGPS() {
  // 1. Si EEPROM pas Trouvé en Sort
  if (foundEepromAddr == 0) {
    Serial.println(F("ERREUR : Aucune EEPROM physique détectée sur le bus I2C !"));
    Serial.println(F("-> Utilisation des Coordonnées GPS par défaut."));
    return;
  }
  // 2. Positionner la lecture au début (Adresse 0)
  Wire.beginTransmission(foundEepromAddr);
  Wire.write(highByte(EEPROM_MEM_START));
  Wire.write(lowByte(EEPROM_MEM_START));
  if (Wire.endTransmission() != 0) {
    Serial.print(F("EEPROM Erreur Critique : Impossible de joindre l'adresse 0x"));
    Serial.println(foundEepromAddr, HEX);
    return;
  }
  // 3. Demander les 25 octets : Magic(1) + Lat(8) + Lng(8) + UTC(8)
  // Sur Due, double = 8 octets. Donc 1 + 8 + 8 + 8 = 25.
  Wire.requestFrom(foundEepromAddr, (byte)25);
  if (Wire.available() >= 25) {
    byte testMagic = Wire.read();
    if (testMagic == 66) {  // On vérifie la signature
      Serial.println(F("EEPROM : Signature valide. Chargement des coordonnées..."));
      // Lecture Latitude (8 octets)
      byte* pLat = reinterpret_cast<byte*>(&currentLat);
      for (int i = 0; i < 8; i++) pLat[i] = Wire.read();
      // Lecture Longitude (8 octets)
      byte* pLng = reinterpret_cast<byte*>(&currentLng);
      for (int i = 0; i < 8; i++) pLng[i] = Wire.read();
      // Lecture UTC (8 octets)
      double tempUtc;
      byte* pUtc = reinterpret_cast<byte*>(&tempUtc);
      for (int i = 0; i < 8; i++) pUtc[i] = Wire.read();
      manualOffset = (int)tempUtc;  // Conversion vers int global
      Serial.print(F("GPS Chargé : "));
      Serial.print(currentLat, 5);
      Serial.print(F(" / "));
      Serial.print(currentLng, 5);
      Serial.print(F(" (UTC: "));
      Serial.print(manualOffset);
      Serial.println(F(")"));
    } else {
      Serial.println(F("EEPROM : Signature Magic incorrecte. Utilisation des valeurs par défaut."));
    }
  }
}

void sendNextion(String cmd) {
  //serial pour test debug simulateur nextion
  Serial1.print(cmd);
  Serial1.write(0xFF);
  Serial1.write(0xFF);
  Serial1.write(0xFF);
}
//conversion decimal vers degrer minutes seconde pour affichage Nextion
String convertToDMS(float decimal, bool isLatitude) {
  String direction;
  if (isLatitude) {
    direction = (decimal >= 0) ? "N" : "S";
  } else {
    direction = (decimal >= 0) ? "E" : "O";
  }

  float absVal = abs(decimal);
  int d = (int)absVal;
  int m = (int)((absVal - d) * 60);
  int s = (int)((absVal - d - m / 60.0) * 3600);

  // On utilise \xB0 qui est le code hexadécimal pur du symbole degré
  String result = String(d) + "\xB0" + (m < 10 ? "0" : "") + String(m) + "'" + (s < 10 ? "0" : "") + String(s) + "''" + direction;

  return result;
}
// --- Mise a Jours et Rafraichhissement de L'Ecran P0
void refreshScreen() {
  if (currentPage == 0) {
    // --- 1. PRÉPARATION DES VARIABLES DE TEMPS ---
    char dateBuf[12], timeBuf[10];
    sprintf(dateBuf, "%02d/%02d/%04d", now.day(), now.month(), now.year());
    sprintf(timeBuf, "%02dh%02d", now.hour(), now.minute());
    // --- 2. LOGIQUE DES PHASES LUNAIRES (Calcul ID Image) ---
    int nextionPicId;
    switch (pIdx) {
      case 0: nextionPicId = 9; break;   // N. Lune
      case 1: nextionPicId = 10; break;  // P. Croiss.
      case 2: nextionPicId = 11; break;  // P. Quartier
      case 3: nextionPicId = 12; break;  // Gibbeuse +
      case 4: nextionPicId = 5; break;   // Pleine Lune
      case 5: nextionPicId = 6; break;   // Gibbeuse -
      case 6: nextionPicId = 7; break;   // D. Quartier
      case 7: nextionPicId = 8; break;   // D. Croiss.
      default: nextionPicId = 9; break;
    }
    // --- DONNÉES ASTRO (Page 0) ---
    sendNextion("sun_r.txt=\"" + textSunRise + "\"");
    sendNextion("sun_s.txt=\"" + textSunSet + "\"");
    sendNextion("moon_r.txt=\"" + textMoonRise + "\"");
    sendNextion("moon_s.txt=\"" + textMoonSet + "\"");
    sendNextion("t_phase.txt=\"" + textPhase + "\"");
    sendNextion("n_illum.val=" + String(moonIllum));
    sendNextion("pic.pic=" + String(nextionPicId));

    // --- HEURE ET INFOS GÉO (Page 0) ---
    sendNextion("t_date.txt=\"" + String(dateBuf) + "\"");
    sendNextion("t_time.txt=\"" + String(timeBuf) + "\"");
    // ----indique si utc manuel ou auto
    String modeStr = isAutoMode ? "AUTO" : "MAN";
    String offsetStr = (currentUtcOffset >= 0 ? "+" : "") + String(currentUtcOffset);
    sendNextion("t_info.txt=\"UTC" + offsetStr + " " + modeStr + "\"");
    // --- HEURE ET INFOS GÉO (Page 0) ---
    sendNextion("t_lat.txt=\"" + convertToDMS(currentLat, true) + "\"");
    sendNextion("t_lng.txt=\"" + convertToDMS(currentLng, false) + "\"");
    //---  envoie temperature
    // 4. Envoye au Nextion temperature qui a deja demande dans le loop
    if (currentTemp < -30) {
      sendNextion("temp.txt=\"--.-\"");
    } else {
      sendNextion("temp.txt=\"" + String(currentTemp, 1) + "\"");
    }
    // --- NIVEAUX LED (Page 0) ---
    sendNextion("led_r.val=" + String(map((int)currentR, 0, 255, 0, 100)));
    sendNextion("led_g.val=" + String(map((int)currentG, 0, 255, 0, 100)));
    sendNextion("led_b.val=" + String(map((int)currentB, 0, 255, 0, 100)));
    sendNextion("led_w.val=" + String(map((int)currentW, 0, 255, 0, 100)));
    // Puissance lumiere des leds
    if (isMaintenanceMode) {
      sendNextion("t_pwr.txt=\"MAINT 100%\"");
    } else {
      // On utilise directement seasonalFactor qui est calculé dans calculateAstroData
      // (qui s'exécute à minuit et au démarrage).
      // On multiplie par 100 pour avoir un pourcentage.
      int displayFactor = (int)(seasonalFactor * 100);
      // Maintenant que displayFactor est mis à jour, on l'affiche
      sendNextion("t_pwr.txt=\"" + String(displayFactor) + "%\"");
    }
  } else if (currentPage == 1) {
    // --- RÉGLAGES SETTINGS (Page 1) ---
    sendNextion("day.val=" + String(now.day()));
    sendNextion("month.val=" + String(now.month()));
    sendNextion("year.val=" + String(now.year()));

    sendNextion("heure.val=" + String(now.hour()));
    sendNextion("minute.val=" + String(now.minute()));
    sendNextion("seconde.val=" + String(now.second()));

    String prefix = (manualOffset >= 0) ? "+" : "";
    sendNextion("t_utc.txt=\"" + prefix + String(manualOffset) + "\"");
    sendNextion("bt_DST.val=" + String(isDstActive ? 1 : 0));

    // ENVOI SYSTEMATIQUE du jour de la semaine (plus de static lastDOW)
    String jours[] = { "DIMANCHE", "LUNDI", "MARDI", "MERCREDI", "JEUDI", "VENDREDI", "SAMEDI" };
    sendNextion("weektext.txt=\"" + jours[now.dayOfTheWeek()] + "\"");
  } else if (currentPage == 2) {
    // Calcul des DMS (Degrés Minutes Secondes)
    double absLat = abs(editLat);
    int dLat = (int)absLat;
    int mLat = (int)((absLat - dLat) * 60);
    int sLat = (int)round((absLat - dLat - mLat / 60.0) * 3600);

    double absLng = abs(editLng);
    int dLng = (int)absLng;
    int mLng = (int)((absLng - dLng) * 60);
    int sLng = (int)round((absLng - dLng - mLng / 60.0) * 3600);

    // 2. ENVOI AU NEXTION
    // Pour la Latitude
    sendNextion("latdeg.val=" + String(dLat));
    sendNextion("latmin.val=" + String(mLat));
    sendNextion("latsec.val=" + String(sLat));
    sendNextion("latgeo.txt=\"" + String(editLat >= 0 ? "N" : "S") + "\"");
    sendNextion("ref latgeo");  // Force le Nextion à redessiner latgeo avec son fond picc
    // Pour la Longitude
    sendNextion("lngdeg.val=" + String(dLng));
    sendNextion("lngmin.val=" + String(mLng));
    sendNextion("lngsec.val=" + String(sLng));
    sendNextion("lnggeo.txt=\"" + String(editLng >= 0 ? "E" : "O") + "\"");
    sendNextion("ref lnggeo");  // Force le redessin
  }
}
// --- FONCTION DE LECTURE Nextion ---
void readNextionCommands() {
  static uint8_t buffer[32];
  static size_t pos = 0;
  static int ffCount = 0;
  while (Serial1.available()) {
    uint8_t c = Serial1.read();
    if (c == 0xFF) {
      ffCount++;
      if (ffCount == 3) {
        if (pos > 0) {
          // on envoie le buffer brut et sa taille
          processCommand(buffer, pos);
        }
        pos = 0;
        ffCount = 0;
      }
    } else {
      ffCount = 0;
      if (pos < sizeof(buffer) && c != 0x1A) {
        buffer[pos++] = c;
      }
    }
  }
}
// --- Fonction WatchDog pour sonde tenperature
bool temperatureDisponible() {
  static unsigned long chrono = millis();
  const unsigned long timeout = 3000ul;
  // 1. Vérifier si la puce a fini son travail
  if (sensors.isConversionComplete()) {
    float tempTest = sensors.getTempCByIndex(0);  // On lit le résultat
    chrono = millis();                            // Reset du watchdog
    // 2. Vérifier si la valeur est réaliste (Sécurité physique entre -10° et +50°)
    if (tempTest != DEVICE_DISCONNECTED_C && tempTest > -10.0 && tempTest < 50.0) {
      currentTemp = tempTest;  // On ne met à jour la variable globale que si c'est valide
    }
    // 3. On retourne VRAI dans TOUS les cas pour dire à la loop() :
    // "La puce a fini, tu peux lancer la prochaine conversion !"
    return true;
  }
  // 3. Gestion du Timeout (Sonde plantée ou débranchée)
  if (millis() - chrono >= timeout) {
    Serial.println(F("ALERTE : Sonde DS18B20 hors ligne ou Hors Limte Fixé !"));
    // Tentative de réinitialisation logicielle du bus OneWire
    sensors.begin();
    sensors.requestTemperatures();
    chrono = millis();
  }
  return false;
}

void setup() {
  // Configuration du Watchdog pour un délai d'environ 16 secondes
  // Si loop() met plus de 16s sans exécuter WDT_CR, la carte redémarre.
  WDT_Enable(WDT, WDT_MR_WDRSTEN | WDT_MR_WDV(0xFFF) | WDT_MR_WDD(0xFFF));
  // 1. Initialisation des ports série (TOUJOURS EN PREMIER)
  Serial.begin(115200);    // Port USB (PC)
  Serial1.begin(115200);   // Port Écran Nextion (Pins 18/19)
  Serial1.setTimeout(10);  // Rapidité pour le Due
  delay(1000);             // Laisse le temps au port série de se stabiliser
  Serial.println("--- ASTRODUE BOOTING ---");
  // 2. Initialisation du ruban LED
  strip.begin();
  strip.setBrightness(BRIGHTNESS);
  // TEST COULEURS PURS pour savoir si RGBW ou GRBW
  // mon SK6812 est en GRBW en interne mais prend le RGBW en configuration led
  strip.setPixelColor(0, strip.Color(255, 0, 0, 0));  // Vert pur
  strip.show();
  delay(2000);
  strip.setPixelColor(0, strip.Color(0, 255, 0, 0));  // Rouge pur
  strip.show();
  delay(2000);
  strip.setPixelColor(0, strip.Color(0, 0, 255, 0));  // Bleu pur
  strip.show();
  delay(2000);
  strip.setPixelColor(0, strip.Color(0, 0, 0, 255));  // Blanc pur
  strip.show();
  delay(2000);
  strip.fill(0);
  strip.show();  // On éteint tout avant de continuer  // Éteint tout au début
  Serial.println("LEDs : OK");
  // 3. Initialisation Bus I2C pour le RTC
  Wire.begin();
  Serial.println("Communication I2C : OK");
  // --- Scan de l'EEPROM ---
  foundEepromAddr = scanEEPROM(EEPROM_MIN_ADDR, EEPROM_MAX_ADDR);
  if (foundEepromAddr != 0) {
    // Si trouvée, on peut charger les données immédiatement
    loadGPS();
  } else {
    Serial.println(F("SYSTEM : EEPROM non trouvée. Sauvegarde désactivée."));
  }
  // 4. Forcer l'écran sur la Page 0 dès le départ
  sendNextion("page 0");
  delay(100);
  sendNextion("t13.txt=\"\"");  // Efface les anciens messages d'Alerte
    // --- CONFIGURATION SÉCURITÉ AXOLOTL SUR NEXTION ---
  int maxSlider = (int)(MAX_POWER_AXO * 100);
  // Envoi des bornes au slider
  sendNextion("settings.l_dimmer.minval=0");
  sendNextion("settings.l_dimmer.maxval=" + String(maxSlider));
  // Positionnement du curseur au max de sécurité
  sendNextion("settings.l_dimmer.val=" + String(maxSlider));
  // Mise a Jour duchamp texte qui affiche le %
  sendNextion("settings.n1.val=" + String(maxSlider));
  // Synchronisation de la variable globale
  intensiteManuelle = MAX_POWER_AXO;
  // 5. Test de présence du RTC
  if (!rtc.begin()) {
    // Alerte sur l'écran Nextion
    sendNextion("t13.txt=\"ERREUR: RTC DS3231 INTROUVABLE\"");
    sendNextion("t13.pco=63488");  // Texte rouge
    // Alerte sur PC
    Serial.println("ERREUR CRITIQUE : RTC introuvable !");
    while (1)
      ;  // Stop
  }
  Serial.println("RTC DS3231 : Detecte !");
  // 6. Calcul initial et affichage
  now = rtc.now();
  // GESTION PERTE ALIMENTATION RTC
  if (rtc.lostPower()) {
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }
  calculateAstroData();  // Calcule levers/couchers et seasonalFactor
  checkRelayAndLeds();   // Calcule les couleurs LED initiales et met à jour lastSentFactor
  refreshScreen();       // Envoie tout au Nextion (t_pwr sera juste !)
  // 7. Sonde de température
  sensors.begin();
  sensors.setWaitForConversion(false);
  sensors.requestTemperatures();

  Serial.println("--- SYSTEME OPERATIONNEL ---");
}
void loop() {
  // Réinitialise le compteur du Watchdog pour éviter le reset automatique du Due
  WDT->WDT_CR = WDT_CR_KEY(0xA5) | WDT_CR_WDRSTT;
  // 1. Mise à jour de l'heure Hors Page 1
  if (currentPage != 1) {
    now = rtc.now();
  }
  // 2. Recalcul Automatique des éphémérides toutes les heures hors page 1
  // Pour Rajouter de la precision sur les Phases et Lever coucher de Lune
  static int lastHour = -1;
  if (currentPage != 1 && now.hour() != lastHour) {
    calculateAstroData();
    lastHour = now.hour();
    Serial.println(F("Calcul horaire effectué."));
  }
  // 3. Lecture des commandes
  readNextionCommands();
  readSerialPC();
  // Si un bouton SAVE a été pressé, on traite les calculs ici
  if (pendingRTCUpdate) {
    rtc.adjust(now);
    pendingRTCUpdate = false;
    Serial.println(F("RTC synchronisé."));
  }
  if (pendingAstroUpdate) {
    calculateAstroData();  // Le calcul lourd (1440 itérations)
    pendingAstroUpdate = false;
    checkRelayAndLeds();
    refreshScreen();  // On rafraîchit l'écran avec les nouvelles éphémérides
    Serial.println(F("Calculs Astro terminés."));
  }
  // 4. GESTION DE LA TEMPÉRATURE
  if (temperatureDisponible()) {
    sensors.requestTemperatures();  // On relance immédiatement la suivante
  }
  // 5. GESTION DES LEDS (Toutes les secondes) sauf page 1
  if (currentPage != 1 && millis() - lastSecondLoop > 1000) {
    lastSecondLoop = millis();
    checkRelayAndLeds();
  }
  // 5.1. LISSAGE ET AFFICHAGE LED (En continu pour un fondu parfait à 25Hz+)
  static unsigned long lastLedFrame = 0;
  if (millis() - lastLedFrame >= 40) {  // 40ms= 25 Hz
    lastLedFrame = millis();
    updateLedsSmoothly();
  }
  // 6. Rafraîchissement de l'écran (Seulement si on est sur la bonne page)
  static int lastMinute = -1;
  if (currentPage == 0 && (now.minute() != lastMinute || millis() - lastScreenRefresh > 5000)) {
    lastMinute = now.minute();
    lastScreenRefresh = millis();
    refreshScreen();
  }

  //  la page 1 vit aussi
  if (currentPage == 1 && millis() - lastScreenRefresh > 1000) {
    lastScreenRefresh = millis();
    refreshScreen();
  }
}
// --- Fonction de lecture du Serial Monitor
void readSerialPC() {
  if (Serial.available()) {
    String pcCmd = Serial.readStringUntil('\n');
    pcCmd.trim();

    // --- COMMANDE SETDATE (Format: SETDATE 2025 6 21) ---
    if (pcCmd.startsWith("SETDATE")) {
      int yr, mo, d;
      if (sscanf(pcCmd.c_str(), "SETDATE %d %d %d", &yr, &mo, &d) == 3) {
        rtc.adjust(DateTime(yr, mo, d, now.hour(), now.minute(), 0));
        now = rtc.now();
        calculateAstroData();  // Recalcule les éphémérides (Levers/Couchers)
        checkRelayAndLeds();   // Met à jour les LEDs INSTANTANÉMENT
        refreshScreen();
        Serial.print(F("Date forcee : "));
        Serial.print(d);
        Serial.print("/");
        Serial.print(mo);
        Serial.print("/");
        Serial.println(yr);
      } else {
        Serial.println(F("Erreur format: 'SETDATE 2025 06 21'"));
      }
    }
    // --- COMMANDE SETTIME (Format: SETTIME 18 30) ---
    else if (pcCmd.startsWith("SETTIME")) {
      int h, m;
      if (sscanf(pcCmd.c_str(), "SETTIME %d %d", &h, &m) == 2) {
        rtc.adjust(DateTime(now.year(), now.month(), now.day(), h, m, 0));
        now = rtc.now();
        calculateAstroData();  // Important si on change de jour (ex: passage à 00h01)
        checkRelayAndLeds();   // Change la lumière tout de suite
        refreshScreen();
        Serial.print(F("Heure forcee : "));
        Serial.print(h);
        Serial.print("h");
        if (m < 10) Serial.print("0");
        Serial.println(m);
      } else {
        Serial.println(F("Erreur format: 'SETTIME 18 30'"));
      }
    }  // --- COMMANDE SETGPS DMS (Format: SETGPS LatD LatM LatS LngD LngM LngS UTC) ---
    // Exemple Paris (Hiver): SETGPS 48 51 23 2 21 07 1
    // Exemple New York: SETGPS 40 42 46 -74 0 21 -5
    else if (pcCmd.startsWith("SETGPS")) {
      int laD, laM, laS, loD, loM, loS, offset;
      // On vérifie maintenant si on a 7 arguments
      if (sscanf(pcCmd.c_str(), "SETGPS %d %d %d %d %d %d %d", &laD, &laM, &laS, &loD, &loM, &loS, &offset) == 7) {
        // Calcul Latitude
        double latDecimal = abs(laD) + (laM / 60.0) + (laS / 3600.0);
        if (laD < 0) latDecimal *= -1.0;  // Gestion Sud
        currentLat = latDecimal;
        // Calcul Longitude
        double lngDecimal = abs(loD) + (loM / 60.0) + (loS / 3600.0);
        if (loD < 0) lngDecimal *= -1.0;  // Gestion Ouest
        currentLng = lngDecimal;
        // Mise à jour de l'Offset UTC
        manualOffset = (double)offset;
        // Recalcul complet
        calculateAstroData();
        refreshScreen();
        Serial.print(F("GPS & UTC mis a jour : Lat="));
        Serial.print(currentLat, 4);
        Serial.print(F(" | Lng="));
        Serial.print(currentLng, 4);
        Serial.print(F(" | UTC="));
        Serial.println((int)manualOffset);
      } else {
        Serial.println(F("Erreur format : SETGPS LatD LatM LatS LngD LngM LngS UTC"));
      }
    }
  }
}
void computeSunChannel() {
  double hourDec = now.hour() + now.minute() / 60.0;
  currentSunAlt = getSunElevation(hourDec);
  if (currentSunAlt > -6.0 && maxElevationToday > -6.0) {
    double xSun = (currentSunAlt + 6.0) / (maxElevationToday + 6.0);
    xSun = constrain(xSun, 0.0, 1.0);

    sunR = 255 * pow(xSun, 0.45);
    sunG = 255 * pow(xSun, 0.85);
    sunB = 255 * pow(xSun, 1.80);
    sunW = 255 * pow(xSun, 2.20);

    sunR = min(sunR, 255.0);
    sunG = min(sunG, 255.0);
    sunB = min(sunB, 255.0);
    sunW = min(sunW, 255.0);
  } else {
    sunR = 0;
    sunG = 0;
    sunB = 0;
    sunW = 0;
  }
}

void computeMoonChannel() {
  moonB = 0;
  moonW = 0;
  isMoonUp = false;
  double nowDec = now.hour() + now.minute() / 60.0;
  int minuteIdx = now.hour() * 60 + now.minute();

  bool moonPresent = false;
  if (moonRiseDec >= 0 && moonSetDec >= 0) {
    if (moonRiseDec < moonSetDec) moonPresent = (nowDec >= moonRiseDec && nowDec < moonSetDec);
    else moonPresent = (nowDec >= moonRiseDec || nowDec < moonSetDec);
  } else if (moonRiseDec >= 0) {
    if (nowDec >= moonRiseDec || (nowDec > 23.0 && flagMoonNext == 1)) moonPresent = true;
  } else if (moonSetDec >= 0) {
    if (nowDec < moonSetDec || (nowDec < 1.0 && flagMoonPrev == 1)) moonPresent = true;
  } else if (flagMoonPrev == 1 || flagMoonNext == 1) {
    moonPresent = true;
  }

  if (moonPresent) {
    isMoonUp = true;
    currentMoonAzimuth = moonAzimuthTable[minuteIdx];

    float phase = moonIllum / 100.0;
    float moonPower = phase * phase;

    float sunWashout = 1.0;
    if (currentSunAlt > -6.0) {
      sunWashout = constrain((-currentSunAlt) / 6.0, 0.0, 1.0);
    }

    // CORRECTION : Pas d'intensiteManuelle ici, juste la valeur physique de base
    moonB = 15.0 * moonPower * sunWashout;
    moonW = 5.0 * moonPower * sunWashout;
  }
}

void checkRelayAndLeds() {
  if (isMaintenanceMode) {
    targetR = 255;
    targetG = 255;
    targetB = 255;
    targetW = 255;
    if (currentPage == 0) {
      sendNextion("led_r.val=100");
      sendNextion("led_g.val=100");
      sendNextion("led_b.val=100");
      sendNextion("led_w.val=100");
    }
    return;
  }

  if (maxElevationToday <= 0) seasonalFactor = 0.0;
  else seasonalFactor = constrain(maxElevationToday / 90.0, 0.0, 1.0);

  int currentFactorPct = (int)(seasonalFactor * 100);
  if (currentFactorPct != lastSentFactor) {
    sendNextion("t_pwr.txt=\"" + String(currentFactorPct) + "%\"");
    lastSentFactor = currentFactorPct;
  }

  computeSunChannel();
  computeMoonChannel();

  // pwrFinal s'applique sur le Soleil
  float pwrFinal = seasonalFactor * intensiteManuelle;

  // CORRECTION : Les targets globales ne gèrent QUE le Soleil (le fond)
  targetR = constrain((int)(sunR * pwrFinal + 0.5), 0, 255);
  targetG = constrain((int)(sunG * pwrFinal + 0.5), 0, 255);
  targetB = constrain((int)(sunB * pwrFinal + 0.5), 0, 255);
  targetW = constrain((int)(sunW * pwrFinal + 0.5), 0, 255);

  if (currentPage == 0) {
    sendNextion("led_r.val=" + String(map(targetR, 0, 255, 0, 100)));
    sendNextion("led_g.val=" + String(map(targetG, 0, 255, 0, 100)));
    sendNextion("led_b.val=" + String(map(targetB, 0, 255, 0, 100)));
    sendNextion("led_w.val=" + String(map(targetW, 0, 255, 0, 100)));
  }
}

void updateLedsSmoothly() {
  
  currentR += (targetR - currentR) * SMOOTH_SPEED;
  currentG += (targetG - currentG) * SMOOTH_SPEED;
  currentB += (targetB - currentB) * SMOOTH_SPEED;
  currentW += (targetW - currentW) * SMOOTH_SPEED;

  int bgG = (int)(currentG + 0.5);
  int bgR = (int)(currentR + 0.5);
  int bgB = (int)(currentB + 0.5);
  int bgW = (int)(currentW + 0.5);

  uint32_t solarColor = strip.Color(bgG, bgR, bgB, bgW);
  strip.fill(solarColor);

  // LA LUNE : Elle s'ajoute localement avec le Dimmer appliqué UNE SEULE FOIS
  if (isMoonUp && moonIllum > 0 && (moonB > 0 || moonW > 0)) {
    float azRad = currentMoonAzimuth * PI / 180.0;
    // Convention miroir assumée (voir AstroEngine.cpp) :
    // LED 0 = côté OUEST du bac -> lever(Est)=LED47, coucher(Ouest)=LED0.
    // Si tu retournes le ruban un jour : inverse ICI (projection = +sin(azRad)).
    float projection = -sin(azRad);
    float centerLed = ((projection + 1.0) / 2.0) * (NUM_LEDS - 1);
    float sigmaBaseMax = 3.5;
    float sigma = sigmaBaseMax * (moonIllum / 100.0);
    if (sigma < 1.0) sigma = 1.0;

    // Application unique du Dimmer sur l'intensité lunaire
    float effectiveMoonB = moonB * intensiteManuelle;
    float effectiveMoonW = moonW * intensiteManuelle;

    for (int i = 0; i < NUM_LEDS; i++) {
      float dist = abs(i - centerLed);
      float intensity = exp(-(dist * dist) / (2.0 * sigma * sigma));

      if (intensity > 0.01) {
        int newB = min(255, bgB + (int)(effectiveMoonB * intensity + 0.5));
        int newW = min(255, bgW + (int)(effectiveMoonW * intensity + 0.5));
        strip.setPixelColor(i, bgG, bgR, newB, newW);
      }
    }
  }
  strip.show();
}