#include <cstdlib>
#include <iostream>
#include <fstream>
#include <memory>
#include <utility>
#include <string>
#include <cstring>
#include <sstream>
#include <map>
#include <boost/asio.hpp>
#include <boost/algorithm/string.hpp>
#include <map>

using namespace std;
using boost::asio::ip::tcp;

struct qInfo {
    string host = "";
    string port = "";
    string filename = "";
};

map<string, string> REQ;
map<int, qInfo> QUERYINFO;

boost::asio::io_context io_context;
boost::asio::io_context web_context;
tcp::socket web_socket(web_context);

class client : public std::enable_shared_from_this<client> {
    public : 
        client(long unsigned int i, boost::asio::io_context& io_context) : resolver(io_context), socket_(io_context), id(i){}

        void start() {
            infile.open("test_case/" + QUERYINFO[id].filename);
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
                        cerr << "connect error" << endl;
                    }
                }
            );
        }

        void do_read() {
            memset(data_, '\0', max_length);
            auto self(shared_from_this());
            socket_.async_read_some(boost::asio::buffer(data_, max_length),
                [this, self](boost::system::error_code ec, unsigned long int length) {
                    if (!ec) {
                        string input = replace_escape(string(data_));
                        output_shell(input);
                        if (input.find("% ") != string::npos) {
                            do_writeCmd();
                        }
                        else {
                            do_read();
                        }
                    }
                }
            );
        }

        void do_writeCmd() {
            auto self(shared_from_this());
            string Cmd = "";
            getline(infile, Cmd);
            if (Cmd.find("exit") != string::npos) {
                infile.close();
            }
            Cmd += '\n';
            output_command(replace_escape(Cmd));
            boost::asio::async_write(socket_, boost::asio::buffer(Cmd.c_str(), Cmd.size()),
                [this, self](boost::system::error_code ec, unsigned long int length) {
                    if (!ec) {
                        do_read();
                    }
                    else {
                        cerr << "write error" << endl;
                    }
                }
            );
        }

        void output_shell(string str) {
            string wrt = "<script>document.getElementById('s" + to_string(id) + "').innerHTML += '" + str + "';</script>\n";

            auto self(shared_from_this());
            boost::asio::async_write(web_socket, boost::asio::buffer(wrt.c_str(), wrt.size()),
                [this, self](boost::system::error_code ec, unsigned long int length) {
                    if (!ec) {
                        do_read();
                    }
                    else {
                        cerr << "write error" << endl;
                    }
                }
            );
        }

        void output_command(string str) {
            string wrt = "<script>document.getElementById('s" + to_string(id) + "').innerHTML += '<b>" + str + "</b>';</script>\n";
            auto self(shared_from_this());
            boost::asio::async_write(web_socket, boost::asio::buffer(wrt.c_str(), wrt.size()),
                [this, self](boost::system::error_code ec, unsigned long int length) {
                    if (!ec) {
                        do_read();
                    }
                    else {
                        cerr << "write error" << endl;
                    }
                }
            );
        }

        string replace_escape(string str){
            boost::algorithm::replace_all(str, "&", "&amp;");
            boost::algorithm::replace_all(str, "\r", "");
            boost::algorithm::replace_all(str, "\n", "&NewLine;");
            boost::algorithm::replace_all(str, "\'", "&apos;");
            boost::algorithm::replace_all(str, "\"", "&quot;");
            boost::algorithm::replace_all(str, "<", "&lt;");
            boost::algorithm::replace_all(str, ">", "&gt;");
            return str;
        }

        boost::asio::ip::tcp::resolver resolver;
        boost::asio::ip::tcp::socket socket_;
        enum { max_length = 4096 };
        char data_[max_length];
        int id;
        ifstream infile;
};

int gethpfID(string hpfxx) {
    if (hpfxx.length() == 0 || (hpfxx[0] != 'h' && hpfxx[0] != 'p' && hpfxx[0] != 'f')) return -1;
    int ret = 0;
    for (long unsigned int i = 1; i < hpfxx.length(); i++) {
        ret *= 10;
        ret += hpfxx[i] - '0';
    }
    return ret;
}

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
                        do_parse();
                        do_clean();
                        do_write("HTTP/1.1 200 OK\r\n");
                        do_process();
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

        void do_clean() {
            memset(data_, '\0', sizeof(data_));
        }

        void do_write(string str) {
            auto self(shared_from_this());
            boost::asio::async_write(socket_, boost::asio::buffer(str.c_str(), str.size()),
                [this, self](boost::system::error_code ec, size_t length) {
                    if (!ec) {}
                }
            );
        }

        void do_process() {
            if (REQ["QUERY_PATH"].find("/panel.cgi") != -1) {
                printHttpPanel();
            }
            else if (REQ["QUERY_PATH"].find("/console.cgi") != -1) {
                try {
                    do_parseQueryString();
                    printHttpConsole();
                    web_socket = std::move(socket_);

                    for (long unsigned int i = 0; i < QUERYINFO.size(); i++) {
                        make_shared<client>(i, io_context)->start();
                    }
                }
                catch (exception& e) {
                    cerr << "Process : Console.cgi error " << e.what() << "\n";
                }
            }
        }

        void do_parseQueryString() {
            QUERYINFO.clear();
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
            // cerr << QUERYINFO.size() << endl;
        }

        
        void printHttpConsole() {
            string HttpConsole = 
            "Content-type: text/html\r\n\r\n"
            "<!DOCTYPE html>"
            "<html lang=\"en\">"
            "<head>"
            "   <meta charset\"UTF-8\" />"
            "   <title>NP Project 3 Sample Console</title>"
            "   <link"
            "       rel=\"stylesheet\""
            "       href=\"https://cdn.jsdelivr.net/npm/bootstrap@4.5.3/dist/css/bootstrap.min.css\""
            "       integrity=\"sha384-TX8t27EcRE3e/ihU7zmQxVncDAy5uIKz4rEkgIXeMed4M0jlfIDPvg6uqKI2xXr2\""
            "       crossorigin=\"anonymous\""
            "   />"
            "   <link"
            "       href=\"https://fonts.googleapis.com/css?family=Source+Code+Pro\""
            "       rel=\"stylesheet\""
            "   />"
            "   <link"
            "       rel=\"icon\""
            "       type=\"image/png\""
            "       href=\"https://cdn0.iconfinder.com/data/icons/small-n-flat/24/678068-terminal-512.pn\""
            "   />"
            "   <style>"
            "       *{"
            "           font-family: 'Source Code Pro', monospace;"
            "           font-size: 1rem !important;"
            "       }"
            "       body{"
            "           background-color: #212529;"
            "       }"
            "       pre{"
            "           color: #cccccc;"
            "       }"
            "       b{"
            "           color: #01b468;"
            "       }"
            "   </style>"
            "</head>"
            "<body>"
            "   <table class=\"table table-dark table-bordered\">"
            "       <thead>"
            "           <tr>";
            for (long unsigned int i = 0; i < QUERYINFO.size(); i++) {
                HttpConsole += "                <th scope=\"col\">" + QUERYINFO[i].host + ":" + QUERYINFO[i].port + "</th>";
            }

            HttpConsole = HttpConsole + 
            "           </tr>"
            "       </thead>"
            "       <tbody>"
            "           <tr>";

            for (long unsigned int i = 0; i < QUERYINFO.size(); i++) {
                HttpConsole += "                <td><pre id=\"s" + to_string(i) + "\" class=\"mb-0\"></pre></td>";
            }

            HttpConsole = HttpConsole +
            "           </tr>"
            "       </tbody>"
            "   </table>"
            "</body>"
            "</html>";

            do_write(HttpConsole);
        }

        void printHttpPanel() {
            string html = ""
                "Content-type: text/html\r\n\r\n "
                    "<!DOCTYPE html> "
                    "<html lang=\"en\"> "
                    "<head> "
                        "<title>NP Project 3 Panel</title> "
                        "<link "
                            "rel=\"stylesheet\" "
                            "href=\"https://cdn.jsdelivr.net/npm/bootstrap@4.5.3/dist/css/bootstrap.min.css\" "
                            "integrity=\"sha384-TX8t27EcRE3e/ihU7zmQxVncDAy5uIKz4rEkgIXeMed4M0jlfIDPvg6uqKI2xXr2\" "
                            "crossorigin=\"anonymous\" "
                        "/> "
                        "<link "
                            "href=\"https://fonts.googleapis.com/css?family=Source+Code+Pro\" "
                            "rel=\"stylesheet\" "
                        "/> "
                        "<link "
                            "rel=\"icon\" "
                            "type=\"image/png\" "
                            "href=\"https://cdn4.iconfinder.com/data/icons/iconsimple-setting-time/512/dashboard-512.png\" "
                        "/> "
                        "<style> "
                            "* { "
                            "    font-family: 'Source Code Pro', monospace; "
                        "} "
                        "</style> "
                    "</head> "
                    "<body class=\"bg-secondary pt-5\"> "
                        "<form action=\"console.cgi\" method=\"GET\"> "
                        "<table class=\"table mx-auto bg-light\" style=\"width: inherit\"> "
                            "<thead class=\"thead-dark\"> "
                            "<tr> "
                                "<th scope=\"col\">#</th> "
                                "<th scope=\"col\">Host</th> "
                                "<th scope=\"col\">Port</th> "
                                "<th scope=\"col\">Input File</th> "
                            "</tr> "
                            "</thead> "
                            "<tbody> ";
            for(int i = 0; i < 5; i++) {
                html += "<tr> "
                            "<th scope=\"row\" class=\"align-middle\">Session " + to_string(i + 1) + "</th> "
                            "<td> "
                            "<div class=\"input-group\"> "
                                "<select name=\"h" + to_string(i) + "\" class=\"custom-select\"> "
                                "<option></option> "
                                "<option value=\"nplinux1.cs.nycu.edu.tw\">nplinux1</option> "
                                "<option value=\"nplinux2.cs.nycu.edu.tw\">nplinux2</option> "
                                "<option value=\"nplinux3.cs.nycu.edu.tw\">nplinux3</option> "
                                "<option value=\"nplinux4.cs.nycu.edu.tw\">nplinux4</option> "
                                "<option value=\"nplinux5.cs.nycu.edu.tw\">nplinux5</option> "
                                "<option value=\"nplinux6.cs.nycu.edu.tw\">nplinux6</option> "
                                "<option value=\"nplinux7.cs.nycu.edu.tw\">nplinux7</option> "
                                "<option value=\"nplinux8.cs.nycu.edu.tw\">nplinux8</option> "
                                "<option value=\"nplinux9.cs.nycu.edu.tw\">nplinux9</option> "
                                "<option value=\"nplinux10.cs.nycu.edu.tw\">nplinux10</option> "
                                "<option value=\"nplinux11.cs.nycu.edu.tw\">nplinux11</option> "
                                "<option value=\"nplinux12.cs.nycu.edu.tw\">nplinux12</option> "
                                "</select> "
                                "<div class=\"input-group-append\"> "
                                "<span class=\"input-group-text\">.cs.nycu.edu.tw</span> "
                                "</div>"
                            "</div> "
                            "</td> "
                            "<td> "
                            "<input name=\"p" + to_string(i) + "\" type=\"text\" class=\"form-control\" size=\"5\" /> "
                            "</td> "
                            "<td> "
                            "<select name=\"f" + to_string(i) + "\" class=\"custom-select\"> "
                                "<option></option> "
                                "<option value=\"t1.txt\">t1.txt</option> "
                                "<option value=\"t2.txt\">t2.txt</option> "
                                "<option value=\"t3.txt\">t3.txt</option> "
                                "<option value=\"t4.txt\">t4.txt</option> "
                                "<option value=\"t5.txt\">t5.txt</option> "
                            "</select> "
                            "</td> "
                        "</tr> ";
            }
            html += "<tr> "
                        "<td colspan=\"3\"></td> "
                        "<td> "
                        "<button type=\"submit\" class=\"btn btn-info btn-block\">Run</button> "
                        "</td> "
                    "</tr> "
                    "</tbody> "
                "</table> "
                "</form> "
            "</body> "
            "</html> ";

            do_write(html);
        }

        tcp::socket socket_;
        enum { max_length = 4096 };
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

        server s(io_context, atoi(argv[1]));
        io_context.run();
    }
    catch (exception& e) {
        cerr << "Exception: " << e.what() << "\n";
    }
    return 0;
}