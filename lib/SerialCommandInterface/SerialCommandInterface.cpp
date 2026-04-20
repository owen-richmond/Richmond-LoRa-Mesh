/**
 * @file SerialCommandInterface.cpp
 * @brief Implementation of SerialCommandInterface
 * @version 2.0
 * @date 2025-10-01
 */

#include "SerialCommandInterface.h"
#include <stdarg.h>

// Color constants
const String SerialCommandInterface::COLOR_RED = "\033[31m";
const String SerialCommandInterface::COLOR_GREEN = "\033[32m";
const String SerialCommandInterface::COLOR_YELLOW = "\033[33m";
const String SerialCommandInterface::COLOR_BLUE = "\033[34m";
const String SerialCommandInterface::COLOR_MAGENTA = "\033[35m";
const String SerialCommandInterface::COLOR_CYAN = "\033[36m";
const String SerialCommandInterface::COLOR_WHITE = "\033[37m";
const String SerialCommandInterface::COLOR_RESET = "\033[0m";

//================================================================//
//          SERIAL COMMAND INTERFACE IMPLEMENTATION               //
//================================================================//

SerialCommandInterface::SerialCommandInterface(MeshNetworkManager* meshManager, 
                                             OperationalModeManager* modeManager,
                                             NodeConfig* nodeConfig)
    : _meshManager(meshManager), _modeManager(modeManager), _nodeConfig(nodeConfig),
      _promptShown(false), _echoEnabled(true), _autoCompleteEnabled(true),
      _historyIndex(0), _historyPosition(0), _aliasCount(0), _completionCount(0),
      _instancePrefixEnabled(false), _colorOutput(true), _timestampOutput(false),
      _verboseOutput(false), _commandsProcessed(0), _invalidCommands(0) {
    
    _startTime = millis();
    _inputBuffer.reserve(256);
    _instanceId = "Node";
    
    initializeCommands();
    initializeAliases();
    setupDefaultConfiguration();
}

SerialCommandInterface::~SerialCommandInterface() {
    // Cleanup if needed
}

void SerialCommandInterface::run() {
    if (Serial.available() > 0) {
        char c = Serial.read();
        
        if (_echoEnabled) {
            Serial.print(c);
        }
        
        if (c == '\n' || c == '\r') {
            handleEnter();
        } else if (c == '\b' || c == 127) {  // Backspace
            handleBackspace();
        } else if (c == '\t') {  // Tab
            handleTab();
        } else if (c == 27) {  // Escape
            handleEscape();
        } else if (c >= 32 && c < 127) {  // Printable characters
            _inputBuffer += c;
        }
    }
}

void SerialCommandInterface::showPrompt() {
    if (_promptShown) return;
    
    Serial.println();
    
    if (_instancePrefixEnabled) {
        Serial.print(getInstancePrefix());
    }
    
    if (_colorOutput) {
        Serial.print(COLOR_GREEN);
    }
    
    Serial.print(_modeManager->getCurrentModeString());
    Serial.print(">");
    
    if (_colorOutput) {
        Serial.print(COLOR_RESET);
    }
    
    Serial.print(" ");
    _promptShown = true;
}

void SerialCommandInterface::processInput() {
    // This is handled by run() method
}

void SerialCommandInterface::handleEnter() {
    Serial.println();  // New line
    
    if (_inputBuffer.length() > 0) {
        addToHistory(_inputBuffer);
        
        String expanded = expandAliases(_inputBuffer);
        CommandResult result = executeCommand(expanded);
        
        _commandsProcessed++;
        if (result != CMD_SUCCESS && result != CMD_HELP_SHOWN) {
            _invalidCommands++;
        }
        
        _inputBuffer = "";
    }
    
    _promptShown = false;
    showPrompt();
}

void SerialCommandInterface::handleBackspace() {
    if (_inputBuffer.length() > 0) {
        _inputBuffer.remove(_inputBuffer.length() - 1);
        Serial.print("\b \b");  // Erase character
    }
}

void SerialCommandInterface::handleTab() {
    if (_autoCompleteEnabled && _inputBuffer.length() > 0) {
        handleAutoComplete();
    }
}

void SerialCommandInterface::handleEscape() {
    // Handle escape sequences for arrow keys, etc.
}

void SerialCommandInterface::handleArrowKeys() {
    // Arrow key handling for history navigation
}

void SerialCommandInterface::handleAutoComplete() {
    findCompletions(_inputBuffer);
    
    if (_completionCount == 1) {
        // Single match - complete it
        _inputBuffer = _completionCandidates[0];
        Serial.print("\r");
        showPrompt();
        Serial.print(_inputBuffer);
    } else if (_completionCount > 1) {
        // Multiple matches - show them
        showCompletions();
        showPrompt();
        Serial.print(_inputBuffer);
    }
}

void SerialCommandInterface::findCompletions(const String& partial) {
    _completionCount = 0;
    _partialCommand = partial;
    
    // Simple command matching (stub implementation)
    const char* commands[] = {
        "send", "broadcast", "ping", "status", "nodes", "routes", 
        "stats", "help", "mode", "config", "reset", "info", "debug"
    };
    
    for (int i = 0; i < 13 && _completionCount < 50; i++) {
        String cmd = commands[i];
        if (cmd.startsWith(partial)) {
            _completionCandidates[_completionCount++] = cmd;
        }
    }
}

void SerialCommandInterface::showCompletions() {
    Serial.println();
    for (int i = 0; i < _completionCount; i++) {
        Serial.print(_completionCandidates[i]);
        Serial.print("  ");
    }
    Serial.println();
}

CommandResult SerialCommandInterface::executeCommand(const String& commandLine) {
    if (commandLine.length() == 0) {
        return CMD_SUCCESS;
    }
    
    CommandContext context;
    parseCommandLine(commandLine, context);
    
    return processCommand(context);
}

void SerialCommandInterface::parseCommandLine(const String& commandLine, CommandContext& context) {
    context.fullCommand = commandLine;
    context.timestamp = millis();
    context.currentMode = _modeManager->getCurrentMode();
    context.debugMode = _verboseOutput;
    
    // Simple parsing - split by spaces
    int spaceIndex = commandLine.indexOf(' ');
    
    if (spaceIndex == -1) {
        // No arguments
        context.command = commandLine;
        context.argCount = 0;
        context.args = nullptr;
    } else {
        // Has arguments
        context.command = commandLine.substring(0, spaceIndex);
        String argsStr = commandLine.substring(spaceIndex + 1);
        
        // Count arguments
        context.argCount = 1;
        int argsLen = (int)argsStr.length();
        for (int i = 0; i < argsLen; i++) {
            if (argsStr[i] == ' ' && i < argsLen - 1 && argsStr[i+1] != ' ') {
                context.argCount++;
            }
        }
        
        // Parse arguments
        context.args = new String[context.argCount];
        int argIndex = 0;
        int lastSpace = 0;
        
        int argsLength = (int)argsStr.length();
        for (int i = 0; i <= argsLength; i++) {
            if (i == argsLength || argsStr[i] == ' ') {
                if (i > lastSpace) {
                    context.args[argIndex++] = argsStr.substring(lastSpace, i);
                }
                lastSpace = i + 1;
            }
        }
    }
}

CommandResult SerialCommandInterface::processCommand(const CommandContext& context) {
    String cmd = context.command;
    cmd.toLowerCase();
    
    // Handle built-in commands
    if (cmd == "help") {
        return SystemCommands::help(context, this);
    } else if (cmd == "send") {
        return MessageCommands::send(context, this);
    } else if (cmd == "broadcast") {
        return MessageCommands::broadcast(context, this);
    } else if (cmd == "ping") {
        return MessageCommands::ping(context, this);
    } else if (cmd == "status") {
        return NetworkCommands::status(context, this);
    } else if (cmd == "nodes") {
        return NetworkCommands::nodes(context, this);
    } else if (cmd == "routes") {
        return NetworkCommands::routes(context, this);
    } else if (cmd == "stats") {
        return NetworkCommands::stats(context, this);
    } else if (cmd == "mode") {
        return SystemCommands::mode(context, this);
    } else if (cmd == "config") {
        return SystemCommands::config(context, this);
    } else if (cmd == "info") {
        return SystemCommands::info(context, this);
    } else if (cmd == "debug") {
        return SystemCommands::debug(context, this);
    } else {
        printError("Unknown command: " + cmd);
        return CMD_ERROR_INVALID_COMMAND;
    }
}

CommandDefinition* SerialCommandInterface::findCommand(const String& commandName) {
    // Stub - would search through command registry
    return nullptr;
}

bool SerialCommandInterface::isCommandAvailableInMode(const CommandDefinition* cmd, OperationalMode_t mode) {
    if (!cmd) return false;
    uint8_t modeBit = 1 << mode;
    return (cmd->availableInModes & modeBit) != 0;
}

String SerialCommandInterface::expandAliases(const String& command) {
    // Check if command starts with any alias
    for (int i = 0; i < _aliasCount; i++) {
        if (command.startsWith(_aliases[i].alias)) {
            String remaining = command.substring(_aliases[i].alias.length());
            return _aliases[i].command + remaining;
        }
    }
    return command;
}

void SerialCommandInterface::addAlias(const String& alias, const String& command) {
    if (_aliasCount < MAX_ALIASES) {
        _aliases[_aliasCount].alias = alias;
        _aliases[_aliasCount].command = command;
        _aliasCount++;
    }
}

void SerialCommandInterface::removeAlias(const String& alias) {
    for (int i = 0; i < _aliasCount; i++) {
        if (_aliases[i].alias == alias) {
            // Shift remaining aliases
            for (int j = i; j < _aliasCount - 1; j++) {
                _aliases[j] = _aliases[j + 1];
            }
            _aliasCount--;
            return;
        }
    }
}

void SerialCommandInterface::clearAliases() {
    _aliasCount = 0;
}

void SerialCommandInterface::addToHistory(const String& command) {
    _commandHistory[_historyIndex] = command;
    _historyIndex = (_historyIndex + 1) % HISTORY_SIZE;
}

String SerialCommandInterface::getFromHistory(int offset) {
    int index = (_historyIndex - offset + HISTORY_SIZE) % HISTORY_SIZE;
    return _commandHistory[index];
}

void SerialCommandInterface::clearHistory() {
    for (int i = 0; i < HISTORY_SIZE; i++) {
        _commandHistory[i] = "";
    }
    _historyIndex = 0;
}

void SerialCommandInterface::print(const String& message) {
    if (_timestampOutput) {
        Serial.print(getTimestamp());
        Serial.print(" ");
    }
    Serial.print(message);
}

void SerialCommandInterface::println(const String& message) {
    print(message);
    Serial.println();
}

void SerialCommandInterface::printf(const char* format, ...) {
    char buffer[256];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    print(String(buffer));
}

void SerialCommandInterface::printError(const String& error) {
    if (_colorOutput) {
        Serial.print(COLOR_RED);
        Serial.print("ERROR: ");
        Serial.print(COLOR_RESET);
    } else {
        Serial.print("ERROR: ");
    }
    Serial.println(error);
}

void SerialCommandInterface::printWarning(const String& warning) {
    if (_colorOutput) {
        Serial.print(COLOR_YELLOW);
        Serial.print("WARNING: ");
        Serial.print(COLOR_RESET);
    } else {
        Serial.print("WARNING: ");
    }
    Serial.println(warning);
}

void SerialCommandInterface::printSuccess(const String& message) {
    if (_colorOutput) {
        Serial.print(COLOR_GREEN);
        Serial.print("SUCCESS: ");
        Serial.print(COLOR_RESET);
    } else {
        Serial.print("SUCCESS: ");
    }
    Serial.println(message);
}

void SerialCommandInterface::printDebug(const String& debug) {
    if (_verboseOutput) {
        if (_colorOutput) {
            Serial.print(COLOR_CYAN);
            Serial.print("DEBUG: ");
            Serial.print(COLOR_RESET);
        } else {
            Serial.print("DEBUG: ");
        }
        Serial.println(debug);
    }
}

void SerialCommandInterface::printColor(const String& message, const String& color) {
    if (_colorOutput) {
        Serial.print(color);
        Serial.print(message);
        Serial.print(COLOR_RESET);
    } else {
        Serial.print(message);
    }
}

void SerialCommandInterface::showHelp() {
    println("\n=== Available Commands ===");
    println("Message Commands:");
    println("  send <nodeId> <message>    - Send message to specific node");
    println("  broadcast <message>        - Broadcast message to all nodes");
    println("  ping <nodeId>              - Ping a specific node");
    println("\nNetwork Commands:");
    println("  status                     - Show network status");
    println("  nodes                      - List known nodes");
    println("  routes                     - Show routing table");
    println("  stats                      - Show statistics");
    println("\nSystem Commands:");
    println("  help                       - Show this help");
    println("  mode <mode>                - Change operational mode");
    println("  config                     - Show configuration");
    println("  info                       - Show system information");
    println("  debug <on|off>             - Toggle debug output");
    println("");
}

void SerialCommandInterface::showCommandHelp(const String& command) {
    println("Help for command: " + command);
}

void SerialCommandInterface::showModeHelp() {
    println("\n=== Operational Modes ===");
    println("0 or INTERACTIVE  - User-controlled messaging");
    println("1 or TESTING      - Always-on testing mode");
    println("2 or PRODUCTION   - Power-optimized sleepy mode");
}

void SerialCommandInterface::showQuickReference() {
    showHelp();
}

double SerialCommandInterface::getCommandSuccessRate() const {
    if (_commandsProcessed == 0) return 100.0;
    return 100.0 * (1.0 - (double)_invalidCommands / _commandsProcessed);
}

uint32_t SerialCommandInterface::getUptimeSeconds() const {
    return (millis() - _startTime) / 1000;
}

String SerialCommandInterface::formatOutput(const String& message) {
    String formatted = message;
    if (_timestampOutput) {
        formatted = getTimestamp() + " " + formatted;
    }
    return formatted;
}

String SerialCommandInterface::getTimestamp() {
    uint32_t uptime = millis() - _startTime;
    uint32_t seconds = uptime / 1000;
    uint32_t minutes = seconds / 60;
    uint32_t hours = minutes / 60;
    
    char buffer[32];
    snprintf(buffer, sizeof(buffer), "[%02lu:%02lu:%02lu]", 
            hours, minutes % 60, seconds % 60);
    return String(buffer);
}

String SerialCommandInterface::getInstancePrefix() {
    return "[" + _instanceId + "] ";
}

void SerialCommandInterface::initializeCommands() {
    // Command initialization
}

void SerialCommandInterface::initializeAliases() {
    // Setup default aliases
    addAlias("s", "send");
    addAlias("b", "broadcast");
    addAlias("p", "ping");
    addAlias("?", "help");
}

void SerialCommandInterface::setupDefaultConfiguration() {
    // Setup defaults
}

//================================================================//
//              COMMAND IMPLEMENTATIONS                           //
//================================================================//

CommandResult MessageCommands::send(const CommandContext& ctx, void* userData) {
    SerialCommandInterface* ui = (SerialCommandInterface*)userData;
    
    if (ctx.argCount < 2) {
        ui->printError("Usage: send <nodeId> <message>");
        return CMD_ERROR_INVALID_ARGS;
    }
    
    uint16_t nodeId = ctx.args[0].toInt();
    String message = ctx.args[1];
    
    // Concatenate remaining args
    for (int i = 2; i < ctx.argCount; i++) {
        message += " " + ctx.args[i];
    }
    
    ui->println("Sending to node " + String(nodeId) + ": " + message);
    return CMD_SUCCESS;
}

CommandResult MessageCommands::broadcast(const CommandContext& ctx, void* userData) {
    SerialCommandInterface* ui = (SerialCommandInterface*)userData;
    
    if (ctx.argCount < 1) {
        ui->printError("Usage: broadcast <message>");
        return CMD_ERROR_INVALID_ARGS;
    }
    
    String message = ctx.args[0];
    for (int i = 1; i < ctx.argCount; i++) {
        message += " " + ctx.args[i];
    }
    
    ui->println("Broadcasting: " + message);
    return CMD_SUCCESS;
}

CommandResult MessageCommands::ping(const CommandContext& ctx, void* userData) {
    SerialCommandInterface* ui = (SerialCommandInterface*)userData;
    
    if (ctx.argCount < 1) {
        ui->printError("Usage: ping <nodeId>");
        return CMD_ERROR_INVALID_ARGS;
    }
    
    uint16_t nodeId = ctx.args[0].toInt();
    ui->println("Pinging node " + String(nodeId));
    return CMD_SUCCESS;
}

CommandResult MessageCommands::reply(const CommandContext& ctx, void* userData) {
    return CMD_SUCCESS;
}

CommandResult MessageCommands::forward(const CommandContext& ctx, void* userData) {
    return CMD_SUCCESS;
}

CommandResult MessageCommands::route(const CommandContext& ctx, void* userData) {
    return CMD_SUCCESS;
}

CommandResult NetworkCommands::status(const CommandContext& ctx, void* userData) {
    SerialCommandInterface* ui = (SerialCommandInterface*)userData;
    ui->println("\n=== Network Status ===");
    ui->println("Node operational");
    return CMD_SUCCESS;
}

CommandResult NetworkCommands::nodes(const CommandContext& ctx, void* userData) {
    SerialCommandInterface* ui = (SerialCommandInterface*)userData;
    ui->println("\n=== Known Nodes ===");
    ui->println("List of nodes would appear here");
    return CMD_SUCCESS;
}

CommandResult NetworkCommands::routes(const CommandContext& ctx, void* userData) {
    SerialCommandInterface* ui = (SerialCommandInterface*)userData;
    ui->println("\n=== Routing Table ===");
    ui->println("Routes would appear here");
    return CMD_SUCCESS;
}

CommandResult NetworkCommands::stats(const CommandContext& ctx, void* userData) {
    SerialCommandInterface* ui = (SerialCommandInterface*)userData;
    ui->println("\n=== Statistics ===");
    ui->println("TX: 0  RX: 0  FWD: 0");
    return CMD_SUCCESS;
}

CommandResult NetworkCommands::scan(const CommandContext& ctx, void* userData) {
    return CMD_SUCCESS;
}

CommandResult NetworkCommands::beacon(const CommandContext& ctx, void* userData) {
    return CMD_SUCCESS;
}

CommandResult NetworkCommands::discover(const CommandContext& ctx, void* userData) {
    return CMD_SUCCESS;
}

CommandResult SystemCommands::help(const CommandContext& ctx, void* userData) {
    SerialCommandInterface* ui = (SerialCommandInterface*)userData;
    ui->showHelp();
    return CMD_HELP_SHOWN;
}

CommandResult SystemCommands::mode(const CommandContext& ctx, void* userData) {
    SerialCommandInterface* ui = (SerialCommandInterface*)userData;
    
    if (ctx.argCount == 0) {
        ui->println("Current mode: " + String(ctx.currentMode));
        return CMD_SUCCESS;
    }
    
    ui->println("Mode change requested to: " + ctx.args[0]);
    return CMD_SUCCESS;
}

CommandResult SystemCommands::config(const CommandContext& ctx, void* userData) {
    SerialCommandInterface* ui = (SerialCommandInterface*)userData;
    ui->println("\n=== Configuration ===");
    ui->println("Config would appear here");
    return CMD_SUCCESS;
}

CommandResult SystemCommands::reset(const CommandContext& ctx, void* userData) {
    return CMD_SUCCESS;
}

CommandResult SystemCommands::reboot(const CommandContext& ctx, void* userData) {
    return CMD_SUCCESS;
}

CommandResult SystemCommands::info(const CommandContext& ctx, void* userData) {
    SerialCommandInterface* ui = (SerialCommandInterface*)userData;
    ui->println("\n=== System Information ===");
    ui->println("Uptime: " + String(ui->getUptimeSeconds()) + " seconds");
    ui->println("Commands processed: " + String(ui->getCommandsProcessed()));
    return CMD_SUCCESS;
}

CommandResult SystemCommands::debug(const CommandContext& ctx, void* userData) {
    SerialCommandInterface* ui = (SerialCommandInterface*)userData;
    
    if (ctx.argCount > 0) {
        String arg = ctx.args[0];
        arg.toLowerCase();
        bool enable = (arg == "on" || arg == "1" || arg == "true");
        ui->enableVerbose(enable);
        ui->println("Debug output " + String(enable ? "enabled" : "disabled"));
    }
    
    return CMD_SUCCESS;
}

CommandResult SystemCommands::log(const CommandContext& ctx, void* userData) {
    return CMD_SUCCESS;
}

CommandResult TestCommands::test(const CommandContext& ctx, void* userData) {
    return CMD_SUCCESS;
}

CommandResult TestCommands::flood(const CommandContext& ctx, void* userData) {
    return CMD_SUCCESS;
}

CommandResult TestCommands::corrupt(const CommandContext& ctx, void* userData) {
    return CMD_SUCCESS;
}

CommandResult TestCommands::loopback(const CommandContext& ctx, void* userData) {
    return CMD_SUCCESS;
}

CommandResult TestCommands::stress(const CommandContext& ctx, void* userData) {
    return CMD_SUCCESS;
}

CommandResult TestCommands::latency(const CommandContext& ctx, void* userData) {
    return CMD_SUCCESS;
}

CommandResult TestCommands::range(const CommandContext& ctx, void* userData) {
    return CMD_SUCCESS;
}

CommandResult PowerCommands::sleep(const CommandContext& ctx, void* userData) {
    return CMD_SUCCESS;
}

CommandResult PowerCommands::wake(const CommandContext& ctx, void* userData) {
    return CMD_SUCCESS;
}

CommandResult PowerCommands::power(const CommandContext& ctx, void* userData) {
    return CMD_SUCCESS;
}

CommandResult PowerCommands::battery(const CommandContext& ctx, void* userData) {
    return CMD_SUCCESS;
}

CommandResult PowerCommands::frequency(const CommandContext& ctx, void* userData) {
    return CMD_SUCCESS;
}
