#include "FilePrint.h"
#include "esp_littlefs.h"

String FilePrint::getLastLogFileName() {
    return lastLogFileName;
}

FilePrint::FilePrint() {
    Log.verboseln("Mounting LittleFS partition");
    esp_err_t err;
    
    //err = esp_littlefs_format(PARTITION_LABEL);

    if (!LittleFS.begin(false, BASE_PATH, MAX_OPEN_FILE, PARTITION_LABEL)){
        Log.errorln("LittleFS Mount Failed. Formatting");
        esp_err_t err = esp_littlefs_format(PARTITION_LABEL);

        if (err != ESP_OK){
            Log.errorln("LittleFS formatting failed");
            return;
        }
    }
    File root = LittleFS.open("/");
    Log.verboseln(F("List root file folder"));

    if(!root){
        Log.errorln("Failed to open directory");
        return;
    }

    if(!root.isDirectory()){
        Log.errorln("\"/\" is not a directory");
        return;
    }
    
    int lastSeq = -1;

    root.rewindDirectory();
    String nextFileName = root.getNextFileName();
    while (nextFileName != "") {
        File nextFile = LittleFS.open(nextFileName);
        uint16_t nextFileSize = nextFile.size();
        nextFile.close();
        Log.traceln("  FILE: %s, SIZE: %d", nextFileName, nextFileSize);

        if (nextFileName.startsWith("/log") && nextFileSize > 0){
            String seq = nextFileName.substring(4, nextFileName.lastIndexOf("."));
            if (lastSeq < seq.toInt()){
                lastSeq = seq.toInt();
                lastLogFileName = nextFileName;
            }
            if (lastSeq >= MAX_LOG_FILE_NUMBER) {
                Log.errorln("Too many log files. Deleting %s", nextFileName);
                LittleFS.remove("/" + nextFileName);
            }
        }
        nextFileName = root.getNextFileName();
    }
    Log.verboseln("Last log sequence: %d", lastSeq);
    

    char buffer [20];
    if (lastSeq >= MAX_LOG_FILE_NUMBER - 1) {
        Log.verboseln("Rotating log files");
        
        // Delete the oldest log file
        if (LittleFS.exists("/log000.txt")){
            Log.verboseln("Deleting oldest log file");
            if (LittleFS.remove("/log000.txt")){
                Log.verboseln("/log000.txt file deleted");
            } else {
                Log.errorln("/log000.txt delete failed");
            }
        }
        String filename = String(buffer);

        // Shifting remaining files
        int nextFile = 1;
        for (int i = 1; i < MAX_LOG_FILE_NUMBER ; i++){
            snprintf(buffer, sizeof(buffer), "/log%03d.txt", i);
            String filename = String(buffer);

            if (!LittleFS.exists(filename)){
                nextFile++;
                lastSeq--;
                continue;
            }

            snprintf(buffer, sizeof(buffer), "/log%03d.txt", i - nextFile);
            String newFilename = String(buffer);
            if (LittleFS.rename(filename, newFilename)){
                Log.verboseln("%s file renamed to %s", filename, newFilename);
            } else {
                Log.errorln("%s rename failed", filename);
            }
        }
    }

    // Prepare writing next log file
    lastSeq++;

    if (lastSeq >= MAX_LOG_FILE_NUMBER) {
        lastSeq = MAX_LOG_FILE_NUMBER - 1;
    }

    snprintf(buffer, sizeof(buffer), "/log%03d.txt", lastSeq);
    String path = String(buffer);
    Log.noticeln("Opening log file for writing: %s", path);
    logFile = LittleFS.open(path, "w");
    if (!logFile){
        Log.errorln("Failed to open log file for writing: %s", path);
        return;
    }

    logFile.println("# Log File");

    initialized = true;
}

size_t FilePrint::write(const uint8_t * buffer, size_t size) {
    if (!initialized) {
        Log.errorln("FilePrint not initialized");
        return 0;
    } 
    return logFile.write(buffer, size);
}

size_t FilePrint::write(uint8_t c) {
    return write(&c, 1);
}

void FilePrint::close() {
    if (!initialized) {
        Log.errorln("FilePrint not initialized");
        return;
    }
    logFile.println("# End of Log File");
    logFile.flush();
    Log.noticeln("Closing log file. It is now %d byte", logFile.size());
    logFile.close();
    initialized = false;
}
