#include "Globals.h"
#include <math.h>
const double MOON_HORIZON = -0.583;  // centre apparent du disque à l'horizon
// --- CONSTANTES MATHÉMATIQUES ---
const double RAD = 0.017453292519943295769;
const double DEG = 57.295779513082320876;

// --- FONCTIONS UTILITAIRES ---
double rev(double x) {
  return x - floor(x / 360.0) * 360.0;
}

double getJulianDay(int y, int m, int d) {
  if (m <= 2) {
    y -= 1;
    m += 12;
  }
  int A = y / 100;
  int B = 2 - A + A / 4;
  return floor(365.25 * (y + 4716)) + floor(30.6001 * (m + 1)) + d + B - 1524.5;
}

double getEpsilon(double n, double dEps) {
  double T = n / 36525.0;
  return 23.439291 - 0.0130042 * T + dEps;
}

// --- FONCTION DST (Heure d'été/hiver) ---
int getDSTOffset(DateTime d) {
  int m = d.month();
  int day = d.day();
  int h = d.hour();
  int dow = d.dayOfTheWeek();
  if (m > 3 && m < 10) return 1;
  if (m < 3 || m > 10) return 0;
  if (m == 3) {
    int prevSunday = day - dow;
    if (prevSunday >= 25) {
      if (day == prevSunday && h < 2) return 0;
      return 1;
    }
    return 0;
  }
  if (m == 10) {
    int prevSunday = day - dow;
    if (prevSunday >= 25) {
      if (day == prevSunday && h < 3) return 1;
      return 0;
    }
    return 1;
  }
  return 0;
}

// --- FORMATAGE HEURE POUR NEXTION ---
void formatTime(double decTime, char* outStr) {
  if (decTime >= 24 && decTime < 24.05) decTime -= 24;
  if (decTime < 0 && decTime > -0.05) decTime += 24;
  if (decTime < 0 || decTime >= 24) {
    strcpy(outStr, "---");
    return;
  }
  int h = (int)decTime;
  int m = (int)round((decTime - h) * 60);
  if (m == 60) {
    m = 0;
    h++;
  }
  if (h >= 24) h = 0;
  sprintf(outStr, "%02dh%02d", h, m);
}

// --- ÉLÉVATION SOLAIRE AVEC RÉFRACTION ---
double getSunElevation(double localDecTime) {
  double n = (getJulianDay(now.year(), now.month(), now.day()) + (localDecTime - currentUtcOffset) / 24.0) - 2451545.0;
  double T = n / 36525.0;
  double L_sun_mean = rev(280.466 + 36000.77 * T);
  double Omega = rev(125.0445 - 1934.1363 * T);
  double dPsi = (-17.20 / 3600.0) * sin(Omega * RAD) - (1.32 / 3600.0) * sin(2 * L_sun_mean * RAD);
  double dEps = (9.20 / 3600.0) * cos(Omega * RAD) + (0.57 / 3600.0) * cos(2 * L_sun_mean * RAD);
  double epsilon = getEpsilon(n, dEps);
  double L_precis = rev(280.46646 + 36000.76983 * T);
  double g_sun = rev(357.52911 + 35999.05029 * T);
  double lambda_sun = rev(L_precis + 1.914602 * sin(g_sun * RAD) + 0.019993 * sin(2 * g_sun * RAD));
  double ra = atan2(cos(epsilon * RAD) * sin(lambda_sun * RAD), cos(lambda_sun * RAD)) * DEG;
  double dec = asin(sin(epsilon * RAD) * sin(lambda_sun * RAD)) * DEG;
  double GMST = rev(280.460618 + 360.985647366 * n);
  double GAST = GMST + dPsi * cos(epsilon * RAD);
  double HA = rev(GAST + currentLng - ra);
  if (HA > 180) HA -= 360;

  double altGeo = asin(sin(currentLat * RAD) * sin(dec * RAD) + cos(currentLat * RAD) * cos(dec * RAD) * cos(HA * RAD)) * DEG;

  double refraction = 0;
  if (altGeo > -1.0) {
    refraction = (1.02 / tan((altGeo + 10.3 / (altGeo + 5.11)) * RAD)) / 60.0;
  }
  return altGeo + refraction;
}

// --- CALCUL GLOBAL ---
void calculateAstroData() {
  // ✅ Code universel (compatible Pérou, France, etc.)
  if (isAutoMode) {
    // Mode Auto : Offset de base manuel + 1 heure automatique si on est en période d'été
    currentUtcOffset = manualOffset + (getDSTOffset(now) ? 1.0 : 0.0);
  } else {
    // Mode Manuel : Offset de base manuel + 1 heure si le bouton DST est activé à l'écran
    currentUtcOffset = manualOffset + (isDstActive ? 1.0 : 0.0);
  }
  double n0 = getJulianDay(now.year(), now.month(), now.day()) - 2451545.0;
  double T0 = n0 / 36525.0;

  // Phase de Lune
  double L_p = rev(280.46646 + 36000.76983 * T0);
  double g_s = rev(357.52911 + 35999.05029 * T0);
  double lambda_s = rev(L_p + 1.9146 * sin(g_s * RAD));
  double M0 = rev(134.96 + 477198.86 * T0);
  double lambda_l0 = rev(218.31 + 481267.88 * T0 + 6.28 * sin(M0 * RAD));
  double diffP = rev(lambda_l0 - lambda_s);
  moonIllum = (int)(50 * (1 - cos(diffP * RAD)));

  if (diffP < 22.5 || diffP >= 337.5) {
    textPhase = "NOUVELLE";
    pIdx = 0;
  } else if (diffP < 67.5) {
    textPhase = "P. CROISSANT";
    pIdx = 1;
  } else if (diffP < 112.5) {
    textPhase = "P. QUARTIER";
    pIdx = 2;
  } else if (diffP < 157.5) {
    textPhase = "GIBBEUSE +";
    pIdx = 3;
  } else if (diffP < 202.5) {
    textPhase = "PLEINE LUNE";
    pIdx = 4;
  } else if (diffP < 247.5) {
    textPhase = "GIBBEUSE -";
    pIdx = 5;
  } else if (diffP < 292.5) {
    textPhase = "D. QUARTIER";
    pIdx = 6;
  } else {
    textPhase = "D. CROISSANT";
    pIdx = 7;
  }

  maxElevationToday = -99;
  double sRise = -1, sSet = -1, mRise = -1, mSet = -1;
  double lastAltS = getSunElevation(0);
  double lastAltM = -999, firstAltM = -999;

  for (int m = 0; m <= 1440; m++) {
    double hD = m / 60.0;
    double aS = getSunElevation(hD);
    if (aS > maxElevationToday) {
      maxElevationToday = aS;
      noonLocal = hD;
    }
    if (lastAltS <= -0.833 && aS > -0.833) sRise = hD;
    if (lastAltS >= -0.833 && aS < -0.833) sSet = hD;
    lastAltS = aS;

    double hn = n0 + (hD - currentUtcOffset) / 24.0;
    double Tn = hn / 36525.0;
    double L_L = rev(218.316 + 481267.881 * Tn);
    double M_L = rev(134.963 + 477198.867 * Tn);
    double D_L = rev(297.850 + 445267.111 * Tn);
    double M_S = rev(357.529 + 35999.050 * Tn);
    double F_L = rev(93.272 + 483202.017 * Tn);  // Argument de latitude
    double Om = rev(125.045 - 1934.136 * Tn);    // Nœud ascendant

    // Longitude écliptique (série enrichie)
    double l_long = L_L
                    + 6.289 * sin(M_L * RAD)
                    - 1.274 * sin((M_L - 2 * D_L) * RAD)
                    + 0.658 * sin(2 * D_L * RAD)
                    - 0.186 * sin(M_S * RAD)
                    - 0.059 * sin((2 * M_L - 2 * D_L) * RAD)
                    - 0.057 * sin((M_L - 2 * D_L + M_S) * RAD)
                    + 0.053 * sin((M_L + 2 * D_L) * RAD)
                    + 0.046 * sin((2 * D_L - M_S) * RAD)
                    - 0.016 * sin(Om * RAD);
    l_long = rev(l_long);

    // ---  latitude écliptique (±5,1°) ---
    double beta = 5.128 * sin(F_L * RAD)
                  + 0.281 * sin((M_L + F_L) * RAD)
                  - 0.278 * sin((F_L - M_L) * RAD)
                  - 0.173 * sin((2 * D_L - F_L) * RAD);

    double dist = 60.2666 - 3.4088 * cos(M_L * RAD)
                  - 0.6355 * cos((2 * D_L - M_L) * RAD)
                  - 0.6072 * cos(2 * D_L * RAD);
    double HP = asin(1.0 / dist) * DEG;

    double G = rev(280.460 + 360.9856 * hn);
    double epsilonLune = getEpsilon(hn, 0.0);

    // --- Conversion (λ, β) -> (AD, Déc) COMPLÈTE ---
    double sinL = sin(l_long * RAD), cosL = cos(l_long * RAD);
    double sinB = sin(beta * RAD), cosB = cos(beta * RAD);
    double cosE = cos(epsilonLune * RAD), sinE = sin(epsilonLune * RAD);

    double dec_L = asin(sinB * cosE + cosB * sinE * sinL) * DEG;
    double ra_L = rev(atan2(sinL * cosE - (sinB / cosB) * sinE, cosL) * DEG);

    double HA_L = rev(G + currentLng - ra_L);
    if (HA_L > 180) HA_L -= 360;

    double aM = asin(sin(currentLat * RAD) * sin(dec_L * RAD) + cos(currentLat * RAD) * cos(dec_L * RAD) * cos(HA_L * RAD)) * DEG;
    aM = aM - HP;

    // --- AZIMUT LUNAIRE "MIROIR" : 0°=N, 90°=OUEST, 180°=S, 270°=EST ---
    // VOLONTAIRE : combiné à projection = -sin(az) dans updateLedsSmoothly(),
    // cela donne un balayage Est->Ouest pour UN RUBAN DONT LA LED 0 EST CÔTÉ OUEST.
    // NE PAS "corriger" le signe de y_az sans retourner physiquement le ruban !
    double y_az = sin(HA_L * RAD);
    double x_az = tan(dec_L * RAD) * cos(currentLat * RAD) - cos(HA_L * RAD) * sin(currentLat * RAD);
    double az = atan2(y_az, x_az) * DEG;
    if (az < 0) az += 360.0;
    if (m < 1440) moonAzimuthTable[m] = az;
    if (m == 0) firstAltM = aM;
    if (lastAltM != -999) {
      if (lastAltM <= MOON_HORIZON && aM > MOON_HORIZON) mRise = hD;
      if (lastAltM >= MOON_HORIZON && aM < MOON_HORIZON) mSet = hD;
    }
    lastAltM = aM;
  }

  sunRiseDec = sRise;
  sunSetDec = sSet;
  moonRiseDec = mRise;
  moonSetDec = mSet;
  flagMoonPrev = (firstAltM > MOON_HORIZON) ? 1 : 0;
  flagMoonNext = (lastAltM > MOON_HORIZON) ? 1 : 0;

  char b[10];
  formatTime(sRise, b);
  textSunRise = String(b);
  formatTime(sSet, b);
  textSunSet = String(b);
  formatTime(mRise, b);
  textMoonRise = String(b);
  formatTime(mSet, b);
  textMoonSet = String(b);
}