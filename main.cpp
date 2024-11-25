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

// 定义HTTP状态码
const int HTTP_OK = 200;
const int HTTP_BAD_REQUEST = 400;
const int HTTP_FORBIDDEN = 403;
const int HTTP_NOT_FOUND = 404;

// HTTP请求结构体
struct HTTPRequest {
    string method;
    string uri;
    string version;
    unordered_map<string, string> headers;
};

// HTTP响应结构体
struct HTTPResponse {
    int statusCode;
    string statusMessage;
    unordered_map<string, string> headers;
    string body;
};

// 解析HTTP请求行
void parseRequestLine(const string& line, HTTPRequest& request) {
    istringstream iss(line);
    iss >> request.method >> request.uri >> request.version;
}

// 解析HTTP头部
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

// 构建HTTP响应
void buildResponse(HTTPResponse& response, const string& docRoot) {
    // 根据状态码设置默认响应消息
    if (response.statusCode == HTTP_OK) {
        response.statusMessage = "OK";
    } else if (response.statusCode == HTTP_BAD_REQUEST) {
        response.statusMessage = "Bad Request";
    } else if (response.statusCode == HTTP_FORBIDDEN) {
        response.statusMessage = "Forbidden";
    } else if (response.statusCode == HTTP_NOT_FOUND) {
        response.statusMessage = "Not Found";
    }

    // 设置Server头部
    response.headers["Server"] = "TritonHTTP/1.1";

    // 如果是200状态码，设置Content-Type和Content-Length头部
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

// 获取文件扩展名
string getFileExtension(const string& filename) {
    size_t pos = filename.find_last_of('.');
    if (pos!= string::npos) {
        return filename.substr(pos + 1);
    }
    return "";
}

// 检查URL是否合法
bool isUrlValid(const string& uri, const string& docRoot) {
    // 检查是否包含非法的路径遍历字符 ".."
    if (uri.find("..")!= string::npos) {
        return false;
    }

    // 检查是否超出文档根目录
    string filePath = docRoot + uri;
    if (filePath.find(docRoot)!= 0) {
        return false;
    }

    return true;
}

// 处理客户端连接
void handleClientConnection(int clientSocket, const string& docRoot) {
    char buffer[1024];
    int bytesRead;

    // 读取请求
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

    // 解析请求
    HTTPRequest request;
    istringstream iss(requestStr);
    string requestLine;
    getline(iss, requestLine);
    parseRequestLine(requestLine, request);

    // 读取并解析头部
    string headerStr;
    getline(iss, headerStr);
    parseHeaders(headerStr, request);

    // 检查URL合法性
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

    // 处理请求
    HTTPResponse response;
    if (request.method == "GET") {
        // 处理GET请求
        string filePath = docRoot + request.uri;
        if (request.uri == "/") {
            filePath += "index.html";
        }

        // 检查文件是否存在
        ifstream file(filePath);
        if (file.is_open()) {
            // 文件存在，读取文件内容作为响应体
            stringstream buffer;
            buffer << file.rdbuf();
            response.body = buffer.str();
            response.statusCode = HTTP_OK;
        } else {
            // 文件不存在，返回404 Not Found
            response.statusCode = HTTP_NOT_FOUND;
        }
    } else {
        // 不支持的方法，返回400 Bad Request
        response.statusCode = HTTP_BAD_REQUEST;
    }

    // 构建响应
    buildResponse(response, docRoot);

    // 发送响应
    string responseStr = "HTTP/1.1 " + to_string(response.statusCode) + " " + response.statusMessage + "\r\n";
    for (const auto& header : response.headers) {
        responseStr += header.first + ": " + header.second + "\r\n";
    }
    responseStr += "\r\n";
    responseStr += response.body;

    send(clientSocket, responseStr.c_str(), responseStr.length(), 0);

    // 关闭客户端连接
    close(clientSocket);
}

int main(int argc, char* argv[]) {
    if (argc!= 3) {
        cerr << "Usage: " << argv[0] << " <port> <doc_root>" << endl;
        return 1;
    }

    int port = stoi(argv[1]);
    string docRoot = argv[2];

    // 创建套接字
    int serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSocket < 0) {
        cerr << "Error creating socket" << endl;
        return 1;
    }

    // 绑定地址和端口
    sockaddr_in serverAddress;
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_addr.s_addr = INADDR_ANY;
    serverAddress.sin_port = htons(port);

    if (bind(serverSocket, (sockaddr*)&serverAddress, sizeof(serverAddress)) < 0) {
        cerr << "Error binding socket" << endl;
        return 1;
    }

    // 监听连接
    if (listen(serverSocket, 5) < 0) {
        cerr << "Error listening for connections" << endl;
        return 1;
    }

    cout << "Server listening on port " << port << "..." << endl;

    while (true) {
        // 接受客户端连接
        sockaddr_in clientAddress;
        socklen_t clientAddressLength = sizeof(clientAddress);
        int clientSocket = accept(serverSocket, (sockaddr*)&clientAddress, &clientAddressLength);
        if (clientSocket < 0) {
            cerr << "Error accepting client connection" << endl;
            continue;
        }

        // 创建线程处理客户端连接
        thread clientThread(handleClientConnection, clientSocket, docRoot);
        clientThread.detach();
    }

    // 关闭服务器套接字
    close(serverSocket);

    return 0;
}
