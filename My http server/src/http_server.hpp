#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h>

#include <chrono>
#include <functional>
#include <map>
#include <string>
#include <thread>
#include <utility>

#include "http_message.hpp"

const size_t nMaxBufferSize = 4096;

struct EventData {
  EventData() : file_descriptor(0), remaining_length(0), current_index(0), cachedInd(0), buffer() {}
  int file_descriptor;
  int cachedInd;
  size_t remaining_length;
  size_t current_index;
  char buffer[nMaxBufferSize];
};

class HttpServer {
 public:
  HttpServer(const std::string& host, std::uint16_t port);
  
  void Start();
  void Stop();

  std::string get_host() const { return m_host; }
  std::uint16_t get_port() const { return m_port; }
  bool isRunning() const { return m_running; }
  void add_response(std::string path, HttpMethod method, HttpResponse response);

 private:
  static const int nBacklogSize = 1000;
  static const int nMaxConnections = 10000;
  static const int nMaxEvents = 10000;
  static const int nThreadPoolSize = 5;
  static const int nMaxPagesServable = 10;

  std::string m_host;
  std::uint16_t m_port;
  int m_sock_fd;
  bool m_running;
  std::thread m_listener_thread;
  std::thread m_worker_threads[nThreadPoolSize];
  int m_worker_epoll_fd[nThreadPoolSize];
  epoll_event m_worker_events[nThreadPoolSize][nMaxEvents];
  std::map<std::string, std::map<HttpMethod, int>> m_request_handlers;

  int curPage = 1;
  char responseBuffer[nMaxPagesServable][nMaxBufferSize];
  int bufferLength[nMaxPagesServable];

  void EpollInit();
  void ListenerFunc();
  void ProcessEpollEvents(int worker_id);
  void HandleEpollEvent(int epoll_fd, EventData* event, std::uint32_t events);

  void HandleRequestData(const EventData& request, EventData* response);

  void control_epoll_event(int epoll_fd, int op, int fd, std::uint32_t events = 0, void* data = nullptr);
};