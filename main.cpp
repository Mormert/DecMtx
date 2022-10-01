#include <iostream>
#include <set>
#include <thread>
#include <enet/enet.h>
#include <vector>
#include <sstream>
#include <random>
#include <unordered_map>

#include "json.hpp"

using namespace std::chrono_literals;

// Globals ;)
std::set<enet_uint32> gKnownNodes; // ip addresses
std::set<enet_uint32> gConnectedTo;
std::unordered_map<enet_uint32, ENetPeer *> gIpToPeer;
ENetHost *gClient;

#define CONSTPORT 1234

enum class CriticalSectionState {
    FREE,
    OCCUPIED,
    REQUESTED
};

struct CriticalSection {
    CriticalSectionState mState;
    int32_t mTime{};
    std::set<int32_t> mRivals;

    std::set<int32_t> mGrantsRecvFrom;
};

std::vector<std::string> split(const std::string &str, char delimiter) {
    std::vector<std::string> internal;
    std::stringstream ss(str);
    std::string tok;

    while (getline(ss, tok, delimiter)) {
        internal.push_back(tok);
    }

    return internal;
}

void send(const std::string &type, const std::string &payload, ENetPeer *peer) {
    char hostStr[100];
    enet_address_get_host_ip(&peer->address, hostStr, 100);
    std::cout << "Sending " << type << " to " << hostStr << std::endl;

    auto random_variable = ((double) rand() / (RAND_MAX));
    if (random_variable <= 1) {
        std::string packetString = type + ";" + payload;
        ENetPacket *packet = enet_packet_create(
                packetString.c_str(),
                strlen(packetString.c_str()) + 1,
                ENET_PACKET_FLAG_RELIABLE);

        enet_peer_send(peer, 0, packet);
    }
}


void handleReceive(std::string &type, std::string &payload, ENetPeer *peer) {
    char hostStr[100];
    enet_address_get_host_ip(&peer->address, hostStr, 100);
    std::cout << "The type is: " << type << "from" << hostStr << std::endl;

    if (type == "NETWORK") {

        std::cout << "I received a list of the nodes in the network" << std::endl;
        nlohmann::json j = nlohmann::json::parse(payload);
        const std::set<int32_t> nodes = j;
        std::set<enet_uint32> nodesu;
        for (auto i: nodes) {
            nodesu.insert(i);
        }
        gKnownNodes = nodesu;

        // Connect to new nodes not known before
        for (auto &node: gKnownNodes) {
            if (node == gClient->address.host) {
                // don't connect to ourself guard
                continue;
            }

            auto it = gConnectedTo.find(node);
            if (it != gConnectedTo.end()) {
                // don't connect twice
                continue;
            }

            ENetAddress address;
            address.host = node;
            address.port = CONSTPORT;
            ENetPeer *newPeer;
            newPeer = enet_host_connect(gClient, &address, 1, 0);
            if (newPeer == nullptr) {
                fprintf(stderr,
                        "No available peers for initiating an ENet connection.\n");
                exit(EXIT_FAILURE);
            }
            //gConnectedTo.insert(node);
        }
    } else if (type == "IMALIVE") {
        std::cout << "IMALIVE RECV: " << payload << std::endl;
    }
}

void Entering() {
    for (int i = 0; i < 5; i++) {
        std::cout << "I am in the critical section!";
        std::this_thread::sleep_for(1000ms);
    }
}

void Request(CriticalSection &cs) {

    cs.mState = CriticalSectionState::REQUESTED;
    cs.mTime++;

    // Send enter message to all other nodes containing the requested
    // critical section, the node's ID and the node's time.

    Entering();
}

#pragma clang diagnostic push
#pragma ide diagnostic ignored "EndlessLoop"

int main() {

    std::cout << "Enter someone in the overlay networks ip address (or 0 if you're first)" << std::endl;
    std::string peerIP;
    std::cin >> peerIP;
    bool iAmFirst = false;
    if (peerIP == "0") {
        iAmFirst = true;
    }

    CriticalSection criticalSection;
    criticalSection.mState = CriticalSectionState::FREE;
    criticalSection.mTime = 0;
    criticalSection.mRivals = {};
    criticalSection.mGrantsRecvFrom = {};

    if (enet_initialize() != 0) {
        std::cout << "ENet init failure occurred!" << std::endl;
        return EXIT_FAILURE;
    }

    ENetAddress address;

    // Bind the client to the default localhost.
    // A specific host address can be specified by
    // enet_address_set_host (& address, "x.x.x.x");
    address.host = ENET_HOST_ANY;
    // Bind the client to constant port 1234.
    address.port = CONSTPORT;
    gClient = enet_host_create(&address, 32, 1, 0, 0);
    if (gClient == nullptr) {
        fprintf(stderr,
                "An error occurred while trying to create an ENet client host.\n");
        exit(EXIT_FAILURE);
    }

    // Connect to first node
    if (!iAmFirst) {
        enet_address_set_host(&address, peerIP.c_str());
        address.port = 1234;
        /* Initiate the connection, allocating the two channels 0 and 1. */
        ENetPeer *peer = enet_host_connect(gClient, &address, 1, 0);
        if (peer == nullptr) {
            fprintf(stderr,
                    "No available peers for initiating an ENet connection.\n");
            exit(EXIT_FAILURE);
        }
        gIpToPeer.insert(std::pair(address.host, peer));
    }

    std::random_device dev;
    std::mt19937 rng(dev());
    std::uniform_int_distribution<std::mt19937::result_type> dist(1, 10000); // distribution in range [1, 10000]
    int myRandomNumber = dist(rng);
    std::string myRandomNumberStr = std::to_string(myRandomNumber);
    std::cout << "My random number: " << myRandomNumber << std::endl;

    while (true) {
        std::cout << "..." << std::endl;

        ENetEvent event;
        if (enet_host_service(gClient, &event, 500)) {

            if (event.type == ENET_EVENT_TYPE_CONNECT) {
                char hostStr[100];
                enet_address_get_host_ip(&event.peer->address, hostStr, 100);
                std::cout << "Connection to " << hostStr << " succeeded." << std::endl;

                gConnectedTo.insert(event.peer->address.host);
                gIpToPeer.insert(std::pair(event.peer->address.host, event.peer));
                gKnownNodes.insert(event.peer->address.host);

                nlohmann::json j = gKnownNodes;
                std::string outPayload = j.dump();

                for (auto ip: gConnectedTo) {
                    ENetPeer *peer = gIpToPeer.at(ip);
                    send("NETWORK", outPayload, peer);
                }
            }

            if (event.type == ENET_EVENT_TYPE_DISCONNECT) {
                char hostStr[100];
                enet_address_get_host_ip(&event.peer->address, hostStr, 100);
                std::cout << "Disconnection from " << hostStr << " succeeded." << std::endl;
                auto it = gKnownNodes.find(event.peer->address.host);
                if (it != gKnownNodes.end()) {
                    gKnownNodes.erase(it);
                }
            }

            if (event.type == ENET_EVENT_TYPE_RECEIVE) {
                std::vector<std::string> recvDataDecode = split((char *) event.packet->data, ';');
                handleReceive(recvDataDecode[0], recvDataDecode[1], event.peer);
            }
        }

        for (auto ip: gConnectedTo) {
            ENetPeer *peer = gIpToPeer.at(ip);
            send("IMALIVE", myRandomNumberStr, peer);
        }

        std::this_thread::sleep_for(500ms);


#pragma clang diagnostic pop

    }

    return 0;

}