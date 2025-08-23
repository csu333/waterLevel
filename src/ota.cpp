// Source: https://github.com/jnsdbr/esp32-ota-update-mqtt/blob/master/src/main.cpp
#include "ota.h"

int contentLength = 0;
bool isValidContentType = false;

extern WiFiClient espClient;

TftpClient<WiFiUDP> tftp;

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

int getDownloadSize(HttpClient http, String bin) {
    //Log.noticeln(F("Connecting to: http://%s/"), http.);

    //if (!espClient.connect(host.c_str(), port)) {
    if (!http.get(bin.c_str())) {
        // Connect failed
        // Probably a choppy network?
        Log.errorln(F("Connection failed. Please check your setup"));
        return -1;
    }

    // Flush data to avoid collision with other streams
    /*espClient.flush();
    delay(50);
    espClient.flush();*/
    
    // Connection Succeed.
    // Fecthing the bin    
    // Get the contents of the bin file
    /*String getRequest = "GET " + bin + " HTTP/1.1\r\n" +
        "Host: " + host + "\r\n" +
        "Cache-Control: no-cache\r\n" +
        "Connection: close\r\n"
        "\r\n";
    espClient.print(getRequest);*/
    delay(50);

    unsigned long timeout = millis();

    //while (espClient.available() == 0) {
    while (http.available() == 0) {
        if (millis() - timeout > 5000) {
            Log.errorln("client Timeout !");
            //espClient.stop();
            http.endRequest();
            return -1;
        }
    }

    /*while (http.available()) {
        // read line till /n
        String line = espClient.readStringUntil('\n');
        // remove space, to check if the line is end of headers
        line.trim();
        Log.verboseln("Received: %s", line.c_str());

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
    }*/
    if (!http.responseStatusCode() == 200) {
        Log.errorln("Got a non 200 status code from server (%d). Exiting OTA Update.", http.responseStatusCode());
        return -1;
    }

    contentLength = http.contentLength();
    Log.noticeln("Got %d bytes from server", contentLength);
    isValidContentType = true;

    // Check what is the contentLength and if content type is `application/octet-stream`
    Log.noticeln("contentLength : %d, isValidContentType : %d", contentLength, isValidContentType);

    if (contentLength && isValidContentType) {
        return contentLength;
    }

    return -1;
}

bool update(String url, int port) {
    bool isHttp = url.startsWith("http://");
    bool isTftp = url.startsWith("tftp://");
    int announcedSize = -1;
    HttpClient * httpClient;

    //espClient = WiFiClient(espClient);

    const esp_partition_t *running  = esp_ota_get_running_partition();
    Log.traceln("Configured partition: %s", running->label);

    if (url.indexOf(",") > 0) {
        announcedSize = atoi(url.substring(url.indexOf(",") + 1).c_str());
        Log.traceln("Announced size: %d", announcedSize);
        url = url.substring(0, url.indexOf(","));
    }

    TftpClient<WiFiUDP> tftp;

    if (!isHttp && !isTftp) {
        Log.warningln(F("Unqualified URL: %s. Assuming http"), url.c_str());
        isHttp = true;
    }

    String bin = getBinName(url);
    String host = getHostName(url);

    int contentLength = 0;
    if (isHttp) {
        HttpClient http = HttpClient(espClient, host, port);
        httpClient = &http;
        contentLength = getDownloadSize(http, bin);
        if (contentLength <= 0) {
            return false;
        }
        Log.traceln("HTTP content length: %d", contentLength); 
    }

    if (isTftp && announcedSize <= 0) {
        const esp_partition_t *ota = esp_ota_get_next_update_partition(NULL);
        contentLength = ota->size - 1;
        Log.traceln("TFTP do not provide size. Using partition size: %d on partition %s", contentLength, ota->label);
    }

    if (isTftp && announcedSize > 0) {
        contentLength = announcedSize;
    }

    // check contentLength and content type
    if (contentLength <= 0) {
        Log.errorln("There was no content in the response");
        //espClient.flush();
        return false;
    }

    // Check if there is enough to OTA Update
    bool canBegin = Update.begin(contentLength);
    if (!canBegin) {
        // not enough space to begin OTA
        // Understand the partitions and
        // space availability
        Log.errorln("Not enough space to begin OTA");
        //espClient.flush();
        return false;
    }

    Log.noticeln("Begin OTA. This may take 2 - 5 mins to complete. Things might be quite for a while.. Patience!");

    size_t written = 0;
    uint8_t buffer[512];
    written = 0;
    uint16_t wait = 0;
    uint32_t received = 0;

    // HTTP download
    if (isHttp) {
    Log.noticeln("Downloading %s", bin.c_str());
        //written = Update.writeStream(http.read());
        while (!httpClient->endOfStream()) {
            size_t read = httpClient->read(buffer, sizeof(buffer));
            received += read;
            Log.traceln("Read %d bytes. Content length so far: %d", read, received);
            if (read) {
                size_t w = Update.write(buffer, read);
                if (w != read) {
                    Log.errorln("Write failed (read: %d, written: %d)", read, w);
                    break;
                }
                written += w;
                Log.verboseln("Written %d bytes, total: %d", w, written);
            }
        }
    }

    // TFTP Download
    if (isTftp) {
        IPAddress tftpIP;
        bin = bin.substring(1);
        Log.noticeln("Downloading %s", bin.c_str());

        if (WiFi.hostByName(host.c_str(), tftpIP) != 1) {
            Log.errorln("DNS lookup failed");
            return false;
        }

        Log.traceln("TFTP IP: %s", tftpIP.toString().c_str());

        if (!tftp.beginDownload(bin.c_str(), tftpIP)) {
            Log.errorln("TFTP begin failed");
            return false;
        }

        while (!tftp.available() && wait++ < 5000) {
            delay(1);
        }
        
        while (!tftp.finished() && !tftp.error()) {
            size_t read = tftp.read(buffer, sizeof(buffer));
            received += read;
            Log.traceln("Read %d bytes. Content length so far: %d", read, received);
            if (read) {
                size_t w = Update.write(buffer, read);
                if (w != read) {
                    Log.errorln("Write failed (read: %d, written: %d)", read, w);
                    break;
                }
                written += w;
                Log.verboseln("Written %d bytes, total: %d", w, written);
            }
            delay(10);
        }

        if (tftp.finished()) {
            Log.traceln("TFTP transfer finished");
        }

        if (!tftp.error()) {
            size_t read = tftp.read(buffer, sizeof(buffer));
            Update.write(buffer, read);
            tftp.stop();
            Log.traceln("TFTP closed");
        }

        if (contentLength <= 0) {
            contentLength = received;
        }

        if (tftp.error()) {
            Log.errorln("TFTP error occurred: %s", tftp.errorMessage().c_str());
            tftp.stop();
            return false;
        }
    }

    if (written == contentLength) {
        Log.noticeln("Written : %d successfully", written);
    }
    else {
        Log.warningln("Written only : %d/%d.", written, contentLength);
        return false;
    }

    // TFTP does not provide file size 
    if (!Update.end(isTftp && announcedSize <= 0)) {
        Log.errorln("Error Occurred. %s (%d)", Update.errorString(), Update.getError());
        return false;
    }

    if (!Update.isFinished()) {
        Log.errorln("Update not finished? Something went wrong!");
        return false;
    }

    Log.noticeln("Update successfully completed.");
    return true;
}