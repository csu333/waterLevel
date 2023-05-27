/********\
 * WIFI *
\********/

bool initWiFi() {
  // Disable persistence in flash for power savings
  WiFi.persistent(false);
  
  WiFi.forceSleepWake();
  delay( 1 );
  
  loadRtc();
  bool fastConnect = false;

  // Connect to WiFi
  WiFi.mode(WIFI_STA);
  if( rtcValid && rtcData.failedConnection < 4) {
    // The RTC data was good, make a quick connection
    Log.traceln(F("Using fast WiFi connect"));
    WiFi.begin(WLAN_SSID, WLAN_PASSWD, rtcData.channel, rtcData.bssid, true);
    fastConnect = true;
  }
  else {
    // The RTC data was not valid, so make a regular connection
    Log.warningln(F("Number of connection failure too high (%d). Using regular connection instead"), rtcData.failedConnection);
    WiFi.begin( WLAN_SSID, WLAN_PASSWD );
  }

  wifiStart = millis();

  WiFi.config(staticIP, gateway, subnet);
  Log.noticeln("Waiting WiFi connection");
  bool connectReset = false;

  // Wait for successful connection
  while (WiFi.status() != WL_CONNECTED) {
    if ((millis() - wifiStart) >= 10000 && fastConnect) {
      // Quick connect is not working, reset WiFi and try regular connection
      Log.warningln(F("Fast connect failed with info:"));
      Log.noticeln(F(" - Channel: %d"), rtcData.channel); 
      Log.noticeln(F(" - BSSID: %x:%x:%x:%x:%x:%x"), rtcData.bssid[0], rtcData.bssid[1], rtcData.bssid[2], rtcData.bssid[3], rtcData.bssid[4], rtcData.bssid[5]);
      WiFi.disconnect( true );
      connectReset = true;
      delay( 50 );
      WiFi.forceSleepBegin();
      delay( 100 );
      WiFi.forceSleepWake();
      delay( 50 );
      WiFi.begin( WLAN_SSID, WLAN_PASSWD );
      break;
    }
    delay(50);
  }

  while (WiFi.status() != WL_CONNECTED) {
    if ((millis() - wifiStart) >= 20000) {
      Log.errorln(F("Something happened, giving up"));
      // Giving up after 30 seconds and going back to sleep
      WiFi.disconnect( true );
      delay( 1 );
      WiFi.mode( WIFI_OFF );
      
      rtcData.failedConnection++;
      rtcDirty = true;
      return false;
    }
    
    delay(50);
  }

  Log.noticeln(F("RSSI: %d dB"), WiFi.RSSI()); 

  if (rtcData.failedConnection > 0) {
    Log.warningln(F("Wifi connected after %d failures"), rtcData.failedConnection);
  }

  // Write current connection info back to RTC
  rtcData.channel = WiFi.channel();
  rtcData.failedConnection = 0;
  memcpy( rtcData.bssid, WiFi.BSSID(), 6 ); // Copy 6 bytes of BSSID (AP's MAC address)
  rtcDirty = true;

  return (WiFi.status() == WL_CONNECTED);
}