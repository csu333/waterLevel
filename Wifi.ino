/********\
 * WIFI *
\********/

bool initWiFi() {
  // Disable persistence in flash for power savings
  WiFi.persistent(false);
  
  WiFi.setSleep(false);
  delay( 1 );
  
  bool fastConnect = false;

  // Connect to WiFi
  WiFi.mode(WIFI_STA);

  if( rtcValid && failedConnection < 4) {
    // The RTC data was good, make a quick connection
    Log.traceln(F("Using fast WiFi connect"));
    WiFi.begin(WLAN_SSID, WLAN_PASSWD, channel, bssid, true);
    fastConnect = true;
  }

  if( failedConnection >= 4 ) {
    // The RTC data was not valid, so make a regular connection
    Log.warningln(F("Number of connection failure too high (%d). Using regular connection instead"), failedConnection);
    WiFi.begin( WLAN_SSID, WLAN_PASSWD );
  }

  if( !rtcValid ) {
    // The RTC data was not valid, so make a regular connection
    Log.warningln(F("This is the first initialisation after reset"));
    WiFi.begin( WLAN_SSID, WLAN_PASSWD );
  }

  wifiStart = millis();

  WiFi.config(staticIP, gateway, subnet);
  Log.noticeln("Waiting WiFi connection");
  bool connectReset = false;

  // Try first with a fast connection
  while (WiFi.status() != WL_CONNECTED) {
    if ((millis() - wifiStart) >= 10000 && fastConnect) {
      // Quick connect is not working, reset WiFi and try regular connection
      Log.warningln(F("Fast connect failed with info:"));
      Log.noticeln(F(" - Channel: %d"), channel); 
      Log.noticeln(F(" - BSSID: %x:%x:%x:%x:%x:%x"), bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5]);
      WiFi.disconnect( true );
      connectReset = true;
      delay( 50 );
      WiFi.setSleep(true);
      delay( 100 );
      WiFi.setSleep(false);
      delay( 50 );
      WiFi.begin( WLAN_SSID, WLAN_PASSWD );
      break;
    }
    delay(50);
  }

  // If it didn't work, try a standard connection
  while (WiFi.status() != WL_CONNECTED) {
    if ((millis() - wifiStart) >= 20000) {
      Log.errorln(F("Something happened, giving up"));
      // Giving up after 30 seconds and going back to sleep
      WiFi.disconnect( true );
      delay( 1 );
      WiFi.mode( WIFI_OFF );
      
      failedConnection++;
      return false;
    }
    
    delay(50);
  }

  Log.noticeln(F("RSSI: %d dB"), WiFi.RSSI()); 

  if (connectReset) {
    Log.noticeln(F("New BSSID: %x:%x:%x:%x:%x:%x"), bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5]);
  }

  if (failedConnection > 0) {
    Log.warningln(F("Wifi connected after %d failures"), failedConnection);
  }

  // Write current connection info back to RTC
  channel = WiFi.channel();
  failedConnection = 0;
  memcpy( bssid, WiFi.BSSID(), 6 ); // Copy 6 bytes of BSSID (AP's MAC address)
  rtcValid = true;

  return (WiFi.status() == WL_CONNECTED);
}