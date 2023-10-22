#ifndef __influxdb_h_
#define __influxdb_h_

#include <iostream>
#include <vector>
#include <map>
#include <string.h>
#include <sstream>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>

class Influx
{
private:
    int sockfd = 0;
    static const unsigned int bufsize = 8196;

    std::string host_;
    unsigned short port_;
    std::string org_;
    std::string bkt_;
    std::string tkn_;

    std::stringstream lines_;
    std::vector<std::string> fields;
    std::string timestamp_;

public:
    Influx(const std::string &host, const unsigned short port, const std::string &org, const std::string &bucket, const std::string &token)
    {
        host_ = host;
        port_ = port;
        org_ = org;
        tkn_ = token;
        bkt_ = bucket;

        struct sockaddr_in serv_addr;
        if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
        {
            std::cerr << "socket() failed\n";
            return;
        }

        serv_addr.sin_family = AF_INET;
        serv_addr.sin_addr.s_addr = inet_addr(host.c_str());
        serv_addr.sin_port = htons(port);

        if (connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
        {
            std::cerr << "connect() failed\n";
            return;
        }
    }
    void close(void)
    {
        if (sockfd > 0)
            ::close(sockfd);
    }

    Influx &meas(const std::string name)
    {
        lines_ << name;
        return *this;
    }
    Influx &tag(const std::string tagKey, const std::string tagValue)
    {
        lines_ << "," << tagKey << "=" << tagValue << " ";
        return *this;
    }

    Influx &field(const std::string fieldKey, const char *fieldValue)
    {
        fields.push_back(fieldKey + "=" + fieldValue);
        return *this;
    }
    Influx &field(const std::string fieldKey, const unsigned long fieldValue)
    {
        fields.push_back(fieldKey + "=" + std::to_string(fieldValue) + "i");
        return *this;
    }
    Influx &field(const std::string fieldKey, const double fieldValue)
    {
        fields.push_back(fieldKey + "=" + std::to_string(fieldValue));
        return *this;
    }
    Influx &timestamp(const unsigned long long time)
    {
        timestamp_ = std::to_string(time);
        return *this;
    }

    int post()
    {
        std::string body = lines_.str();
        // Construct fields section
        for (size_t i = 0; i < fields.size(); i ++) {
            body += fields[i];
            if (i+1 < fields.size()) {
                body += ",";
            }
        }
        body += " " + timestamp_;

        char header[512];
        std::string buffer;

        ssize_t len = sprintf(header, "POST /api/v2/write?bucket=%s&org=%s&precision=s HTTP/1.1\r\nHost: %s:%d\r\nUser-Agent: influxdb-client-cheader\r\nContent-Length: %d\r\nAuthorization: Token %s\r\n\r\n",
                              bkt_.c_str(), org_.c_str(), host_.c_str(), port_, (int)body.length(), tkn_.c_str());

        // Combine header and body
        buffer = std::string(header) + body;
        size_t buffer_len = buffer.length();

        if (write(sockfd, buffer.c_str(), buffer_len) < len)
        {
            std::cerr << "Could not POST\n";
            return -1;
        }

        return 0;
    }
};

#endif
