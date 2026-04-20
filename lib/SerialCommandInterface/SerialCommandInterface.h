/**
 * @file SerialCommandInterface.h
 * @brief Advanced serial command interface for mesh network control
 * @version 2.0
 * @date 2025-09-27
 * @details
 * ENHANCED SERIAL COMMAND INTERFACE with:
 * - Mode-aware command sets
 * - Advanced message sending capabilities
 * - Network diagnostics and monitoring
 * - Multi-instance debugging support
 * - Extensible command framework
 * - Auto-completion and help system
 */

#ifndef SERIAL_COMMAND_INTERFACE_H
#define SERIAL_COMMAND_INTERFACE_H

#include <Arduino.h>
#include "MeshNetworkManager.h"
#include "OperationalModeManager.h"

//================================================================//
//                  COMMAND SYSTEM DEFINITIONS                    //
//================================================================//

/**
 * @struct NodeConfig (Forward declaration from main.cpp)
 */
struct NodeConfig;

/**
 * @enum CommandResult
 * @brief Result codes for command execution
 */
enum CommandResult {
    CMD_SUCCESS = 0,
    CMD_ERROR_INVALID_COMMAND,
    CMD_ERROR_INVALID_ARGS,
    CMD_ERROR_NOT_AVAILABLE_IN_MODE,
    CMD_ERROR_NETWORK_ERROR,
    CMD_ERROR_SYSTEM_ERROR,
    CMD_ERROR_PERMISSION_DENIED,
    CMD_HELP_SHOWN
};

/**
 * @struct CommandContext
 * @brief Context information for command execution
 */
struct CommandContext {
    String fullCommand;
    String command;
    String* args;
    int argCount;
    OperationalMode_t currentMode;
    bool debugMode;
    uint32_t timestamp;
    
    CommandContext() : args(nullptr), argCount(0), currentMode(INTERACTIVE_MODE), 
                      debugMode(false), timestamp(0) {}
    ~CommandContext() { if (args) delete[] args; }
};

/**
 * @struct CommandDefinition
 * @brief Definition structure for commands
 */
struct CommandDefinition {
    const char* name;
    const char* description;
    const char* usage;
    uint8_t minArgs;
    uint8_t maxArgs;
    uint8_t availableInModes;  // Bitmask: 1=Interactive, 2=Testing, 4=Production
    CommandResult (*handler)(const CommandContext& ctx, void* userData);
};

//================================================================//
//                  COMMAND CATEGORIES                            //
//================================================================//

/**
 * @class MessageCommands
 * @brief Commands for sending messages and controlling communication
 */
class MessageCommands {
public:
    static CommandResult send(const CommandContext& ctx, void* userData);
    static CommandResult broadcast(const CommandContext& ctx, void* userData);
    static CommandResult ping(const CommandContext& ctx, void* userData);
    static CommandResult reply(const CommandContext& ctx, void* userData);
    static CommandResult forward(const CommandContext& ctx, void* userData);
    static CommandResult route(const CommandContext& ctx, void* userData);
};

/**
 * @class NetworkCommands
 * @brief Commands for network management and diagnostics
 */
class NetworkCommands {
public:
    static CommandResult status(const CommandContext& ctx, void* userData);
    static CommandResult nodes(const CommandContext& ctx, void* userData);
    static CommandResult routes(const CommandContext& ctx, void* userData);
    static CommandResult stats(const CommandContext& ctx, void* userData);
    static CommandResult scan(const CommandContext& ctx, void* userData);
    static CommandResult beacon(const CommandContext& ctx, void* userData);
    static CommandResult discover(const CommandContext& ctx, void* userData);
};

/**
 * @class SystemCommands
 * @brief Commands for system control and configuration
 */
class SystemCommands {
public:
    static CommandResult help(const CommandContext& ctx, void* userData);
    static CommandResult mode(const CommandContext& ctx, void* userData);
    static CommandResult config(const CommandContext& ctx, void* userData);
    static CommandResult reset(const CommandContext& ctx, void* userData);
    static CommandResult reboot(const CommandContext& ctx, void* userData);
    static CommandResult info(const CommandContext& ctx, void* userData);
    static CommandResult debug(const CommandContext& ctx, void* userData);
    static CommandResult log(const CommandContext& ctx, void* userData);
};

/**
 * @class TestCommands
 * @brief Commands for testing and development
 */
class TestCommands {
public:
    static CommandResult test(const CommandContext& ctx, void* userData);
    static CommandResult flood(const CommandContext& ctx, void* userData);
    static CommandResult corrupt(const CommandContext& ctx, void* userData);
    static CommandResult loopback(const CommandContext& ctx, void* userData);
    static CommandResult stress(const CommandContext& ctx, void* userData);
    static CommandResult latency(const CommandContext& ctx, void* userData);
    static CommandResult range(const CommandContext& ctx, void* userData);
};

/**
 * @class PowerCommands
 * @brief Commands for power management
 */
class PowerCommands {
public:
    static CommandResult sleep(const CommandContext& ctx, void* userData);
    static CommandResult wake(const CommandContext& ctx, void* userData);
    static CommandResult power(const CommandContext& ctx, void* userData);
    static CommandResult battery(const CommandContext& ctx, void* userData);
    static CommandResult frequency(const CommandContext& ctx, void* userData);
};

//================================================================//
//                SERIAL COMMAND INTERFACE CLASS                  //
//================================================================//

/**
 * @class SerialCommandInterface
 * @brief Advanced command-line interface for mesh network control
 */
class SerialCommandInterface {
private:
    // System references
    MeshNetworkManager* _meshManager;
    OperationalModeManager* _modeManager;
    NodeConfig* _nodeConfig;
    
    // Command processing
    String _inputBuffer;
    String _lastCommand;
    bool _promptShown;
    bool _echoEnabled;
    bool _autoCompleteEnabled;
    
    // Command history
    static const int HISTORY_SIZE = 10;
    String _commandHistory[HISTORY_SIZE];
    int _historyIndex;
    int _historyPosition;
    
    // Aliases and shortcuts
    struct CommandAlias {
        String alias;
        String command;
    };
    static const int MAX_ALIASES = 20;
    CommandAlias _aliases[MAX_ALIASES];
    int _aliasCount;
    
    // Auto-completion
    String _completionCandidates[50];
    int _completionCount;
    String _partialCommand;
    
    // Multi-instance support
    String _instanceId;
    bool _instancePrefixEnabled;
    
    // Output formatting
    bool _colorOutput;
    bool _timestampOutput;
    bool _verboseOutput;
    
    // Statistics
    uint32_t _commandsProcessed;
    uint32_t _invalidCommands;
    uint32_t _startTime;

public:
    SerialCommandInterface(MeshNetworkManager* meshManager, 
                          OperationalModeManager* modeManager,
                          NodeConfig* nodeConfig);
    ~SerialCommandInterface();
    
    // Main interface
    void run();
    void showPrompt();
    void processInput();
    
    // Configuration
    void setInstanceId(const String& id) { _instanceId = id; }
    void enableInstancePrefix(bool enable) { _instancePrefixEnabled = enable; }
    void enableEcho(bool enable) { _echoEnabled = enable; }
    void enableAutoComplete(bool enable) { _autoCompleteEnabled = enable; }
    void enableColorOutput(bool enable) { _colorOutput = enable; }
    void enableTimestamp(bool enable) { _timestampOutput = enable; }
    void enableVerbose(bool enable) { _verboseOutput = enable; }
    
    // Command management
    void addAlias(const String& alias, const String& command);
    void removeAlias(const String& alias);
    void clearAliases();
    
    // History management
    void addToHistory(const String& command);
    String getFromHistory(int offset);
    void clearHistory();
    
    // Output utilities
    void print(const String& message);
    void println(const String& message);
    void printf(const char* format, ...);
    void printError(const String& error);
    void printWarning(const String& warning);
    void printSuccess(const String& message);
    void printDebug(const String& debug);
    
    // Color output (ANSI codes)
    void printColor(const String& message, const String& color);
    static const String COLOR_RED;
    static const String COLOR_GREEN;
    static const String COLOR_YELLOW;
    static const String COLOR_BLUE;
    static const String COLOR_MAGENTA;
    static const String COLOR_CYAN;
    static const String COLOR_WHITE;
    static const String COLOR_RESET;
    
    // Help system
    void showHelp();
    void showCommandHelp(const String& command);
    void showModeHelp();
    void showQuickReference();
    
    // Statistics
    uint32_t getCommandsProcessed() const { return _commandsProcessed; }
    uint32_t getInvalidCommands() const { return _invalidCommands; }
    double getCommandSuccessRate() const;
    uint32_t getUptimeSeconds() const;

private:
    // Command processing internals
    CommandResult executeCommand(const String& commandLine);
    CommandResult processCommand(const CommandContext& context);
    void parseCommandLine(const String& commandLine, CommandContext& context);
    CommandDefinition* findCommand(const String& commandName);
    bool isCommandAvailableInMode(const CommandDefinition* cmd, OperationalMode_t mode);
    String expandAliases(const String& command);
    
    // Auto-completion
    void handleAutoComplete();
    void findCompletions(const String& partial);
    void showCompletions();
    
    // Input handling
    void handleSpecialKeys(char key);
    void handleBackspace();
    void handleTab();
    void handleEnter();
    void handleEscape();
    void handleArrowKeys();
    
    // Output formatting
    String formatOutput(const String& message);
    String getTimestamp();
    String getInstancePrefix();
    
    // Initialization
    void initializeCommands();
    void initializeAliases();
    void setupDefaultConfiguration();
};

//================================================================//
//                  COMMAND REGISTRY                              //
//================================================================//

/**
 * @class CommandRegistry
 * @brief Registry for all available commands
 */
class CommandRegistry {
private:
    static CommandDefinition _commands[];
    static int _commandCount;
    
public:
    static CommandDefinition* getCommand(int index);
    static CommandDefinition* findCommand(const String& name);
    static int getCommandCount() { return _commandCount; }
    static void registerCommand(const CommandDefinition& cmd);
    static void unregisterCommand(const String& name);
    
    // Filtered access by mode
    static int getCommandsForMode(OperationalMode_t mode, CommandDefinition** commands, int maxCount);
    static void printCommandsForMode(OperationalMode_t mode);
};

//================================================================//
//                  MESSAGE FORMATTING UTILITIES                  //
//================================================================//

/**
 * @class MessageFormatter
 * @brief Utilities for formatting various types of messages
 */
class MessageFormatter {
public:
    static String formatNetworkStatus(const MeshNetworkManager::NetworkStats& stats);
    static String formatNodeInfo(uint16_t nodeId, const String& nodeName);
    static String formatRouteEntry(const RouteEntry& route);
    static String formatPacketInfo(const MeshPacket& packet);
    static String formatSystemInfo(const NodeConfig& config);
    static String formatModeInfo(OperationalMode_t mode, const ModeConfig& config);
    static String formatMemoryInfo();
    static String formatUptimeInfo(uint32_t uptimeMs);
    
    // Table formatting
    static void printTable(const String& title, const String* headers, int headerCount,
                          const String* rows, int rowCount, int columnCount);
    static void printSeparator(char character = '-', int length = 60);
    static String padString(const String& str, int width, bool rightAlign = false);
    
    // Data visualization
    static String createProgressBar(int value, int maxValue, int width = 20);
    static String createHistogram(int* values, int count, int width = 40);
};

//================================================================//
//                  INTERACTIVE FEATURES                          //
//================================================================//

/**
 * @class InteractiveFeatures
 * @brief Advanced interactive features for the command interface
 */
class InteractiveFeatures {
public:
    // Command line editing
    static void enableLineEditing(bool enable);
    static void handleLineEdit(String& buffer, char key);
    
    // Tab completion
    static bool handleTabCompletion(String& buffer, const String* candidates, int count);
    
    // Command suggestions
    static void showCommandSuggestions(const String& partial);
    
    // Interactive prompts
    static bool confirmAction(const String& message);
    static String promptForInput(const String& prompt, const String& defaultValue = "");
    static int promptForChoice(const String& question, const String* options, int optionCount);
    
    // Progress indicators
    static void showProgressSpinner();
    static void showProgressBar(int progress, int total);
    static void hideProgress();
    
private:
    static bool _lineEditingEnabled;
    static bool _progressVisible;
    static uint8_t _spinnerState;
};

//================================================================//
//                  MACRO DEFINITIONS                             //
//================================================================//

// Mode availability masks
#define MODE_INTERACTIVE  0x01
#define MODE_TESTING      0x02
#define MODE_PRODUCTION   0x04
#define MODE_ALL         (MODE_INTERACTIVE | MODE_TESTING | MODE_PRODUCTION)
#define MODE_NON_PROD    (MODE_INTERACTIVE | MODE_TESTING)

// Command argument limits
#define MAX_ARGS 10
#define MAX_ARG_LENGTH 64

// ANSI color codes
#define ANSI_RED     "\033[31m"
#define ANSI_GREEN   "\033[32m"
#define ANSI_YELLOW  "\033[33m"
#define ANSI_BLUE    "\033[34m"
#define ANSI_MAGENTA "\033[35m"
#define ANSI_CYAN    "\033[36m"
#define ANSI_WHITE   "\033[37m"
#define ANSI_RESET   "\033[0m"

#endif // SERIAL_COMMAND_INTERFACE_H
