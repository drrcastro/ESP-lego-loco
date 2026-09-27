#include "ESPNowManager.h"

static const uint8_t BROADCAST_MAC[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

#if defined(ESP32)
  #if defined(ESP_ARDUINO_VERSION) && defined(ESP_ARDUINO_VERSION_VAL)
    #if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
      #define ESP_NOW_RECV_V3
    #endif
  #endif
#endif

// Static low-level bridge functions
#if defined(ESP32)
  #if defined(ESP_NOW_RECV_V3)
  static void espNowPlatformRecv(const esp_now_recv_info_t *recv_info, const uint8_t *data, int len) {
      if (recv_info && data) {
          ESPNowManager::instance().handleEspNowRecv(recv_info->src_addr, data, len);
      }
  }
  #else
  static void espNowPlatformRecv(const uint8_t *mac, const uint8_t *data, int len) {
      ESPNowManager::instance().handleEspNowRecv(mac, data, len);
  }
  #endif
  static void espNowPlatformSend(const uint8_t *mac, esp_now_send_status_t status) {
      ESPNowManager::instance().handleEspNowSend(mac, (uint8_t)status);
  }
#elif defined(ESP8266)
  static void espNowPlatformRecv(uint8_t *mac, uint8_t *data, uint8_t len) {
      ESPNowManager::instance().handleEspNowRecv(mac, data, (int)len);
  }
  static void espNowPlatformSend(uint8_t *mac, uint8_t status) {
      ESPNowManager::instance().handleEspNowSend(mac, status);
  }
#endif

ESPNowManager& ESPNowManager::instance() {
    static ESPNowManager inst;
    return inst;
}

ESPNowManager::ESPNowManager() {
}

bool ESPNowManager::begin(NodeType role, uint8_t channel) {
    _role = role;
    _channel = channel;

#if defined(ESP32)
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    esp_wifi_set_promiscuous(true);
    esp_wifi_set_channel(_channel, WIFI_SECOND_CHAN_NONE);
    esp_wifi_set_promiscuous(false);

    WiFi.macAddress(_ownMac);
    generateNodeId();

    if (esp_now_init() != ESP_OK) {
        Serial.println(F("[ESP-NOW] Init Failed!"));
        return false;
    }

    esp_now_register_send_cb(espNowPlatformSend);
    esp_now_register_recv_cb(espNowPlatformRecv);

#elif defined(ESP8266)
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    wifi_set_channel(_channel);

    WiFi.macAddress(_ownMac);
    generateNodeId();

    if (esp_now_init() != 0) {
        Serial.println(F("[ESP-NOW] Init Failed on ESP8266!"));
        return false;
    }

    esp_now_set_self_role(ESP_NOW_ROLE_COMBO);
    esp_now_register_send_cb(espNowPlatformSend);
    esp_now_register_recv_cb(espNowPlatformRecv);
#endif

    // Always register broadcast peer
    addPeer(BROADCAST_MAC);

    Serial.printf("[ESP-NOW] Initialized Node: %s (Role: %d, MAC: %s, Ch: %d)\n",
                  _nodeId, _role, getMacAddressStr().c_str(), _channel);

    // Initial presence announcement
    announcePresence();

    return true;
}

void ESPNowManager::generateNodeId() {
    const char* prefix = "NODE";
    switch (_role) {
        case NODE_TYPE_MASTER: prefix = "MASTER"; break;
        case NODE_TYPE_LOCO:   prefix = "LOCO";   break;
        case NODE_TYPE_TRACK:  prefix = "TRACK";  break;
        default: break;
    }
    // Formatted with last 2 bytes of MAC for compact human-friendly name
    snprintf(_nodeId, sizeof(_nodeId), "%s_%02X%02X", prefix, _ownMac[4], _ownMac[5]);
}

void ESPNowManager::getMacAddress(uint8_t* mac) const {
    memcpy(mac, _ownMac, 6);
}

String ESPNowManager::getMacAddressStr() const {
    char buf[18];
    snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
             _ownMac[0], _ownMac[1], _ownMac[2], _ownMac[3], _ownMac[4], _ownMac[5]);
    return String(buf);
}

bool ESPNowManager::addPeer(const uint8_t* mac) {
#if defined(ESP32)
    if (esp_now_is_peer_exist(mac)) {
        return true;
    }
    esp_now_peer_info_t peerInfo = {};
    memcpy(peerInfo.peer_addr, mac, 6);
    peerInfo.channel = _channel;
    peerInfo.encrypt = false;
    return (esp_now_add_peer(&peerInfo) == ESP_OK);

#elif defined(ESP8266)
    if (esp_now_is_peer_exist((uint8_t*)mac)) {
        return true;
    }
    return (esp_now_add_peer((uint8_t*)mac, ESP_NOW_ROLE_COMBO, _channel, NULL, 0) == 0);
#endif
}

bool ESPNowManager::isPeer(const uint8_t* mac) const {
#if defined(ESP32)
    return esp_now_is_peer_exist(mac);
#elif defined(ESP8266)
    return esp_now_is_peer_exist((uint8_t*)mac);
#endif
}

bool ESPNowManager::sendBroadcast(const void* data, size_t len) {
    return sendUnicast(BROADCAST_MAC, data, len);
}

bool ESPNowManager::sendUnicast(const uint8_t* targetMac, const void* data, size_t len) {
    if (!isPeer(targetMac)) {
        addPeer(targetMac);
    }
#if defined(ESP32)
    esp_err_t res = esp_now_send(targetMac, (const uint8_t*)data, len);
    return (res == ESP_OK);
#elif defined(ESP8266)
    int res = esp_now_send((uint8_t*)targetMac, (uint8_t*)data, len);
    return (res == 0);
#endif
}

bool ESPNowManager::sendToNode(const char* targetNodeId, const void* data, size_t len) {
    DiscoveredNode* node = findNode(targetNodeId);
    if (node) {
        return sendUnicast(node->mac, data, len);
    }
    // If target not in cache, fallback to broadcast so target can process
    return sendBroadcast(data, len);
}

void ESPNowManager::announcePresence() {
    MsgDiscoveryAnnounce msg = {};
    msg.msgType = MSG_DISCOVERY_ANNOUNCE;
    msg.nodeType = (uint8_t)_role;
    strncpy(msg.nodeId, _nodeId, sizeof(msg.nodeId) - 1);
    memcpy(msg.mac, _ownMac, 6);
    msg.firmwareVersion = 0x0200; // v2.0
    msg.capabilities = 0xFF;

    sendBroadcast(&msg, sizeof(msg));
}

void ESPNowManager::sendHeartbeat() {
    MsgHeartbeat msg = {};
    msg.msgType = MSG_HEARTBEAT;
    strncpy(msg.nodeId, _nodeId, sizeof(msg.nodeId) - 1);
    msg.nodeType = (uint8_t)_role;
    msg.rssi = -50;
    msg.uptimeSec = millis() / 1000;

    if (_hasMasterMac) {
        sendUnicast(_masterMac, &msg, sizeof(msg));
    } else {
        sendBroadcast(&msg, sizeof(msg));
    }
}

void ESPNowManager::setMasterMac(const uint8_t* mac) {
    memcpy(_masterMac, mac, 6);
    _hasMasterMac = true;
    addPeer(mac);
}

DiscoveredNode* ESPNowManager::findNode(const char* nodeId) {
    for (auto& n : _nodes) {
        if (strncmp(n.nodeId, nodeId, sizeof(n.nodeId)) == 0) {
            return &n;
        }
    }
    return nullptr;
}

DiscoveredNode* ESPNowManager::findNodeByMac(const uint8_t* mac) {
    for (auto& n : _nodes) {
        if (memcmp(n.mac, mac, 6) == 0) {
            return &n;
        }
    }
    return nullptr;
}

void ESPNowManager::registerOrUpdateNode(const MsgDiscoveryAnnounce& msg, const uint8_t* mac, int8_t rssi) {
    DiscoveredNode* existing = findNodeByMac(mac);
    bool isNew = (existing == nullptr);

    if (existing) {
        strncpy(existing->nodeId, msg.nodeId, sizeof(existing->nodeId) - 1);
        existing->nodeType = (NodeType)msg.nodeType;
        existing->lastSeenMs = millis();
        existing->rssi = rssi;
        existing->isOnline = true;
        if (_onNodeEventCb) _onNodeEventCb(*existing, false);
    } else {
        DiscoveredNode newNode = {};
        strncpy(newNode.nodeId, msg.nodeId, sizeof(newNode.nodeId) - 1);
        snprintf(newNode.friendlyName, sizeof(newNode.friendlyName), "%s", msg.nodeId);
        memcpy(newNode.mac, mac, 6);
        newNode.nodeType = (NodeType)msg.nodeType;
        newNode.lastSeenMs = millis();
        newNode.rssi = rssi;
        newNode.isOnline = true;

        addPeer(mac);
        _nodes.push_back(newNode);
        Serial.printf("[ESP-NOW] Registered New Node: %s (Type: %d)\n", newNode.nodeId, newNode.nodeType);

        if (_onNodeEventCb) _onNodeEventCb(_nodes.back(), true);
    }
}

void ESPNowManager::updateNodeHeartbeat(const char* nodeId, int8_t rssi) {
    DiscoveredNode* n = findNode(nodeId);
    if (n) {
        n->lastSeenMs = millis();
        n->rssi = rssi;
        n->isOnline = true;
    }
}

void ESPNowManager::setNodeFriendlyName(const char* nodeId, const char* name) {
    DiscoveredNode* n = findNode(nodeId);
    if (n) {
        strncpy(n->friendlyName, name, sizeof(n->friendlyName) - 1);
    }
}

void ESPNowManager::update() {
    uint32_t now = millis();

    // Regular heartbeat transmission (every 2.5s)
    if (_role != NODE_TYPE_MASTER) {
        if (now - _lastHeartbeatMs >= 2500) {
            _lastHeartbeatMs = now;
            sendHeartbeat();
        }
    }

    // Master node timeout monitor for offline nodes (every 2.0s)
    if (_role == NODE_TYPE_MASTER) {
        if (now - _lastCleanupMs >= 2000) {
            _lastCleanupMs = now;
            for (auto& n : _nodes) {
                if (n.isOnline && (now - n.lastSeenMs > 7000)) {
                    n.isOnline = false;
                    Serial.printf("[ESP-NOW] Node %s timed out / went offline\n", n.nodeId);
                    if (_onNodeEventCb) _onNodeEventCb(n, false);
                }
            }
        }
    }
}

void ESPNowManager::handleEspNowRecv(const uint8_t *mac, const uint8_t *data, int len) {
    if (len <= 0 || !data) return;

    // Handle discovery broadcast automatically
    uint8_t msgType = data[0];
    if (msgType == MSG_DISCOVERY_ANNOUNCE && len >= (int)sizeof(MsgDiscoveryAnnounce)) {
        const MsgDiscoveryAnnounce* ann = (const MsgDiscoveryAnnounce*)data;
        if (instance()._role == NODE_TYPE_MASTER) {
            instance().registerOrUpdateNode(*ann, mac);
            // Reply with ACK so the node knows the master's MAC
            MsgDiscoveryAck ack = {};
            ack.msgType = MSG_DISCOVERY_ACK;
            strncpy(ack.targetNodeId, ann->nodeId, sizeof(ack.targetNodeId) - 1);
            memcpy(ack.masterMac, instance()._ownMac, 6);
            ack.wifiChannel = instance()._channel;
            ack.serverEpochTime = millis();
            instance().sendUnicast(mac, &ack, sizeof(ack));
        }
    } else if (msgType == MSG_DISCOVERY_ACK && len >= (int)sizeof(MsgDiscoveryAck)) {
        const MsgDiscoveryAck* ack = (const MsgDiscoveryAck*)data;
        if (strncmp(ack->targetNodeId, instance()._nodeId, sizeof(ack->targetNodeId)) == 0) {
            instance().setMasterMac(ack->masterMac);
            Serial.printf("[ESP-NOW] Linked to Master: %02X:%02X:%02X:%02X:%02X:%02X\n",
                          ack->masterMac[0], ack->masterMac[1], ack->masterMac[2],
                          ack->masterMac[3], ack->masterMac[4], ack->masterMac[5]);
        }
    } else if (msgType == MSG_HEARTBEAT && len >= (int)sizeof(MsgHeartbeat)) {
        const MsgHeartbeat* hb = (const MsgHeartbeat*)data;
        instance().updateNodeHeartbeat(hb->nodeId, hb->rssi);
    }

    // Forward to application callback
    if (instance()._onReceiveCb) {
        instance()._onReceiveCb(mac, data, len);
    }
}

void ESPNowManager::handleEspNowSend(const uint8_t *mac, uint8_t status) {
    // Optional transmission status logging
}
