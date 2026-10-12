#pragma once

#include <string>
#include <vector>

struct GameServer {
    std::string ip;
    int port = 0;
    std::string name;
    int players = 0;
    int maxPlayers = 0;
    int uptimeSeconds = 0;
};

bool getServersFromMaster(std::vector<GameServer>& serverList, std::string& errorMessage);
bool readServerListPacket(const std::string& packet, std::vector<GameServer>& serverList, std::string& errorMessage);
