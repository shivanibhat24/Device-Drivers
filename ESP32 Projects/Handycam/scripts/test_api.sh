#!/usr/bin/env bash
# test_api.sh — Quick curl-based tests for Handycam HTTP API
# Usage: ./test_api.sh 192.168.1.42

IP=${1:-"192.168.1.42"}
BASE="http://$IP"

echo "═══════════════════════════════════════"
echo "  HANDYCAM API TEST   target: $IP"
echo "═══════════════════════════════════════"

echo -e "\n[1] Status check"
curl -s "$BASE/status" | python3 -m json.tool

echo -e "\n[2] Single shot"
curl -s -X POST "$BASE/execute" \
  -H "Content-Type: application/json" \
  -d '{"intent":"single","count":1,"delay_ms":0,"filter":"none"}' | python3 -m json.tool

echo -e "\n[3] Cinematic burst (5 shots, 2s delay, warm filter)"
curl -s -X POST "$BASE/execute" \
  -H "Content-Type: application/json" \
  -d '{"intent":"burst_capture","count":5,"delay_ms":2000,"interval_ms":300,"filter":"cinematic"}' | python3 -m json.tool

echo -e "\n[4] Timed sequence — sepia, 3 shots every 2s"
curl -s -X POST "$BASE/execute" \
  -H "Content-Type: application/json" \
  -d '{"intent":"timed_sequence","count":3,"delay_ms":0,"interval_ms":2000,"filter":"sepia"}' | python3 -m json.tool

echo -e "\n[5] Stop signal"
curl -s -X POST "$BASE/stop" | python3 -m json.tool

echo -e "\nDone."
