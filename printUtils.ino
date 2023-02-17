void printPrefix(Print* _logOutput, int logLevel) {
    printTimestamp(_logOutput);
    //printLogLevel (_logOutput, logLevel);
}

void printTimestamp(Print* _logOutput) {

  // Division constants
  const unsigned long MSECS_PER_SEC       = 1000;

  // Total time
  const unsigned long msecs               =  millis();
  const unsigned long secs                =  msecs / MSECS_PER_SEC;

  // Time in components
  const unsigned int MilliSeconds        =  msecs % MSECS_PER_SEC;
  const unsigned int Seconds             =  secs  % 100 ;

  // Time as string
  char timestamp[16];
  sprintf(timestamp, "%d-%02d.%03d ", rtcData.failedConnection, Seconds, MilliSeconds);
  _logOutput->print(timestamp);
}