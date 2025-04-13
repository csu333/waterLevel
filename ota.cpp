// Source: https://github.com/jnsdbr/esp32-ota-update-mqtt/blob/master/src/main.cpp
#include "Arduino.h"
#include <WiFi.h>
#include <Update.h>
#include <ArduinoLog.h>

int contentLength = 0;
bool isValidContentType = false;

extern WiFiClient espClient;

String getHeaderValue(String header, String headerName) {
    return header.substring(strlen(headerName.c_str()));
}

String getBinName(String url) {
    int index = 0;

    // Search for first /
    for (int i = 0; i < url.length(); i++) {
        if (url[i] == ':') {
            i += 3;
        }
        if (url[i] == '/') {
            index = i;
            break;
        }
    }

    String binName = "";
    binName.reserve(url.length() - index);

    // Create binName
    for (int i = index; i < url.length(); i++) {
        binName += url[i];
    }

    return binName;
}

String getHostName(String url) {
     int start_index = 0;
     int end_index = 0;

    // Search for : or last /
    for (int i = 0; i < url.length(); i++) {
        if (url[i] == ':') {
            i += 3;
            start_index = i;
        }
        if (url[i] == '/') {
            end_index = i;
            break;
        }
    }

    String hostName = "";

    // Create hostName
    for (int i = start_index; i < end_index; i++) {
        hostName += url[i];
    }

    Log.noticeln(F("Host name: %s"), hostName.c_str());

    return hostName;
}

bool update(String url, int port) {
    String bin = getBinName(url);
    String host = getHostName(url);

    Log.noticeln(F("Connecting to: http://%s/ using client %d"), host.c_str(), espClient.connected());
    if (espClient.connect(host.c_str(), port)) {
        // Connection Succeed.
        // Fecthing the bin
        Log.noticeln("Fetching Bin: %s", bin.c_str());
        
        // Get the contents of the bin file
        espClient.print(String("GET ") + bin + " HTTP/1.1\r\n" +
            "Host: " + host + "\r\n" +
            "Cache-Control: no-cache\r\n" +
            "Connection: close\r\n\r\n");

        unsigned long timeout = millis();

        while (espClient.available() == 0) {
            if (millis() - timeout > 5000) {
                Serial.println("client Timeout !");
                espClient.stop();
                return false;
            }
        }
        while (espClient.available()) {
            // read line till /n
            String line = espClient.readStringUntil('\n');
            // remove space, to check if the line is end of headers
            line.trim();

            // if the the line is empty,
            // this is end of headers
            // break the while and feed the
            // remaining `client` to the
            // Update.writeStream();
            if (!line.length()) {
                //headers ended
                break; // and get the OTA started
            }

            // Check if the HTTP Response is 200
            // else break and Exit Update
            if (line.startsWith("HTTP/1.1")) {
                if (line.indexOf("200") < 0) {
                    Log.errorln("Got a non 200 status code from server (%s). Exiting OTA Update.", line.c_str());
                    return false;
                }
            }

            // extract headers here
            // Start with content length
            if (line.startsWith("Content-Length: ")) {
                contentLength = atoi((getHeaderValue(line, "Content-Length: ")).c_str());
                Log.noticeln("Got %d bytes from server", contentLength);
            }

            // Next, the content type
            if (line.startsWith("Content-Type: ")) {
                String contentType = getHeaderValue(line, "Content-Type: ");
                Log.noticeln("Got %s payload.", contentType.c_str());
                if (contentType != "application/octet-stream") {
                    Log.warningln(F("Content type is not supported: %s. Trying anyway"), contentType);
                }
                isValidContentType = true;
            }
        }
    }
    else {
        // Connect to S3 failed
        // May be try?
        // Probably a choppy network?
        Log.errorln(F("Connection to %s failed. Please check your setup"), host.c_str());
        // retry??
    }

    // Check what is the contentLength and if content type is `application/octet-stream`
    Log.noticeln("contentLength : %d, isValidContentType : %d", contentLength, isValidContentType);

    // check contentLength and content type
    if (contentLength && isValidContentType) {
        // Check if there is enough to OTA Update
        bool canBegin = Update.begin(contentLength);
        if (canBegin) {
            Log.noticeln("Begin OTA. This may take 2 - 5 mins to complete. Things might be quite for a while.. Patience!");
            size_t written = Update.writeStream(espClient);

            if (written == contentLength) {
                Log.noticeln("Written : %d successfully", written);
            }
            else {
                Log.warningln("Written only : %d/%d. Retry?", written, contentLength);
                // retry??
            }

            if (Update.end()) {
                Log.noticeln("OTA done!");
                if (Update.isFinished()) {
                    Log.noticeln("Update successfully completed.");
                    return true;
                }
                else {
                    Log.errorln("Update not finished? Something went wrong!");
                }
            }
            else {
                Log.errorln("Error Occurred. Error #: %d", Update.getError());
            }
        }
        else {
            // not enough space to begin OTA
            // Understand the partitions and
            // space availability
            Log.errorln("Not enough space to begin OTA");
            espClient.flush();
        }
    }
    else {
        Log.errorln("There was no content in the response");
        espClient.flush();
    }
    return false;
}