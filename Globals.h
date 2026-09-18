#ifndef GLOBALS_H
#define GLOBALS_H

#include <Arduino.h>
#include <RTClib.h>
#include <Adafruit_NeoPixel.h>

// --- Edition page 1
extern double editLat;
extern double editLng;
extern int editManualOffset;

// --- VARIABLES EXTERNES (Système et Etats) ---
extern double currentLat;
extern double currentLng;
extern const float MAX_POWER_AXO;
extern DateTime now;
extern RTC_DS3231 rtc;
extern Adafruit_NeoPixel strip;

extern int currentPage;
extern bool isAutoMode;
extern bool isDstActive;
extern float intensiteManuelle;
extern bool isMaintenanceMode;
extern double manualOffset;
extern double currentUtcOffset;
extern int lastSentFactor;

extern bool pendingRTCUpdate;
extern bool pendingAstroUpdate;

// --- VARIABLES EXTERNES (Données Astro) ---
extern double noonLocal;
extern double currentSunAlt;
extern double maxElevationToday;
extern double seasonalFactor;
extern int moonIllum;
extern int pIdx;
extern int flagMoonPrev;
extern int flagMoonNext;
extern double moonRiseDec;
extern double moonSetDec;
extern double sunRiseDec;
extern double sunSetDec;

extern String textSunRise;
extern String textSunSet;
extern String textMoonRise;
extern String textMoonSet;
extern String textPhase;

// --- VARIABLES EXTERNES (Lissage et Lune) ---
extern float sunR, sunG, sunB, sunW;
extern float moonB, moonW;
extern float targetR, targetG, targetB, targetW;
extern float currentR, currentG, currentB, currentW;
extern double currentMoonAzimuth;
extern bool isMoonUp;
extern const float SMOOTH_SPEED;
extern double moonAzimuthTable[1441];

// --- PROTOTYPES DES NOUVELLES FONCTIONS ---
void computeSunChannel();
void computeMoonChannel();
void updateLedsSmoothly();
// --- PROTOTYPES DES FONCTIONS ---
// Fonctions de Communication et Système
void sendNextion(String cmd);
void refreshScreen();
void processCommand(uint8_t* buf, size_t len);
void readSerialPC();
void saveGPS(double lat, double lng, double utc);

// Fonctions Astronomiques (AstroEngine.cpp)
void calculateAstroData();
double getSunElevation(double localDecTime);
double getJulianDay(int y, int m, int d);
int getDSTOffset(DateTime d);
void formatTime(double decTime, char* outStr);

// Fonctions Matérielles
void checkRelayAndLeds();
bool temperatureDisponible();

#endif