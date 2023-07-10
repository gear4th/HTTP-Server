#include "http_server.hpp"
#include <fstream>
#include <sstream> //std::stringstream
#include <stdexcept>
#include <cerrno>
#include <iostream>


int main(){
    std::string host = "0.0.0.0";
    int port = 8091;

    HttpServer server(host,port);

    // add page we are serving
    try{
    HttpResponse response(HttpStatusCode::Ok); 
    std::ifstream inFile;
    inFile.open("index.html");

    std::stringstream strStream;
    strStream << inFile.rdbuf(); //read the file
    std::string content = strStream.str(); //str holds the content of the file

    response.SetHeader("Content-Type", "text/html");
    response.SetContent(content);
    
    server.add_response("/", HttpMethod::GET,response);
    server.add_response("/index.html", HttpMethod::GET,response);
    }
    catch(std::exception &e){
        std::cerr << "Error occurred: " << e.what() << std::endl;
        return -1;
    }

    try{
        std::cout << "Starting the http web server.." << std::endl;
        server.Start();
        std::cout << "Server started and listening on " << host << ":" << port << std::endl;

        std::cout << "Enter [quit] to stop the server" << std::endl;
        std::string command;
        while (std::cin >> command, command != "quit")
        ;
        std::cout << "'quit' command entered. Stopping the web server.."
                << std::endl;
        server.Stop();
        std::cout << "Server stopped" << std::endl;
    }
    catch(std::exception& e){
        std::cerr << "Error occurred: " << e.what() << std::endl;
        return -1;
    }
}