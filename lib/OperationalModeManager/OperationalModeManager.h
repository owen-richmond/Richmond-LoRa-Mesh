/**
 * @file OperationalModeManager.h
 * @brief Manages operational modes for the mesh network node
 * @version 2.0
 * @date 2025-09-27
 * @details
 * ENHANCED OPERATIONAL MODE MANAGER with:
 * - Three distinct operational modes
 * - Smooth mode transitions
 * - Mode-specific behavior management
 * - Power optimization per mode
 * - Extensible framework for future modes
 */

#ifndef OPERATIONAL_MODE_MANAGER_H
#define OPERATIONAL_MODE_MANAGER_H

#include <Arduino.h>

//================================================================//
//                  OPERATIONAL MODE DEFINITIONS                  //
//================================================================//

/**
 * @enum OperationalMode_t
 * @brief Defines the operational modes for the mesh node
 */
enum OperationalMode_t {
    INTERACTIVE_MODE = 0,   // Mode 1: User-controlled message sending via serial
    TESTING_MODE = 1,       // Mode 2: Always-on testing with automatic forwarding
    PRODUCTION_MODE = 2,    // Mode 3: Power-optimized sleepy node
    NUM_OPERATIONAL_MODES   // Total number of modes (for validation)
};

/**
 * @struct ModeConfig
 * @brief Configuration parameters for each operational mode
 */
struct ModeConfig {
    // General settings
    bool serialEnabled;
    bool debugOutput;
    bool autoTesting;
    
    // Power management
    bool sleepEnabled;
    uint32_t sleepCycleMs;
    uint32_t wakeWindowMs;
    uint8_t cpuFrequency;  // 0=1MHz, 1=2MHz, 2=8MHz, 3=16MHz, 4=32MHz, 5=64MHz
    
    // Networking behavior
    bool autoForwarding;
    bool beaconEnabled;
    uint32_t beaconIntervalMs;
    bool ackRequired;
    uint8_t maxRetries;
    
    // Message behavior
    bool periodicMessages;
    uint32_t messageIntervalMs;
    bool heartbeatEnabled;
    uint32_t heartbeatIntervalMs;
    
    // LED behavior
    bool ledEnabled;
    uint32_t ledBlinkPattern;  // Bit pattern for LED blinking
    uint16_t ledBlinkDuration; // Duration of each blink in ms
    
    ModeConfig() : 
        serialEnabled(true), debugOutput(true), autoTesting(false),
        sleepEnabled(false), sleepCycleMs(3600000), wakeWindowMs(120000), cpuFrequency(5),
        autoForwarding(true), beaconEnabled(true), beaconIntervalMs(300000), 
        ackRequired(false), maxRetries(3),
        periodicMessages(false), messageIntervalMs(60000),
        heartbeatEnabled(true), heartbeatIntervalMs(2000),
        ledEnabled(true), ledBlinkPattern(0x1), ledBlinkDuration(50) {}
};

/**
 * @class ModeStatistics
 * @brief Statistics tracking for each operational mode
 */
class ModeStatistics {
public:
    uint32_t modeStartTime;
    uint32_t totalTimeInMode;
    uint32_t modeEntryCount;
    uint32_t messagesProcessed;
    uint32_t commandsExecuted;
    uint32_t sleepCycles;
    uint32_t wakeEvents;
    
    ModeStatistics() : modeStartTime(0), totalTimeInMode(0), modeEntryCount(0),
                      messagesProcessed(0), commandsExecuted(0), sleepCycles(0), wakeEvents(0) {}
    
    void reset() {
        modeStartTime = millis();
        totalTimeInMode = 0;
        modeEntryCount++;
        messagesProcessed = 0;
        commandsExecuted = 0;
        sleepCycles = 0;
        wakeEvents = 0;
    }
    
    void update() {
        if (modeStartTime > 0) {
            totalTimeInMode = millis() - modeStartTime;
        }
    }
};

//================================================================//
//                  MODE TRANSITION CALLBACKS                     //
//================================================================//

/**
 * @class ModeTransitionCallbacks
 * @brief Callback interface for mode transition events
 */
class ModeTransitionCallbacks {
public:
    virtual ~ModeTransitionCallbacks() {}
    virtual void onModeEnter(OperationalMode_t newMode) {}
    virtual void onModeExit(OperationalMode_t oldMode) {}
    virtual void onModeTransition(OperationalMode_t from, OperationalMode_t to) {}
    virtual void onModeConfigChange(OperationalMode_t mode, const ModeConfig& config) {}
};

//================================================================//
//                OPERATIONAL MODE MANAGER CLASS                  //
//================================================================//

/**
 * @class OperationalModeManager
 * @brief Manages operational modes and their behaviors
 */
class OperationalModeManager {
private:
    OperationalMode_t _currentMode;
    OperationalMode_t _previousMode;
    ModeConfig _modeConfigs[NUM_OPERATIONAL_MODES];
    ModeStatistics _modeStats[NUM_OPERATIONAL_MODES];
    
    // Mode transition management
    bool _transitionInProgress;
    uint32_t _transitionStartTime;
    uint32_t _transitionTimeout;
    
    // Periodic tasks
    uint32_t _lastHeartbeat;
    uint32_t _lastPeriodicMessage;
    uint32_t _lastBeacon;
    uint32_t _lastStatUpdate;
    
    // Power management
    bool _sleepPending;
    uint32_t _sleepStartTime;
    uint32_t _wakeupTime;
    
    // Callbacks
    ModeTransitionCallbacks* _callbacks;
    
    // LED management
    uint32_t _lastLedUpdate;
    uint8_t _ledPatternIndex;
    bool _ledState;

public:
    OperationalModeManager(OperationalMode_t initialMode = INTERACTIVE_MODE);
    ~OperationalModeManager();
    
    // Mode management
    bool setMode(OperationalMode_t newMode);
    OperationalMode_t getCurrentMode() const { return _currentMode; }
    OperationalMode_t getPreviousMode() const { return _previousMode; }
    bool isTransitionInProgress() const { return _transitionInProgress; }
    
    // Configuration management
    void setModeConfig(OperationalMode_t mode, const ModeConfig& config);
    ModeConfig getModeConfig(OperationalMode_t mode) const;
    ModeConfig getCurrentModeConfig() const { return _modeConfigs[_currentMode]; }
    
    // Statistics
    ModeStatistics getModeStatistics(OperationalMode_t mode) const;
    ModeStatistics getCurrentModeStatistics() const { return _modeStats[_currentMode]; }
    void resetStatistics(OperationalMode_t mode);
    void resetAllStatistics();
    
    // Callback management
    void setTransitionCallbacks(ModeTransitionCallbacks* callbacks);
    
    // Main execution
    void run();
    
    // Mode-specific event handlers
    void onModeChange(OperationalMode_t newMode);
    void onModeExit(OperationalMode_t oldMode);
    void onMessageReceived();
    void onCommandExecuted();
    void onSleepRequest();
    void onWakeupRequest();
    
    // Utility functions
    String getModeString(OperationalMode_t mode) const;
    String getCurrentModeString() const { return getModeString(_currentMode); }
    bool isSleepMode() const;
    bool canEnterSleep() const;
    uint32_t getTimeInCurrentMode() const;
    
    // Power management
    void requestSleep();
    void requestWakeup();
    void handlePowerManagement();
    
private:
    // Initialization
    void initializeDefaultConfigs();
    void initializeInteractiveConfig();
    void initializeTestingConfig();
    void initializeProductionConfig();
    
    // Mode transition handling
    bool validateModeTransition(OperationalMode_t from, OperationalMode_t to);
    void performModeTransition(OperationalMode_t newMode);
    void finalizeModeTransition();
    
    // Periodic task handlers
    void handleHeartbeat();
    void handlePeriodicMessages();
    void handleBeacons();
    void handleStatisticsUpdate();
    void handleLedPattern();
    
    // Power management helpers
    void enterSleepMode();
    void exitSleepMode();
    bool shouldWakeUp();
    void adjustCpuFrequency(uint8_t frequency);
    
    // LED management
    void updateLedPattern();
    void setLedPattern(uint32_t pattern, uint16_t duration);
    
    // Validation helpers
    bool isValidMode(OperationalMode_t mode) const;
    void logModeTransition(OperationalMode_t from, OperationalMode_t to);
};

//================================================================//
//                  MODE-SPECIFIC CONFIGURATIONS                  //
//================================================================//

/**
 * @class InteractiveModeHandler
 * @brief Specialized handler for interactive mode behaviors
 */
class InteractiveModeHandler {
public:
    static void configure(ModeConfig& config);
    static void onEnter(OperationalModeManager* manager);
    static void onExit(OperationalModeManager* manager);
    static void run(OperationalModeManager* manager);
};

/**
 * @class TestingModeHandler
 * @brief Specialized handler for testing mode behaviors
 */
class TestingModeHandler {
public:
    static void configure(ModeConfig& config);
    static void onEnter(OperationalModeManager* manager);
    static void onExit(OperationalModeManager* manager);
    static void run(OperationalModeManager* manager);
    
private:
    static uint32_t _lastAutoTest;
    static uint16_t _testCounter;
};

/**
 * @class ProductionModeHandler
 * @brief Specialized handler for production mode behaviors
 */
class ProductionModeHandler {
public:
    static void configure(ModeConfig& config);
    static void onEnter(OperationalModeManager* manager);
    static void onExit(OperationalModeManager* manager);
    static void run(OperationalModeManager* manager);
    
private:
    static uint32_t _lastPowerCheck;
    static bool _deepSleepEnabled;
};

//================================================================//
//                  UTILITY FUNCTIONS                             //
//================================================================//

/**
 * @brief Convert operational mode enum to human-readable string
 */
inline const char* operationalModeToString(OperationalMode_t mode) {
    switch (mode) {
        case INTERACTIVE_MODE: return "INTERACTIVE";
        case TESTING_MODE: return "TESTING";
        case PRODUCTION_MODE: return "PRODUCTION";
        default: return "UNKNOWN";
    }
}

/**
 * @brief Parse string to operational mode enum
 */
inline OperationalMode_t stringToOperationalMode(const String& modeStr) {
    String upper = modeStr;
    upper.toUpperCase();
    
    if (upper == "INTERACTIVE" || upper == "0") return INTERACTIVE_MODE;
    if (upper == "TESTING" || upper == "1") return TESTING_MODE;
    if (upper == "PRODUCTION" || upper == "2") return PRODUCTION_MODE;
    
    return INTERACTIVE_MODE; // Default fallback
}

//================================================================//
//                  POWER MANAGEMENT UTILITIES                    //
//================================================================//

/**
 * @class PowerManager
 * @brief Helper class for power management operations
 */
class PowerManager {
public:
    enum CpuFrequency {
        CPU_1MHZ = 0,
        CPU_2MHZ = 1,
        CPU_8MHZ = 2,
        CPU_16MHZ = 3,
        CPU_32MHZ = 4,
        CPU_64MHZ = 5
    };
    
    static bool setCpuFrequency(CpuFrequency freq);
    static CpuFrequency getCpuFrequency();
    static uint32_t estimatePowerConsumption(const ModeConfig& config);
    static void enableLowPowerMode(bool enable);
    static void configureWakeupSources(uint32_t sources);
    
private:
    static CpuFrequency _currentFrequency;
    static bool _lowPowerEnabled;
};

#endif // OPERATIONAL_MODE_MANAGER_H
