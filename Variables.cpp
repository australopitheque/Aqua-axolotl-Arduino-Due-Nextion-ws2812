#include <Arduino.h>
#include "Globals.h"

// --- CONFIGURATION GEOGRAPHIQUE ---
double currentLat = 48.8566;
double currentLng = 2.3522;
double editLat = 48.8566;  // Tampon d'édition page 2
double editLng = 2.3522;
const float MAX_POWER_AXO = 0.4;
int editManualOffset = 0;

// --- ETATS ET PARAMETRES ---
int currentPage = 0;
bool isAutoMode = true;
bool isDstActive = false;
float intensiteManuelle = 1.0;
bool isMaintenanceMode = false;
double manualOffset = 1.0;
double currentUtcOffset = 1.0;
int lastSentFactor = -1;

// --- FLAGS ---
bool pendingRTCUpdate = false;
bool pendingAstroUpdate = false;

// --- VARIABLES POUR LISSAGE ET EFFET LUNE ---
float sunR = 0, sunG = 0, sunB = 0, sunW = 0;
float moonB = 0, moonW = 0;
float targetR = 0, targetG = 0, targetB = 0, targetW = 0;
float currentR = 0, currentG = 0, currentB = 0, currentW = 0;
double currentMoonAzimuth = 0;
bool isMoonUp = false;
const float SMOOTH_SPEED = 0.08;  // Vitesse de lissage (0.05 = très lent, 0.15 = rapide)
double moonAzimuthTable[1441];    // Tableau pour stocker l'azimut minute par minute

// --- VARIABLES ASTRO ---
double noonLocal = 0;
double currentSunAlt = 0;
double maxElevationToday = 0;
double seasonalFactor = 1.0;
int moonIllum = 0;
int pIdx = 0;
int flagMoonPrev = 0;
int flagMoonNext = 0;
double moonRiseDec = -1;
double moonSetDec = -1;
double sunRiseDec = 0.0;
double sunSetDec = 0.0;

String textSunRise = "---";
String textSunSet = "---";
String textMoonRise = "---";
String textMoonSet = "---";
String textPhase = "---";

// --- OBJETS ---
DateTime now;
RTC_DS3231 rtc;
// Le ruban est défini ICI
Adafruit_NeoPixel strip = Adafruit_NeoPixel(48, 5, NEO_RGBW + NEO_KHZ800);