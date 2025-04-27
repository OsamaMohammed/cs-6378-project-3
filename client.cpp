/*
Sample commands:-
For read:- ./client [server key]
For write:- ./client [server key] [message]
*/

#include <arpa/inet.h>   // For socket programming (inet_addr, htons, etc.)
#include <sys/socket.h>  // For creating and managing sockets
#include <unistd.h>      // For POSIX system calls, used to invoke sleep and usleep

#include <array>    // For array operations
#include <chrono>   // clock to seed for random value
#include <cstdlib>  // For general utilities , random time before send
#include <ctime>    // For time-related functions , to get time for scheduling
#include <fstream>  // For file handling ,used to read config file
#include <iostream>
#include <vector>  // For vector operations

using namespace std;

#define PORT 4430
#define YELLOW "\033[1;33m"  // Yellow
#define GREEN "\033[32m"     // Green
#define RED "\033[31m"       // Red
#define RESET "\033[0m"      // RESET

vector<string> readConfig(const string& filename) {
    vector<string> config;
    ifstream file(filename);
    if (!file) {
        cout << "File not found" << endl;
        exit(1);
    }
    string line;
    while (getline(file, line)) {
        config.push_back(line.substr(line.find('=') + 1));
    }
    file.close();
    return config;
}

// 0 will be received if no value found
void readMessage(int serverSocket, array<unsigned short int, 3> msg) {
    unsigned short int buffer;
    ssize_t bytesRead = recv(serverSocket, &buffer, sizeof(buffer), 0);
    if (bytesRead == -1) {
        cout << RED << "Read failed" << RESET << endl;
    } else if (bytesRead == 0) {
        cout << "Connection closed by peer" << endl;
    } else {
        if (msg[0] == 1) {
            cout << GREEN << "Received value for key " << RESET << msg[1] << " = " << buffer << endl;
        } else {
            if (buffer == 1) {
                cout << GREEN << "Write acknowledgement received for " << RESET << msg[1] << GREEN << " with value "
                     << RESET << msg[2] << endl;
            } else {
                cout << RED << "Write failed at server side for " << RESET << msg[1] << RED << " with value " << RESET
                     << msg[2] << endl;
            }
        }
    }
    close(serverSocket);
}

int sendConnectionRequests(string serverIP, int serverId) {
    struct sockaddr_in targetServerAddress;
    targetServerAddress.sin_family = AF_INET;
    targetServerAddress.sin_addr.s_addr = inet_addr(serverIP.c_str());
    targetServerAddress.sin_port = htons(PORT);
    int targetSocket = socket(AF_INET, SOCK_STREAM, 0);
    // special feature for immediate reuse of port
    // int opt = 1;
    // setsockopt(targetSocket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    if (connect(targetSocket, (const struct sockaddr*)&targetServerAddress, sizeof(targetServerAddress)) < 0) {
        targetSocket = 0;
    }
    return targetSocket;
}

/*
param 1 = ./client
param 2 = action (read or write), it is needed to handle a few cases
param 3 = which server to read and also tells the key (optional for write)
messages to be sent always remain randomised

new changes:-
->key not mandatory for write (done)
->remove action, read only if total arguments are two including exe file name i.e. ./client 107, write will work with
anything(done)
->sent message should be of six bytes(done)
->on write server will send acknowledgement, after client receives acknowledgement(done)
->randomise read for 3 associated servers
->on write value cant be zero(done)
// test 500 value
// write 2 params

*/
int main(int argc, char* argv[]) {
    srand(chrono::duration_cast<chrono::microseconds>(chrono::high_resolution_clock::now().time_since_epoch()).count());
    vector<string> serverIPs = readConfig("config.txt");
    int serverSocket, serverId, retries, value;
    unsigned short int key;
    array<unsigned short int, 3> msg = {0, 0, 0};
    serverSocket = 0;
    retries = (argc == 2) ? 3 : 2;
    if (argc == 2) {
        key = stoi(argv[1]);
        serverId = key % 7;
        int options[] = {serverId, serverId + 1, serverId + 2};
        srand(time(0));
        serverId = options[rand() % 3];
    } else if (argc == 1) {
        srand(time(0));
        key = (rand() % 65535) + 1;
        value = rand() % 65530;
        serverId = key % 7;
    } else {
        key = stoi(argv[1]);
        value = stoi(argv[2]);
        serverId = key % 7;
    }
    for (int i = 0; i < retries; i++) {
        serverSocket = sendConnectionRequests(serverIPs[serverId], serverId);
        if (serverSocket > 0) {
            cout << "Connected to server " << serverId << endl;
            break;
        } else {
            serverId = (key + i + 1) % 7;
        }
    }
    if (serverSocket <= 0) {
        cout << RED << "Connections to all applicable servers failed" << RESET << endl;
        return 0;
    }
    if (argc == 2) {
        msg[0] = 1;  // read
        msg[1] = key;
        size_t msgSize = sizeof(unsigned short int) * 3;
        if (send(serverSocket, msg.data(), msgSize, 0) < 0) {
            cout << RED << "Failure while sending reading Request" << RESET << endl;
        } else {
            cout << YELLOW << "Request Sent to server " << RESET << serverId << YELLOW << "!" << RESET << endl;
            readMessage(serverSocket, msg);
        }
    } else {
        msg[0] = 2;  // write
        msg[1] = key;
        msg[2] = value;
        size_t msgSize = sizeof(unsigned short int) * 3;
        if (send(serverSocket, msg.data(), msgSize, 0) < 0) {
            cout << RED << "Write to Server " << RESET << serverId << RED << " failed" << RESET << endl;
        } else {
            cout << YELLOW << "Write request sent with key " << RESET << msg[1] << YELLOW << " and msg " << RESET
                 << msg[2] << YELLOW << " to server " << RESET << serverId << endl;
            readMessage(serverSocket, msg);
        }
    }
    return 0;
}

/*

// remove read/write param judge by argc
// test the sending contain it in 6 bytes
// write acknowledgement , usigned short int 0 or 1
g++ -pthread -o server main.cpp -std=c++11 -fnon-call-exceptions

*/