// Adapted from https://github.com/jnsdbr/esp32-ota-update-mqtt/blob/master/src/main.cpp

String getHeaderValue(String header, String headerName) {
    return header.substring(strlen(headerName.c_str()));
}

String getBinName(String url) {
    int index = 0;

    // Search for last /
    for (int i = 0; i < url.length(); i++) {
        if (url[i] == '/') {
            index = i;
        }
    }

    String binName = "";

    // Create binName
    for (int i = index; i < url.length(); i++) {
        binName += url[i];
    }

    return binName;
}

String getHostName(String url) {
     int index = 0;

    // Search for last /
    for (int i = 0; i < url.length(); i++) {
        if (url[i] == '/') {
            index = i;
        }
    }

    String hostName = "";

    // Create binName
    for (int i = 0; i < index; i++) {
        hostName += url[i];
    }

    return hostName;
}

bool update(String url, int port) {
  float voltage = getVoltage();
  if (voltage < 3.7) {
    Log.errorln(F("Votage is too low. Ensure battery delivers at least 3.7V (currently: %F)"), voltage);
    return false;
  }

  //Start LittleFS
  if(!LittleFS.begin()){ 
    Log.errorln(F("Impossible to start LittleFS"));
    return false;
  }

  String bin = getBinName(url);
  String host = getHostName(url);

  WiFiClient otaClient;
  bool resume = !LittleFS.exists("/completed");
  File file;

  String header = "Host: ";
  header += host;
  header += "\r\nCache-Control: no-cache\r\n";
  header += "Connection: close\r\n";

  if (resume) {
    file = LittleFS.open("/update.bin", "a");
    header += "Range: bytes=";
    header += file.size();
    header += "\r\n";
    Log.traceln(F("File already exists. Resuming."));
  } else {
    LittleFS.remove("/completed");
    LittleFS.remove("/update.bin");

    Log.traceln(F("New file created."));
    file = LittleFS.open("/update.bin", "w");
  }

  Log.noticeln(F("File open. Ready to receive data"));

  if (!otaClient.connect(host.c_str(), port)) {
    // Probably a choppy network?
    Log.errorln(F("Connection to %s failed. Please check your setup"), host.c_str());
    file.close();
    return false;
  }

  // Connection Succeed.
  // Fecthing the bin
  Log.noticeln(F("Fetching Bin: %s"), bin.c_str());

  // Get the contents of the bin file
  String request = "GET ";
  request += bin;
  request += " HTTP/1.1\r\n";
  request += header;
  request += "\r\n";

  Log.traceln(F("Sending request: %s"), request.c_str());
  otaClient.print(request);

  unsigned long timeout = millis();

  while (otaClient.available() == 0) {
    if (millis() - timeout > 10000) {
      Log.errorln(F("Client Timeout!"));
      otaClient.stop();
      file.close();
      return false;
    }
    delay(100);
  }

  otaClient.setTimeout(5000);
  int contentLength;
  bool acceptRange = false;

  while (otaClient.available()) {
    // read line till /n
    String line = otaClient.readStringUntil('\n');
    // remove space, to check if the line is end of headers
    line.trim();
    Log.traceln(F("Header found. %s"), line.c_str());

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
        Log.errorln(F("Got a non 200 status code from server. Exiting OTA Update."));
        otaClient.stop();
        file.close();
        return false;
      }
    }

    // extract headers here
    // Start with content length
    if (line.startsWith("Content-Length: ")) {
      contentLength = atoi(getHeaderValue(line, "Content-Length: ").c_str());
      Log.noticeln(F("Got %d bytes from server"), contentLength);
    }

    // Next, the content type
    if (line.startsWith("Content-Type: ")) {
      String contentType = getHeaderValue(line, "Content-Type: ");
      Log.noticeln(F("Got %s payload."), contentType.c_str());
    }

    if (line.startsWith("Accept-Ranges: ")) {
      Log.noticeln(F("This server accepts ranges"));
      acceptRange = true;
    }
  }

  if (contentLength <= 0) {
    Log.errorln(F("Content length incorrect"));
    otaClient.stop();
    file.close();
    return false;
  }

  if (resume && !acceptRange) {
    // Loading file from scratch anyway
    Log.warningln(F("It seems file server does not support HTTP range requests"));
    file.close();
    file = LittleFS.open("/update.bin", "w");
  }

  // Start reading file
  int downloaded = 0;
  byte buffer[256];
  while (downloaded < contentLength) {
    int toRead = sizeof(buffer);
    if (downloaded + toRead > contentLength) {
      toRead = contentLength - downloaded;
    }
    int read = otaClient.readBytes(buffer, toRead);
    downloaded += read;

    Log.traceln(F("Received %d bytes"), downloaded);

    if (read < toRead) {
      Log.errorln(F("Not enough byte read. I requested %d, but I received %d. Downloaded so far: %d. Aborting now."), read, toRead, downloaded);
      otaClient.stop();
      file.close();
      return false;
    }

    if (file.write(buffer, read) != read) {
      Log.errorln(F("Error saving to file. Deleting it."));
      file.close();
      otaClient.stop();
      LittleFS.remove("/update.bin");
      return false;
    }
  }

  otaClient.stop();
  file.close();
  file = LittleFS.open("/completed", "w");
  file.write("OK");
  file.close();

  // File saved. Now we can safely apply the update
  file = LittleFS.open("/update.bin", "r");
  if (!Update.begin(file.size())) {
    Log.errorln(F("Not enough space to perform the OTA"));
    file.close();
    return false;
  }

  Log.noticeln(F("Ready to write %d bytes"), file.size());
  Log.noticeln(F("Begin OTA. This may take 2 - 5 mins to complete. Things might be quite for a while.. Patience!"));
      
  size_t written = Update.writeStream(file);

  if (written == file.size()) {
      Log.noticeln(F("Written :%d successfully"), written);
  }
  else {
      Log.errorln(F("Written only : %d/%d."), written, file.size());
      file.close();
      return false;
  }

  if (!Update.end()) {
    Log.errorln(F("Update not finished? Something went wrong!"));
    file.close();
    return false;
  }

  if (!Update.isFinished()) {
    Log.errorln(F("Error Occurred. Error #: %d"), Update.getError());
    file.close();
    return false;
  }

  Log.noticeln(F("OTA done!"));
  file.close();
  return true;
}


