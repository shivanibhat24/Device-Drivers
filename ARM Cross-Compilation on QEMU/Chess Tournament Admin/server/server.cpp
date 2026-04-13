/**
 * chess-arm-tournament: Boost.Asio TCP Game Server
 * Runs on QEMU ARM (cross-compiled) or natively on x86.
 *
 * Protocol (newline-terminated):
 *   CLIENT → SERVER:
 *     AUTH:<lichess_username>   register player
 *     MOVE:<uci>                e.g. MOVE:e2e4
 *     RESIGN                    forfeit game
 *     STATUS                    request full game state
 *     MODE:pvp|engine           set game mode
 *
 *   SERVER → CLIENT:
 *     ASSIGNED:white|black|spectator
 *     JOINED:<username>
 *     MOVE:<username>:<uci>
 *     STATE:<json>
 *     RESIGN:<username>
 *     GAMEOVER:<winner>
 *     LEFT:<username>
 *     ERROR:<reason>
 */

#include <boost/asio.hpp>
#include <iostream>
#include <memory>
#include <set>
#include <thread>
#include <sstream>
#include "game_state.hpp"

using boost::asio::ip::tcp;

class Session;
class Server;

// ── Global game state ─────────────────────────────────────────────────────────
GameState g_game;

// ── Session ───────────────────────────────────────────────────────────────────
class Session : public std::enable_shared_from_this<Session> {
public:
    Session(tcp::socket socket, Server& server)
        : socket_(std::move(socket)), server_(server) {}

    void start() {
        session_id_ = generate_id();
        std::cout << "[+] Client connected: " << session_id_ << "\n";
        do_read();
    }

    void deliver(const std::string& msg) {
        boost::asio::async_write(
            socket_, boost::asio::buffer(msg + "\n"),
            [](boost::system::error_code, std::size_t){});
    }

    const std::string& id() const { return session_id_; }

private:
    void do_read() {
        auto self = shared_from_this();
        boost::asio::async_read_until(socket_, buf_, '\n',
            [this, self](boost::system::error_code ec, std::size_t) {
                if (!ec) {
                    std::istream is(&buf_);
                    std::string line;
                    std::getline(is, line);
                    if (!line.empty() && line.back() == '\r') line.pop_back();
                    handle_message(line);
                    do_read();
                } else {
                    std::cout << "[-] Client disconnected: " << session_id_ << "\n";
                    on_disconnect();
                }
            });
    }

    void handle_message(const std::string& msg);
    void on_disconnect();

    static std::string generate_id() {
        static std::atomic<int> counter{0};
        return "player_" + std::to_string(++counter);
    }

    tcp::socket           socket_;
    boost::asio::streambuf buf_;
    Server&               server_;
    std::string           session_id_;
    std::string           username_;
};

// ── Server ────────────────────────────────────────────────────────────────────
class Server {
public:
    Server(boost::asio::io_context& io, short port)
        : acceptor_(io, tcp::endpoint(tcp::v4(), port)) {
        std::cout << "[*] Chess Tournament Server on port " << port << "\n";
        do_accept();
    }

    void broadcast(const std::string& msg, const std::string& exclude_id = "") {
        std::lock_guard<std::mutex> lock(sessions_mtx_);
        for (auto& s : sessions_)
            if (s->id() != exclude_id)
                s->deliver(msg);
    }

    void add_session(std::shared_ptr<Session> s) {
        std::lock_guard<std::mutex> lock(sessions_mtx_);
        sessions_.insert(s);
    }

    void remove_session(std::shared_ptr<Session> s) {
        std::lock_guard<std::mutex> lock(sessions_mtx_);
        sessions_.erase(s);
    }

    size_t player_count() {
        std::lock_guard<std::mutex> lock(sessions_mtx_);
        return sessions_.size();
    }

private:
    void do_accept() {
        acceptor_.async_accept(
            [this](boost::system::error_code ec, tcp::socket socket) {
                if (!ec) {
                    auto session = std::make_shared<Session>(std::move(socket), *this);
                    add_session(session);
                    session->start();
                    // Send current state to new joiner
                    {
                        std::lock_guard<std::mutex> lock(g_game.mtx);
                        session->deliver("STATE:" + g_game.to_json());
                    }
                }
                do_accept();
            });
    }

    tcp::acceptor                            acceptor_;
    std::set<std::shared_ptr<Session>>       sessions_;
    std::mutex                               sessions_mtx_;
};

// ── Message handler ───────────────────────────────────────────────────────────
void Session::handle_message(const std::string& raw) {
    std::cout << "[" << session_id_ << "] " << raw << "\n";

    if (raw.rfind("AUTH:", 0) == 0) {
        username_ = raw.substr(5);
        std::lock_guard<std::mutex> lock(g_game.mtx);
        Player p;
        p.session_id        = session_id_;
        p.lichess_username  = username_;
        if (g_game.players.empty()) {
            p.color = PlayerColor::WHITE;
            deliver("ASSIGNED:white");
        } else if (g_game.players.size() == 1) {
            p.color = PlayerColor::BLACK;
            deliver("ASSIGNED:black");
        } else {
            p.color = PlayerColor::SPECTATOR;
            deliver("ASSIGNED:spectator");
        }
        g_game.players[session_id_] = p;
        server_.broadcast("JOINED:" + username_);

    } else if (raw.rfind("MOVE:", 0) == 0) {
        std::string move = raw.substr(5);
        std::lock_guard<std::mutex> lock(g_game.mtx);
        if (g_game.game_over) { deliver("ERROR:game is over"); return; }
        auto it = g_game.players.find(session_id_);
        if (it == g_game.players.end()) { deliver("ERROR:not authenticated"); return; }
        bool is_white = (it->second.color == PlayerColor::WHITE);
        if (is_white != g_game.white_turn) { deliver("ERROR:not your turn"); return; }
        g_game.move_history.push_back(move);
        g_game.white_turn = !g_game.white_turn;
        server_.broadcast("MOVE:" + username_ + ":" + move);
        server_.broadcast("STATE:" + g_game.to_json());

    } else if (raw == "RESIGN") {
        std::lock_guard<std::mutex> lock(g_game.mtx);
        g_game.game_over = true;
        g_game.winner    = (g_game.white_turn ? "black" : "white");
        server_.broadcast("RESIGN:" + username_);
        server_.broadcast("GAMEOVER:" + g_game.winner);

    } else if (raw == "STATUS") {
        std::lock_guard<std::mutex> lock(g_game.mtx);
        deliver("STATE:" + g_game.to_json());

    } else if (raw.rfind("MODE:", 0) == 0) {
        std::string mode = raw.substr(5);
        std::lock_guard<std::mutex> lock(g_game.mtx);
        if (mode == "pvp")         g_game.mode = GameMode::PLAYER_VS_PLAYER;
        else if (mode == "engine") g_game.mode = GameMode::PLAYER_VS_ENGINE;
        server_.broadcast("MODE:" + mode);

    } else {
        deliver("ERROR:unknown command: " + raw);
    }
}

void Session::on_disconnect() {
    server_.remove_session(shared_from_this());
    if (!username_.empty())
        server_.broadcast("LEFT:" + username_);
}

// ── main ──────────────────────────────────────────────────────────────────────
int main(int argc, char* argv[]) {
    try {
        short port = (argc > 1) ? static_cast<short>(std::stoi(argv[1])) : 5000;
        boost::asio::io_context io;
        Server server(io, port);

        unsigned int n = std::max(2u, std::thread::hardware_concurrency());
        std::cout << "[*] Thread pool: " << n << " threads\n";
        std::vector<std::thread> threads;
        for (unsigned int i = 0; i < n; ++i)
            threads.emplace_back([&io]{ io.run(); });
        for (auto& t : threads) t.join();

    } catch (std::exception& e) {
        std::cerr << "[!] Fatal: " << e.what() << "\n";
        return 1;
    }
}
