/**
 * @file OperationalModeManager.cpp
 * @brief Implementation of OperationalModeManager
 * @version 2.0
 * @date 2025-10-01
 */

#include "OperationalModeManager.h"

// Static member initialization for mode handlers
uint32_t TestingModeHandler::_lastAutoTest = 0;
uint16_t TestingModeHandler::_testCounter = 0;
uint32_t ProductionModeHandler::_lastPowerCheck = 0;
bool ProductionModeHandler::_deepSleepEnabled = false;

// PowerManager static members
PowerManager::CpuFrequency PowerManager::_currentFrequency = CPU_64MHZ;
bool PowerManager::_lowPowerEnabled = false;

//================================================================//
//         OPERATIONALMODEMANAGER IMPLEMENTATION                  //
//================================================================//

OperationalModeManager::OperationalModeManager(OperationalMode_t initialMode)
    : _currentMode(initialMode), _previousMode(initialMode),
      _transitionInProgress(false), _transitionStartTime(0), _transitionTimeout(5000),
      _lastHeartbeat(0), _lastPeriodicMessage(0), _lastBeacon(0), _lastStatUpdate(0),
      _sleepPending(false), _sleepStartTime(0), _wakeupTime(0),
      _callbacks(nullptr), _lastLedUpdate(0), _ledPatternIndex(0), _ledState(false) {
    
    initializeDefaultConfigs();
}

OperationalModeManager::~OperationalModeManager() {
    // Cleanup if needed
}

void OperationalModeManager::initializeDefaultConfigs() {
    initializeInteractiveConfig();
    initializeTestingConfig();
    initializeProductionConfig();
}

void OperationalModeManager::initializeInteractiveConfig() {
    ModeConfig& config = _modeConfigs[INTERACTIVE_MODE];
    config.serialEnabled = true;
    config.debugOutput = true;
    config.autoTesting = false;
    config.sleepEnabled = false;
    config.sleepCycleMs = 3600000;
    config.wakeWindowMs = 120000;
    config.cpuFrequency = 5;  // 64MHz
    config.autoForwarding = true;
    config.beaconEnabled = true;
    config.beaconIntervalMs = 300000;
    config.ackRequired = false;
    config.maxRetries = 3;
    config.periodicMessages = false;
    config.messageIntervalMs = 60000;
    config.heartbeatEnabled = true;
    config.heartbeatIntervalMs = 5000;
    config.ledEnabled = true;
    config.ledBlinkPattern = 0x1;
    config.ledBlinkDuration = 100;
}

void OperationalModeManager::initializeTestingConfig() {
    ModeConfig& config = _modeConfigs[TESTING_MODE];
    config.serialEnabled = true;
    config.debugOutput = true;
    config.autoTesting = true;
    config.sleepEnabled = false;
    config.sleepCycleMs = 0;
    config.wakeWindowMs = 0;
    config.cpuFrequency = 5;  // 64MHz
    config.autoForwarding = true;
    config.beaconEnabled = true;
    config.beaconIntervalMs = 60000;
    config.ackRequired = true;
    config.maxRetries = 5;
    config.periodicMessages = true;
    config.messageIntervalMs = 30000;
    config.heartbeatEnabled = true;
    config.heartbeatIntervalMs = 2000;
    config.ledEnabled = true;
    config.ledBlinkPattern = 0x3;  // Double blink
    config.ledBlinkDuration = 50;
}

void OperationalModeManager::initializeProductionConfig() {
    ModeConfig& config = _modeConfigs[PRODUCTION_MODE];
    config.serialEnabled = false;
    config.debugOutput = false;
    config.autoTesting = false;
    config.sleepEnabled = true;
    config.sleepCycleMs = 3600000;
    config.wakeWindowMs = 60000;
    config.cpuFrequency = 2;  // 8MHz for power saving
    config.autoForwarding = true;
    config.beaconEnabled = false;
    config.beaconIntervalMs = 0;
    config.ackRequired = false;
    config.maxRetries = 2;
    config.periodicMessages = false;
    config.messageIntervalMs = 0;
    config.heartbeatEnabled = false;
    config.heartbeatIntervalMs = 0;
    config.ledEnabled = false;
    config.ledBlinkPattern = 0x0;
    config.ledBlinkDuration = 0;
}

bool OperationalModeManager::setMode(OperationalMode_t newMode) {
    if (!isValidMode(newMode)) {
        Serial.println("ERROR: Invalid mode");
        return false;
    }
    
    if (newMode == _currentMode) {
        return true;  // Already in this mode
    }
    
    if (!validateModeTransition(_currentMode, newMode)) {
        Serial.println("ERROR: Invalid mode transition");
        return false;
    }
    
    performModeTransition(newMode);
    return true;
}

bool OperationalModeManager::validateModeTransition(OperationalMode_t from, OperationalMode_t to) {
    // All transitions are valid for now
    return isValidMode(from) && isValidMode(to);
}

void OperationalModeManager::performModeTransition(OperationalMode_t newMode) {
    _transitionInProgress = true;
    _transitionStartTime = millis();
    
    // Call exit callback for old mode
    onModeExit(_currentMode);
    
    // Update state
    _previousMode = _currentMode;
    _currentMode = newMode;
    
    // Update statistics
    _modeStats[_previousMode].update();
    _modeStats[_currentMode].reset();
    
    // Call enter callback for new mode
    onModeChange(newMode);
    
    // Apply CPU frequency for new mode
    adjustCpuFrequency(_modeConfigs[newMode].cpuFrequency);
    
    finalizeModeTransition();
    
    logModeTransition(_previousMode, newMode);
}

void OperationalModeManager::finalizeModeTransition() {
    _transitionInProgress = false;
    
    if (_callbacks) {
        _callbacks->onModeTransition(_previousMode, _currentMode);
    }
}

void OperationalModeManager::setModeConfig(OperationalMode_t mode, const ModeConfig& config) {
    if (isValidMode(mode)) {
        _modeConfigs[mode] = config;
        
        if (_callbacks) {
            _callbacks->onModeConfigChange(mode, config);
        }
    }
}

ModeConfig OperationalModeManager::getModeConfig(OperationalMode_t mode) const {
    if (isValidMode(mode)) {
        return _modeConfigs[mode];
    }
    return ModeConfig();
}

ModeStatistics OperationalModeManager::getModeStatistics(OperationalMode_t mode) const {
    if (isValidMode(mode)) {
        return _modeStats[mode];
    }
    return ModeStatistics();
}

void OperationalModeManager::resetStatistics(OperationalMode_t mode) {
    if (isValidMode(mode)) {
        _modeStats[mode] = ModeStatistics();
    }
}

void OperationalModeManager::resetAllStatistics() {
    for (int i = 0; i < NUM_OPERATIONAL_MODES; i++) {
        _modeStats[i] = ModeStatistics();
    }
}

void OperationalModeManager::setTransitionCallbacks(ModeTransitionCallbacks* callbacks) {
    _callbacks = callbacks;
}

void OperationalModeManager::run() {
    // Update current mode statistics
    _modeStats[_currentMode].update();
    
    // Handle mode-specific periodic tasks
    ModeConfig& config = _modeConfigs[_currentMode];
    
    if (config.heartbeatEnabled) {
        handleHeartbeat();
    }
    
    if (config.periodicMessages) {
        handlePeriodicMessages();
    }
    
    if (config.beaconEnabled) {
        handleBeacons();
    }
    
    if (config.ledEnabled) {
        handleLedPattern();
    }
    
    // Handle power management
    if (config.sleepEnabled) {
        handlePowerManagement();
    }
    
    // Update statistics periodically
    handleStatisticsUpdate();
}

void OperationalModeManager::onModeChange(OperationalMode_t newMode) {
    Serial.print("Entering mode: ");
    Serial.println(getModeString(newMode));
    
    if (_callbacks) {
        _callbacks->onModeEnter(newMode);
    }
    
    // Mode-specific initialization
    switch (newMode) {
        case INTERACTIVE_MODE:
            InteractiveModeHandler::onEnter(this);
            break;
        case TESTING_MODE:
            TestingModeHandler::onEnter(this);
            break;
        case PRODUCTION_MODE:
            ProductionModeHandler::onEnter(this);
            break;
        case NUM_OPERATIONAL_MODES:
        default:
            Serial.print("Error: Invalid operational mode in onModeChange: ");
            Serial.println(static_cast<int>(newMode));
            break;
    }
}

void OperationalModeManager::onModeExit(OperationalMode_t oldMode) {
    Serial.print("Exiting mode: ");
    Serial.println(getModeString(oldMode));
    
    if (_callbacks) {
        _callbacks->onModeExit(oldMode);
    }
    
    // Mode-specific cleanup
    switch (oldMode) {
        case INTERACTIVE_MODE:
            InteractiveModeHandler::onExit(this);
            break;
        case TESTING_MODE:
            TestingModeHandler::onExit(this);
            break;
        case PRODUCTION_MODE:
            ProductionModeHandler::onExit(this);
            break;
        case NUM_OPERATIONAL_MODES:
        default:
            Serial.print("Error: Invalid operational mode in onModeExit: ");
            Serial.println(static_cast<int>(oldMode));
            break;
    }
}

void OperationalModeManager::onMessageReceived() {
    _modeStats[_currentMode].messagesProcessed++;
}

void OperationalModeManager::onCommandExecuted() {
    _modeStats[_currentMode].commandsExecuted++;
}

void OperationalModeManager::onSleepRequest() {
    if (_modeConfigs[_currentMode].sleepEnabled) {
        requestSleep();
    }
}

void OperationalModeManager::onWakeupRequest() {
    requestWakeup();
}

String OperationalModeManager::getModeString(OperationalMode_t mode) const {
    return String(operationalModeToString(mode));
}

bool OperationalModeManager::isSleepMode() const {
    return _sleepPending || _modeConfigs[_currentMode].sleepEnabled;
}

bool OperationalModeManager::canEnterSleep() const {
    return _modeConfigs[_currentMode].sleepEnabled && !_transitionInProgress;
}

uint32_t OperationalModeManager::getTimeInCurrentMode() const {
    if (_modeStats[_currentMode].modeStartTime > 0) {
        return millis() - _modeStats[_currentMode].modeStartTime;
    }
    return 0;
}

void OperationalModeManager::requestSleep() {
    if (canEnterSleep()) {
        _sleepPending = true;
        _sleepStartTime = millis();
        _wakeupTime = _sleepStartTime + _modeConfigs[_currentMode].sleepCycleMs;
    }
}

void OperationalModeManager::requestWakeup() {
    if (_sleepPending) {
        _sleepPending = false;
        _modeStats[_currentMode].wakeEvents++;
    }
}

void OperationalModeManager::handlePowerManagement() {
    if (_sleepPending) {
        if (shouldWakeUp()) {
            exitSleepMode();
        }
    } else if (canEnterSleep()) {
        // Check if we should enter sleep based on wake window
        if (_modeConfigs[_currentMode].sleepCycleMs > 0) {
            enterSleepMode();
        }
    }
}

void OperationalModeManager::enterSleepMode() {
    Serial.println("Entering sleep mode");
    _sleepPending = true;
    _sleepStartTime = millis();
    _wakeupTime = _sleepStartTime + _modeConfigs[_currentMode].sleepCycleMs;
    _modeStats[_currentMode].sleepCycles++;
}

void OperationalModeManager::exitSleepMode() {
    Serial.println("Exiting sleep mode");
    _sleepPending = false;
    _modeStats[_currentMode].wakeEvents++;
}

bool OperationalModeManager::shouldWakeUp() {
    return (millis() >= _wakeupTime);
}

void OperationalModeManager::adjustCpuFrequency(uint8_t frequency) {
    PowerManager::setCpuFrequency((PowerManager::CpuFrequency)frequency);
}

void OperationalModeManager::handleHeartbeat() {
    uint32_t now = millis();
    ModeConfig& config = _modeConfigs[_currentMode];
    
    if (now - _lastHeartbeat >= config.heartbeatIntervalMs) {
        // Heartbeat logic - could toggle LED, send beacon, etc.
        _lastHeartbeat = now;
    }
}

void OperationalModeManager::handlePeriodicMessages() {
    uint32_t now = millis();
    ModeConfig& config = _modeConfigs[_currentMode];
    
    if (now - _lastPeriodicMessage >= config.messageIntervalMs) {
        // Send periodic message
        _lastPeriodicMessage = now;
    }
}

void OperationalModeManager::handleBeacons() {
    uint32_t now = millis();
    ModeConfig& config = _modeConfigs[_currentMode];
    
    if (now - _lastBeacon >= config.beaconIntervalMs) {
        // Send beacon
        _lastBeacon = now;
    }
}

void OperationalModeManager::handleStatisticsUpdate() {
    uint32_t now = millis();
    if (now - _lastStatUpdate >= 1000) {  // Update every second
        _modeStats[_currentMode].update();
        _lastStatUpdate = now;
    }
}

void OperationalModeManager::handleLedPattern() {
    uint32_t now = millis();
    ModeConfig& config = _modeConfigs[_currentMode];
    
    if (now - _lastLedUpdate >= config.ledBlinkDuration) {
        updateLedPattern();
        _lastLedUpdate = now;
    }
}

void OperationalModeManager::updateLedPattern() {
    ModeConfig& config = _modeConfigs[_currentMode];
    
    // Check bit pattern
    bool shouldBeOn = (config.ledBlinkPattern >> _ledPatternIndex) & 0x1;
    
    _ledState = shouldBeOn;
    _ledPatternIndex = (_ledPatternIndex + 1) % 32;
    
    // Actual LED control would go here
    // digitalWrite(LED_PIN, _ledState ? HIGH : LOW);
}

void OperationalModeManager::setLedPattern(uint32_t pattern, uint16_t duration) {
    ModeConfig& config = _modeConfigs[_currentMode];
    config.ledBlinkPattern = pattern;
    config.ledBlinkDuration = duration;
}

bool OperationalModeManager::isValidMode(OperationalMode_t mode) const {
    return (mode >= 0 && mode < NUM_OPERATIONAL_MODES);
}

void OperationalModeManager::logModeTransition(OperationalMode_t from, OperationalMode_t to) {
    Serial.print("Mode transition: ");
    Serial.print(getModeString(from));
    Serial.print(" -> ");
    Serial.println(getModeString(to));
}

//================================================================//
//                MODE HANDLER IMPLEMENTATIONS                    //
//================================================================//

void InteractiveModeHandler::configure(ModeConfig& config) {
    // Already configured in initializeInteractiveConfig
}

void InteractiveModeHandler::onEnter(OperationalModeManager* manager) {
    Serial.println("Interactive mode active - Serial commands enabled");
}

void InteractiveModeHandler::onExit(OperationalModeManager* manager) {
    // Cleanup if needed
}

void InteractiveModeHandler::run(OperationalModeManager* manager) {
    // Mode-specific execution
}

void TestingModeHandler::configure(ModeConfig& config) {
    // Already configured in initializeTestingConfig
}

void TestingModeHandler::onEnter(OperationalModeManager* manager) {
    Serial.println("Testing mode active - Auto-testing enabled");
    _lastAutoTest = millis();
    _testCounter = 0;
}

void TestingModeHandler::onExit(OperationalModeManager* manager) {
    _testCounter = 0;
}

void TestingModeHandler::run(OperationalModeManager* manager) {
    // Auto-testing logic would go here
}

void ProductionModeHandler::configure(ModeConfig& config) {
    // Already configured in initializeProductionConfig
}

void ProductionModeHandler::onEnter(OperationalModeManager* manager) {
    Serial.println("Production mode active - Power optimization enabled");
    _deepSleepEnabled = true;
    _lastPowerCheck = millis();
}

void ProductionModeHandler::onExit(OperationalModeManager* manager) {
    _deepSleepEnabled = false;
}

void ProductionModeHandler::run(OperationalModeManager* manager) {
    // Production mode logic
}

//================================================================//
//                POWER MANAGER IMPLEMENTATION                    //
//================================================================//

bool PowerManager::setCpuFrequency(CpuFrequency freq) {
    _currentFrequency = freq;
    // Platform-specific frequency setting would go here
    return true;
}

PowerManager::CpuFrequency PowerManager::getCpuFrequency() {
    return _currentFrequency;
}

uint32_t PowerManager::estimatePowerConsumption(const ModeConfig& config) {
    // Estimate based on configuration
    uint32_t consumption = 1000;  // Base consumption in uA
    
    if (config.sleepEnabled) {
        consumption /= 10;
    }
    
    return consumption;
}

void PowerManager::enableLowPowerMode(bool enable) {
    _lowPowerEnabled = enable;
    // Platform-specific low power mode enabling
}

void PowerManager::configureWakeupSources(uint32_t sources) {
    // Platform-specific wakeup source configuration
}
