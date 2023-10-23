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
    int verbosity = 0;
    static const unsigned int bufsize = 8196;

    std::string host_;
    unsigned short port_;
    std::string org_;
    std::string bkt_;
    std::string tkn_;

    std::string body;
    std::string tags;

    typedef struct {
        std::string name;
        std::vector<std::string> fields;
    } Measurement;
    std::vector<Measurement> measurements;
    
    // std::vector<std::string> fields;
    std::string timestamp_;

public:
    Influx(const std::string &host, const unsigned short port, const std::string &org, const std::string &bucket, const std::string &token)
    {
        host_ = host;
        port_ = port;
        org_ = org;
        tkn_ = token;
        bkt_ = bucket;
    }

    int connectNow()
    {
        fprintf(stdout, "influxdb: Connecting.\n");
        sockfd = sockfd ? sockfd : socket(AF_INET, SOCK_STREAM, 0);
        if (sockfd <= 0)
        {
            fprintf(stderr, "influxdb: socket failed\n");
            return -1;
        }

        struct sockaddr_in serv_addr;
        serv_addr.sin_family = AF_INET;
        serv_addr.sin_addr.s_addr = inet_addr(host_.c_str());
        serv_addr.sin_port = htons(port_);

        if (connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
        {
            std::cerr << "influxdb: connect() failed\n";
            return -2;
        }

        return 0;
    }

    void setVerbosity(int verbosity)
    {
        this->verbosity = verbosity;
    }

    void closeDB(void)
    {
        if (sockfd > 0)
            ::close(sockfd);
    }

    Influx &clear()
    {
        measurements.clear();
        body.clear();
        return *this;
    }

    /**
     * Create new measurement
     */
    Influx &meas(const std::string name)
    {
        measurements.push_back(Measurement {
            .name = name,
        });
        return *this;
    }
    /**
     * Only allow 1 tag
     */
    Influx &tag(const std::string tagKey, const std::string tagValue)
    {
        tags = "," + tagKey + "=" + tagValue;
        return *this;
    }

    /**
     * Append field key-value to measurement map
     **/
    Influx &field(const std::string fieldKey, const char *fieldValue)
    {
        Measurement *m = &(measurements.back());
        m->fields.push_back(
            fieldKey + "=" + fieldValue
        );
        return *this;
    }
    Influx &field(const std::string fieldKey, const unsigned long fieldValue)
    {
        Measurement *m = &(measurements.back());
        m->fields.push_back(
            fieldKey + "=" + std::to_string(fieldValue) + "i"
        );
        return *this;
    }
    Influx &field(const std::string fieldKey, const double fieldValue)
    {
        Measurement *m = &(measurements.back());
        m->fields.push_back(
            fieldKey + "=" + std::to_string(fieldValue)
        );
        return *this;
    }
    Influx &timestamp(const unsigned long long time)
    {
        timestamp_ = std::to_string(time);
        return *this;
    }

    /**
     * @return return value of write() to socket
     */
    int post()
    {
        body = "";

        /**
         * Construct body
         */
        for (const Measurement &measurement : measurements)
        {
            // New measurement
            std::string line;
            line += measurement.name + tags + " ";

            // Iterate through fields vector
            // for (const std::string &field : measurement.fields)
            for (size_t i=0; i<measurement.fields.size(); i++)
            {
                const std::string field = measurement.fields[i];

                // Append field to fields line
                line += field;

                // If not last, add comma
                if (i+1 < measurement.fields.size())
                    line += ",";
            }

            line += " " + timestamp_ + "\n";
            body += line;
        }
        

        // // Construct fields section
        // for (size_t i = 0; i < fields.size(); i++)
        // {
        //     body += fields[i];
        //     if (i + 1 < fields.size())
        //     {
        //         body += ",";
        //     }
        // }
        // 

        char header[512];
        std::string buffer;

        ssize_t len = sprintf(header, "POST /api/v2/write?bucket=%s&org=%s&precision=s HTTP/1.1\r\nHost: %s:%d\r\nUser-Agent: influxdb-client-cheader\r\nContent-Length: %d\r\nAuthorization: Token %s\r\n\r\n",
                              bkt_.c_str(), org_.c_str(), host_.c_str(), port_, (int)body.length(), tkn_.c_str());

        // Combine header and body
        buffer = std::string(header) + body;
        size_t buffer_len = buffer.length();

        if (verbosity)
            fprintf(stdout, "influxdb: %s\n",buffer.c_str());

        int rc = write(sockfd, buffer.c_str(), buffer_len);
        if (rc < len)
        {
            // TODO Do this properly :)
            fprintf(stderr, "influxdb: Could not POST!\n");
            if (rc == 0)
            {
                fprintf(stderr, "influxdb: Lost connection\n");
                // Disconnected
                this->connectNow();
            }
            else
                return -1;
        }

        /**
         * TODO: Validated influx request
         */
        if (verbosity) {
            char buf[512];
            int rb = read(sockfd, buf, 512);
            printf("Rad: %d bytes -> %s\n", rb, buf);
        }

        measurements.clear();

        return rc;
    }
};

#endif
