#include "nlohmann/json.hpp"
#include <iostream>
#include <string>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

using json = nlohmann::json;

int main() {
    int sock = socket(AF_INET, SOCK_STREAM, 0);

    sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(12345);
    server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    if (connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        std::cout << "连接失败\n";
        return -1;
    }
    std::cout << "连接成功" << std::endl;

    while (true) {
        json req;
        int opt;
        std::cin >> opt;
        if (opt == 1) {
            req["action"] = "login";
            std::string ac, pwd;
            std::cin >> ac >> pwd;
            req["username"] = ac;
            req["password"] = pwd;
        }
        else if (opt == 2) {
            req["action"] = "register";
            std::string ac, pwd;
            std::cin >> ac >> pwd;
            req["username"] = ac;
            req["password"] = pwd;
        }
        else if (opt == 3) { req["action"] = "exit"; }
        else if (opt == 4) { req["action"] = "getbalance"; }

        // ================= 核心修复：发送端封包 =================
        std::string s = req.dump();
        uint32_t req_len = htonl(s.size()); // 将长度转为网络字节序
        std::string packet;
        packet.append((char*)&req_len, 4);  // 1. 先压入 4 字节的长度包头
        packet.append(s);                   // 2. 再压入 JSON 包体
        
        std::cout << "准备发送请求包, 长度: " << packet.size() << " bytes..." << std::endl;
        
        int s_res = send(sock, packet.data(), packet.size(), 0);
        
        std::cout << "实际已发出字节数: " << s_res << std::endl;

        // ================= 核心修复：接收端拆包 =================
        uint32_t reply_len_net = 0;
        int n = recv(sock, (char*)&reply_len_net, 4, 0); // 先严格读取 4 字节的长度信息
        if (n == 4) {
            uint32_t reply_len = ntohl(reply_len_net);   // 转回主机字节序
            
            std::string reply_str;
            reply_str.resize(reply_len);
            
            // 循环读取直到读满包体
            size_t total_read = 0;
            while(total_read < reply_len) {
                int r = recv(sock, &reply_str[total_read], reply_len - total_read, 0);
                if(r > 0) total_read += r;
                else break;
            }
            std::cout << "收到回复: " << reply_str << std::endl;
        } else {
            std::cout << "与服务器断开连接或接收包头失败" << std::endl;
            break;
        }
    }

    close(sock);
    return 0;
}