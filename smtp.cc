#include <stdlib.h>
#include <stdio.h>
#include <sys/socket.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <strings.h>
#include <iostream>
#include <pthread.h>
#include <vector>
#include <algorithm>
#include <string.h>
#include <signal.h>
#include <fcntl.h>
#include <dirent.h>
#include <regex>
#include <chrono>
#include <ctime>
#include <pthread.h>
#include <sys/file.h>
#include <map>
//
#include "analyzer.h"

/*global variables*/
int p_flag = 0;
int v_flag = 0;
int port = 2500;
volatile bool shuttingDown = false;
const int maxConnections = 105;
volatile int numConn = 0;
volatile int fds[maxConnections];
std::vector<std::string> mailboxes;
std::regex pattern("<[.a-zA-Z0-9_]+@[a-zA-Z0-9_]+>");
std::map<std::string, pthread_mutex_t> mutexMap;
std::string emailDirectory;

/*generic function to write to stderr if v flag is on*/
void DebugLog(int fd, const char *message, bool server)
{
  if (v_flag)
  {
    if (server)
    {
      fprintf(stderr, "[%d] S: %s", fd, message);
    }
    else
    {
      fprintf(stderr, "[%d] S: %s\r\n", fd, message);
    }
  }
}
/*
helper method to write to a socket file descriptor
keeps writing until all the bytes are written
*/
bool DoWrite(int fd, const char *buf, int len)
{
  int sent = 0;
  while (sent < len)
  {
    int n = write(fd, &buf[sent], len - sent);
    if (n < 0)
    {
      return false;
    }
    sent += n;
  }
  return true;
}

/*
custom signal handler to close all open socket connections on SIGINT
*/
void SignalHandler(int signal)
{
  shuttingDown = true;
  for (int fd : fds)
  {
    /*iterate through all the file descriptors, set to non-blocking and close it*/
    if (fd != 0)
    {
      int status = fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
      const char *buffer = "-ERR Server shutting down\r\n";
      DoWrite(fd, buffer, strlen(buffer));
      DebugLog(fd, buffer, true);
      close(fd);
    }
  }
  exit(signal);
}
/*function to check whether a function is valid, i.e. it has an associated .mbox file in the provided directory*/
bool ValidEmail(std::string email)
{
  std::cout << email << "\n";
  size_t UserPos = email.find('@');
  std::string user = email.substr(0, UserPos);
  std::cout << user << "\n";
  std::string domain = email.substr(UserPos + 1, email.length());
  domain.pop_back();
  std::cout << domain << "\n";
  for (int i = 0; i < mailboxes.size(); i++)
  {
    std::string curMail = mailboxes[i];
    curMail.erase(curMail.length() - 5);
    std::cout << curMail << "\n";
    if ((curMail == user) && (domain == "localhost"))
    {
      return true;
    }
  }
  return false;
}
/*use regex to extract the from inside the <> brackets*/
std::string FindEmail(std::string email)
{
  auto words_begin = std::sregex_iterator(email.begin(), email.end(), pattern);
  auto words_end = std::sregex_iterator();

  for (std::sregex_iterator i = words_begin; i != words_end; ++i)
  {
    std::smatch match = *i;
    std::string match_str = match.str();
    return match_str;
  }
  return "";
}

/*worker function that is spawned for each thread*/
void *WorkerFunction(void *arg)
{
    /*states: con_est -> initial -> trans_begin -> recep_started -> data*/
    int clientSocket = *((int *)arg);
    std::vector<char> buffer;
    char temp_buffer[1024];
    int bytes_read = 0;

    std::vector<std::string> recipients;
    std::string state = "con_est";
    std::string mailFrom = "";
    std::vector<char> dataBuffer;
    int fd = -1;
    std::string filename = "";

    /*read through the client socket*/
    while ((bytes_read = read(clientSocket, temp_buffer, sizeof(temp_buffer))) > 0)
    {
        for (int i = 0; i < bytes_read; i++)
        {
            /*data state*/
            if (state == "data")
            {
                if (temp_buffer[i] == '\n' && dataBuffer.size() >= 4)
                {
                    std::string endOfData(dataBuffer.end() - 4, dataBuffer.end());
                    std::cout << "End of Data Check: " << endOfData << "\n";

                    if (endOfData == "\r\n.\r")
                    {
                        ////////////

                        // Start analyzing
                        analyzeEmailContent(dataBuffer);

                        ////////////

                        int sent = 0;
                        for (std::string& recipient : recipients)
                        {
                            size_t atPos = recipient.find('@');
                            if (atPos == std::string::npos || atPos < 1)
                            {
                                std::cerr << "Invalid email format for recipient: " << recipient << std::endl;
                                continue;  // Skip if the email format is incorrect
                            }

                            std::string user = recipient.substr(1, atPos - 1) + ".mbox";  // Ensure the key matches initialization
                            std::string path = emailDirectory + "/" + user;
                            std::cout << "Attempting to send to: " << recipient << " | File: " << path << "\n";

                            bool lockAcquired = false;
                            for (int attempt = 0; attempt < 5 && !lockAcquired; ++attempt)
                            {
                                fd = open(path.c_str(), O_WRONLY | O_APPEND);
                                if (fd == -1)
                                {
                                    std::cerr << "Attempt " << attempt + 1 << ": Unable to open file " << path << ", errno: " << strerror(errno) << std::endl;
                                    continue;
                                }

                                int fileLockStatus = flock(fd, LOCK_EX | LOCK_NB);
                                if (fileLockStatus != 0)
                                {
                                    std::cerr << "Attempt " << attempt + 1 << ": Failed to acquire file lock, errno: " << strerror(errno) << std::endl;
                                    close(fd);
                                    continue;
                                }

                                auto it = mutexMap.find(user);
                                if (it == mutexMap.end())
                                {
                                    std::cerr << "Mutex for user " << user << " not found." << std::endl;
                                }
                                else
                                {
                                    int mutexLockStatus = pthread_mutex_trylock(&it->second);
                                    if (mutexLockStatus != 0)
                                    {
                                        std::cerr << "Failed to acquire mutex lock for user: " << user << ", error: " << strerror(mutexLockStatus) << std::endl;
                                    }
                                    else
                                    {
                                        std::cout << "Mutex lock acquired for user: " << user << std::endl;
                                        lockAcquired = true;
                                    }
                                }

                                if (lockAcquired)
                                {
                                    // Proceed with sending email logic
                                    auto time = std::chrono::system_clock::now();
                                    std::time_t curTime = std::chrono::system_clock::to_time_t(time);
                                    std::string header = "From " + mailFrom + " " + std::ctime(&curTime) + "\r\n";
                                    write(fd, header.c_str(), header.size());
                                    std::string message(dataBuffer.begin(), dataBuffer.end() - 2);
                                    write(fd, message.c_str(), message.size());

                                    // Unlock and close
                                    pthread_mutex_unlock(&it->second);
                                    flock(fd, LOCK_UN);
                                    close(fd);
                                    std::cout << "Email sent and recorded for " << recipient << std::endl;
                                    sent++;  // Update the count of successfully sent emails
                                }
                                else
                                {
                                    std::cerr << "Failed to lock resources for " << recipient << " after multiple attempts.\n";
                                }
                            }
                        }

                        if (sent > 0)
                        {
                            std::cout << "Emails successfully sent to " << sent << " recipients.\n";
                            state = "initial";
                            recipients.clear();
                            mailFrom = "";
                            const char* buf1 = "250 OK\r\n";
                            DoWrite(clientSocket, buf1, strlen(buf1));
                            DebugLog(clientSocket, buf1, true);
                        }
                        else
                        {
                            state = "initial";
                            recipients.clear();
                            mailFrom = "";
                            const char* buf2 = "550 Failed to send to any recipient\r\n";
                            DoWrite(clientSocket, buf2, strlen(buf2));
                            DebugLog(clientSocket, buf2, true);
                        }
                    }
                    else
                    {
                        dataBuffer.push_back(temp_buffer[i]);
                    }
                }
                else
                {
                    dataBuffer.push_back(temp_buffer[i]);
                }
                continue;
            }

            /*Hit <CRLF>*/
            if (temp_buffer[i] == '\n' && !buffer.empty() && buffer.back() == '\r')
            {
                std::string message(buffer.begin(), buffer.end() - 1);
                const char *buf = message.c_str();
                DebugLog(clientSocket, buf, false);
                buffer.clear();

                /*Extracting the command*/
                size_t spacePos = message.find(' ');
                std::string command;
                if (spacePos != -1)
                {
                    command = message.substr(0, spacePos);
                }
                else
                {
                    command = message;
                }

                for (int i = 0; i < command.length(); i++)
                {
                    command[i] = std::tolower(command[i]);
                }

                /*Reply for HELO*/
                if (command == "helo")
                {
                    if (state == "initial" || state == "con_est")
                    {
                        if (spacePos != -1)
                        {
                            std::string restCommand = message.substr(spacePos, message.length());
                            std::regex pattern(" .+");
                            if (std::regex_match(restCommand, pattern))
                            {
                                state = "initial";
                                const char *buf = "250 localhost\r\n";
                                DoWrite(clientSocket, buf, strlen(buf));
                                DebugLog(clientSocket, buf, true);
                            }
                        }
                        else
                        {
                            const char *buf = "502 Domain Name Missing\r\n";
                            DoWrite(clientSocket, buf, strlen(buf));
                            DebugLog(clientSocket, buf, true);
                        }
                    }
                    else
                    {
                        const char *buf = "503 Bad Sequence of commands\r\n";
                        DoWrite(clientSocket, buf, strlen(buf));
                        DebugLog(clientSocket, buf, true);
                    }
                }

                /*Response to MAIL FROM: or RCPT TO: */
                else if (command == "mail" || command == "rcpt")
                {
                    size_t colonPos = message.find(':');
                    if (colonPos == -1)
                    {
                        const char *buf = "503 Unknown Command\r\n";
                        DoWrite(clientSocket, buf, strlen(buf));
                        DebugLog(clientSocket, buf, true);
                        continue;
                    }
                    command = message.substr(0, colonPos);
                    for (int i = 0; i < command.length(); i++)
                    {
                        command[i] = std::tolower(command[i]);
                    }

                    /*MAIL FROM command response*/
                    if (command == "mail from")
                    {
                        std::string restCommand = message.substr(colonPos, message.length());
                        if (state == "initial")
                        {
                            /*check the validity of each email to make sure it follows the correct format*/
                            std::cout << "Mail from: " << restCommand << "\n";
                            if (std::regex_search(restCommand, pattern))
                            {
                                state = "trans_begin";
                                mailFrom = FindEmail(restCommand);
                                const char *buf = "250 OK\r\n";
                                DoWrite(clientSocket, buf, strlen(buf));
                                DebugLog(clientSocket, buf, true);
                            }
                            else
                            {
                                const char *buf = "502 Incorrect email format\r\n";
                                DoWrite(clientSocket, buf, strlen(buf));
                                DebugLog(clientSocket, buf, true);
                            }
                        }
                        else
                        {
                            const char *buf = "503 Bad Sequence of Commands\r\n";
                            DoWrite(clientSocket, buf, strlen(buf));
                            DebugLog(clientSocket, buf, true);
                        }
                    }

                    /*RCPT To command response*/
                    else if (command == "rcpt to")
                    {
                        std::string restCommand = message.substr(colonPos, message.length());
                        if (state == "trans_begin" || state == "recep_start")
                        {
                            /*check the format of the rcpt emails and check whether it exists in the mailbox*/
                            if (std::regex_search(restCommand, pattern))
                            {
                                std::string recep = FindEmail(restCommand);
                                std::cout << recep << "\n";
                                if (ValidEmail(recep.substr(1, recep.length() - 1)))
                                {
                                    recipients.push_back(recep);
                                    state = "recep_start";
                                    const char *buf = "250 OK\r\n";
                                    DoWrite(clientSocket, buf, strlen(buf));
                                    DebugLog(clientSocket, buf, true);
                                }
                                else
                                {
                                    const char *buf = "550 No such user here\r\n";
                                    DoWrite(clientSocket, buf, strlen(buf));
                                    DebugLog(clientSocket, buf, true);
                                }
                            }
                            else
                            {
                                const char *buf = "502 Incorrect email format\r\n";
                                DoWrite(clientSocket, buf, strlen(buf));
                                DebugLog(clientSocket, buf, true);
                            }
                        }
                        else
                        {
                            const char *buf = "503 Bad Sequence of Commands\r\n";
                            DoWrite(clientSocket, buf, strlen(buf));
                            DebugLog(clientSocket, buf, true);
                        }
                    }
                }

                /*Response to QUIT command*/
                else if (command == "quit" && message.length() == 4)
                {
                    numConn -= 1;
                    for (int i = 0; i < sizeof(fds) / sizeof(int); i++)
                    {
                        if (fds[i] == clientSocket)
                        {
                            fds[i] = 0;
                            break;
                        }
                    }
                    /*cleanup after quit is issued*/
                    const char *buf = "221 localhost Service closing transmission channel\r\n";
                    DoWrite(clientSocket, buf, strlen(buf));
                    DebugLog(clientSocket, buf, true);
                    close(clientSocket);
                    flock(fd, LOCK_UN);
                    pthread_mutex_unlock(&(mutexMap[filename]));
                    pthread_exit(NULL);
                }

                /*Response to NOOP command*/
                else if (command == "noop" && message.length() == 4)
                {
                    if (state == "con_est")
                    {
                        const char *buf = "503 Bad Sequence of commands\r\n";
                        DoWrite(clientSocket, buf, strlen(buf));
                        DebugLog(clientSocket, buf, true);
                    }
                    else
                    {
                        const char *buf = "250 OK\r\n";
                        DoWrite(clientSocket, buf, strlen(buf));
                        DebugLog(clientSocket, buf, true);
                    }
                }

                /*Response to RSET command*/
                else if (command == "rset" && message.length() == 4)
                {
                    if (state == "con_est")
                    {
                        const char *buf = "503 Bad Sequence of commands\r\n";
                        DoWrite(clientSocket, buf, strlen(buf));
                        DebugLog(clientSocket, buf, true);
                    }
                    else
                    {
                        state = "initial";
                        recipients.clear();
                        mailFrom = "";
                        const char *buf = "250 OK\r\n";
                        DoWrite(clientSocket, buf, strlen(buf));
                        DebugLog(clientSocket, buf, true);
                    }
                }
                else if (command == "data")
                {
                    if (state == "recep_start")
                    {
                        /*if data command is issued, then set state to data*/
                        state = "data";
                        dataBuffer.clear();
                        const char *buf = "354 Start mail input; end with <CRLF>.<CRLF>\r\n";
                        DoWrite(clientSocket, buf, strlen(buf));
                        DebugLog(clientSocket, buf, true);
                    }
                    else
                    {
                        const char *buf = "503 Bad Sequence of Commands\r\n";
                        DoWrite(clientSocket, buf, strlen(buf));
                        DebugLog(clientSocket, buf, true);
                    }
                }
                else
                {
                    const char *buf = "502 Unknown Command\r\n";
                    DoWrite(clientSocket, buf, strlen(buf));
                    DebugLog(clientSocket, buf, true);
                }
            }
            else
            {
                buffer.push_back(temp_buffer[i]);
            }
        }
    }
    pthread_exit(NULL);
}

void initializeMutexForUser(const std::string& user) {
    pthread_mutex_t newMutex;
    if (pthread_mutex_init(&newMutex, NULL) != 0) {
        std::cerr << "Failed to initialize mutex for user: " << user << std::endl;
    } else {
        mutexMap[user] = newMutex;
        std::cout << "Mutex initialized for user: " << user << std::endl;
    }
}




/////////////////////////////////////////////


int main(int argc, char *argv[])
{

  /*intialize sigaction with sa handler set to 0*/
  struct sigaction sa;
  sa.sa_handler = SignalHandler;
  sa.sa_flags = 0;
  sigemptyset(&sa.sa_mask);

  if (sigaction(SIGINT, &sa, NULL) == -1)
  {
    fprintf(stderr, "SIGACTION Error");
    return 1;
  }

  /*Checking command line arguments*/
  int c;
  while ((c = getopt(argc, argv, "avp:")) != -1)
  {
    switch (c)
    {
    case 'a':
      fprintf(stderr, "Martin Sander\r\n");
      exit(0);
    case 'v':
      v_flag = 1;
      break;
    case 'p':
      p_flag = 1;
      if (isdigit(*optarg))
      {
        port = atoi(optarg);
      }
      break;
    }
  }

  if (optind >= argc)
  {
    fprintf(stderr, "No directory provided!\n");
    return 1;
  }

  emailDirectory = argv[optind];

/*parse through the directory and each file to a vector to use later*/
    DIR *dir;
    struct dirent *dp;
    if ((dir = opendir(emailDirectory.c_str())) == NULL) {
        fprintf(stderr, "Failed to open directory!");
        return 1;
    }

    while ((dp = readdir(dir)) != NULL) {
        if (strcmp(dp->d_name, ".") == 0 || strcmp(dp->d_name, "..") == 0) {
            continue;
        }

        std::string mailboxName(dp->d_name);
        mailboxes.push_back(mailboxName);
        initializeMutexForUser(mailboxName); // Initialize a mutex for each mailbox
    }


    closedir(dir);

  /*Create a new socket*/
  int sockfd = socket(PF_INET, SOCK_STREAM, 0);
  if (sockfd < 0)
  {
    fprintf(stderr, "Error creating socket");
    return 1;
  }

  int opt = 1;
  int ret = setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
  if (ret < 0) {
      fprintf(stderr, "Error setting socket options: %s\n", strerror(errno));
      return 1;
  }

  /*bind to the specified port or default*/
  struct sockaddr_in servaddr;
  bzero(&servaddr, sizeof(servaddr));
  servaddr.sin_family = AF_INET;
  servaddr.sin_addr.s_addr = htons(INADDR_ANY);
  servaddr.sin_port = htons(port);
  if (bind(sockfd, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0)
  {
    fprintf(stderr, "Fail to bind\n");
    return 1;
  }

  if (listen(sockfd, 100) < 0)
  {
    fprintf(stderr, "Error listening on port.");
    return 1;
  }

  while (true)
  {
    /*accept a new connection*/
    struct sockaddr_in clientaddr;
    socklen_t clientaddrlen = sizeof(clientaddr);
    int commd_fd = accept(sockfd, (struct sockaddr *)&clientaddr, &clientaddrlen);
    /*if maximum concurrent connections exceeded*/
    if (numConn >= 105)
    {
      const char *buffer = "503 Maximum concurrent connections exceeded. Closing connection!\r\n";
      DoWrite(commd_fd, buffer, strlen(buffer));
      close(commd_fd);
      if (v_flag)
      {
        fprintf(stderr, "[%d] S: %s", commd_fd, buffer);
        fprintf(stderr, "[%d] Connection closed\r\n", commd_fd);
      }
      continue;
    }
    /*if there is space*/
    else
    {
      const char *buffer = "220 localhost Simple Mail Transfer Service Ready\r\n";
      DoWrite(commd_fd, buffer, strlen(buffer));
      DebugLog(commd_fd, buffer, true);
    }
    /*find a place to store the new fd in the array*/
    for (int i = 0; i < sizeof(fds) / sizeof(int); i++)
    {
      if (fds[i] == 0)
      {
        fds[i] = commd_fd;
        /*if shutting down flag is already set then immediately close the new connection*/
        if (shuttingDown)
        {
          const char *buffer = "-ERR Server shutting down\r\n";
          DoWrite(commd_fd, buffer, strlen(buffer));
          close(commd_fd);
          if (v_flag)
          {
            fprintf(stderr, "[%d] S: %s", commd_fd, buffer);
          }
          break;
        }
        break;
      }
    }

    pthread_t thread;
    int ret = pthread_create(&thread, NULL, WorkerFunction, (void *)&commd_fd);
    pthread_detach(thread);
  }

  close(sockfd);

  return 0;
}
