#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>
#include <iterator>
#include <iomanip>
#include <algorithm>

// Helper function to send a command to the server and print the response
void send_command(int sockfd, const char *cmd) {
    write(sockfd, cmd, strlen(cmd));
    printf("Sent: %s", cmd); // Print the command sent for debugging
}

// Base64 encoding function
std::string base64_encode(const std::string &in) {
    std::string out;
    static const char lookup[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    int val = 0, valb = -6;
    for (unsigned char c : in) {
        val = (val << 8) + c;
        valb += 8;
        while (valb >= 0) {
            out.push_back(lookup[(val >> valb) & 0x3F]);
            valb -= 6;
        }
    }
    if (valb > -6) out.push_back(lookup[((val << 8) >> (valb + 8)) & 0x3F]);
    while (out.size() % 4) out.push_back('=');
    return out;
}

// Function to read file contents
std::string read_file(const std::string &filename) {
    std::ifstream file(filename, std::ios::binary);
    if (!file) {
        std::cerr << "Error: Cannot open file " << filename << std::endl;
        return "";
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

int main(int argc, char* argv[]) {
    // Create a socket
    if (argc != 2) {
	std::cout << "Input Error: You need to enter a file-path to send to the server" << std::endl;
    }

    std::string filename = argv[1];
    std::string file_content = read_file(filename);

    // Check if file_content is empty and terminate if true
    if (file_content.empty()) {
        std::cerr << "Error: File content is empty, file may not exist or cannot be read" << std::endl;
        return 1; // Exit with an error code
    }    

    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("Error creating socket");
        return 1;
    }

    // Server address
    struct sockaddr_in servaddr;
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(2500);
    servaddr.sin_addr.s_addr = inet_addr("127.0.0.1");

    // Connect to the server
    if (connect(sockfd, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0) {
        perror("Connection failed");
        return 1;
    }

    send_command(sockfd, "HELO localhost\r\n");
    send_command(sockfd, "MAIL FROM:<martin@localhost>\r\n");
    send_command(sockfd, "RCPT TO:<sriram@localhost>\r\n");
    send_command(sockfd, "DATA\r\n");
    sleep(1);

    // Wait for the server to respond with "354 Start mail input; end with <CRLF>.<CRLF>"
    char buffer[1024];
    int n = read(sockfd, buffer, sizeof(buffer) - 1);
    if (n > 0) {
        buffer[n] = '\0';
        printf("Received: %s\n", buffer);
    }

    // Send the file as an attachment
    std::string encoded_content = base64_encode(file_content);
    std::string attachment_header = "Content-Type: application/octet-stream\r\nContent-Disposition: attachment; filename=\"" + filename + "\"\r\nContent-Transfer-Encoding: base64\r\n\r\n";    
    
    send_command(sockfd, attachment_header.c_str());
    send_command(sockfd, encoded_content.c_str());
    send_command(sockfd, "\r\n--boundary--\r\n");

    // End of email data
    send_command(sockfd, ".\r\n");

    // Close the session
    send_command(sockfd, "QUIT\r\n");

    close(sockfd);

    return 0;
}
