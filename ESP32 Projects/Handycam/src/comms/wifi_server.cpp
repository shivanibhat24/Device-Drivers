/**
 * wifi_server.cpp
 *
 * Endpoints:
 *   POST /execute   — send a CommandPayload JSON body
 *   POST /stop      — set g_stop_requested = true
 *   GET  /status    — returns current state as JSON
 *   GET  /           — returns HTML info page
 */

#include "wifi_server.h"
#include "../system/config.h"
#include "../execution/command_parser.h"
#include "../system/state_machine.h"

#include <WiFi.h>
#include <WebServer.h>
#include <Arduino.h>

extern volatile bool  g_cmd_ready;
extern CommandPayload g_pending_cmd;
extern HandycamState  g_state;

volatile bool g_stop_requested = false;

static WebServer server(HTTP_PORT);

// ─── Handlers ────────────────────────────────────────────────

static void handle_execute() {
    if (!server.hasArg("plain")) {
        server.send(400, "application/json", R"({"error":"empty body"})");
        return;
    }

    CommandPayload cmd = parse_command(server.arg("plain"));
    if (!cmd.valid) {
        server.send(422, "application/json", R"({"error":"invalid command"})");
        return;
    }

    if (g_state != STATE_IDLE) {
        server.send(409, "application/json", R"({"error":"busy"})");
        return;
    }

    g_pending_cmd = cmd;
    g_cmd_ready   = true;

    String resp = R"({"status":"accepted","intent":")" +
                  String(intent_name(cmd.intent)) + R"(","count":)" +
                  String(cmd.count) + "}";
    server.send(202, "application/json", resp);
}

static void handle_stop() {
    g_stop_requested = true;
    server.send(200, "application/json", R"({"status":"stop_requested"})");
}

static void handle_status() {
    String resp = R"({"state":")" + String(state_name(g_state)) + R"("})";
    server.send(200, "application/json", resp);
}

static void handle_root() {
    server.send(200, "text/html",
        F("<h2>Handycam</h2>"
          "<p>POST /execute — send capture command JSON</p>"
          "<p>POST /stop   — abort current sequence</p>"
          "<p>GET  /status — poll device state</p>"));
}

// ─── Public API ──────────────────────────────────────────────

void wifi_server_begin() {
    Serial.printf("[WIFI] Connecting to %s ", WIFI_SSID);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    unsigned long t0 = millis();
    while (WiFi.status() != WL_CONNECTED) {
        delay(500); Serial.print('.');
        if (millis() - t0 > 15000) {
            Serial.println(F("\n[WIFI] Connection timeout — check credentials"));
            return;
        }
    }
    Serial.printf("\n[WIFI] Connected, IP: %s\n", WiFi.localIP().toString().c_str());

    server.on("/",        HTTP_GET,  handle_root);
    server.on("/execute", HTTP_POST, handle_execute);
    server.on("/stop",    HTTP_POST, handle_stop);
    server.on("/status",  HTTP_GET,  handle_status);
    server.begin();
}

void wifi_server_handle() {
    server.handleClient();
}
