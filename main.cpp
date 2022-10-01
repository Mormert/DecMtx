#include <iostream>
#include <set>
#include <thread>
#include <enet/enet.h>

using namespace std::chrono_literals;

enum class CriticalSectionState{
    FREE, OCCUPIED, REQUESTED
};

struct CriticalSection{
    CriticalSectionState mState;
    int32_t mTime{};
    std::set<int32_t> mRivals;

    std::set<int32_t> mGrantsRecvFrom;

};

void Entering(){
    for(int i = 0; i < 5; i++){
        std::cout << "I am in the critical section!";
        std::this_thread::sleep_for(1000ms);
    }
}

void Request(CriticalSection& cs){

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
    if(peerIP == "0"){
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
    ENetHost *client;
    /* Bind the client to the default localhost.     */
    /* A specific host address can be specified by   */
    /* enet_address_set_host (& address, "x.x.x.x"); */
    address.host = ENET_HOST_ANY;
    /* Bind the client to port 1234. */
    address.port = 1234;
    client = enet_host_create(&address  /* the address to bind the client host to */,
                              32        /* allow up to 32 clients and/or outgoing connections */,
                              1         /* allow up to 2 channels to be used, 0 and 1 */,
                              0         /* assume any amount of incoming bandwidth */,
                              0         /* assume any amount of outgoing bandwidth */);
    if (client == nullptr) {
        fprintf(stderr,
                "An error occurred while trying to create an ENet client host.\n");
        exit(EXIT_FAILURE);
    }

    ENetPeer *peer;

    if(!iAmFirst){
        enet_address_set_host(&address, peerIP.c_str());
        address.port = 1234;
        /* Initiate the connection, allocating the two channels 0 and 1. */
        peer = enet_host_connect(client, &address, 1, 0);
        if (peer == nullptr) {
            fprintf(stderr,
                    "No available peers for initiating an ENet connection.\n");
            exit(EXIT_FAILURE);
        }
    }

    ENetEvent event;

    while(true){
        std::cout << "..." << std::endl;
        if (enet_host_service(client, &event, 100))

            if(event.type == ENET_EVENT_TYPE_CONNECT){
                char hostStr[100];
                enet_address_get_host_ip(&event.peer->address, hostStr, 100);
                std::cout << "Connection from " << hostStr << " succeeded." << std::endl;
            }

            if(event.type == ENET_EVENT_TYPE_DISCONNECT) {
                char hostStr[100];
                enet_address_get_host_ip(&event.peer->address, hostStr, 100);
                std::cout << "Disconnection from " << hostStr << " succeeded." << std::endl;
            }

        if (event.type == ENET_EVENT_TYPE_RECEIVE) {
            printf("A packet of length %zu containing `%s` was received from %s on channel %u.\n",
                   event.packet->dataLength,
                   event.packet->data,
                   event.peer->data,
                   event.channelID);


        }

    }
#pragma clang diagnostic pop

    return 0;
}