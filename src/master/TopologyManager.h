#pragma once
#include <Arduino.h>
#include <vector>
#include <map>
#include "ProtocolMessages.h"
#include "ConfigStore.h"

struct TrackSegmentEdge {
    uint16_t fromBeaconId;
    uint16_t toBeaconId;
    uint32_t transitTimeMs;
    uint8_t  routeType; // 0=MAIN/STRAIGHT, 1=SIDING/TURNOUT
};

struct TrackNodeVertex {
    uint16_t beaconId;
    uint8_t  role; // 0=LOCATOR, 1=STATION_ARRIVAL, 2=APPROACH, 3=DEPARTURE, 4=SIDING
    char     stationId[16];
    char     description[32];
    uint32_t lastSeenMs;
};

class TopologyManager {
public:
    static TopologyManager& instance();

    void begin();
    void update();

    // Event hooks from telemetry
    void onBeaconDetected(const char* locoId, uint16_t blockId, int8_t currentSpeed);
    void onTrackOccupancyChanged(const char* trackId, bool occupied);
    void onLocoCircuitStatus(const MsgCircuitStatus& status);
    void onTrainLengthReport(const MsgTrainLengthReport& report);

    // Learning Lap control
    bool startLearningLap(const char* locoId, uint8_t calibrationSpeed = 35);
    bool stopLearningLap(const char* locoId);

    // Topology JSON serialization for Web UI SVG rendering
    String getTopologyJson();
    bool saveTopology();
    bool loadTopology();

    // Query active layout data
    const std::vector<TrackSegmentEdge>& getEdges() const { return _edges; }
    const std::vector<TrackNodeVertex>& getVertices() const { return _vertices; }

private:
    TopologyManager();
    ~TopologyManager() = default;

    std::vector<TrackSegmentEdge> _edges;
    std::vector<TrackNodeVertex>  _vertices;
    std::map<String, uint16_t>    _locoLastBeacon;
    std::map<String, uint32_t>    _locoLastBeaconTime;

    void registerOrUpdateBeacon(uint16_t beaconId, uint8_t role = 0, const char* stationId = "");
    void registerOrUpdateEdge(uint16_t fromB, uint16_t toB, uint32_t dtMs, uint8_t routeType = 0);
};
