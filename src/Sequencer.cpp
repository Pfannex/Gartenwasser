#include "Sequencer.h"

#include "Logger.h"

namespace {

constexpr uint8_t kMaxQueueLength = 5;

uint8_t queue[kMaxQueueLength] = {0};
uint8_t queueLength = 0;
uint8_t currentIndex = 0;
bool running = false;

}  // namespace

void Sequencer::begin() {
  running = false;
  queueLength = 0;
  currentIndex = 0;
}

bool Sequencer::start(const uint8_t *valves, uint8_t count) {
  if (count == 0) {
    return false;
  }
  queueLength = count < kMaxQueueLength ? count : kMaxQueueLength;
  for (uint8_t i = 0; i < queueLength; i++) {
    queue[i] = valves[i];
  }
  currentIndex = 0;
  running = true;
  Logger::logf(Logger::Type::INFO, Logger::Source::SEQ, "Automatik gestartet, %u Ventil(e) in Warteschlange",
               queueLength);
  return true;
}

void Sequencer::stop() {
  if (running) {
    Logger::log(Logger::Type::INFO, Logger::Source::SEQ, "Automatik gestoppt");
  }
  running = false;
  queueLength = 0;
  currentIndex = 0;
}

bool Sequencer::isRunning() {
  return running;
}

uint8_t Sequencer::getActiveValve() {
  if (!running || currentIndex >= queueLength) {
    return 0;
  }
  return queue[currentIndex];
}

uint8_t Sequencer::advance() {
  if (!running) {
    return 0;
  }
  currentIndex++;
  if (currentIndex >= queueLength) {
    running = false;
    Logger::log(Logger::Type::INFO, Logger::Source::SEQ, "Automatik abgeschlossen");
    return 0;
  }
  Logger::logf(Logger::Type::INFO, Logger::Source::SEQ, "Wechsel zu V%u", queue[currentIndex]);
  return queue[currentIndex];
}

uint8_t Sequencer::getPendingCount() {
  if (!running || currentIndex + 1 >= queueLength) {
    return 0;
  }
  return queueLength - currentIndex - 1;
}

uint8_t Sequencer::getPendingValve(uint8_t offset) {
  const uint8_t idx = currentIndex + 1 + offset;
  if (idx >= queueLength) {
    return 0;
  }
  return queue[idx];
}
