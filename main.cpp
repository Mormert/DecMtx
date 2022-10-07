#include <iostream>
#include <set>
#include <thread>
#include <enet/enet.h>
#include <vector>
#include <sstream>
#include <random>
#include <unordered_map>
#include <mutex>
#include <fstream>
#include <map>

#include "json.hpp"

using namespace std::chrono_literals;
std::map<std::string, std::string> gAddressMap = {
        {"elin", "25.38.41.160"},
        {"alex", "25.37.205.76"},
        {"johan", "25.37.213.180"},
        {"john", "25.46.31.99"}};

std::string GetIpName(const std::string& ip)
{

    std::map<std::string, std::string> inverseAddressMap;
    for(auto i : gAddressMap)
    {
        inverseAddressMap.insert(std::pair(i.second, i.first));
    }

    auto it = inverseAddressMap.find(ip);
    if(it != inverseAddressMap.end())
    {
        if(it->second == "johan")
        {
            return "johan the great";
        }
        if(it->second == "elin")
        {
            return "elin the conqueror";
        }
        if(it->second == "alex")
        {
            return "alex the knight";
        }
        if(it->second == "john")
        {
            return "john the master";
        }
        return it->second;
    }
    return ip;
}

// Globals ;)
std::set<enet_uint32> gKnownNodes; // ip addresses
std::set<enet_uint32> gConnectedTo;
std::unordered_map<enet_uint32, ENetPeer *> gIpToPeer;
ENetAddress gMyAddress;
ENetHost *gClient;

std::mutex gEnetMutex;

#define CONSTPORT 1234

enum class CriticalSectionState
{
    FREE,
    OCCUPIED,
    REQUESTED
};

struct CriticalSection
{
    CriticalSectionState mState;
    int32_t mTime{};
    int32_t mMyRequestTime{};
    std::set<int32_t> mRivals;

    std::set<int32_t> mGrantsRecvFrom;
};

CriticalSection criticalSection;

std::vector<std::string> split(const std::string &str, char delimiter)
{
    std::vector<std::string> internal;
    std::stringstream ss(str);
    std::string tok;

    while (getline(ss, tok, delimiter))
    {
        internal.push_back(tok);
    }

    return internal;
}

void send(const std::string &type, const std::string &payload, ENetPeer *peer)
{
    char hostStr[100];
    enet_address_get_host_ip(&peer->address, hostStr, 100);
    std::cout << "\nSending " << type << " to " << GetIpName(hostStr) << std::endl;

    if (peer->address.host == gMyAddress.host)
    {
        std::cout << "Not sending this to meself!\n";
        return;
    }

    auto random_variable = ((double)rand() / (RAND_MAX));
    if (random_variable <= 1)
    {
        std::string packetString = type + ";" + payload;

        std::lock_guard<std::mutex> lock(gEnetMutex);
        ENetPacket *packet = enet_packet_create(
                packetString.c_str(),
                strlen(packetString.c_str()) + 1,
                ENET_PACKET_FLAG_RELIABLE);

        enet_peer_send(peer, 0, packet);
    }
}

void handleReceive(std::string &type, std::string &payload, ENetPeer *peer)
{
    char hostStr[100];
    enet_address_get_host_ip(&peer->address, hostStr, 100);
    std::cout << "\nIncoming message " << type << " from: " << GetIpName(hostStr) << std::endl;

    if (type == "NETWORK")
    {

        std::cout << "I received a list of the nodes in the network: " << std::endl;
        nlohmann::json j = nlohmann::json::parse(payload);
        std::cout << j.dump(4) << std::endl;
        const std::set<int32_t> nodes = j;
        std::set<enet_uint32> nodesu;
        for (auto i : nodes)
        {
            nodesu.insert(i);
        }
        gKnownNodes = nodesu;

        for (auto node : gKnownNodes)
        {
            char hostStr[100];
            ENetAddress address;
            address.port = CONSTPORT;
            address.host = node;
            enet_address_get_host_ip(&address, hostStr, 100);
            std::cout << GetIpName(hostStr) << std::endl;
        }

        // Connect to new nodes not known before
        for (auto &node : gKnownNodes)
        {
            if (node == gMyAddress.host)
            {
                // don't connect to ourself guard
                continue;
            }

            auto it = gConnectedTo.find(node);
            if (it != gConnectedTo.end())
            {
                // don't connect twice
                continue;
            }

            ENetAddress address;
            address.host = node;
            address.port = CONSTPORT;
            ENetPeer *newPeer;
            newPeer = enet_host_connect(gClient, &address, 1, 0);
            if (newPeer == nullptr)
            {
                fprintf(stderr,
                        "No available peers for initiating an ENet connection.\n");
                exit(EXIT_FAILURE);
            }
            // gConnectedTo.insert(node);
        }
    }
    else if (type == "ENTER")
    {
        criticalSection.mTime = max(stoi(payload), criticalSection.mTime) + 1;
        if (criticalSection.mState == CriticalSectionState::FREE)
        {
            send("GRANT", "", peer);
        }
        else if (criticalSection.mState == CriticalSectionState::REQUESTED)
        {
            int payloadInt = std::stoi(payload);
            if (payloadInt < criticalSection.mMyRequestTime)
            {
                send("GRANT", "", peer);
            }
            else if (payloadInt > criticalSection.mMyRequestTime)
            {
                // add the rival
                criticalSection.mRivals.insert(peer->address.host);
            }
            else if (payloadInt == criticalSection.mMyRequestTime)
            {
                // compare ip with my ip
                if (peer->address.host < gMyAddress.host)
                {
                    send("GRANT", "", peer);
                }
                else
                {
                    criticalSection.mRivals.insert(peer->address.host);
                }
            }
        }
        else
        { // Occupied
            criticalSection.mRivals.insert(peer->address.host);
        }
    }
    else if (type == "GRANT")
    {
        criticalSection.mGrantsRecvFrom.insert(peer->address.host);
        /* if(criticalSection.mGrantsRecvFrom.size() == gConnectedTo.size() - 1){}*/
    }
}

void Entering()
{
    criticalSection.mState = CriticalSectionState::OCCUPIED;

    for (int i = 0; i < 5; i++)
    {
        std::cout << " ------------------------ I am in the critical section! ------------------------" << std::endl;
        std::this_thread::sleep_for(1000ms);
    }

    criticalSection.mState = CriticalSectionState::FREE;
    std::cout << "Exiting critical section" << std::endl;
    // Exit the critical section ...
    for (auto ip : criticalSection.mRivals)
    {
        ENetPeer *peer = gIpToPeer.at(ip);
        send("GRANT", "", peer);
    }
    criticalSection.mRivals.clear();
}

void Request()
{

    criticalSection.mState = CriticalSectionState::REQUESTED;
    criticalSection.mTime++;
    criticalSection.mMyRequestTime = criticalSection.mTime;
    std::cout << "Requesting at " << criticalSection.mMyRequestTime << std::endl;

    // Send enter message to all other nodes containing the requested
    // critical section, the node's ID and the node's time.

    for (auto &ip : gConnectedTo)
    {
        ENetPeer *peer = gIpToPeer.at(ip);
        send("ENTER", std::to_string(criticalSection.mMyRequestTime), peer);
    }

    // Wait until we have all the grants ...
    // TODO: Unsure if this can cause problems
    // Ugly solution with polling...
    while (criticalSection.mGrantsRecvFrom.size() < gConnectedTo.size())
    {
        // spin spin spin
        std::this_thread::sleep_for(1ms);
    }

    criticalSection.mGrantsRecvFrom.clear();
    std::cout << "Entering" << std::endl;
    Entering();
}

#pragma clang diagnostic push
#pragma ide diagnostic ignored "EndlessLoop"

int main()
{

    std::ifstream infile("myip.txt");
    std::string myIp;
    std::getline(infile, myIp);

    if (myIp.empty())
    {
        std::cout << "Enter your own IP address: ";
        std::cin >> myIp;
        std::ofstream of("myip.txt");
        of << myIp;
    }

    std::cout << "Your ip address is:" << myIp << std::endl;

    enet_address_set_host(&gMyAddress, myIp.c_str());

    std::cout << gMyAddress.host << std::endl;

    std::cout << "Enter someone in the overlay networks ip address (or 0 if you're first)" << std::endl;

    std::string peerIP;
    std::cin >> peerIP;

    auto it = gAddressMap.find(peerIP);
    if (it != gAddressMap.end())
    {
        peerIP = it->second;
    }

    bool iAmFirst = false;
    if (peerIP == "0")
    {
        iAmFirst = true;
    }

    criticalSection.mState = CriticalSectionState::FREE;
    criticalSection.mTime = 0;
    criticalSection.mRivals = {};
    criticalSection.mGrantsRecvFrom = {};

    if (enet_initialize() != 0)
    {
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
    if (gClient == nullptr)
    {
        fprintf(stderr,
                "An error occurred while trying to create an ENet client host.\n");
        exit(EXIT_FAILURE);
    }

    // Connect to first node
    if (!iAmFirst)
    {
        enet_address_set_host(&address, peerIP.c_str());
        address.port = 1234;
        /* Initiate the connection, allocating the two channels 0 and 1. */
        ENetPeer *peer = enet_host_connect(gClient, &address, 1, 0);
        if (peer == nullptr)
        {
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

    std::thread sendThread2 = std::thread([&]()
                                          {
                                              while (true)
                                              {
                                                  std::mt19937_64 eng{std::random_device{}()};
                                                  std::uniform_int_distribution<> dist{5, 7};
                                                  if (gConnectedTo.size() > 0 || iAmFirst)
                                                  {
                                                      const auto sleepFor = dist(eng);
                                                      std::cout << "\nSleeping for " << sleepFor << " seconds." << std::endl;
                                                      std::this_thread::sleep_for(std::chrono::seconds{sleepFor});
                                                      Request();
                                                  }
                                              }
                                          });

    while (true)
    {
        ENetEvent event;

        gEnetMutex.lock();
        int service = enet_host_service(gClient, &event, 0);
        gEnetMutex.unlock();
        if (service)
        {
            if (event.type == ENET_EVENT_TYPE_CONNECT)
            {
                char hostStr[100];
                enet_address_get_host_ip(&event.peer->address, hostStr, 100);
                std::cout << "Connection to " << GetIpName(hostStr) << " succeeded." << std::endl;

                gConnectedTo.insert(event.peer->address.host);
                gIpToPeer.insert(std::pair(event.peer->address.host, event.peer));
                gKnownNodes.insert(event.peer->address.host);

                nlohmann::json j = gKnownNodes;
                std::string outPayload = j.dump();

                for (auto ip : gConnectedTo)
                {
                    ENetPeer *peer = gIpToPeer.at(ip);
                    send("NETWORK", outPayload, peer);
                }

                if (criticalSection.mState == CriticalSectionState::REQUESTED)
                {
                    send("ENTER", std::to_string(criticalSection.mMyRequestTime), event.peer);
                }
            }

            if (event.type == ENET_EVENT_TYPE_DISCONNECT)
            {
                char hostStr[100];
                enet_address_get_host_ip(&event.peer->address, hostStr, 100);
                std::cout << "DISCONNECTION FROM " << GetIpName(hostStr) << "!" << std::endl;
                auto it = gKnownNodes.find(event.peer->address.host);
                if (it != gKnownNodes.end())
                {
                    gKnownNodes.erase(it);
                }

            }

            if (event.type == ENET_EVENT_TYPE_RECEIVE)
            {
                std::vector<std::string> recvDataDecode = split((char *)event.packet->data, ';');
                if (recvDataDecode.size() == 1)
                {
                    // if the payload is non-existent, then we add an empty payload
                    recvDataDecode.push_back("");
                }
                handleReceive(recvDataDecode[0], recvDataDecode[1], event.peer);
            }
        }

        std::this_thread::sleep_for(10ms);

#pragma clang diagnostic pop
    }

    return 0;
}