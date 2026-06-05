#!/usr/bin/env python3
"""
handycam_nlp.py — Desktop NLP layer for Handycam
Converts natural-language phrases into structured JSON commands
and POSTs them to the ESP32-CAM HTTP server.

Requires:  pip install openai requests   (or anthropic, google-cloud-speech, etc.)
Usage:     python handycam_nlp.py --ip 192.168.1.42
           then type commands at the prompt.
"""

import argparse
import json
import re
import requests
import sys

# ─── Simple rule-based NLP (no API key needed for demo) ──────

def parse_intent(text: str) -> str:
    t = text.lower()
    if any(w in t for w in ["burst", "rapid", "quick", "fast"]):
        return "burst_capture"
    if any(w in t for w in ["continuous", "until i say", "keep"]):
        return "continuous"
    if any(w in t for w in ["sequence", "shots", "times", "photos"]):
        return "timed_sequence"
    return "single"

def parse_count(text: str) -> int:
    # Match "5 shots", "a few" → 5, "some" → 3, explicit number
    match = re.search(r'\b(\d+)\s*(shot|photo|frame|image|pic)', text, re.I)
    if match:
        return int(match.group(1))
    if re.search(r'\b(a few|few)\b', text, re.I):
        return 5
    if re.search(r'\bsome\b', text, re.I):
        return 3
    if re.search(r'\ba (shot|photo|frame)\b', text, re.I):
        return 1
    return 1

def parse_delay(text: str) -> int:
    """Returns delay in milliseconds."""
    match = re.search(r'after\s+(\d+)\s*(second|sec|s\b)', text, re.I)
    if match:
        return int(match.group(1)) * 1000
    match = re.search(r'in\s+(\d+)\s*(second|sec|s\b)', text, re.I)
    if match:
        return int(match.group(1)) * 1000
    return 0

def parse_interval(text: str) -> int:
    """Returns inter-shot interval in milliseconds."""
    match = re.search(r'every\s+(\d+(\.\d+)?)\s*(second|sec|s\b)', text, re.I)
    if match:
        return int(float(match.group(1)) * 1000)
    if "slow" in text.lower():
        return 3000
    return 0

def parse_filter(text: str) -> str:
    t = text.lower()
    if "cinematic" in t:
        return "cinematic"
    if "sepia" in t or "vintage" in t or "retro" in t:
        return "vintage" if "vintage" in t or "retro" in t else "sepia"
    if "warm" in t:
        return "warm"
    if "cool" in t or "cold" in t:
        return "cool"
    if "gray" in t or "grey" in t or "black and white" in t or "bw" in t:
        return "grayscale"
    return "none"

def nlp_to_command(text: str) -> dict:
    return {
        "intent":      parse_intent(text),
        "count":       parse_count(text),
        "delay_ms":    parse_delay(text),
        "interval_ms": parse_interval(text),
        "filter":      parse_filter(text),
    }

# ─── HTTP transport ──────────────────────────────────────────

def send_command(ip: str, port: int, cmd: dict) -> None:
    url = f"http://{ip}:{port}/execute"
    print(f"\n→ Sending to {url}")
    print(f"  Payload: {json.dumps(cmd, indent=2)}")
    try:
        r = requests.post(url, json=cmd, timeout=5)
        print(f"  Response [{r.status_code}]: {r.text}")
    except requests.exceptions.ConnectionError:
        print(f"  [ERROR] Could not connect to {url}")

def send_stop(ip: str, port: int) -> None:
    try:
        r = requests.post(f"http://{ip}:{port}/stop", timeout=3)
        print(f"  Stop → [{r.status_code}]: {r.text}")
    except Exception as e:
        print(f"  [ERROR] {e}")

# ─── REPL ────────────────────────────────────────────────────

def main():
    ap = argparse.ArgumentParser(description="Handycam NLP client")
    ap.add_argument("--ip",   default="192.168.1.1",  help="ESP32-CAM IP address")
    ap.add_argument("--port", default=80, type=int,    help="HTTP port")
    args = ap.parse_args()

    print("╔══════════════════════════════════════╗")
    print("║       HANDYCAM NLP CLIENT            ║")
    print(f"║  Target: {args.ip}:{args.port:<22}║")
    print("║  Type 'stop' to abort, 'quit' to exit║")
    print("╚══════════════════════════════════════╝\n")

    while True:
        try:
            text = input("🎤  > ").strip()
        except (EOFError, KeyboardInterrupt):
            print("\nBye.")
            sys.exit(0)

        if not text:
            continue
        if text.lower() in ("quit", "exit", "q"):
            sys.exit(0)
        if text.lower() == "stop":
            send_stop(args.ip, args.port)
            continue

        cmd = nlp_to_command(text)
        send_command(args.ip, args.port, cmd)

if __name__ == "__main__":
    main()
