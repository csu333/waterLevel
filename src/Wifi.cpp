#include "Wifi.h"
#include <esp_wifi.h>

/********\
 * WIFI *
\********/

bool initWiFi()
{
    // Disable persistence in flash for power savings
    WiFi.persistent(false);
    WiFi.setSleep(false);
    delay(1);

    bool fastConnect = false;
    Log.verboseln("Setting hostname");
    WiFi.setHostname(ROOT_TOPIC.c_str());

    // Connect to WiFi
    Log.verboseln("Setting Station mode");
    WiFi.mode(WIFI_STA);
    delay(1);

    if (rtcValid && failedConnection < 4 && channel != 0 && bssid[0] != 0)
    {
        // The RTC data was good, make a quick connection
        Log.verboseln(F("Using fast WiFi connect"));
        WiFi.begin(WLAN_SSID, WLAN_PASSWD, channel, bssid, true);
        fastConnect = true;
    }

    if (failedConnection >= 4)
    {
        // The RTC data was not valid, so make a regular connection
        Log.warningln(F("Number of connection failure too high (%d). Using regular connection instead"), failedConnection);
        WiFi.begin(WLAN_SSID, WLAN_PASSWD);
    }

    if (!rtcValid)
    {
        // The RTC data was not valid, so make a regular connection
        Log.warningln(F("This is the first initialisation after reset"));
        WiFi.begin(WLAN_SSID, WLAN_PASSWD);
    }

    wifiStart = millis();

    WiFi.config(staticIP, gateway, subnet, dns);
    Log.noticeln("Waiting WiFi connection");
    bool connectReset = false;

    // Try first with a fast connection
    while (WiFi.status() != WL_CONNECTED && fastConnect && (millis() - wifiStart) < 5000)
    {
        delay(50);
    }

    if (WiFi.status() == WL_CONNECTED && fastConnect) {
        Log.noticeln(F("Fast connect success after %d ms"), (millis() - wifiStart));
    } 

    if (WiFi.status() != WL_CONNECTED && fastConnect) {
        // Quick connect is not working, reset WiFi and try regular connection
        Log.warningln(F("Fast connect failed"));
        Log.noticeln(F(" - Channel: %d"), channel);
        Log.noticeln(F(" - BSSID: %x:%x:%x:%x:%x:%x"), bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5]);
        WiFi.disconnect(true);
        connectReset = true;
        fastConnect = false;
        delay(50);
        WiFi.setSleep(true);
        delay(100);
        WiFi.setSleep(false);
        delay(50);
        WiFi.begin(WLAN_SSID, WLAN_PASSWD);
    }

    // If it didn't work, try a standard connection
    while (WiFi.status() != WL_CONNECTED && (millis() - wifiStart) < 10000)
    {
        delay(50);
    }

    if (WiFi.status() == WL_CONNECTED && !fastConnect) {
        Log.noticeln(F("Regular connect success after %d ms"), (millis() - wifiStart));
    } 
    
    if (WiFi.status() != WL_CONNECTED) {
        Log.errorln(F("Wifi did not connect, giving up"));
        // Giving up and going back to sleep
        WiFi.disconnect(true);
        delay(1);
        WiFi.mode(WIFI_OFF);

        failedConnection++;
        return false;
    }

    Log.noticeln(F("RSSI: %d dB"), WiFi.RSSI());

    if (connectReset)
    {
        Log.noticeln(F("New BSSID: %x:%x:%x:%x:%x:%x"), bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5]);
    }

    if (failedConnection > 0)
    {
        Log.warningln(F("Wifi connected after %d failures"), failedConnection);
    }

    // Write current connection info back to RTC
    channel = WiFi.channel();
    failedConnection = 0;
    memcpy(bssid, WiFi.BSSID(), 6); // Copy 6 bytes of BSSID (AP's MAC address)
    rtcValid = true;

    return (WiFi.status() == WL_CONNECTED);
}