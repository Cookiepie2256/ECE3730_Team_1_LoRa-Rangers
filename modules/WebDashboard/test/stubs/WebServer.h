// Host-side stand-in for the ESP32 WebServer library. Records what handlers
// send, and lets tests fake a GET request with testGet().
#ifndef STUB_WEBSERVER_H
#define STUB_WEBSERVER_H

#include "Arduino.h"
#include <functional>
#include <map>

enum HTTPMethod { HTTP_ANY, HTTP_GET, HTTP_POST };

class WebServer {
  public:
    typedef std::function<void(void)> THandlerFunction;

    explicit WebServer(int port = 80) : port_(port) { last() = this; }

    // The most recently constructed server (the dashboard's), for tests
    static WebServer *&last() {
        static WebServer *s = nullptr;
        return s;
    }

    void on(const char *uri, HTTPMethod, THandlerFunction fn) { routes_[uri] = fn; }
    void onNotFound(THandlerFunction fn) { notFound_ = fn; }
    void begin() { begun = true; }
    void handleClient() {}

    bool hasArg(const String &name) const { return args_.count(name.c_str()) > 0; }
    String arg(const String &name) const {
        auto it = args_.find(name.c_str());
        return it == args_.end() ? String("") : String(it->second);
    }

    void sendHeader(const char *, const char *, bool = false) {}
    void send(int c, const char *type, const char *content) {
        record(c, type, content, std::strlen(content));
    }
    void send_P(int c, const char *type, const char *content, size_t len) {
        record(c, type, content, len);
    }

    // Test helper: run the handler for `path` as a GET request
    void testGet(const std::string &path,
                 const std::map<std::string, std::string> &args = {}) {
        args_ = args;
        code = 0;
        body.clear();
        auto it = routes_.find(path);
        if (it != routes_.end()) {
            it->second();
        } else if (notFound_) {
            notFound_();
        }
    }

    bool begun = false;
    int code = 0;
    std::string contentType;
    std::string body;

  private:
    void record(int c, const char *type, const char *content, size_t len) {
        code = c;
        contentType = type ? type : "";
        body.assign(content, len);
    }

    int port_;
    std::map<std::string, THandlerFunction> routes_;
    THandlerFunction notFound_;
    std::map<std::string, std::string> args_;
};

#endif // STUB_WEBSERVER_H
