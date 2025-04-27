#include <arpa/inet.h>
#include <execinfo.h>
#include <netinet/in.h>
#include <pthread.h>
#include <sys/socket.h>
#include <termios.h>
#include <unistd.h>

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <mutex>
#include <thread>
using namespace std;

// Socket Listening Port
const int port = 4430;
// Current Process ID
char dcId;
// hostIndex
int hostIndex;
// Key store
array<array<unsigned short int, 2>, 2000> store;
int storeIndex = 0;
// mutex for store
mutex storeMutex;
// Server Socket identifier
int serverSocket;

#define READ_REQUEST 1
#define WRITE_REQUEST 2
#define RECOVER_REQUEST 3
#define RECOVER_WRITE 4
#define REPLICATE_WRITE 5

// declar array of strings
array<char *, 7> hosts = {
    "10.176.69.32", "10.176.69.33", "10.176.69.34", "10.176.69.35", "10.176.69.36", "10.176.69.37", "10.176.69.38",
};

int randNum(int min, int max) { return min + rand() % (max - min + 1); }

// Function to get a single character without waiting for Enter
char getChar() {
    struct termios oldt, newt;
    char ch;
    // Get current terminal settings
    tcgetattr(STDIN_FILENO, &oldt);
    newt = oldt;
    // Disable canonical mode and echo
    newt.c_lflag &= ~(ICANON | ECHO);
    // Set the new settings immediately
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);
    // Read a single character
    read(STDIN_FILENO, &ch, 1);
    // Restore old settings
    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
    return ch;
}

// Read key from store
// Return value or 0 if not found
unsigned short int readStore(unsigned short int key) {
    for (int i = 0; i < storeIndex; i++) {
        if (store[i][0] == key) {
            return store[i][1];
        }
    }
    return 0;
}

int hashKey(unsigned short int key) { return ((key + 7) % 7); }

bool isKeyRelatedToHost(unsigned short int key, int recoverHostIndex) {
    int hash = hashKey(key);
    // We only save keys in the same server, copy it to the next 2 servers only
    for (int i = -2; i <= 0; i++) {
        if (hash == (recoverHostIndex + i + 7) % 7) {
            return true;
        }
    }
    return false;
}

// Write key to store
bool writeStore(unsigned short int key, unsigned short int value, bool propigate = true) {
    // Propigate the request to the next servers
    bool canMakeWrite = !propigate;
    if (propigate) {
        short int propigateNextCount = 0;
        short int successPropigateCount = 0;
        if (hashKey(key) == hostIndex) {
            // We are the designated server, replicate to the next 2 servers
            propigateNextCount = 2;
        } else if (hashKey(key) == hostIndex + 1) {
            // The designated server is down, we will replicate to the next 1 server
            propigateNextCount = 1;
        } else {
            return false;  // Only replica available, no write allowed
        }

        for (int i = 1; i <= propigateNextCount; i++) {
            char *peer = hosts[(hostIndex + i + 7) % 7];

            /**
             * Open connection and send replica write to the peer
             *
             */
            int peerSocket = socket(AF_INET, SOCK_STREAM, 0);
            if (peerSocket < 0) {
                cout << "[RW] Error at socket(): " << strerror(errno) << endl;
                continue;
            }
            sockaddr_in service;  // initialising service as sockaddr_in structure
            service.sin_family = AF_INET;
            service.sin_addr.s_addr = inet_addr(peer);
            service.sin_port = htons(port);
            if (connect(peerSocket, (struct sockaddr *)&service, sizeof(service)) < 0) {
                // Error connecting to server
                close(peerSocket);
                continue;
            }
            unsigned short int message[3];
            message[0] = REPLICATE_WRITE;  // Recovery Request
            message[1] = key;
            message[2] = value;
            send(peerSocket, message, sizeof(message), 0);
            int rbyteCount = recv(peerSocket, message, sizeof(message), 0);
            if (rbyteCount < 0) {
                cout << "[RW] Server recv error: " << strerror(errno) << endl;
            } else if (rbyteCount == 0) {
                // Connection closed
                cout << "[RW] Server recv error: " << strerror(errno) << endl;
            } else {
                successPropigateCount++;
            }
            close(peerSocket);
        }
        canMakeWrite = ((propigateNextCount - successPropigateCount) <=
                        1);  // if request was propigated at least once, write is allowed
        printf("[RW] Propigated %d keys to %d servers, allow write: %d\n", successPropigateCount, propigateNextCount,
               canMakeWrite);
    }

    if (!canMakeWrite) {
        return false;  // Propigation failed, we are the only replica online, no write allowed
    }

    for (int i = 0; i < storeIndex; i++) {
        if (store[i][0] == key) {
            // Key already exists, update value
            store[i][1] = value;
            return true;
        }
    }

    // Key doesnt exist, append it to our keyStore
    storeMutex.lock();
    store[storeIndex] = {key, value};
    storeIndex++;
    storeMutex.unlock();
    return true;
}

void recoverHost(unsigned short int recoverHostIndex, int peerSocket) {
    printf("[RH] Help Recovering host %d\n", recoverHostIndex);
    // Send all store values to host
    unsigned short int message[3];
    int keycount = 0;
    for (int i = 0; i < storeIndex; i++) {
        if (isKeyRelatedToHost(store[i][0], recoverHostIndex)) {
            // printf("[RH] Sending key %d to %d\n", store[i][0], recoverHostIndex);
            // We only send keys that is related to the host only
            message[0] = RECOVER_WRITE;  // Recover write
            message[1] = store[i][0];
            message[2] = store[i][1];
            send(peerSocket, message, sizeof(message), 0);
            keycount++;
        }
    }
    close(peerSocket);
    printf("[RH] Sent %d keys to %d\n", keycount, recoverHostIndex);
}

// Recover our keys if any from nearby servers
void recoverKeys() {
    int recoveredKeys = 0;
    // Connect to 2 servers back and 2 server forward to get all related keys
    for (int i = -2; i <= 2; i++) {
        if (i == 0) {
            // Skip our own server
            continue;
        }
        int peerSocket = socket(AF_INET, SOCK_STREAM, 0);
        if (peerSocket < 0) {
            cout << "[R] Error at socket(): " << strerror(errno) << endl;
            return;
        }
        // Peer IP address
        char *peer = hosts[(hostIndex + i + 7) % 7];
        printf("[R] Recovering from %s\n", peer);
        sockaddr_in service;  // initialising service as sockaddr_in structure
        service.sin_family = AF_INET;
        service.sin_addr.s_addr = inet_addr(peer);
        service.sin_port = htons(port);
        int retry = 0;
        while (retry < 5) {
            if (connect(peerSocket, (struct sockaddr *)&service, sizeof(service)) < 0) {
                // cout << "[R] connect(): Error connecting to server: " << strerror(errno) << endl;
                // close(peerSocket);
                retry++;
                usleep(2 * 1000000);  // 2 seconds sleep
                continue;
            } else {
                break;
            }
        }
        if (retry >= 5) {
            cout << "[R] Failed to connect to server: " << peer << " - " << strerror(errno) << endl;
            close(peerSocket);
            continue;
        }
        // cout << "[R] Connected to server: " << peer << endl;
        unsigned short int message[3];
        message[0] = RECOVER_REQUEST;  // Recovery Request
        message[1] = hostIndex;
        message[2] = 0;
        send(peerSocket, message, sizeof(message), 0);
        while (true) {
            int rbyteCount = recv(peerSocket, message, sizeof(message), 0);
            // printf("[R] recv rbyteCount %d\n", rbyteCount);
            if (rbyteCount < 0) {
                cout << "[R] Server recv error: " << strerror(errno) << endl;
                break;
            } else if (rbyteCount == 0) {
                // Connection closed
                cout << "[R] Server recv error: " << strerror(errno) << endl;
                break;
            } else {
                // printf("[R] Recovering from %s, %d,%d,%d\n", peer, message[0], message[1], message[2]);
                writeStore(message[1], message[2], false);
                recoveredKeys++;
            }
        }
        close(peerSocket);
        printf("[R] Recovered %d keys from %s\n", recoveredKeys, peer);
    }
}

// Socket server initialized
void socketServer() {
    serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSocket < 0) {
        cout << "Error at socket(): " << strerror(errno) << endl;
        return;
    }
    sockaddr_in service;  // initialising service as sockaddr_in structure
    service.sin_family = AF_INET;
    service.sin_addr.s_addr = inet_addr(hosts[hostIndex]);
    service.sin_port = htons(port);
    if (bind(serverSocket, (struct sockaddr *)&service, sizeof(service)) < 0) {
        cout << "bind() failed: " << strerror(errno) << endl;
        close(serverSocket);
        exit(1);
    }

    // 4. Listen to incomming connections
    if (listen(serverSocket, 1) < 0) {
        cout << "listen(): Error listening on socket: " << strerror(errno) << endl;
        exit(1);
    }

    // Accepting new incoming messages
    int acceptSocket;

    unsigned short int incoming[3];
    int rbyteCount;
    printf("Server Started at %s:%d\n", hosts[hostIndex], port);

    // We listen for incoming requests
    while (true) {
        acceptSocket = accept(serverSocket, NULL, NULL);
        if (acceptSocket < 0) {
            cout << "accept meesage failed: " << strerror(errno) << endl;
            continue;
        }
        rbyteCount = recv(acceptSocket, incoming, sizeof(incoming), 0);
        if (rbyteCount < 0 && rbyteCount != 6) {
            cout << "Server recv error: " << strerror(errno) << endl;
        } else {
            unsigned short int value;
            unsigned short int didWrite;
            // Process message here
            printf("[S] Server recv %d, %d, %d\n", incoming[0], incoming[1], incoming[2]);
            switch (incoming[0]) {
                case READ_REQUEST:  // Read
                    value = readStore(incoming[1]);
                    send(acceptSocket, &value, sizeof(value), 0);
                    break;
                case WRITE_REQUEST:  // Write
                    didWrite = writeStore(incoming[1], incoming[2]);
                    send(acceptSocket, &didWrite, sizeof(didWrite), 0);
                    break;
                case RECOVER_REQUEST:  // Recovery Request
                    // message = {r, hostIndex, null}
                    thread(recoverHost, incoming[1], acceptSocket).detach();
                    continue;  // So we dont close the socket, it will be closed once the thread finishes
                    break;
                case REPLICATE_WRITE:                             // Replicate write command
                    writeStore(incoming[1], incoming[2], false);  // we write it without propigation
                    send(acceptSocket, &incoming[1], sizeof(incoming[1]), 0);
                    break;
                default:
                    cout << "Invalid message type" << endl;
            }
            close(acceptSocket);
        }
    }
}

void commandThread() {
    char cmd;
    unsigned short int value;
    printf("Commands Read, Write, Print, Count, Generate, Quit, XRecover [r,w,p,c,g,q,x]:\n");
    while (true) {
        cmd = getChar();
        cout << endl;
        switch (cmd) {
            case 'r':
                // Read
                value = readStore(hostIndex);
                cout << "Value: " << value << endl;
                break;
            case 'w':
                // Write
                writeStore(hostIndex, randNum(1, 100));
                break;
            case 'p':
                // Print store values
                for (int i = 0; i < storeIndex; i++) {
                    cout << store[i][0] << ":" << store[i][1] << ":" << hashKey(store[i][0]) + 1 << endl;
                }
                break;
            case 'g':  // Generate
                for (int i = 0; i < 20; i++) {
                    writeStore(randNum(1, 100) * 7 + hostIndex, randNum(1, 1000));
                }
                break;
            case 'c':
                // count
                cout << "Store size: " << storeIndex << endl;
                break;
            case 'q':
                // Quit
                close(serverSocket);
                exit(0);
                break;
            case 'x':
                // Recover
                thread(recoverKeys).detach();
                break;
            default:
                cout << "Invalid command" << endl;
        }
    }
}

void segfaultHandler(int signal, siginfo_t *info, void *ucontext) {
    std::cerr << "Segmentation fault at address: " << info->si_addr << std::endl;

    // Optionally print a backtrace
    void *array[10];
    size_t size = backtrace(array, 10);
    backtrace_symbols_fd(array, size, STDERR_FILENO);

    exit(1);
}

int main(int argc, char *argv[]) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = segfaultHandler;
    sa.sa_flags = SA_SIGINFO;  // Important: get more info

    sigaction(SIGSEGV, &sa, nullptr);

    // if no argument is passed, exit
    if (argc < 2) {
        cout << "Project 3 by: Osamah Alzacko & Anurag" << endl;
        cout << "Servers we are going to use: DC01, DC02, DC03, DC04, DC05, DC06, DC07" << endl;
        cout << "Its hardcoded, so we need to use those servers." << endl;
        cout << "Usage: ./server {id}" << endl;
        cout << "id: [1-5] has to be from 1 to 5 for each process" << endl;
        return 1;
    }
    // get first agrument as id
    dcId = atoi(argv[1]);

    // Validate input
    if (dcId < 1 || dcId > 5) {
        cout << "Invalid id" << endl;
        return 1;
    }

    // Init variables
    hostIndex = dcId - 1;

    // First of all, we run the server
    thread th1(socketServer);

    // Run command thread
    thread th2(commandThread);

    usleep(100 * 1000);
    // Start boradcasting messages to other processes
    // Wait for the thread to finish
    th1.join();
    th2.join();
    return 0;
}
