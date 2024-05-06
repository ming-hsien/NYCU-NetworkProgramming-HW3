#include <cstdlib>
#include <iostream>
#include <fstream>
#include <memory>
#include <utility>
#include <string>
#include <cstring>
#include <sstream>
#include <map>
#include <sys/wait.h>
#include <boost/asio.hpp>
#include <boost/algorithm/string.hpp>

using namespace std;
using boost::asio::ip::tcp;

struct qInfo {
    string host = "";
    string port = "";
    string filename = "";
};

map<string, string> REQ;
map<int, qInfo> QUERYINFO;

class client : public std::enable_shared_from_this<client> {
    public : 
        client(boost::asio::io_context& io_context, int i) : resolver(io_context), socket_(io_context), id(i){}

        void start() {
            memset(data_, '\0', max_length);
            infile.open("./test_case/" + QUERYINFO[id].filename);
            if (infile.is_open()) {
                do_resolve();
            }
            else {
                cerr << "file open error" << endl;
                socket_.close();
            }
        }
    
    private :
        void do_resolve() {
            tcp::resolver::query resolve_query(QUERYINFO[id].host, QUERYINFO[id].port);
            auto self(shared_from_this());
            resolver.async_resolve(
                resolve_query,
                [this, self](boost::system::error_code ec, tcp::resolver::results_type resolve_ip) {
                    if (!ec) {
                        do_connect(resolve_ip);
                    }
                    else {
                        cerr << "resolve error" << endl;
                        socket_.close();
                    }
                }
            );
        }

        void do_connect(tcp::resolver::results_type resolve_ip) {
            auto self(shared_from_this());
            boost::asio::async_connect(
                socket_, resolve_ip,
                [this, self](boost::system::error_code ec, const tcp::endpoint& endpoint) {
                    if (!ec) {
                        do_read();
                    }
                    else {
                        cerr << ec << endl;
                        socket_.close();
                    }
                }
            );
        }

        void do_read() {
            auto self(shared_from_this());
            socket_.async_read_some(boost::asio::buffer(data_, max_length),
                [this, self](boost::system::error_code ec, size_t length) {
                    if (!ec) {
                        if (length) {
                            data_[length] = '\0';
                            string input = replace_escape(string(data_));
                            output_shell(input);
                            if (input.find("%") != string::npos) {
                                do_writeCmd();
                            }
                            else {
                                do_read();
                            }
                        }
                        else {
                            cerr << "read error" << endl;
                            socket_.close();
                        }
                    }
                }
            );
        }

        void do_writeCmd() {
            auto self(shared_from_this());
            string Cmd = "";
            getline(infile, Cmd);
            Cmd += '\n';
            output_command(Cmd);
            boost::asio::async_write(socket_, boost::asio::buffer(Cmd, Cmd.size()),
                [this, self](boost::system::error_code ec, long unsigned int length) {
                    if (!ec) {
                        do_read();
                    }
                }
            );
        }

        void output_shell(string str) {
            cout << "<script>document.getElementById('s" << id << "').innerHTML += '" << str << "';</script>\n" << flush;
        }

        void output_command(string str) {
            cout << "<script>document.getElementById('s" << id << "').innerHTML += '<b>" << str << "</b>';</script>\n" << flush;
        }

        string replace_escape(string str){
            string buffer = str;
            boost::algorithm::replace_all(buffer, "&", "&amp;");
            boost::algorithm::replace_all(buffer, "\r", "");
            boost::algorithm::replace_all(buffer, "\n", "&NewLine;");
            boost::algorithm::replace_all(buffer, "\'", "&apos;");
            boost::algorithm::replace_all(buffer, "\"", "&quot;");
            boost::algorithm::replace_all(buffer, "<", "&lt;");
            boost::algorithm::replace_all(buffer, ">", "&gt;");
            return buffer;
        }

        boost::asio::ip::tcp::resolver resolver;
        boost::asio::ip::tcp::socket socket_;
        enum { max_length = 1024 };
        char data_[max_length];
        int id;
        ifstream infile;
};

void do_getenv() {
    REQ["REQUEST_METHOD"] = getenv("REQUEST_METHOD");
    REQ["REQUEST_URI"] = getenv("REQUEST_URI");
    REQ["SERVER_PROTOCOL"] = getenv("SERVER_PROTOCOL");
    REQ["HTTP_HOST"] = getenv("HTTP_HOST");
    REQ["QUERY_STRING"] = getenv("QUERY_STRING");
    REQ["SERVER_ADDR"] = getenv("SERVER_ADDR");
    REQ["SERVER_PORT"] = getenv("SERVER_PORT");
    REQ["REMOTE_ADDR"] = getenv("REMOTE_ADDR");
    REQ["REMOTE_PORT"] = getenv("REMOTE_PORT");
}

int gethpfID(string hpfxx) {
    if (hpfxx.length() == 0 || (hpfxx[0] != 'h' && hpfxx[0] != 'p' && hpfxx[0] != 'f')) return -1;
    int ret = 0;
    for (long unsigned int i = 1; i < hpfxx.length(); i++) {
        ret *= 10;
        ret += hpfxx[i] - '0';
    }
    return ret;
}

void do_parseQueryString() {
    string QueryString = REQ["QUERY_STRING"];
    char *p1 = strtok(&QueryString[0], "&");
    while (p1 != NULL) {
        string buf = p1;
        if (buf.find("=") != string::npos && buf.find("=") != buf.length() - 1) {
            char is_hOrpOrf = buf.substr(0, buf.find("="))[0];
            int qID = gethpfID(buf.substr(0, buf.find("=")));
            string qInfo = buf.substr(buf.find("=") + 1, buf.length() - buf.find("=") - 1);
            if (is_hOrpOrf == 'h') {
                QUERYINFO[qID].host = qInfo;
            }
            else if (is_hOrpOrf == 'p') {
                QUERYINFO[qID].port = qInfo;
            }
            else if (is_hOrpOrf == 'f') {
                QUERYINFO[qID].filename = qInfo;
            } 
        }
        p1 = strtok(NULL, "&");
    }
}

void printHttpConsole() {
    cout << "Content-type: text/html\r\n\r\n";
    string HttpConsole = 
    "<!DOCTYPE html>\n"
    "<html lang=\"en\">\n"
    "<head>\n"
    "   <meta charset\"UTF-8\" />\n"
    "   <title>NP Project 3 Sample Console</title>\n"
    "   <link\n"
    "       rel=\"stylesheet\"\n"
    "       href=\"https://cdn.jsdelivr.net/npm/bootstrap@4.5.3/dist/css/bootstrap.min.css\"\n"
    "       integrity=\"sha384-TX8t27EcRE3e/ihU7zmQxVncDAy5uIKz4rEkgIXeMed4M0jlfIDPvg6uqKI2xXr2\"\n"
    "       crossorigin=\"anonymous\"\n"
    "   />\n"
    "   <link\n"
    "       href=\"https://fonts.googleapis.com/css?family=Source+Code+Pro\"\n"
    "       rel=\"stylesheet\"\n"
    "   />\n"
    "   <link\n"
    "       rel=\"icon\"\n"
    "       type=\"image/png\"\n"
    "       href=\"https://cdn0.iconfinder.com/data/icons/small-n-flat/24/678068-terminal-512.pn\"\n"
    "   />\n"
    "   <style>\n"
    "       * {\n"
    "           font-family: 'Source Code Pro', monospace;\n"
    "           font-size: 1rem !important;\n"
    "       }\n"
    "       body {\n"
    "           background-color: #212529;\n"
    "       }\n"
    "       pre {\n"
    "           color: #cccccc;\n"
    "       }\n"
    "       b {\n"
    "           color: #01b468;\n"
    "       }\n"
    "   </style>\n"
    "</head>\n"
    "<body>\n"
    "   <table class=\"table table-dark table-bordered\">\n"
    "       <thead>\n"
    "           <tr>\n";
    for (long unsigned int i = 0; i < QUERYINFO.size(); i++) {
        HttpConsole += "                <th scope=\"col\">" + QUERYINFO[i].host + ":" + QUERYINFO[i].port + "</th>\n";
    }

    HttpConsole = HttpConsole + 
    "           </tr>\n"
    "       </thead>\n"
    "       <tbody>\n"
    "           <tr>\n";

    for (long unsigned int i = 0; i < QUERYINFO.size(); i++) {
        HttpConsole += "                <td><pre id=\"s" + to_string(i) + "\" class=\"mb-0\"></pre></td>\n";
    }

    HttpConsole = HttpConsole +
    "           </tr>\n"
    "       </tbody>\n"
    "   </table>\n"
    "</body>\n"
    "</html>\n";

    cout << HttpConsole << flush;
}

int main(int argc, char* argv[]) {
    try {
        do_getenv();
        do_parseQueryString();
        boost::asio::io_context io_context;

        for (long unsigned int i = 0; i < QUERYINFO.size(); i++) {
            std::make_shared<client>(io_context, i)->start();
        }
        io_context.run();
    }
    catch (exception& e) {
        cerr << "Exception: " << e.what() << "\n";
    }
    return 0;
}