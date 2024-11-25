#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <unordered_map>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <thread>

using namespace std;

// ����HTTP״̬��
const int HTTP_OK = 200;
const int HTTP_BAD_REQUEST = 400;
const int HTTP_FORBIDDEN = 403;
const int HTTP_NOT_FOUND = 404;

// HTTP����ṹ��
struct HTTPRequest {
    string method;
    string uri;
    string version;
    unordered_map<string, string> headers;
};

// HTTP��Ӧ�ṹ��
struct HTTPResponse {
    int statusCode;
    string statusMessage;
    unordered_map<string, string> headers;
    string body;
};

// ����HTTP������
void parseRequestLine(const string& line, HTTPRequest& request) {
    istringstream iss(line);
    iss >> request.method >> request.uri >> request.version;
}

// ����HTTPͷ��
void parseHeaders(const string& headerStr, HTTPRequest& request) {
    istringstream iss(headerStr);
    string line;
    while (getline(iss, line) && line!= "\r") {
        size_t pos = line.find(':');
        if (pos!= string::npos) {
            string key = line.substr(0, pos);
            string value = line.substr(pos + 1);
            request.headers[key] = value;
        }
    }
}

// ����HTTP��Ӧ
void buildResponse(HTTPResponse& response, const string& docRoot) {
    // ����״̬������Ĭ����Ӧ��Ϣ
    if (response.statusCode == HTTP_OK) {
        response.statusMessage = "OK";
    } else if (response.statusCode == HTTP_BAD_REQUEST) {
        response.statusMessage = "Bad Request";
    } else if (response.statusCode == HTTP_FORBIDDEN) {
        response.statusMessage = "Forbidden";
    } else if (response.statusCode == HTTP_NOT_FOUND) {
        response.statusMessage = "Not Found";
    }

    // ����Serverͷ��
    response.headers["Server"] = "TritonHTTP/1.1";

    // �����200״̬�룬����Content-Type��Content-Lengthͷ��
    if (response.statusCode == HTTP_OK) {
        string fileExtension = getFileExtension(response.body);
        if (fileExtension == "html") {
            response.headers["Content-Type"] = "text/html";
        } else if (fileExtension == "jpg" || fileExtension == "png") {
            response.headers["Content-Type"] = "image/" + fileExtension;
        }
        response.headers["Content-Length"] = to_string(response.body.length());
    }
}

// ��ȡ�ļ���չ��
string getFileExtension(const string& filename) {
    size_t pos = filename.find_last_of('.');
    if (pos!= string::npos) {
        return filename.substr(pos + 1);
    }
    return "";
}

// ���URL�Ƿ�Ϸ�
bool isUrlValid(const string& uri, const string& docRoot) {
    // ����Ƿ�����Ƿ���·�������ַ� ".."
    if (uri.find("..")!= string::npos) {
        return false;
    }

    // ����Ƿ񳬳��ĵ���Ŀ¼
    string filePath = docRoot + uri;
    if (filePath.find(docRoot)!= 0) {
        return false;
    }

    return true;
}

// ����ͻ�������
void handleClientConnection(int clientSocket, const string& docRoot) {
    char buffer[1024];
    int bytesRead;

    // ��ȡ����
    string requestStr;
    while ((bytesRead = recv(clientSocket, buffer, sizeof(buffer), 0)) > 0) {
        requestStr.append(buffer, bytesRead);
        if (requestStr.find("\r\n\r\n")!= string::npos) {
            break;
        }
    }

    if (bytesRead < 0) {
        cerr << "Error reading request from client" << endl;
        close(clientSocket);
        return;
    }

    // ��������
    HTTPRequest request;
    istringstream iss(requestStr);
    string requestLine;
    getline(iss, requestLine);
    parseRequestLine(requestLine, request);

    // ��ȡ������ͷ��
    string headerStr;
    getline(iss, headerStr);
    parseHeaders(headerStr, request);

    // ���URL�Ϸ���
    if (!isUrlValid(request.uri, docRoot)) {
        HTTPResponse response;
        response.statusCode = HTTP_NOT_FOUND;
        buildResponse(response, docRoot);

        string responseStr = "HTTP/1.1 " + to_string(response.statusCode) + " " + response.statusMessage + "\r\n";
        for (const auto& header : response.headers) {
            responseStr += header.first + ": " + header.second + "\r\n";
        }
        responseStr += "\r\n";

        send(clientSocket, responseStr.c_str(), responseStr.length(), 0);
        close(clientSocket);
        return;
    }

    // ��������
    HTTPResponse response;
    if (request.method == "GET") {
        // ����GET����
        string filePath = docRoot + request.uri;
        if (request.uri == "/") {
            filePath += "index.html";
        }

        // ����ļ��Ƿ����
        ifstream file(filePath);
        if (file.is_open()) {
            // �ļ����ڣ���ȡ�ļ�������Ϊ��Ӧ��
            stringstream buffer;
            buffer << file.rdbuf();
            response.body = buffer.str();
            response.statusCode = HTTP_OK;
        } else {
            // �ļ������ڣ�����404 Not Found
            response.statusCode = HTTP_NOT_FOUND;
        }
    } else {
        // ��֧�ֵķ���������400 Bad Request
        response.statusCode = HTTP_BAD_REQUEST;
    }

    // ������Ӧ
    buildResponse(response, docRoot);

    // ������Ӧ
    string responseStr = "HTTP/1.1 " + to_string(response.statusCode) + " " + response.statusMessage + "\r\n";
    for (const auto& header : response.headers) {
        responseStr += header.first + ": " + header.second + "\r\n";
    }
    responseStr += "\r\n";
    responseStr += response.body;

    send(clientSocket, responseStr.c_str(), responseStr.length(), 0);

    // �رտͻ�������
    close(clientSocket);
}

int main(int argc, char* argv[]) {
    if (argc!= 3) {
        cerr << "Usage: " << argv[0] << " <port> <doc_root>" << endl;
        return 1;
    }

    int port = stoi(argv[1]);
    string docRoot = argv[2];

    // �����׽���
    int serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSocket < 0) {
        cerr << "Error creating socket" << endl;
        return 1;
    }

    // �󶨵�ַ�Ͷ˿�
    sockaddr_in serverAddress;
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_addr.s_addr = INADDR_ANY;
    serverAddress.sin_port = htons(port);

    if (bind(serverSocket, (sockaddr*)&serverAddress, sizeof(serverAddress)) < 0) {
        cerr << "Error binding socket" << endl;
        return 1;
    }

    // ��������
    if (listen(serverSocket, 5) < 0) {
        cerr << "Error listening for connections" << endl;
        return 1;
    }

    cout << "Server listening on port " << port << "..." << endl;

    while (true) {
        // ���ܿͻ�������
        sockaddr_in clientAddress;
        socklen_t clientAddressLength = sizeof(clientAddress);
        int clientSocket = accept(serverSocket, (sockaddr*)&clientAddress, &clientAddressLength);
        if (clientSocket < 0) {
            cerr << "Error accepting client connection" << endl;
            continue;
        }

        // �����̴߳���ͻ�������
        thread clientThread(handleClientConnection, clientSocket, docRoot);
        clientThread.detach();
    }

    // �رշ������׽���
    close(serverSocket);

    return 0;
}
