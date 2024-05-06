#include <cstdlib>
#include <iostream>
#include <memory>
#include <utility>
#include <string>
#include <sstream>
#include <sys/wait.h>
#include <boost/asio.hpp>
#include <map>

using namespace std;
using boost::asio::ip::tcp;

int state;

class session : public enable_shared_from_this<session> {
    public : 
        session(tcp::socket socket) : socket_(move(socket)){}

        void start() {
            do_read();
        }
    
    private :
        void do_read() {
            // 確保Session存活的時間比 async 操作還要長
            auto self(shared_from_this());
            socket_.async_read_some(boost::asio::buffer(data_, max_length),
                [this, self](boost::system::error_code ec, size_t length) {
                    if (!ec) {
                        pid_t pid;
                        while((pid = fork()) < 0) {
                            waitpid(-1, &state, WNOHANG);
                            usleep(1000);
                        }
                        if (pid == 0) {
                            do_parse();
                            do_setenv();
                            do_dup2(socket_.native_handle());
                            do_clean();
                            cout << "HTTP/1.1 200 OK\r\n" << flush;
                            char **argv = stoc(REQ["QUERY_PATH"]);
                            int e = execv(REQ["QUERY_PATH"].c_str(), argv);
                            if(e == -1){
                                exit(1);
                            }
                            exit(0);
                        }
                        else if (pid > 0) {
                            socket_.close();    
                            while(waitpid(-1, &state, WNOHANG) > 0);
                        }
                    }
                }
            );
        }

        void do_parse() {
            stringstream ss;
            string spt = "";
            ss << string(data_);
            int count = 0;
            while (ss >> spt) {
                switch (count){
                    case 0 :
                        REQ["REQUEST_METHOD"] = spt;
                    case 1 : 
                        REQ["REQUEST_URI"] = spt;
                    case 2 :
                        REQ["SERVER_PROTOCOL"] = spt;
                    case 4 :
                        REQ["HTTP_HOST"] = spt;
                    default:
                        break;
                }
                count++;
            }
            
            int stringPos = REQ["REQUEST_URI"].find('?');
            if (stringPos != -1) {
                REQ["QUERY_STRING"] = REQ["REQUEST_URI"].substr(stringPos + 1, REQ["REQUEST_URI"].length() - stringPos);
                REQ["QUERY_PATH"] = "." + REQ["REQUEST_URI"].substr(0, stringPos);
            }
            else {
                REQ["QUERY_PATH"] = "." + REQ["REQUEST_URI"];
            }

            REQ["SERVER_ADDR"] = socket_.local_endpoint().address().to_string();
            REQ["SERVER_PORT"] = to_string(socket_.local_endpoint().port());
            REQ["REMOTE_ADDR"] = socket_.remote_endpoint().address().to_string();
            REQ["REMOTE_PORT"] = to_string(socket_.remote_endpoint().port());
        }

        void do_setenv() {
            for (auto req : REQ) {
                if (req.first != "QUERY_PATH")
                    setenv(req.first.c_str(), req.second.c_str(), 1);
            }
        }

        void do_dup2(int sock) {
            dup2(sock, 0);
            dup2(sock, 1);
            // dup2(sock, 2);
        }

        void do_clean() {
            memset(data_, '\0', sizeof(data_));
            socket_.close();
        }

        char** stoc(string s) {
            char** argv = new char*[2];
            argv[0] = new char[s.size()];
            strcpy(argv[0], s.c_str());
            argv[1] = new char;
            argv[1] = NULL;
            return argv;
        }

        tcp::socket socket_;
        enum { max_length = 1024 };
        char data_[max_length];
        map<string, string> REQ;
};

class server {
    public : 
        server(boost::asio::io_context& io_context, short port) : acceptor_(io_context, tcp::endpoint(tcp::v4(), port)) {
            do_accept();
        }
    private:
        void do_accept() {
            acceptor_.async_accept(
                [this](boost::system::error_code ec, tcp::socket socket) {
                    if (!ec) {
                        make_shared<session>(move(socket))->start();
                    }

                    do_accept();
                }
            );
        }

        tcp::acceptor acceptor_;
};

int main(int argc, char* argv[]) {
    try {
        if (argc != 2) {
            cerr << "Usage: async_tcp_echo_server <port>\n";
            return 1;
        }

        boost::asio::io_context io_context;

        server s(io_context, atoi(argv[1]));
        io_context.run();
    }
    catch (exception& e) {
        cerr << "Exception: " << e.what() << "\n";
    }
    return 0;
}