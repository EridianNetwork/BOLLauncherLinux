#include "serverlist.h"

#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <cstdlib>
#include <cstring>
#include <map>

static const std::string PACKET_START = "\xFF\xFF\xFF\xFF";
static const char MASTER_HOST[] = "169.58.207.95";
static const char MASTER_PORT[] = "27900";
static const unsigned char SECRET_MIX = 0x5A;
static const unsigned char MIXED_KEY[] = {0x19, 0x12, 0x1B, 0x14, 0x1D, 0x1F, 0x05, 0x17, 0x1F, 0x05, 0x09, 0x12, 0x1B, 0x08, 0x1F, 0x1E, 0x05, 0x09, 0x1F, 0x19, 0x08, 0x1F, 0x0E};

static std::string getMasterKey()
{
    std::string key;
    for (unsigned char letter : MIXED_KEY) {
        key += char(letter ^ SECRET_MIX);
    }
    return key;
}

static int toNumber(const std::string& text)
{
    return std::atoi(text.c_str());
}

static std::map<std::string, std::string> splitKeysAndValues(std::string line)
{
    std::map<std::string, std::string> values;
    if (!line.empty() && line[0] == '\\') {
        line = line.substr(1);
    }

    std::vector<std::string> parts;
    std::string current;
    for (char letter : line) {
        if (letter == '\\') {
            parts.push_back(current);
            current = "";
        } else {
            current += letter;
        }
    }
    parts.push_back(current);

    for (size_t i = 0; i + 1 < parts.size(); i += 2) {
        values[parts[i]] = parts[i + 1];
    }
    return values;
}

bool readServerListPacket(const std::string& packet, std::vector<GameServer>& serverList, std::string& errorMessage)
{
    std::string header = PACKET_START + "getserversResponse";
    if (packet.compare(0, header.size(), header) != 0) {
        errorMessage = "The master server sent an unexpected reply";
        return false;
    }

    std::string body = packet.substr(header.size());
    if (!body.empty() && body[0] == '\n') {
        body = body.substr(1);
    }

    size_t endPosition = body.find(PACKET_START);
    if (endPosition != std::string::npos) {
        body = body.substr(0, endPosition);
    }

    size_t start = 0;
    while (start < body.size()) {
        size_t lineEnd = body.find('\n', start);
        if (lineEnd == std::string::npos) {
            lineEnd = body.size();
        }
        std::string line = body.substr(start, lineEnd - start);
        start = lineEnd + 1;

        if (line.empty()) {
            continue;
        }

        std::map<std::string, std::string> values = splitKeysAndValues(line);
        GameServer server;
        server.ip = values["ip"];
        server.port = toNumber(values["port"]);
        server.name = values["hostname"];
        server.players = toNumber(values["clients"]);
        server.maxPlayers = toNumber(values["sv_maxclients"]);
        server.uptimeSeconds = toNumber(values["uptime"]);
        serverList.push_back(server);
    }
    return true;
}

bool getServersFromMaster(std::vector<GameServer>& serverList, std::string& errorMessage)
{
    addrinfo hints;
    std::memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;

    addrinfo* masterInfo = NULL;
    if (getaddrinfo(MASTER_HOST, MASTER_PORT, &hints, &masterInfo) != 0 || masterInfo == NULL) {
        errorMessage = "Could not find the master server";
        return false;
    }

    int mySocket = socket(AF_INET, SOCK_DGRAM, 0);
    if (mySocket < 0) {
        freeaddrinfo(masterInfo);
        errorMessage = "Could not open the connection";
        return false;
    }

    timeval waitTime;
    waitTime.tv_sec = 1;
    waitTime.tv_usec = 500000;
    setsockopt(mySocket, SOL_SOCKET, SO_RCVTIMEO, &waitTime, sizeof(waitTime));

    std::string request = PACKET_START + "getservers BorderlandsOnline " + getMasterKey();
    ssize_t sent = sendto(mySocket, request.data(), request.size(), 0, masterInfo->ai_addr, masterInfo->ai_addrlen);
    freeaddrinfo(masterInfo);

    if (sent < 0) {
        close(mySocket);
        errorMessage = "Could not reach the master server";
        return false;
    }

    char buffer[8192];
    ssize_t received = recvfrom(mySocket, buffer, sizeof(buffer), 0, NULL, NULL);
    close(mySocket);

    if (received <= 0) {
        errorMessage = "The master server did not answer";
        return false;
    }

    return readServerListPacket(std::string(buffer, received), serverList, errorMessage);
}
