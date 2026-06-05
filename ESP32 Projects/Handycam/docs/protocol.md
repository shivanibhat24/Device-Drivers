# Handycam HTTP Protocol Reference

## Base URL

```
http://<ESP32-IP>:<PORT>
```

Default port: **80**

---

## Endpoints

### POST /execute — Trigger capture

**Request body (application/json):**

| Field | Type | Required | Range | Default | Description |
|-------|------|----------|-------|---------|-------------|
| `intent` | string | ✅ | see below | — | Capture mode |
| `count` | int | ✅ | 1–50 | 1 | Number of frames |
| `delay_ms` | int | ❌ | 0–60000 | 0 | Pre-capture delay (ms) |
| `interval_ms` | int | ❌ | 0–10000 | mode default | Inter-shot interval (ms) |
| `filter` | string | ❌ | see below | `"none"` | Post-processing preset |
| `brightness` | int | ❌ | -127–+127 | 0 | Only used with `filter: brightness` |

**Intent values:**

| Value | Behaviour |
|-------|-----------|
| `single` | One shot |
| `burst_capture` | Rapid-fire (default 150 ms interval) |
| `timed_sequence` | N shots at controlled interval (default 1500 ms) |
| `continuous` | Loop until `/stop` received |

**Filter values:** `none`, `grayscale`, `sepia`, `cinematic`, `vintage`, `warm`, `cool`, `brightness`

**Example — timed cinematic sequence:**
```json
POST /execute
{
  "intent":      "timed_sequence",
  "count":        5,
  "delay_ms":     10000,
  "interval_ms":  1500,
  "filter":       "cinematic"
}
```

**Response 202:**
```json
{ "status": "accepted", "intent": "timed_sequence", "count": 5 }
```

---

### POST /stop — Abort sequence

No body required.

```json
{ "status": "stop_requested" }
```

---

### GET /status — Poll state

```json
{ "state": "CAPTURE_SEQUENCE" }
```

State values: `IDLE`, `CMD_RECEIVED`, `PARSE_INTENT`, `SCHEDULE_ACTION`,
`CAPTURE_SEQUENCE`, `APPLY_FILTER`, `SEND_RESULT`, `ERROR`

---

## Error Codes

| HTTP Code | JSON body | Cause |
|-----------|-----------|-------|
| 400 | `{"error":"empty body"}` | No JSON in POST body |
| 409 | `{"error":"busy"}` | Sequence already running |
| 422 | `{"error":"invalid command"}` | JSON parsed but command invalid |
