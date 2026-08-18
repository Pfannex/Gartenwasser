#include "Diagnostics.h"

#include <cstdio>

#include "I2C.h"
#include "Logger.h"

namespace {

bool i2cOk = true;
char lastError[96] = "";
bool newErrorPending = false;

void onLoggerError(const char *message) {
  char timestamp[16];
  Logger::currentTimestamp(timestamp, sizeof(timestamp));
  snprintf(lastError, sizeof(lastError), "%s %s", timestamp, message);
  newErrorPending = true;
}

}  // namespace

void Diagnostics::begin() {
  i2cOk = true;
  lastError[0] = '\0';
  newErrorPending = false;
  Logger::setErrorCallback(&onLoggerError);
}

bool Diagnostics::checkI2cStatus() {
  const bool reachable = I2C::isMcp23017Reachable();
  if (reachable == i2cOk) {
    return false;
  }
  i2cOk = reachable;
  if (!i2cOk) {
    Logger::log(Logger::Type::ERROR, Logger::Source::I2C, "MCP23017 nicht erreichbar (I2C-Bus-Ausfall).");
  } else {
    Logger::log(Logger::Type::INFO, Logger::Source::I2C, "MCP23017 wieder erreichbar.");
  }
  return true;
}

bool Diagnostics::isI2cOk() {
  return i2cOk;
}

bool Diagnostics::consumeNewError() {
  if (!newErrorPending) {
    return false;
  }
  newErrorPending = false;
  return true;
}

const char *Diagnostics::getLastError() {
  return lastError;
}
