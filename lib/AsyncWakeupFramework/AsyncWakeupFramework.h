/**
 * @file AsyncWakeupFramework.h
 * @brief Framework for asynchronous wakeup and event management
 * @version 2.1 - FIXED
 * @date 2025-10-01
 * @details
 * FIXED VERSION - Resolved TimerConfig naming conflict and PowerManager inconsistency
 */

#ifndef ASYNC_WAKEUP_FRAMEWORK_H
#define ASYNC_WAKEUP_FRAMEWORK_H

#include <Arduino.h>

// Forward declarations to ensure proper type visibility
struct WakeupTimerConfig;
class AsyncPowerManager;

//====//
//    WAKEUP EVENT DEFINITIONS    //
//====//

/**
 * @enum WakeupSource_t
 * @brief Sources that can trigger asynchronous wakeups
 */
enum WakeupSource_t {
    WAKEUP_TIMER = 0,    // Scheduled timer-based wakeup
    WAKEUP_NETWORK,    // Network message or beacon
    WAKEUP_GPIO,    // External GPIO interrupt
    WAKEUP_RTC,    // Real-time clock alarm
    WAKEUP_WATCHDOG,    // Watchdog timer
    WAKEUP_BATTERY,    // Battery level threshold
    WAKEUP_TEMPERATURE,    // Temperature threshold
    WAKEUP_ACCELEROMETER,    // Motion detection
    WAKEUP_USER_REQUEST,    // User-initiated wakeup
    WAKEUP_MESH_SYNC,    // Mesh network synchronization
    WAKEUP_EMERGENCY,    // Emergency override
    NUM_WAKEUP_SOURCES
};

/**
 * @enum WakeupPriority_t
 * @brief Priority levels for wakeup events
 */
enum WakeupPriority_t {
    PRIORITY_LOW = 0,
    PRIORITY_NORMAL = 1,
    PRIORITY_HIGH = 2,
    PRIORITY_CRITICAL = 3,
    PRIORITY_EMERGENCY = 4
};

/**
 * @struct WakeupEvent
 * @brief Structure describing a wakeup event
 */
struct WakeupEvent {
    WakeupSource_t source;
    WakeupPriority_t priority;
    uint32_t scheduledTime;    // When the wakeup should occur (0 = immediate)
    uint32_t duration;    // How long to stay awake (0 = until manually slept)
    uint16_t eventId;    // Unique identifier for this event
    void* eventData;    // Optional data associated with the event
    bool recurring;    // Whether this is a recurring event
    uint32_t recurInterval;    // Interval for recurring events (ms)
    bool enabled;    // Whether this event is currently active
    
    WakeupEvent() : source(WAKEUP_TIMER), priority(PRIORITY_NORMAL), 
    scheduledTime(0), duration(0), eventId(0), eventData(nullptr),
    recurring(false), recurInterval(0), enabled(true) {}
};

/**
 * @class WakeupCallback
 * @brief Abstract base class for wakeup event callbacks
 */
class WakeupCallback {
public:
    virtual ~WakeupCallback() {}
    virtual void onWakeupEvent(const WakeupEvent& event) = 0;
    virtual void onWakeupPrepare(const WakeupEvent& event) {}
    virtual void onWakeupComplete(const WakeupEvent& event) {}
    virtual void onWakeupFailed(const WakeupEvent& event, int errorCode) {}
};

//====//
//    TIMER MANAGEMENT    //
//====//

/**
 * @struct WakeupTimerConfig
 * @brief Configuration for hardware timers used in wakeup events
 * @note Renamed from TimerConfig to avoid conflicts with nRF52 SDK
 */
struct WakeupTimerConfig {
    uint8_t timerId;    // Hardware timer ID (0-3 for nRF52)
    uint32_t frequency;    // Timer frequency in Hz
    bool autoReload;    // Whether timer auto-reloads
    bool enableInterrupts;    // Whether to enable interrupts
    WakeupPriority_t priority;  // Interrupt priority level
    
    WakeupTimerConfig() : timerId(0), frequency(32768), autoReload(true), 
    enableInterrupts(true), priority(PRIORITY_NORMAL) {}
};

// Type alias for backward compatibility (if needed)
typedef WakeupTimerConfig TimerConfig_t;

/**
 * @class HardwareTimer
 * @brief Hardware timer management for precise wakeup timing
 */
class HardwareTimer {
private:
    uint8_t _timerId;
    uint32_t _frequency;
    bool _running;
    bool _autoReload;
    WakeupCallback* _callback;
    
public:
    HardwareTimer(uint8_t timerId);
    ~HardwareTimer();
    
    bool initialize(const WakeupTimerConfig& config);
    bool start(uint32_t durationMs);
    bool stop();
    bool isRunning() const { return _running; }
    
    void setCallback(WakeupCallback* callback) { _callback = callback; }
    void handleInterrupt();
    
    // Static interrupt handlers (for global ISR routing)
    static void timer0Handler();
    static void timer1Handler();
    static void timer2Handler();
    static void timer3Handler();
    
private:
    bool configureNrfTimer(const WakeupTimerConfig& config);
    void enableTimerInterrupts();
    void disableTimerInterrupts();
};

//====//
//    POWER MANAGEMENT INTEGRATION    //
//====//

/**
 * @struct PowerState
 * @brief Current system power state information
 */
struct PowerState {
    bool sleeping;
    uint32_t sleepStartTime;
    uint32_t totalSleepTime;
    uint32_t wakeupCount;
    WakeupSource_t lastWakeupSource;
    uint32_t batteryLevel;    // Battery percentage (0-100)
    int16_t temperature;    // Temperature in 0.1°C
    bool lowPowerMode;
    
    PowerState() : sleeping(false), sleepStartTime(0), totalSleepTime(0),
    wakeupCount(0), lastWakeupSource(WAKEUP_TIMER), 
    batteryLevel(100), temperature(200), lowPowerMode(false) {}
};

/**
 * @class AsyncPowerManager
 * @brief Manages system power states and wakeup coordination for async framework
 */
class AsyncPowerManager {
private:
    PowerState _state;
    bool _deepSleepEnabled;
    uint32_t _minimumSleepTime;
    uint32_t _wakeupLatency;
    
public:
    AsyncPowerManager();
    
    // Power state management
    bool enterSleep(uint32_t durationMs = 0);
    bool exitSleep();
    bool isAsleep() const { return _state.sleeping; }
    
    // Configuration
    void enableDeepSleep(bool enable) { _deepSleepEnabled = enable; }
    void setMinimumSleepTime(uint32_t timeMs) { _minimumSleepTime = timeMs; }
    void setWakeupLatency(uint32_t latencyMs) { _wakeupLatency = latencyMs; }
    
    // State information
    PowerState getCurrentState() const { return _state; }
    uint32_t getEstimatedSleepTime() const;
    bool canEnterSleep() const;
    
    // Hardware integration
    void configureWakeupSources(uint32_t sourceMask);
    void enableWakeupSource(WakeupSource_t source, bool enable);
    
private:
    void updatePowerState();
    void configureLowPowerMode();
    void restoreNormalMode();
};

//====//
//    NETWORK SYNCHRONIZATION    //
//====//

/**
 * @struct NetworkSync
 * @brief Network synchronization parameters for coordinated wakeups
 */
struct NetworkSync {
    bool enabled;
    uint32_t syncInterval;    // Network sync interval (ms)
    uint32_t timeOffset;    // Local time offset from network time
    uint16_t coordinatorId;    // Node ID of time coordinator
    uint32_t lastSyncTime;    // Last successful sync timestamp
    int32_t clockDrift;    // Measured clock drift (us/hour)
    
    NetworkSync() : enabled(false), syncInterval(30000), timeOffset(0),
    coordinatorId(0), lastSyncTime(0), clockDrift(0) {}
};

/**
 * @class NetworkTimeSync
 * @brief Handles network time synchronization for coordinated mesh wakeups
 */
class NetworkTimeSync {
private:
    NetworkSync _config;
    bool _isCoordinator;
    uint32_t _baseTime;
    uint32_t _lastSyncAttempt;
    
public:
    NetworkTimeSync();
    
    // Configuration
    void enableSync(bool enable) { _config.enabled = enable; }
    void setCoordinator(bool isCoordinator, uint16_t coordinatorId = 0);
    void setSyncInterval(uint32_t intervalMs) { _config.syncInterval = intervalMs; }
    
    // Time synchronization
    bool performSync();
    uint32_t getNetworkTime();
    bool isTimeSynced() const;
    int32_t getTimeDrift() const { return _config.clockDrift; }
    
    // Coordinated scheduling
    uint32_t calculateGlobalWakeupTime(uint32_t localTime, uint32_t offsetMs);
    bool scheduleCoordinatedWakeup(uint32_t networkTime, uint16_t* nodeList, int nodeCount);
    
private:
    void updateClockDrift();
    bool sendSyncRequest();
    bool processSyncResponse(const uint8_t* data, size_t length);
};

//====//
//    ASYNC WAKEUP FRAMEWORK CLASS    //
//====//

/**
 * @class AsyncWakeupFramework
 * @brief Main framework class for asynchronous wakeup management
 */
class AsyncWakeupFramework {
private:
    // Event management
    static const int MAX_WAKEUP_EVENTS = 32;
    WakeupEvent _events[MAX_WAKEUP_EVENTS];
    bool _eventSlots[MAX_WAKEUP_EVENTS];
    int _activeEventCount;
    uint16_t _nextEventId;
    
    // Hardware timers
    static const int MAX_TIMERS = 4;
    HardwareTimer* _timers[MAX_TIMERS];
    int _timerCount;
    
    // Callbacks
    static const int MAX_CALLBACKS = 8;
    WakeupCallback* _callbacks[MAX_CALLBACKS];
    int _callbackCount;
    
    // System integration - FIXED: Changed PowerManager* to AsyncPowerManager*
    AsyncPowerManager* _powerManager;
    NetworkTimeSync* _networkSync;
    
    // State tracking
    bool _initialized;
    bool _enabled;
    uint32_t _lastUpdate;
    WakeupEvent* _currentEvent;
    
    // Statistics
    uint32_t _totalWakeups;
    uint32_t _missedWakeups;
    uint32_t _frameworkUptime;

public:
    AsyncWakeupFramework();
    ~AsyncWakeupFramework();
    
    // Initialization
    bool begin();
    void end();
    bool isInitialized() const { return _initialized; }
    
    // Framework control
    void enable(bool enabled) { _enabled = enabled; }
    bool isEnabled() const { return _enabled; }
    void run();  // Main execution loop
    
    // Event management
    uint16_t scheduleWakeup(const WakeupEvent& event);
    bool cancelWakeup(uint16_t eventId);
    bool modifyWakeup(uint16_t eventId, const WakeupEvent& newEvent);
    WakeupEvent* getWakeupEvent(uint16_t eventId);
    int getActiveEventCount() const { return _activeEventCount; }
    
    // Convenience scheduling methods
    uint16_t scheduleTimerWakeup(uint32_t delayMs, uint32_t durationMs = 0, bool recurring = false);
    uint16_t scheduleNetworkWakeup(uint32_t networkTime, uint32_t durationMs = 0);
    uint16_t scheduleGpioWakeup(uint8_t pin, bool edge, uint32_t durationMs = 0);
    uint16_t scheduleRtcWakeup(uint32_t timestamp, uint32_t durationMs = 0);
    
    // Callback management
    bool registerCallback(WakeupCallback* callback);
    bool unregisterCallback(WakeupCallback* callback);
    void clearCallbacks();
    
    // Timer management
    HardwareTimer* allocateTimer();
    bool releaseTimer(HardwareTimer* timer);
    int getAvailableTimerCount() const;
    
    // System integration - FIXED: Changed return type from PowerManager* to AsyncPowerManager*
    AsyncPowerManager* getPowerManager() { return _powerManager; }
    NetworkTimeSync* getNetworkSync() { return _networkSync; }
    
    // Network synchronization
    bool enableNetworkSync(bool enable, uint16_t coordinatorId = 0);
    bool isNetworkSynced() const;
    uint32_t getNetworkTime() const;
    
    // Immediate actions
    void triggerWakeup(WakeupSource_t source, WakeupPriority_t priority = PRIORITY_NORMAL);
    void requestSleep(uint32_t durationMs = 0);
    
    // Diagnostics and statistics
    void printStatus();
    void printEventList();
    uint32_t getTotalWakeups() const { return _totalWakeups; }
    uint32_t getMissedWakeups() const { return _missedWakeups; }
    float getWakeupReliability() const;
    
    // Interrupt handling (public for ISR access)
    void handleInterrupt();
    void handleTimerInterrupt(uint8_t timerId);
    void handleGpioInterrupt(uint8_t pin);
    
private:
    // Initialization helpers
    bool initializeHardware();
    bool initializeTimers();
    void setupInterrupts();
    
    // Event processing
    void processEvents();
    void processEvent(WakeupEvent& event);
    bool isEventReady(const WakeupEvent& event) const;
    void executeEvent(const WakeupEvent& event);
    void completeEvent(WakeupEvent& event);
    
    // Event storage management
    int findFreeEventSlot();
    WakeupEvent* findEventById(uint16_t eventId);
    void cleanupExpiredEvents();
    
    // Timer allocation
    HardwareTimer* findAvailableTimer();
    void configureTimerForEvent(HardwareTimer* timer, const WakeupEvent& event);
    
    // Callback management
    void notifyCallbacks(const WakeupEvent& event);
    void notifyWakeupPrepare(const WakeupEvent& event);
    void notifyWakeupComplete(const WakeupEvent& event);
    void notifyWakeupFailed(const WakeupEvent& event, int errorCode);
    
    // Power management integration
    void handlePowerStateChange();
    bool shouldPreventSleep() const;
    
    // Error handling
    void handleFrameworkError(int errorCode, const String& description);
    void logEvent(const WakeupEvent& event, const String& message);
};

//====//
//    UTILITY FUNCTIONS    //
//====//

/**
 * @brief Convert wakeup source to human-readable string
 */
inline const char* wakeupSourceToString(WakeupSource_t source) {
    static const char* sourceNames[] = {
    "TIMER", "NETWORK", "GPIO", "RTC", "WATCHDOG",
    "BATTERY", "TEMPERATURE", "ACCELEROMETER", 
    "USER_REQUEST", "MESH_SYNC", "EMERGENCY"
    };
    return (source < NUM_WAKEUP_SOURCES) ? sourceNames[source] : "UNKNOWN";
}

/**
 * @brief Convert wakeup priority to human-readable string
 */
inline const char* wakeupPriorityToString(WakeupPriority_t priority) {
    static const char* priorityNames[] = {
    "LOW", "NORMAL", "HIGH", "CRITICAL", "EMERGENCY"
    };
    return (priority <= PRIORITY_EMERGENCY) ? priorityNames[priority] : "UNKNOWN";
}

//====//
//    FUTURE EXPANSION HOOKS    //
//====//

/**
 * @class WakeupExtension
 * @brief Abstract base class for framework extensions
 */
class WakeupExtension {
public:
    virtual ~WakeupExtension() {}
    virtual bool initialize(AsyncWakeupFramework* framework) = 0;
    virtual void update() = 0;
    virtual bool handleWakeupEvent(const WakeupEvent& event) { return false; }
    virtual const char* getName() const = 0;
};

/**
 * @class ExtensionRegistry
 * @brief Registry for framework extensions
 */
class ExtensionRegistry {
private:
    static const int MAX_EXTENSIONS = 8;
    WakeupExtension* _extensions[MAX_EXTENSIONS];
    int _extensionCount;
    
public:
    ExtensionRegistry() : _extensionCount(0) {}
    bool registerExtension(WakeupExtension* extension);
    bool unregisterExtension(WakeupExtension* extension);
    void updateAllExtensions();
    void notifyExtensions(const WakeupEvent& event);
};

//====//
//    GLOBAL INSTANCE DECLARATION    //
//====//

// Global framework instance (defined in implementation file)
extern AsyncWakeupFramework* g_WakeupFramework;

#endif // ASYNC_WAKEUP_FRAMEWORK_H
