#ifndef TEST_NODE_HELPER_H
#define TEST_NODE_HELPER_H

#include <Arduino.h>
#include "MeshNode.h"

/**
 * @file TestNodeHelper.h
 * @brief Provides diagnostic functions for testing the MeshNode.
 * @details This helper is intended to be used with a specific 'test_node'
 * build environment to print detailed, real-time status information about
 * the node without cluttering the main application logic.
 */

/**
 * @brief Prints a detailed diagnostic report for the given mesh node.
 * @param node The MeshNode object to diagnose.
 */
void printTestDiagnostics(MeshNode& node) {
    static uint32_t reportCounter = 0;

    Serial.println("\n=====================================");
    Serial.println("        NODE DIAGNOSTIC REPORT       ");
    Serial.println("=====================================");
    Serial.printf("Report #%lu\n", ++reportCounter);
    Serial.printf("Uptime: %lu ms\n", millis());
    Serial.println("-------------------------------------");
    Serial.println("      NODE CONFIGURATION & STATE     ");
    Serial.println("-------------------------------------");
    Serial.printf("Device ID: %u\n", node.getDeviceId());
    Serial.printf("Network ID: %u\n", node.getNetworkId());
    Serial.printf("Last Processed Packet ID: %u\n", node.getLastProcessedPacketID());
    Serial.println("=====================================\n");
}

#endif // TEST_NODE_HELPER_H
