/**
 * @file AsyncWakeupFramework.cpp
 * @brief Implementation of AsyncWakeupFramework
 * @version 2.1
 * @date 2025-10-01
 */

#include "AsyncWakeupFramework.h"

// Global framework instance
AsyncWakeupFramework* g_WakeupFramework = nullptr;

//================================================================//
//          HARDWARETIMER IMPLEMENTATION                          //
//================================================================//

HardwareTimer::HardwareTimer(uint8_t timerId)
    : _timerId(timerId), _frequency(32768), _running(false), 
      _autoReload(true), _callback(nullptr) {
}

HardwareTimer::~HardwareTimer() {
    stop();
}

bool HardwareTimer::initialize(const WakeupTimerConfig& config) {
    _timerId = config.timerId;
    _frequency = config.frequency;
    _autoReload = config.autoReload;
    
    return configureNrfTimer(config);
}

bool HardwareTimer::configureNrfTimer(const WakeupTimerConfig& config) {
    // Platform-specific nRF52 timer configuration would go here
    return true;
}

bool HardwareTimer::start(uint32_t durationMs) {
    _running = true;
    return true;
}

bool HardwareTimer::stop() {
    _running = false;
    return true;
}

void HardwareTimer::handleInterrupt() {
    if (_callback) {
        WakeupEvent event;
        event.source = WAKEUP_TIMER;
        _callback->onWakeupEvent(event);
    }
}

void HardwareTimer::enableTimerInterrupts() {
    // Enable nRF52 timer interrupts
}

void HardwareTimer::disableTimerInterrupts() {
    // Disable nRF52 timer interrupts
}

// Static interrupt handlers
void HardwareTimer::timer0Handler() {
    if (g_WakeupFramework) {
        g_WakeupFramework->handleTimerInterrupt(0);
    }
}

void HardwareTimer::timer1Handler() {
    if (g_WakeupFramework) {
        g_WakeupFramework->handleTimerInterrupt(1);
    }
}

void HardwareTimer::timer2Handler() {
    if (g_WakeupFramework) {
        g_WakeupFramework->handleTimerInterrupt(2);
    }
}

void HardwareTimer::timer3Handler() {
    if (g_WakeupFramework) {
        g_WakeupFramework->handleTimerInterrupt(3);
    }
}

//================================================================//
//        ASYNCPOWERMANAGER IMPLEMENTATION                        //
//================================================================//

AsyncPowerManager::AsyncPowerManager()
    : _deepSleepEnabled(false), _minimumSleepTime(1000), _wakeupLatency(100) {
}

bool AsyncPowerManager::enterSleep(uint32_t durationMs) {
    if (!canEnterSleep()) {
        return false;
    }
    
    _state.sleeping = true;
    _state.sleepStartTime = millis();
    
    // Platform-specific sleep entry would go here
    
    return true;
}

bool AsyncPowerManager::exitSleep() {
    if (!_state.sleeping) {
        return false;
    }
    
    uint32_t sleepDuration = millis() - _state.sleepStartTime;
    _state.totalSleepTime += sleepDuration;
    _state.sleeping = false;
    _state.wakeupCount++;
    
    restoreNormalMode();
    
    return true;
}

uint32_t AsyncPowerManager::getEstimatedSleepTime() const {
    if (_state.sleeping) {
        return millis() - _state.sleepStartTime;
    }
    return 0;
}

bool AsyncPowerManager::canEnterSleep() const {
    return !_state.sleeping;
}

void AsyncPowerManager::configureWakeupSources(uint32_t sourceMask) {
    // Configure which sources can wake the system
}

void AsyncPowerManager::enableWakeupSource(WakeupSource_t source, bool enable) {
    // Enable/disable specific wakeup source
}

void AsyncPowerManager::updatePowerState() {
    // Update power state metrics
}

void AsyncPowerManager::configureLowPowerMode() {
    // Enter low power mode
    _state.lowPowerMode = true;
}

void AsyncPowerManager::restoreNormalMode() {
    // Exit low power mode
    _state.lowPowerMode = false;
}

//================================================================//
//          NETWORKTIMESYNC IMPLEMENTATION                        //
//================================================================//

NetworkTimeSync::NetworkTimeSync()
    : _isCoordinator(false), _baseTime(0), _lastSyncAttempt(0) {
}

void NetworkTimeSync::setCoordinator(bool isCoordinator, uint16_t coordinatorId) {
    _isCoordinator = isCoordinator;
    _config.coordinatorId = coordinatorId;
}

bool NetworkTimeSync::performSync() {
    if (_isCoordinator) {
        // Coordinator broadcasts time
        _baseTime = millis();
        _config.lastSyncTime = _baseTime;
        return true;
    } else {
        // Client requests sync
        return sendSyncRequest();
    }
}

uint32_t NetworkTimeSync::getNetworkTime() {
    if (_config.lastSyncTime > 0) {
        return millis() - _config.lastSyncTime + _config.timeOffset;
    }
    return millis();
}

bool NetworkTimeSync::isTimeSynced() const {
    if (_config.lastSyncTime == 0) return false;
    
    uint32_t timeSinceSync = millis() - _config.lastSyncTime;
    return (timeSinceSync < _config.syncInterval * 2);
}

uint32_t NetworkTimeSync::calculateGlobalWakeupTime(uint32_t localTime, uint32_t offsetMs) {
    return getNetworkTime() + offsetMs;
}

bool NetworkTimeSync::scheduleCoordinatedWakeup(uint32_t networkTime, uint16_t* nodeList, int nodeCount) {
    // Schedule coordinated wakeup for multiple nodes
    return true;
}

void NetworkTimeSync::updateClockDrift() {
    // Measure and update clock drift
}

bool NetworkTimeSync::sendSyncRequest() {
    _lastSyncAttempt = millis();
    // Send sync request packet
    return true;
}

bool NetworkTimeSync::processSyncResponse(const uint8_t* data, size_t length) {
    // Process sync response from coordinator
    return true;
}

//================================================================//
//      ASYNCWAKEUPFRAMEWORK IMPLEMENTATION                       //
//================================================================//

AsyncWakeupFramework::AsyncWakeupFramework()
    : _activeEventCount(0), _nextEventId(1), _timerCount(0), _callbackCount(0),
      _powerManager(nullptr), _networkSync(nullptr), _initialized(false),
      _enabled(false), _lastUpdate(0), _currentEvent(nullptr),
      _totalWakeups(0), _missedWakeups(0), _frameworkUptime(0) {
    
    memset(_eventSlots, 0, sizeof(_eventSlots));
    memset(_timers, 0, sizeof(_timers));
    memset(_callbacks, 0, sizeof(_callbacks));
    
    g_WakeupFramework = this;
}

AsyncWakeupFramework::~AsyncWakeupFramework() {
    end();
    
    if (g_WakeupFramework == this) {
        g_WakeupFramework = nullptr;
    }
}

bool AsyncWakeupFramework::begin() {
    if (_initialized) {
        return true;
    }
    
    // Create power manager
    _powerManager = new AsyncPowerManager();
    if (!_powerManager) {
        Serial.println("ERROR: Failed to create power manager");
        return false;
    }
    
    // Create network sync
    _networkSync = new NetworkTimeSync();
    if (!_networkSync) {
        Serial.println("ERROR: Failed to create network sync");
        delete _powerManager;
        _powerManager = nullptr;
        return false;
    }
    
    // Initialize hardware
    if (!initializeHardware()) {
        Serial.println("ERROR: Failed to initialize hardware");
        delete _networkSync;
        delete _powerManager;
        _networkSync = nullptr;
        _powerManager = nullptr;
        return false;
    }
    
    _initialized = true;
    _enabled = true;
    _lastUpdate = millis();
    _frameworkUptime = 0;
    
    Serial.println("AsyncWakeupFramework initialized successfully");
    return true;
}

void AsyncWakeupFramework::end() {
    if (!_initialized) return;
    
    _enabled = false;
    
    // Cleanup timers
    for (int i = 0; i < _timerCount; i++) {
        if (_timers[i]) {
            delete _timers[i];
            _timers[i] = nullptr;
        }
    }
    _timerCount = 0;
    
    // Cleanup managers
    if (_networkSync) {
        delete _networkSync;
        _networkSync = nullptr;
    }
    
    if (_powerManager) {
        delete _powerManager;
        _powerManager = nullptr;
    }
    
    _initialized = false;
}

bool AsyncWakeupFramework::initializeHardware() {
    // Initialize hardware timers
    return initializeTimers();
}

bool AsyncWakeupFramework::initializeTimers() {
    // Initialize available hardware timers
    for (int i = 0; i < MAX_TIMERS; i++) {
        _timers[i] = new HardwareTimer(i);
        if (_timers[i]) {
            _timerCount++;
        }
    }
    
    setupInterrupts();
    return true;
}

void AsyncWakeupFramework::setupInterrupts() {
    // Setup interrupt handlers for timers
}

void AsyncWakeupFramework::run() {
    if (!_initialized || !_enabled) return;
    
    uint32_t now = millis();
    _frameworkUptime = now;
    
    processEvents();
    
    _lastUpdate = now;
}

void AsyncWakeupFramework::processEvents() {
    for (int i = 0; i < MAX_WAKEUP_EVENTS; i++) {
        if (_eventSlots[i] && _events[i].enabled) {
            if (isEventReady(_events[i])) {
                processEvent(_events[i]);
            }
        }
    }
    
    cleanupExpiredEvents();
}

void AsyncWakeupFramework::processEvent(WakeupEvent& event) {
    _currentEvent = &event;
    
    notifyWakeupPrepare(event);
    executeEvent(event);
    completeEvent(event);
    
    _currentEvent = nullptr;
}

bool AsyncWakeupFramework::isEventReady(const WakeupEvent& event) const {
    if (event.scheduledTime == 0) {
        return true;  // Immediate event
    }
    
    return (millis() >= event.scheduledTime);
}

void AsyncWakeupFramework::executeEvent(const WakeupEvent& event) {
    _totalWakeups++;
    
    // Notify callbacks
    notifyCallbacks(event);
    
    // Handle recurring events
    if (event.recurring && event.recurInterval > 0) {
        WakeupEvent* evt = const_cast<WakeupEvent*>(&event);
        evt->scheduledTime = millis() + evt->recurInterval;
    }
}

void AsyncWakeupFramework::completeEvent(WakeupEvent& event) {
    if (!event.recurring) {
        // Remove one-time event
        for (int i = 0; i < MAX_WAKEUP_EVENTS; i++) {
            if (_eventSlots[i] && _events[i].eventId == event.eventId) {
                _eventSlots[i] = false;
                _activeEventCount--;
                break;
            }
        }
    }
    
    notifyWakeupComplete(event);
}

uint16_t AsyncWakeupFramework::scheduleWakeup(const WakeupEvent& event) {
    int slot = findFreeEventSlot();
    if (slot < 0) {
        _missedWakeups++;
        return 0;
    }
    
    _events[slot] = event;
    _events[slot].eventId = _nextEventId++;
    _eventSlots[slot] = true;
    _activeEventCount++;
    
    return _events[slot].eventId;
}

bool AsyncWakeupFramework::cancelWakeup(uint16_t eventId) {
    WakeupEvent* event = findEventById(eventId);
    if (!event) return false;
    
    event->enabled = false;
    return true;
}

bool AsyncWakeupFramework::modifyWakeup(uint16_t eventId, const WakeupEvent& newEvent) {
    WakeupEvent* event = findEventById(eventId);
    if (!event) return false;
    
    uint16_t oldId = event->eventId;
    *event = newEvent;
    event->eventId = oldId;  // Preserve ID
    
    return true;
}

WakeupEvent* AsyncWakeupFramework::getWakeupEvent(uint16_t eventId) {
    return findEventById(eventId);
}

uint16_t AsyncWakeupFramework::scheduleTimerWakeup(uint32_t delayMs, uint32_t durationMs, bool recurring) {
    WakeupEvent event;
    event.source = WAKEUP_TIMER;
    event.priority = PRIORITY_NORMAL;
    event.scheduledTime = millis() + delayMs;
    event.duration = durationMs;
    event.recurring = recurring;
    event.recurInterval = recurring ? delayMs : 0;
    
    return scheduleWakeup(event);
}

uint16_t AsyncWakeupFramework::scheduleNetworkWakeup(uint32_t networkTime, uint32_t durationMs) {
    WakeupEvent event;
    event.source = WAKEUP_NETWORK;
    event.priority = PRIORITY_HIGH;
    event.scheduledTime = networkTime;
    event.duration = durationMs;
    
    return scheduleWakeup(event);
}

uint16_t AsyncWakeupFramework::scheduleGpioWakeup(uint8_t pin, bool edge, uint32_t durationMs) {
    WakeupEvent event;
    event.source = WAKEUP_GPIO;
    event.priority = PRIORITY_NORMAL;
    event.duration = durationMs;
    
    return scheduleWakeup(event);
}

uint16_t AsyncWakeupFramework::scheduleRtcWakeup(uint32_t timestamp, uint32_t durationMs) {
    WakeupEvent event;
    event.source = WAKEUP_RTC;
    event.priority = PRIORITY_NORMAL;
    event.scheduledTime = timestamp;
    event.duration = durationMs;
    
    return scheduleWakeup(event);
}

bool AsyncWakeupFramework::registerCallback(WakeupCallback* callback) {
    if (_callbackCount >= MAX_CALLBACKS) return false;
    
    _callbacks[_callbackCount++] = callback;
    return true;
}

bool AsyncWakeupFramework::unregisterCallback(WakeupCallback* callback) {
    for (int i = 0; i < _callbackCount; i++) {
        if (_callbacks[i] == callback) {
            // Shift remaining callbacks
            for (int j = i; j < _callbackCount - 1; j++) {
                _callbacks[j] = _callbacks[j + 1];
            }
            _callbackCount--;
            return true;
        }
    }
    return false;
}

void AsyncWakeupFramework::clearCallbacks() {
    _callbackCount = 0;
}

HardwareTimer* AsyncWakeupFramework::allocateTimer() {
    return findAvailableTimer();
}

bool AsyncWakeupFramework::releaseTimer(HardwareTimer* timer) {
    // Timer deallocation
    return true;
}

int AsyncWakeupFramework::getAvailableTimerCount() const {
    return _timerCount;
}

bool AsyncWakeupFramework::enableNetworkSync(bool enable, uint16_t coordinatorId) {
    if (!_networkSync) return false;
    
    _networkSync->enableSync(enable);
    if (enable) {
        _networkSync->setCoordinator(false, coordinatorId);
    }
    
    return true;
}

bool AsyncWakeupFramework::isNetworkSynced() const {
    return _networkSync ? _networkSync->isTimeSynced() : false;
}

uint32_t AsyncWakeupFramework::getNetworkTime() const {
    return _networkSync ? _networkSync->getNetworkTime() : millis();
}

void AsyncWakeupFramework::triggerWakeup(WakeupSource_t source, WakeupPriority_t priority) {
    WakeupEvent event;
    event.source = source;
    event.priority = priority;
    event.scheduledTime = 0;  // Immediate
    
    scheduleWakeup(event);
}

void AsyncWakeupFramework::requestSleep(uint32_t durationMs) {
    if (_powerManager) {
        _powerManager->enterSleep(durationMs);
    }
}

void AsyncWakeupFramework::printStatus() {
    Serial.println("\n=== AsyncWakeupFramework Status ===");
    Serial.println("Initialized: " + String(_initialized ? "Yes" : "No"));
    Serial.println("Enabled: " + String(_enabled ? "Yes" : "No"));
    Serial.println("Active events: " + String(_activeEventCount));
    Serial.println("Total wakeups: " + String(_totalWakeups));
    Serial.println("Missed wakeups: " + String(_missedWakeups));
    Serial.println("Reliability: " + String(getWakeupReliability(), 2) + "%");
    Serial.println("Uptime: " + String(_frameworkUptime / 1000) + " seconds");
}

void AsyncWakeupFramework::printEventList() {
    Serial.println("\n=== Scheduled Events ===");
    for (int i = 0; i < MAX_WAKEUP_EVENTS; i++) {
        if (_eventSlots[i]) {
            Serial.print("Event ");
            Serial.print(_events[i].eventId);
            Serial.print(": ");
            Serial.print(wakeupSourceToString(_events[i].source));
            Serial.print(" Priority: ");
            Serial.println(wakeupPriorityToString(_events[i].priority));
        }
    }
}

float AsyncWakeupFramework::getWakeupReliability() const {
    if (_totalWakeups == 0) return 100.0f;
    return 100.0f * (1.0f - (float)_missedWakeups / _totalWakeups);
}

void AsyncWakeupFramework::handleInterrupt() {
    // Generic interrupt handler
}

void AsyncWakeupFramework::handleTimerInterrupt(uint8_t timerId) {
    if (timerId < _timerCount && _timers[timerId]) {
        _timers[timerId]->handleInterrupt();
    }
}

void AsyncWakeupFramework::handleGpioInterrupt(uint8_t pin) {
    // GPIO interrupt handling
}

int AsyncWakeupFramework::findFreeEventSlot() {
    for (int i = 0; i < MAX_WAKEUP_EVENTS; i++) {
        if (!_eventSlots[i]) {
            return i;
        }
    }
    return -1;
}

WakeupEvent* AsyncWakeupFramework::findEventById(uint16_t eventId) {
    for (int i = 0; i < MAX_WAKEUP_EVENTS; i++) {
        if (_eventSlots[i] && _events[i].eventId == eventId) {
            return &_events[i];
        }
    }
    return nullptr;
}

void AsyncWakeupFramework::cleanupExpiredEvents() {
    // Remove expired non-recurring events
    for (int i = 0; i < MAX_WAKEUP_EVENTS; i++) {
        if (_eventSlots[i] && !_events[i].enabled) {
            _eventSlots[i] = false;
            _activeEventCount--;
        }
    }
}

HardwareTimer* AsyncWakeupFramework::findAvailableTimer() {
    for (int i = 0; i < _timerCount; i++) {
        if (_timers[i] && !_timers[i]->isRunning()) {
            return _timers[i];
        }
    }
    return nullptr;
}

void AsyncWakeupFramework::configureTimerForEvent(HardwareTimer* timer, const WakeupEvent& event) {
    if (!timer) return;
    
    WakeupTimerConfig config;
    config.timerId = 0;
    config.frequency = 32768;
    config.autoReload = event.recurring;
    
    timer->initialize(config);
}

void AsyncWakeupFramework::notifyCallbacks(const WakeupEvent& event) {
    for (int i = 0; i < _callbackCount; i++) {
        if (_callbacks[i]) {
            _callbacks[i]->onWakeupEvent(event);
        }
    }
}

void AsyncWakeupFramework::notifyWakeupPrepare(const WakeupEvent& event) {
    for (int i = 0; i < _callbackCount; i++) {
        if (_callbacks[i]) {
            _callbacks[i]->onWakeupPrepare(event);
        }
    }
}

void AsyncWakeupFramework::notifyWakeupComplete(const WakeupEvent& event) {
    for (int i = 0; i < _callbackCount; i++) {
        if (_callbacks[i]) {
            _callbacks[i]->onWakeupComplete(event);
        }
    }
}

void AsyncWakeupFramework::notifyWakeupFailed(const WakeupEvent& event, int errorCode) {
    for (int i = 0; i < _callbackCount; i++) {
        if (_callbacks[i]) {
            _callbacks[i]->onWakeupFailed(event, errorCode);
        }
    }
}

void AsyncWakeupFramework::handlePowerStateChange() {
    // Handle power state transitions
}

bool AsyncWakeupFramework::shouldPreventSleep() const {
    return _activeEventCount > 0;
}

void AsyncWakeupFramework::handleFrameworkError(int errorCode, const String& description) {
    Serial.print("Framework Error ");
    Serial.print(errorCode);
    Serial.print(": ");
    Serial.println(description);
}

void AsyncWakeupFramework::logEvent(const WakeupEvent& event, const String& message) {
    Serial.print("[");
    Serial.print(wakeupSourceToString(event.source));
    Serial.print("] ");
    Serial.println(message);
}

//================================================================//
//          EXTENSIONREGISTRY IMPLEMENTATION                      //
//================================================================//

bool ExtensionRegistry::registerExtension(WakeupExtension* extension) {
    if (_extensionCount >= MAX_EXTENSIONS) return false;
    
    _extensions[_extensionCount++] = extension;
    return true;
}

bool ExtensionRegistry::unregisterExtension(WakeupExtension* extension) {
    for (int i = 0; i < _extensionCount; i++) {
        if (_extensions[i] == extension) {
            for (int j = i; j < _extensionCount - 1; j++) {
                _extensions[j] = _extensions[j + 1];
            }
            _extensionCount--;
            return true;
        }
    }
    return false;
}

void ExtensionRegistry::updateAllExtensions() {
    for (int i = 0; i < _extensionCount; i++) {
        if (_extensions[i]) {
            _extensions[i]->update();
        }
    }
}

void ExtensionRegistry::notifyExtensions(const WakeupEvent& event) {
    for (int i = 0; i < _extensionCount; i++) {
        if (_extensions[i]) {
            _extensions[i]->handleWakeupEvent(event);
        }
    }
}
