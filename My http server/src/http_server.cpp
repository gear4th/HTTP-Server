#include "http_server.hpp"

#include <arpa/inet.h>
#include <unistd.h>
#include <iostream>

#include <stdexcept>
#include <string.h>

HttpServer::HttpServer(const std::string &host, std::uint16_t port)
    : m_host(host),
      m_port(port),
      m_sock_fd(0),
      m_running(false){
  
  if ((m_sock_fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0)) < 0) {
    throw std::runtime_error("Unable to create socket");
  }
}

void HttpServer::add_response(std::string path, HttpMethod method, HttpResponse response){
  std::string uri(path);
  std::string strResponse = response_to_string(response, 1);
  if(curPage >= nMaxPagesServable){
    throw std::runtime_error("Cannot add more web pages to serve");
  }
  memcpy(responseBuffer[curPage], strResponse.c_str(), nMaxBufferSize);
  bufferLength[curPage] = strResponse.length();
  m_request_handlers[uri].insert(std::make_pair(method, curPage++));
  return;
}

void HttpServer::Start() {
  int opt = 1;
  sockaddr_in server_address;

  if (setsockopt(m_sock_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt,
                 sizeof(opt)) < 0) {
    throw std::runtime_error("Unable to set options to socket ");
  }

  server_address.sin_family = AF_INET;
  server_address.sin_addr.s_addr = INADDR_ANY;
  inet_pton(AF_INET, m_host.c_str(), &(server_address.sin_addr.s_addr));
  server_address.sin_port = htons(m_port);

  if (bind(m_sock_fd, (sockaddr *)&server_address, sizeof(server_address)) < 0) {
    throw std::runtime_error("Unable to bind to socket");
  }

  if (listen(m_sock_fd, nBacklogSize) < 0) {
    std::string msg = "Unable to listen on port " + std::__cxx11::to_string(m_port);
    throw std::runtime_error(msg);
  }

  EpollInit();
  m_running = true;
  m_listener_thread = std::thread(&HttpServer::ListenerFunc, this);
  for (int i = 0; i < nThreadPoolSize; i++) {
    m_worker_threads[i] = std::thread(&HttpServer::ProcessEpollEvents, this, i);
  }
}

void HttpServer::Stop() {
  m_running = false;
  m_listener_thread.join();
  for (int i = 0; i < nThreadPoolSize; i++) {
    m_worker_threads[i].join();
  }
  for (int i = 0; i < nThreadPoolSize; i++) {
    close(m_worker_epoll_fd[i]);
  }
  close(m_sock_fd);
}

void HttpServer::EpollInit() {
  for (int i = 0; i < nThreadPoolSize; i++) {
    if ((m_worker_epoll_fd[i] = epoll_create1(0)) < 0) {
      throw std::runtime_error(
          "Unable to create epoll file descriptor for worker number " + std::__cxx11::to_string(i));
    }
  }
}

void HttpServer::ListenerFunc() {
  EventData *client_data;
  int client_fd;
  int current_worker = 0;

  // accept new connections and distribute tasks to worker threads
  while (m_running) {
    client_fd = accept4(m_sock_fd, NULL, NULL,
                        SOCK_NONBLOCK);
    if (client_fd < 0) continue;

    client_data = new EventData();
    client_data->file_descriptor = client_fd;
    control_epoll_event(m_worker_epoll_fd[current_worker], EPOLL_CTL_ADD,
                        client_fd, EPOLLIN, client_data);
    
    current_worker = (current_worker+1)%nThreadPoolSize;
  }
}

void HttpServer::ProcessEpollEvents(int worker_id) {
  EventData *data;
  int epoll_fd = m_worker_epoll_fd[worker_id];
  while (m_running) {
    int nfds = epoll_wait(m_worker_epoll_fd[worker_id],
                          m_worker_events[worker_id], HttpServer::nMaxEvents, 0);
    if (nfds < 0) {
      continue;
    }

    for (int i = 0; i < nfds; i++) {
      const epoll_event &current_event = m_worker_events[worker_id][i];
      data = reinterpret_cast<EventData *>(current_event.data.ptr);
      if ((current_event.events & EPOLLHUP) ||
          (current_event.events & EPOLLERR)) {
        control_epoll_event(epoll_fd, EPOLL_CTL_DEL, data->file_descriptor);
        close(data->file_descriptor);
        delete data;
      } else if ((current_event.events == EPOLLIN) ||
                 (current_event.events == EPOLLOUT)) {
        HandleEpollEvent(epoll_fd, data, current_event.events);
      } else {  // something unexpected
        control_epoll_event(epoll_fd, EPOLL_CTL_DEL, data->file_descriptor);
        close(data->file_descriptor);
        delete data;
      }
    }
  }
}

void HttpServer::HandleEpollEvent(int epoll_fd, EventData *data,
                                  std::uint32_t events) {
  int fd = data->file_descriptor;
  EventData *request, *response;

  if (events == EPOLLIN) {
    request = data;
    ssize_t byte_count = recv(fd, request->buffer, nMaxBufferSize, 0);
    if (byte_count > 0) {  // we have fully received the message, I mean there is no way to check if we need to receive more unlike send
      response = new EventData();
      response->file_descriptor = fd;
      HandleRequestData(*request, response);
      control_epoll_event(epoll_fd, EPOLL_CTL_MOD, fd, EPOLLOUT, response);
      delete request;
    } else if (byte_count == 0) {  // client has closed connection
      control_epoll_event(epoll_fd, EPOLL_CTL_DEL, fd);
      close(fd);
      delete request;
    } else {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {  // retry
        request->file_descriptor = fd;
        control_epoll_event(epoll_fd, EPOLL_CTL_MOD, fd, EPOLLIN, request);
      } else {  // other error
        control_epoll_event(epoll_fd, EPOLL_CTL_DEL, fd);
        close(fd);
        delete request;
      }
    }
  } else {
    response = data;
    ssize_t byte_count;

    if(response->cachedInd != 0){
      byte_count =
        send(fd, responseBuffer[response->cachedInd] + response->current_index, response->remaining_length, 0);
    }
    else{
      byte_count =
        send(fd,  response->buffer + response->current_index, response->remaining_length, 0);
    }

    if (byte_count >= 0) {
      if (byte_count < response->remaining_length) {  // there are still bytes to write
        response->current_index += byte_count;
        response->remaining_length -= byte_count;
        control_epoll_event(epoll_fd, EPOLL_CTL_MOD, fd, EPOLLOUT, response);
      } else {  // we have written the complete message
        request = new EventData();
        request->file_descriptor = fd;
        control_epoll_event(epoll_fd, EPOLL_CTL_MOD, fd, EPOLLIN, request);
        delete response;
      }
    } else {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {  // retry
        control_epoll_event(epoll_fd, EPOLL_CTL_ADD, fd, EPOLLOUT, response);
      } else {  // other error
        control_epoll_event(epoll_fd, EPOLL_CTL_DEL, fd);
        close(fd);
        delete response;
      }
    }
  }
}

void HttpServer::HandleRequestData(const EventData &raw_request,
                                EventData *raw_response) {
  std::string request_string(raw_request.buffer), response_string;
  HttpRequest http_request;
  HttpResponse http_response;

  try {
    http_request = requestString_to_requestObject(request_string);
    // this is a good request
    auto it = m_request_handlers.find(http_request.uri());
    if (it == m_request_handlers.end()) {  // this uri is not registered
      http_response =  HttpResponse(HttpStatusCode::NotFound);
    }
    else{
      auto callback_it = it->second.find(http_request.method());
      if (callback_it == it->second.end()) {  // no handler for this method
        http_response = HttpResponse(HttpStatusCode::MethodNotAllowed);
      }
      else{
        int responseInd = callback_it->second;
        raw_response->remaining_length = bufferLength[responseInd];
        raw_response->cachedInd = responseInd;
        return;
      }
    }
  } catch (const std::invalid_argument &e) {
    http_response = HttpResponse(HttpStatusCode::BadRequest);
    http_response.SetContent(e.what());
  } catch (const std::logic_error &e) {
    http_response = HttpResponse(HttpStatusCode::HttpVersionNotSupported);
    http_response.SetContent(e.what());
  } catch (const std::exception &e) {
    http_response = HttpResponse(HttpStatusCode::InternalServerError);
    http_response.SetContent(e.what());
  }

  // Set response to write to client
  response_string =
      response_to_string(http_response, http_request.method() != HttpMethod::HEAD);
  memcpy(raw_response->buffer, response_string.c_str(), nMaxBufferSize);
  raw_response->remaining_length = response_string.length();
}

void HttpServer::control_epoll_event(int epoll_fd, int op, int fd,
                                     std::uint32_t events, void *data) {
  if (op == EPOLL_CTL_DEL) {
    if (epoll_ctl(epoll_fd, op, fd, nullptr) < 0) {
      throw std::runtime_error("Unable to delete epoll event file descriptor");
    }
  } else {
    epoll_event ev;
    ev.events = events;
    ev.data.ptr = data;
    if (epoll_ctl(epoll_fd, op, fd, &ev) < 0) {
      throw std::runtime_error("Unable to add epoll event file descriptor");
    }
  }
}