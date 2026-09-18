#include <Arduino.h>
#include "Globals.h"


// --- FONCTION DE TRAITEMENT DES COMMANDES ---

void processCommand(uint8_t* buf, size_t len) {
  bool needRefresh = false;
  bool timeChanged = false;

  // 1. GESTION DU DIMMER (Cas spécial binaire : "DIMMER" + 1 octet brut)
  if (len == 7 && memcmp(buf, "DIMMER", 6) == 0) {
    uint8_t valSliderBrute = buf[6];
    intensiteManuelle = valSliderBrute / 100.0;
    // Sécurité Axolotl
    if (intensiteManuelle > MAX_POWER_AXO) intensiteManuelle = MAX_POWER_AXO;
    if (!isMaintenanceMode) {
      checkRelayAndLeds();
    }
    Serial.print(F("Master Dimmer Reçu  : "));
    Serial.print(valSliderBrute);
    Serial.println(F("%"));
    return;
  }
  // LOG DE DEBUG POUR TOUTES LES AUTRES COMMANDES
  Serial.print(F("Nextion dit : "));
  for (size_t i = 0; i < len; i++) Serial.print((char)buf[i]);
  Serial.println();
  // 2. NAVIGATION
  if (len == 2 && memcmp(buf, "P0", 2) == 0) {
    currentPage = 0;
    now = rtc.now();  // abandonne les modifications NON enregistrées
    needRefresh = true;
  } else if (len == 2 && memcmp(buf, "P1", 2) == 0) {
    currentPage = 1;
    editManualOffset = manualOffset;  // <--- Snapshot du tampon pour le fuseau
    needRefresh = true;
  } else if (len == 2 && memcmp(buf, "P2", 2) == 0) {
    currentPage = 2;
    editLat = currentLat;  //  snapshot du tampon des coordonnées
    editLng = currentLng;
    editManualOffset = manualOffset;  // <--- si TZ est réglé en P2
    needRefresh = true;
  }
  // 3. RÉGLAGES DATE & HEURE (Page 1)
  else if (len == 5 && memcmp(buf, "H_INC", 5) == 0) {
    now = now + TimeSpan(0, 1, 0, 0);
    timeChanged = true;
  } else if (len == 5 && memcmp(buf, "H_DEC", 5) == 0) {
    now = now - TimeSpan(0, 1, 0, 0);
    timeChanged = true;
  } else if (len == 5 && memcmp(buf, "M_INC", 5) == 0) {
    now = now + TimeSpan(0, 0, 1, 0);
    timeChanged = true;
  } else if (len == 5 && memcmp(buf, "M_DEC", 5) == 0) {
    now = now - TimeSpan(0, 0, 1, 0);
    timeChanged = true;
  } else if (len == 5 && memcmp(buf, "S_INC", 5) == 0) {
    now = now + TimeSpan(0, 0, 0, 1);
    timeChanged = true;
  } else if (len == 5 && memcmp(buf, "S_DEC", 5) == 0) {
    now = now - TimeSpan(0, 0, 0, 1);
    timeChanged = true;
  } else if (len == 5 && memcmp(buf, "D_INC", 5) == 0) {
    now = now + TimeSpan(1, 0, 0, 0);
    timeChanged = true;
  } else if (len == 5 && memcmp(buf, "D_DEC", 5) == 0) {
    now = now - TimeSpan(1, 0, 0, 0);
    timeChanged = true;
  } else if (len == 6 && memcmp(buf, "MO_INC", 6) == 0) {
    int m = now.month() + 1;
    int y = now.year();
    int d = now.day();
    if (m > 12) {
      m = 1;
      y++;
    }

    // Garde-fou : clamp le jour au max du mois cible
    int maxDay = 31;
    if (m == 2) maxDay = (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0)) ? 29 : 28;
    else if (m == 4 || m == 6 || m == 9 || m == 11) maxDay = 30;
    d = constrain(d, 1, maxDay);

    now = DateTime(y, m, d, now.hour(), now.minute(), 0);
    timeChanged = true;

  } else if (len == 6 && memcmp(buf, "MO_DEC", 6) == 0) {
    int m = now.month() - 1;
    int y = now.year();
    int d = now.day();
    if (m < 1) {
      m = 12;
      y--;
    }

    // Garde-fou identique pour la décrémentation
    int maxDay = 31;
    if (m == 2) maxDay = (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0)) ? 29 : 28;
    else if (m == 4 || m == 6 || m == 9 || m == 11) maxDay = 30;
    d = constrain(d, 1, maxDay);

    now = DateTime(y, m, d, now.hour(), now.minute(), 0);
    timeChanged = true;
  } else if (len == 5 && memcmp(buf, "Y_INC", 5) == 0) {
    int y = now.year() + 1;
    int m = now.month();
    int d = now.day();

    // Garde-fou : clamp le jour si on change d'année bissextile
    int maxDay = 31;
    if (m == 2) maxDay = (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0)) ? 29 : 28;
    else if (m == 4 || m == 6 || m == 9 || m == 11) maxDay = 30;
    d = constrain(d, 1, maxDay);

    now = DateTime(y, m, d, now.hour(), now.minute(), 0);
    timeChanged = true;

  } else if (len == 5 && memcmp(buf, "Y_DEC", 5) == 0) {
    int y = now.year() - 1;
    int m = now.month();
    int d = now.day();

    // Garde-fou identique pour la décrémentation
    int maxDay = 31;
    if (m == 2) maxDay = (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0)) ? 29 : 28;
    else if (m == 4 || m == 6 || m == 9 || m == 11) maxDay = 30;
    d = constrain(d, 1, maxDay);

    now = DateTime(y, m, d, now.hour(), now.minute(), 0);
    timeChanged = true;
  }

  // 4. RÉGLAGES GPS (Page 2) — travail UNIQUEMENT sur le tampon
  else if (len == 9 && memcmp(buf, "LAT_D_INC", 9) == 0) {
    editLat = constrain(editLat + 1.0, -90.0, 90.0);
    needRefresh = true;
  } else if (len == 9 && memcmp(buf, "LAT_D_DEC", 9) == 0) {
    editLat = constrain(editLat - 1.0, -90.0, 90.0);
    needRefresh = true;
  } else if (len == 9 && memcmp(buf, "LAT_M_INC", 9) == 0) {
    editLat = constrain(editLat + 1.0 / 60.0, -90.0, 90.0);
    needRefresh = true;
  } else if (len == 9 && memcmp(buf, "LAT_M_DEC", 9) == 0) {
    editLat = constrain(editLat - 1.0 / 60.0, -90.0, 90.0);
    needRefresh = true;
  } else if (len == 9 && memcmp(buf, "LAT_S_INC", 9) == 0) {
    editLat = constrain(editLat + 1.0 / 3600.0, -90.0, 90.0);
    needRefresh = true;
  } else if (len == 9 && memcmp(buf, "LAT_S_DEC", 9) == 0) {
    editLat = constrain(editLat - 1.0 / 3600.0, -90.0, 90.0);
    needRefresh = true;
  } else if (len == 9 && memcmp(buf, "LNG_D_INC", 9) == 0) {
    editLng = constrain(editLng + 1.0, -180.0, 180.0);
    needRefresh = true;
  } else if (len == 9 && memcmp(buf, "LNG_D_DEC", 9) == 0) {
    editLng = constrain(editLng - 1.0, -180.0, 180.0);
    needRefresh = true;
  } else if (len == 9 && memcmp(buf, "LNG_M_INC", 9) == 0) {
    editLng = constrain(editLng + 1.0 / 60.0, -180.0, 180.0);
    needRefresh = true;
  } else if (len == 9 && memcmp(buf, "LNG_M_DEC", 9) == 0) {
    editLng = constrain(editLng - 1.0 / 60.0, -180.0, 180.0);
    needRefresh = true;
  } else if (len == 9 && memcmp(buf, "LNG_S_INC", 9) == 0) {
    editLng = constrain(editLng + 1.0 / 3600.0, -180.0, 180.0);
    needRefresh = true;
  } else if (len == 9 && memcmp(buf, "LNG_S_DEC", 9) == 0) {
    editLng = constrain(editLng - 1.0 / 3600.0, -180.0, 180.0);
    needRefresh = true;
  }
  // 5. UTC & DST
  else if (len == 7 && memcmp(buf, "TZ_AUTO", 7) == 0) {
    isAutoMode = !isAutoMode;
    needRefresh = true;
  } else if (len == 6 && memcmp(buf, "TZ_INC", 6) == 0) {
    if (!isAutoMode) {
      editManualOffset++;  // <--- On travaille uniquement sur le tampon
      if (editManualOffset > 12) editManualOffset = 12;
      needRefresh = true;
    }
  } else if (len == 6 && memcmp(buf, "TZ_DEC", 6) == 0) {
    if (!isAutoMode) {
      editManualOffset--;  // <--- On travaille uniquement sur le tampon
      if (editManualOffset < -12) editManualOffset = -12;
      needRefresh = true;
    }
  } else if (len >= 5 && memcmp(buf, "DST=", 4) == 0) {
    if (!isAutoMode) {
      isDstActive = (buf[4] == '1');
      needRefresh = true;
    }
  }
  // 6. ENREGISTREMENT (Les fonctions cruciales)
  else if (len == 8 && memcmp(buf, "SAVE_RTC", 8) == 0) {
    pendingRTCUpdate = true;    // Indique au loop qu'il faut écrire dans le DS3231
    pendingAstroUpdate = true;  // Indique qu'il faut recalculer les levers/couchers
    needRefresh = true;
    Serial.println(F("Commande : SAVE_RTC reçue."));
  } else if (len == 8 && memcmp(buf, "SAVE_GPS", 8) == 0) {
    currentLat = editLat;  //  validation  Lat & Lng
    currentLng = editLng;
    manualOffset = editManualOffset;                        // <--- Validation du fuseau horaire
    saveGPS(currentLat, currentLng, (double)manualOffset);  // Appel de ta fonction EEPROM
    pendingAstroUpdate = true;
    needRefresh = true;
    Serial.println(F("Commande : SAVE_GPS reçue."));
  }
  // 7. MAINTENANCE (MAINT=1 ou MAINT=0)
  else if (len == 7 && memcmp(buf, "MAINT=", 6) == 0) {
    int etat = (buf[6] == '1') ? 1 : 0;
    if (etat == 1) {
      isMaintenanceMode = true;
      Serial.println(F("Mode Maintenance : ACTIF"));
      // Retour visuel immédiat
      sendNextion("t_pwr.txt=\"MAINT. 100%\"");
      // Alignement immédiat des valeurs courantes au maximum (255)
      // updateLedsSmoothly() prendra le relais sans saut visuel ni écrasement
      currentR = 255.0;
      currentG = 255.0;
      currentB = 255.0;
      currentW = 255.0;
    } else {
      isMaintenanceMode = false;
      Serial.println(F("Mode Maintenance : INACTIF"));
      lastSentFactor = -1;
      checkRelayAndLeds();
    }
    needRefresh = true;
  }
  // --- FINALISATION ET MISE À JOUR ÉCRAN ---
  if (timeChanged || needRefresh) {
    // Si on est en train d'éditer, on affiche l'offset calculé avec le tampon editManualOffset
    int activeOffset = (currentPage == 1 || currentPage == 2) ? editManualOffset : manualOffset;

    if (isAutoMode) {
      int dst = getDSTOffset(now);
      isDstActive = (dst > 0);
      currentUtcOffset = activeOffset + dst;
    } else {
      currentUtcOffset = activeOffset + (isDstActive ? 1 : 0);
    }

    if (!isMaintenanceMode || needRefresh || timeChanged) {
      refreshScreen();
    }
  }
}
