#include <arpa/inet.h>
#include <netinet/in.h>
#include <pthread.h>
#include <sys/socket.h>
#include <termios.h>
#include <unistd.h>

#include <cstdio>
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

// Write key to store
void writeStore(unsigned short int key, unsigned short int value) {
    for (int i = 0; i < storeIndex; i++) {
        if (store[i][0] == key) {
            store[i][1] = value;
            return;
        }
    }
    storeMutex.lock();
    store[storeIndex] = {key, value};
    storeIndex++;
    storeMutex.unlock();
    cout << "writing " << key << ":" << value << endl;
    return;
}

// Socket server initialized
void socketServer() {
    int serverSocket = socket(AF_INET, SOCK_STREAM, 0);
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
            // Process message here
            switch (incoming[0]) {
                case 1:  // Read
                    value = readStore(incoming[1]);
                    send(acceptSocket, &value, sizeof(value), 0);
                    break;
                case 2:  // Write
                    writeStore(incoming[1], incoming[2]);
                    send(acceptSocket, &incoming[1], sizeof(incoming[1]), 0);
                    break;
                case 3:  // Recovery
                    // TODO
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
    while (true) {
        cout << "Commands Read, Write, Print, Count, Generate, Quit [r,w,p,c,g,q]:";
        cmd = getChar();
        cout << endl;
        switch (cmd) {
            case 'r':
                // Read
                value = readStore(1);
                cout << "Value: " << value << endl;
                break;
            case 'w':
                // Write
                writeStore(1, randNum(1, 100));
                break;
            case 'p':
                // Print store values
                for (int i = 0; i < storeIndex; i++) {
                    cout << store[i][0] << ":" << store[i][1] << endl;
                }
                break;
            case 'g':  // Generate
                for (int i = 0; i < 2000; i++) {
                    writeStore(randNum(1, 1000), randNum(1, 1000));
                }
                break;
            case 'c':
                // count
                cout << "Store size: " << storeIndex << endl;
                break;
            case 'q':
                // Quit
                exit(0);
                break;
            default:
                cout << "Invalid command" << endl;
        }
    }
}

int main(int argc, char *argv[]) {
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
