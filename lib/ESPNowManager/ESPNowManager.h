#pragma once
#include <Arduino.h>
#include <vector>
#include <functional>
#include "ProtocolMessages.h"

#if defined(ESP32)
  #include <WiFi.h>
  #include <esp_now.h>
  #include <esp_wifi.h>
#elif defined(ESP8266)
  #include <ESP8266WiFi.h>
  #include <espnow.h>
#endif

struct DiscoveredNode {
    char     nodeId[16];
    char     friendlyName[32];
    uint8_t  mac[6];
    NodeType nodeType;
    uint32_t lastSeenMs;
    int8_t   rssi;
    bool     isOnline;
    // Cached telemetry
    int8_t   speed;
    uint16_t currentBlock;
    uint8_t  switchPosition;
    uint8_t  beamOccupied;
    uint16_t batteryMv;
};

class ESPNowManager {
public:
    using MessageCallback = std::function<void(const uint8_t* mac, const uint8_t* data, int len)>;
    using NodeEventCallback = std::function<void(const DiscoveredNode& node, bool isNew)>;

    static ESPNowManager& instance();

    bool begin(NodeType role, uint8_t channel = 1);
    void update();

    // Node identification
    const char* getNodeId() const { return _nodeId; }
    NodeType getNodeRole() const { return _role; }
    void getMacAddress(uint8_t* mac) const;
    String getMacAddressStr() const;

    // Transmission
    bool sendBroadcast(const void* data, size_t len);
    bool sendUnicast(const uint8_t* targetMac, const void* data, size_t len);
    bool sendToNode(const char* targetNodeId, const void* data, size_t len);

    // Peer Management
    bool addPeer(const uint8_t* mac);
    bool isPeer(const uint8_t* mac) const;

    // Discovery & Registry (Master)
    void announcePresence();
    void sendHeartbeat();
    const std::vector<DiscoveredNode>& getDiscoveredNodes() const { return _nodes; }
    DiscoveredNode* findNode(const char* nodeId);
    DiscoveredNode* findNodeByMac(const uint8_t* mac);
    void registerOrUpdateNode(const MsgDiscoveryAnnounce& msg, const uint8_t* mac, int8_t rssi = -50);
    void updateNodeHeartbeat(const char* nodeId, int8_t rssi);
    void setNodeFriendlyName(const char* nodeId, const char* name);

    // Callbacks
    void onReceive(MessageCallback cb) { _onReceiveCb = cb; }
    void onNodeEvent(NodeEventCallback cb) { _onNodeEventCb = cb; }

    // Master specific
    void setMasterMac(const uint8_t* mac);
    const uint8_t* getMasterMac() const { return _masterMac; }
    bool hasMasterMac() const { return _hasMasterMac; }

    // Internal low-level static callbacks invoked by platform layer
    static void handleEspNowRecv(const uint8_t *mac, const uint8_t *data, int len);
    static void handleEspNowSend(const uint8_t *mac, uint8_t status);

private:
    ESPNowManager();
    ~ESPNowManager() = default;

    NodeType _role = NODE_TYPE_UNKNOWN;
    char _nodeId[16] = {0};
    uint8_t _ownMac[6] = {0};
    uint8_t _masterMac[6] = {0};
    bool _hasMasterMac = false;
    uint8_t _channel = 1;

    std::vector<DiscoveredNode> _nodes;
    MessageCallback _onReceiveCb = nullptr;
    NodeEventCallback _onNodeEventCb = nullptr;

    uint32_t _lastHeartbeatMs = 0;
    uint32_t _lastCleanupMs = 0;

    void generateNodeId();
};

