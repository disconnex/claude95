#!/usr/bin/env python3
"""claude95d - the gateway for Claude 95, the Windows 95 Claude client.

Windows 95 can't speak modern TLS, so the Windows 95 PC talks plain HTTP to
this service on the local network, and this service talks to the Anthropic API.
The API key stays here. Only addresses in C95_ALLOW may connect.

Protocol (HTTP/1.0, every body is Windows-1252 text with CRLF line ends):
  GET  /hello         -> "claude95 gateway OK" plus the model name
  GET  /usage         -> today's message and search counts
  POST /chat          body = the user's message
       X-Conversation: <id>   (omit to start a new conversation)
    <- 200, X-Conversation: <id>, body = Claude's reply
  POST /assist        body = text selected in another program (may be empty)
       X-Action: rewrite|proofread|shorter|longer|formal|friendly|summarize|
                 continue|explain|custom
       X-Instruction: <the user's own instruction>   (optional; required for custom)
    <- 200, body = the finished text
  Errors come back as 4xx/5xx with a short message to show the user.

Settings come from the environment; see claude95.env.example.
"""
import ipaddress
import json
import logging
import os
import re
import secrets
import sys
import threading
import time
import unicodedata
from datetime import date, datetime
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

import anthropic

HOST = os.environ.get("C95_HOST", "0.0.0.0")
PORT = int(os.environ.get("C95_PORT", "8095"))
ALLOW = [ipaddress.ip_network(a.strip(), strict=False)
         for a in os.environ.get("C95_ALLOW", "").split(",") if a.strip()]
MODEL = os.environ.get("C95_MODEL", "claude-haiku-5-5")
EFFORT = os.environ.get("C95_EFFORT", "medium")
DAILY_MESSAGES = int(os.environ.get("C95_DAILY_MESSAGES", "300"))
DAILY_SEARCHES = int(os.environ.get("C95_DAILY_SEARCHES", "40"))
MACHINE = os.environ.get("C95_MACHINE", "a vintage PC running Windows 95")
# systemd's StateDirectory= sets STATE_DIRECTORY; otherwise use the current directory.
STATE = os.environ.get("C95_STATE", os.path.join(os.environ.get("STATE_DIRECTORY", "."), "usage.json"))

MAX_MESSAGE = 60000        # characters in one message (room for Word documents)
MAX_TURNS = 80             # messages kept in one conversation
IDLE_EXPIRY = 12 * 3600    # forget conversations idle this long
MAX_TOKENS = 4000
MAX_ASSIST_TOKENS = 8000   # room to rewrite a few pages
MAX_PAUSES = 3             # pause_turn resumptions per reply

SYSTEM = f"""You are Claude, an AI assistant made by Anthropic, running inside "Claude 95": a chat program on {MACHINE}. Your replies are relayed through a small gateway server on the local network and shown in a plain Windows 95 text box.

Formatting rules, because the text box can't render anything else:
- Plain text only. No Markdown: no **bold**, no # headings, no tables, no code fences.
- For lists use simple lines starting with "- " or "1. ".
- No emoji. Stick to characters a Windows 95 font can show.
- Keep answers reasonably short; the screen may be 640x480 or 800x600. Go longer only when asked.

You can search the web when a question needs current or specific information. Don't search for things you already know well.

You know Windows 95, DOS and 90s hardware and software well, and can help with the machine itself: drivers, IRQs, CONFIG.SYS/AUTOEXEC.BAT, Office 95 and period software. Only bring up the machine or retro computing when it's relevant to what was asked."""

ASSIST_SYSTEM = """You are a writing assistant built into "Claude 95", working on text from a Windows 95 program such as Word 95, WordPad or Notepad. The user selected some text and picked an action.

Return only the finished text, ready to drop into their document:
- No preamble or commentary (never "Here's the rewritten text:"), no quotation marks around it, no notes afterwards.
- Plain text only, no Markdown. Put each paragraph on its own line, without blank lines between paragraphs unless the original had them.
- Keep the author's voice, language, names and facts unless the action asks otherwise.
- Don't bring in people, names or details that aren't in the text or the instruction, and don't assume anyone's gender: keep the pronouns the text uses.
- For summaries and explanations, write the summary or explanation itself, briefly.
If the request can't be done, reply with one short plain sentence saying why."""

# The writing actions the client offers. "custom" uses the user's own instruction.
ACTIONS = {
    "rewrite": "Rewrite this text so it reads better: clearer and smoother, same meaning, similar length.",
    "proofread": "Fix the spelling, grammar and punctuation in this text. Change nothing else.",
    "shorter": "Make this text shorter and tighter, keeping the key points.",
    "longer": "Expand this text with more detail and examples, in the same voice.",
    "formal": "Rewrite this text in a more formal, professional tone.",
    "friendly": "Rewrite this text in a warmer, friendlier tone.",
    "summarize": "Summarize this text in a few sentences.",
    "continue": "Continue writing from where this text leaves off, in the same voice and style: "
                "one or two paragraphs. Return only the new text, not the original.",
    "explain": "Explain what this text means, in plain language.",
    "custom": "",
}

log = logging.getLogger("claude95d")
client = anthropic.Anthropic()
lock = threading.Lock()
conversations = {}   # id -> {"messages": [...], "seen": time}


# ------------------------------------------------------------------ usage
def load_usage():
    try:
        with open(STATE) as f:
            u = json.load(f)
        if u.get("day") == date.today().isoformat():
            return u
    except (OSError, ValueError):
        pass
    return {"day": date.today().isoformat(), "messages": 0, "searches": 0,
            "input_tokens": 0, "output_tokens": 0}


usage = load_usage()


def save_usage():
    tmp = STATE + ".tmp"
    with open(tmp, "w") as f:
        json.dump(usage, f)
    os.replace(tmp, STATE)


def roll_day():
    global usage
    if usage["day"] != date.today().isoformat():
        usage = load_usage()


# ------------------------------------------------------------------ text
# Characters Windows-1252 lacks, mapped to something readable.
SUBST = {"→": "->", "←": "<-", "⇒": "=>", "≤": "<=", "≥": ">=",
         "≠": "!=", "≈": "~", "−": "-", "✓": "v", "✔": "v",
         "✗": "x", " ": " ", " ": " ", " ": " "}


def to_win95(text):
    """Strip Markdown leftovers and encode for a Windows 95 edit control."""
    text = re.sub(r"\*\*(.+?)\*\*", r"\1", text)
    text = re.sub(r"(?m)^#{1,6}\s+", "", text)
    text = re.sub(r"(?m)^```\w*\s*$\n?", "", text)
    text = "".join(SUBST.get(c, c) for c in text)
    out = []
    for c in text:
        try:
            c.encode("cp1252")
            out.append(c)
        except UnicodeEncodeError:
            plain = unicodedata.normalize("NFKD", c).encode("cp1252", "ignore").decode("cp1252")
            out.append(plain or "?")
    text = "".join(out).replace("\r\n", "\n").strip()
    return text.replace("\n", "\r\n").encode("cp1252")


def from_win95(body):
    return body.decode("cp1252", "replace").replace("\r\n", "\n").strip()


# ------------------------------------------------------------------ Claude
def tools(searches_left):
    if searches_left <= 0:
        return []
    return [{"type": "web_search_20250305", "name": "web_search",
             "max_uses": min(3, searches_left)}]


def call_claude(messages, system_text, max_tokens, search=True, cut_note=""):
    """Gets one reply, appending the assistant turn(s) to messages.
    Returns (reply, searches); reply is None if Claude declined."""
    system = [{"type": "text", "text": system_text, "cache_control": {"type": "ephemeral"}},
              {"type": "text", "text": f"Today is {datetime.now():%A %B %d %Y}."}]
    searches = 0
    for _ in range(MAX_PAUSES + 1):
        left = DAILY_SEARCHES - usage["searches"] - searches if search else 0
        resp = client.messages.create(
            model=MODEL, max_tokens=max_tokens, system=system,
            messages=messages, tools=tools(left),
            thinking={"type": "disabled"}, output_config={"effort": EFFORT},
            cache_control={"type": "ephemeral"})
        u = resp.usage
        stu = getattr(u, "server_tool_use", None)
        searches += (getattr(stu, "web_search_requests", 0) or 0) if stu else 0
        with lock:
            usage["input_tokens"] += (u.input_tokens or 0) + (u.cache_read_input_tokens or 0) \
                + (u.cache_creation_input_tokens or 0)
            usage["output_tokens"] += u.output_tokens or 0
        log.info("model call: stop=%s in=%s cached=%s out=%s searches=%s", resp.stop_reason,
                 u.input_tokens, u.cache_read_input_tokens, u.output_tokens, searches)
        messages.append({"role": "assistant", "content": resp.content})
        if resp.stop_reason != "pause_turn":
            break
        # A paused server-tool turn resumes when the assistant turn is sent back as-is.
    with lock:
        usage["searches"] += searches

    if resp.stop_reason == "refusal":
        return None, searches
    parts, sources = [], []
    # Text before the last search ("I'll look that up") is narration, not the answer.
    last_tool = max((i for i, b in enumerate(resp.content) if b.type == "server_tool_use"), default=-1)
    for block in resp.content[last_tool + 1:]:
        if block.type != "text":
            continue
        parts.append(block.text)
        for c in getattr(block, "citations", None) or []:
            url = getattr(c, "url", None)
            if url and url not in [s[1] for s in sources]:
                sources.append((getattr(c, "title", None) or url, url))
    reply = "".join(parts).strip() or "(no reply)"
    if resp.stop_reason == "max_tokens":
        reply += cut_note
    if sources:
        reply += "\n\nSources:\n" + "\n".join(f"{i}. {t} - {u}" for i, (t, u) in enumerate(sources[:6], 1))
    return reply, searches


def ask(conv, text):
    """A chat turn in an ongoing conversation."""
    start = len(conv["messages"])
    conv["messages"].append({"role": "user", "content": text})
    reply, searches = call_claude(conv["messages"], SYSTEM, MAX_TOKENS,
                                  cut_note="\n\n[Reply cut off - ask me to continue.]")
    if reply is None:
        del conv["messages"][start:]
        reply = "Sorry, I can't help with that one."
    return reply, searches


def assist(action, instruction, text):
    """A one-off writing task on text selected in another program."""
    task = instruction or ACTIONS[action]
    content = f"{task}\n\n<text>\n{text}\n</text>" if text else task
    reply, searches = call_claude(
        [{"role": "user", "content": content}], ASSIST_SYSTEM, MAX_ASSIST_TOKENS,
        search=action in ("explain", "custom"),
        cut_note="\n\n[Claude 95: the result was cut off. Try a shorter selection.]")
    return (reply or "Sorry, I can't help with that one."), searches


def api_error_text(e):
    if isinstance(e, anthropic.APIStatusError):
        log.error("API error %s: %s", e.status_code, e.message)
        return f"Claude's servers returned an error ({e.status_code}). Try again shortly."
    log.error("API connection error: %s", e)
    return "The gateway couldn't reach Claude's servers. Check the internet connection."


# ------------------------------------------------------------------ HTTP
class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.0"
    server_version = "claude95d/1.0"

    def log_message(self, fmt, *args):
        log.info("%s %s", self.client_address[0], fmt % args)

    def send_text(self, code, text, headers=None):
        body = to_win95(text) if isinstance(text, str) else text
        self.send_response(code)
        self.send_header("Content-Type", "text/plain; charset=windows-1252")
        self.send_header("Content-Length", str(len(body)))
        for k, v in (headers or {}).items():
            self.send_header(k, v)
        self.end_headers()
        self.wfile.write(body)

    def allowed(self):
        addr = ipaddress.ip_address(self.client_address[0])
        if any(addr in net for net in ALLOW):
            return True
        log.warning("refused %s", self.client_address[0])
        self.send_text(403, "This gateway doesn't accept connections from this address.")
        return False

    def do_GET(self):
        if not self.allowed():
            return
        roll_day()
        if self.path == "/hello":
            self.send_text(200, f"claude95 gateway OK\nmodel {MODEL}")
        elif self.path == "/usage":
            self.send_text(200, f"Today ({usage['day']}):\n"
                                f"messages {usage['messages']} of {DAILY_MESSAGES}\n"
                                f"web searches {usage['searches']} of {DAILY_SEARCHES}\n"
                                f"tokens in {usage['input_tokens']}, out {usage['output_tokens']}")
        else:
            self.send_text(404, "Unknown request.")

    def do_POST(self):
        if not self.allowed():
            return
        if self.path not in ("/chat", "/assist"):
            return self.send_text(404, "Unknown request.")
        try:
            length = int(self.headers.get("Content-Length", "0"))
        except ValueError:
            length = -1
        if length < 0 or length > MAX_MESSAGE:
            return self.send_text(400, f"Text must be under {MAX_MESSAGE} characters.")
        text = from_win95(self.rfile.read(length)) if length else ""
        roll_day()
        if usage["messages"] >= DAILY_MESSAGES:
            return self.send_text(429, f"Daily limit of {DAILY_MESSAGES} messages reached. "
                                       "It resets at midnight.")
        if self.path == "/assist":
            return self.do_assist(text)
        if not text:
            return self.send_text(400, "Empty message.")

        now = time.time()
        with lock:
            for cid in [c for c, v in conversations.items() if now - v["seen"] > IDLE_EXPIRY]:
                del conversations[cid]
            cid = self.headers.get("X-Conversation", "").strip()
            if cid not in conversations:
                cid = secrets.token_hex(8)
                conversations[cid] = {"messages": [], "seen": now, "busy": False}
            conv = conversations[cid]
            if conv["busy"]:
                return self.send_text(409, "Still working on your last message.")
            if len(conv["messages"]) >= MAX_TURNS:
                return self.send_text(413, "This conversation is full. Start a new chat (File > New Chat).")
            conv["busy"] = True
            conv["seen"] = now
            usage["messages"] += 1
        before = len(conv["messages"])
        try:
            reply, searches = ask(conv, text)
        except (anthropic.APIStatusError, anthropic.APIConnectionError) as e:
            del conv["messages"][before:]
            return self.send_text(502, api_error_text(e))
        finally:
            conv["busy"] = False
            with lock:
                save_usage()
        self.send_text(200, reply, {"X-Conversation": cid, "X-Searches": str(searches)})

    def do_assist(self, text):
        action = self.headers.get("X-Action", "rewrite").strip().lower()
        # Header values arrive as Latin-1; the client sent Windows-1252.
        instruction = self.headers.get("X-Instruction", "").encode("latin-1", "replace") \
            .decode("cp1252", "replace").strip()[:500]
        if action not in ACTIONS:
            return self.send_text(400, f"Unknown action: {action}")
        if action == "custom" and not instruction:
            return self.send_text(400, "Tell Claude what to do.")
        if not text and not instruction:
            return self.send_text(400, "Select some text first, or tell Claude what to write.")
        with lock:
            usage["messages"] += 1
        try:
            reply, searches = assist(action, instruction, text)
        except (anthropic.APIStatusError, anthropic.APIConnectionError) as e:
            return self.send_text(502, api_error_text(e))
        finally:
            with lock:
                save_usage()
        self.send_text(200, reply, {"X-Searches": str(searches)})


def main():
    logging.basicConfig(level=logging.INFO, format="%(levelname)s %(message)s")
    if not ALLOW:
        sys.exit("Set C95_ALLOW to the Windows 95 PC's address (see claude95.env.example).")
    srv = ThreadingHTTPServer((HOST, PORT), Handler)
    srv.daemon_threads = True
    log.info("listening on %s:%d for %s, model %s", HOST, PORT, ", ".join(map(str, ALLOW)), MODEL)
    srv.serve_forever()


if __name__ == "__main__":
    main()
