// Source: https://github.com/jnsdbr/esp32-ota-update-mqtt/blob/master/src/main.cpp
#include "ota.h"

int contentLength = 0;
bool isValidContentType = false;

extern WiFiClient espClient;

TFTPClient tftp;

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

int getDownloadSize(esp_http_client_handle_t http) {
    delay(50);

    unsigned long timeout = millis();

    contentLength = esp_http_client_fetch_headers(http);
    int returnCode = !esp_http_client_get_status_code(http);
    if (returnCode && returnCode != 200) {
        Log.errorln("Got a non 200 status code from server (%d). Exiting OTA Update.", returnCode);
        return -1;
    }

    Log.noticeln("Got %d bytes from server", contentLength);

    return contentLength;
}

bool update(String url, int port) {
    bool isHttp = url.startsWith("http://");
    bool isTftp = url.startsWith("tftp://");
    int announcedSize = -1;
    esp_http_client_handle_t http;

    const esp_partition_t *running  = esp_ota_get_running_partition();
    Log.traceln("Configured partition: %s", running->label);

    if (url.indexOf(",") > 0) {
        announcedSize = atoi(url.substring(url.indexOf(",") + 1).c_str());
        Log.traceln("Announced size: %d", announcedSize);
        url = url.substring(0, url.indexOf(","));
    }

    if (!isHttp && !isTftp) {
        Log.warningln(F("Unqualified URL: %s. Assuming http"), url.c_str());
        isHttp = true;
    }

    String bin = getBinName(url);
    String host = getHostName(url);

    int contentLength = 0;
    if (isHttp) {
        esp_http_client_config_t config = {
            .url = url.c_str()
        };
        http = esp_http_client_init(&config);
        if (esp_http_client_open(http, 0) == ESP_FAIL) {
            Log.errorln("Failed to open HTTP connection");
            esp_http_client_cleanup(http);
            return false;
        }
        
        if (esp_http_client_write(http, NULL, 0) < 0) {
            Log.errorln("Failed to send HTTP request");
            esp_http_client_cleanup(http);
            return false;
        }

        contentLength = getDownloadSize(http);
        if (contentLength <= 0) {
            Log.errorln("There was no content in the response");
            esp_http_client_cleanup(http);
            return false;
        }
        Log.traceln("HTTP content length: %d", contentLength); 
    }

    const esp_partition_t *ota = esp_ota_get_next_update_partition(NULL);
    if (ota == NULL) {
        Log.errorln("Failed to get next partition");
        esp_http_client_cleanup(http);
        return false;
    }

    Log.traceln("Next partition: %s", ota->label);

    if (isTftp && announcedSize <= 0) {
        contentLength = ota->size - 1;
        Log.traceln("TFTP do not provide size. Using partition size: %d on partition %s", contentLength, ota->label);
    }

    if (isTftp && announcedSize > 0) {
        contentLength = announcedSize;
    }

    // check contentLength and content type
    if (contentLength <= 0) {
        Log.errorln("There was no content in the response");
        return false;
    }

    // Check if there is enough to OTA Update
    bool canBegin = Update.begin(contentLength);
    if (!canBegin) {
        // not enough space to begin OTA
        // Understand the partitions and
        // space availability
        Log.errorln("Not enough space to begin OTA");
        return false;
    }

    Log.noticeln("Begin OTA. This may take 2 - 5 mins to complete. Things might be quite for a while.. Patience!");

    size_t written = 0;
    const int BUFFSIZE = 512;
    char buffer[BUFFSIZE];
    written = 0;
    uint16_t wait = 0;
    uint32_t received = 0;
    bool image_header_was_checked = false;
    esp_ota_handle_t update_handle = 0;
    esp_err_t ret;
    int read;

    // HTTP download
    if (isHttp) {
        Log.noticeln("Downloading %s", bin.c_str());

        if (contentLength <= 0) {
            contentLength = OTA_SIZE_UNKNOWN;
        } 

        read = esp_http_client_read(http, buffer, BUFFSIZE);
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

        if (!tftp.initialize()) {
            Log.errorln("TFTP initialize failed: %s", tftp.getLastErrorMessage());
            return false;
        }

        if (!tftp.beginDownload(tftpIP, bin.c_str())) {
            Log.errorln("TFTP begin download failed: %s", tftp.getLastErrorMessage());
            return false;
        }

        read = tftp.readBlock((uint8_t*) buffer, BUFFSIZE);
    }

    while (read > 0 || (isTftp && (!tftp.isDownloadComplete() || tftp.getLastErrorCode() != 0))) {
        received += read;
        Log.traceln("Read %d bytes. Content length so far: %d", read, received);

        if (image_header_was_checked == false) {
            esp_app_desc_t new_app_info;
            if (read > sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t) + sizeof(esp_app_desc_t)) {
                // check current version with downloading
                if (esp_efuse_check_secure_version(new_app_info.secure_version) == false) {
                    Log.errorln(F("This a new app can not be downloaded due to a secure version is lower than stored in efuse."));
                    if (isHttp) { esp_http_client_cleanup(http); }
                    if (isTftp) { tftp.stop(); }
                    return false;
                }

                if (!update_handle) {
                    ret = esp_ota_begin(ota, contentLength, &update_handle);
                    if (ret != ESP_OK) {
                        Log.errorln("OTA begin failed: %s", esp_err_to_name(ret));

                        if (update_handle) {
                            esp_ota_abort(update_handle);
                        }

                        if (isHttp) { esp_http_client_cleanup(http); }
                        if (isTftp) { tftp.stop(); }

                        return false;
                    }
                    
                }

                image_header_was_checked = true;

            }
        }

        if (read) {
            ret = esp_ota_write(update_handle, (const void *)buffer, read);

            if (ret != ESP_OK) {
                Log.errorln("OTA write failed: %s", esp_err_to_name(ret));
                esp_ota_abort(update_handle);
                esp_http_client_cleanup(http);
                return false;
            }

            written += read;
            Log.verboseln("Written %d bytes, total: %d", read, written);
        }
        if (isHttp) { read = esp_http_client_read(http, buffer, BUFFSIZE); }
        if (isTftp) { read = tftp.readBlock((uint8_t*) buffer, BUFFSIZE); }
    }

    if (isTftp && tftp.getLastErrorCode()) {
        Log.errorln("TFTP error: %s", tftp.getLastErrorMessage());
        tftp.stop();
        return false;
    }

    if (tftp.isDownloadComplete()) {
        tftp.stop();
        Log.noticeln("TFTP download complete");
        contentLength = written;
    }

    Log.noticeln("OTA download finished");
    ret = esp_ota_end(update_handle);
    if (ret != ESP_OK) {
        Log.errorln("OTA end failed: %s", esp_err_to_name(ret));
        esp_http_client_cleanup(http);
        return false;
    }

    Log.noticeln("Setting boot partition: %s", ota->label);
    ret = esp_ota_set_boot_partition(ota);
    if (ret != ESP_OK) {
        Log.errorln("OTA set boot partition failed: %s", esp_err_to_name(ret));
        esp_http_client_cleanup(http);
        return false;
    }

    if (written == contentLength) {
        Log.noticeln("Written : %d successfully", written);
    }
    else {
        Log.warningln("Written only : %d/%d.", written, contentLength);
        return false;
    }

    Log.noticeln("Update successfully completed.");
    return true;
}