#pragma once
#include <string>
#include <vector>
#include <map>
#include <mutex>

enum class GameMode { WAITING, PLAYER_VS_PLAYER, PLAYER_VS_ENGINE };
enum class PlayerColor { WHITE, BLACK, SPECTATOR };

struct Player {
    std::string session_id;
    std::string lichess_username;
    PlayerColor color;
    bool ready = false;
};

struct GameState {
    std::string fen = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
    std::vector<std::string> move_history;
    std::map<std::string, Player> players;
    GameMode mode = GameMode::WAITING;
    bool white_turn = true;
    bool game_over = false;
    std::string winner = "";
    std::mutex mtx;

    std::string to_json() {
        std::string json = "{";
        json += "\"fen\":\"" + fen + "\",";
        json += "\"white_turn\":" + std::string(white_turn ? "true" : "false") + ",";
        json += "\"game_over\":" + std::string(game_over ? "true" : "false") + ",";
        json += "\"winner\":\"" + winner + "\",";
        json += "\"moves\":[";
        for (size_t i = 0; i < move_history.size(); i++) {
            json += "\"" + move_history[i] + "\"";
            if (i + 1 < move_history.size()) json += ",";
        }
        json += "]}";
        return json;
    }
};
