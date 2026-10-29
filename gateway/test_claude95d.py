"""Tests for claude95d with a fake Anthropic client: no API key or network needed.

    python -m unittest -v test_claude95d   (from gateway/, with requirements installed)
"""
import http.client
import os
import tempfile
import threading
import unittest
from types import SimpleNamespace as NS

os.environ.update(C95_ALLOW="127.0.0.1", ANTHROPIC_API_KEY="test-key",
                  C95_STATE=os.path.join(tempfile.mkdtemp(), "usage.json"))
import anthropic  # noqa: E402
import claude95d  # noqa: E402


def text(t, citations=None):
    return NS(type="text", text=t, citations=citations)


def response(*content, stop="end_turn", searches=0):
    return NS(content=list(content), stop_reason=stop,
              usage=NS(input_tokens=10, output_tokens=5, cache_read_input_tokens=0,
                       cache_creation_input_tokens=0,
                       server_tool_use=NS(web_search_requests=searches)))


class FakeMessages:
    def __init__(self):
        self.replies = []
        self.calls = []

    def create(self, **kw):
        self.calls.append({**kw, "messages": list(kw["messages"])})
        r = self.replies.pop(0)
        if isinstance(r, Exception):
            raise r
        return r


class GatewayTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.server = claude95d.ThreadingHTTPServer(("127.0.0.1", 0), claude95d.Handler)
        threading.Thread(target=cls.server.serve_forever, daemon=True).start()
        cls.port = cls.server.server_address[1]

    @classmethod
    def tearDownClass(cls):
        cls.server.shutdown()

    def setUp(self):
        self.fake = FakeMessages()
        claude95d.client = NS(messages=self.fake)
        claude95d.conversations.clear()
        claude95d.usage.update(messages=0, searches=0)

    def request(self, method, path, body=None, headers=None):
        c = http.client.HTTPConnection("127.0.0.1", self.port, timeout=10)
        c.request(method, path, body=body, headers=headers or {})
        r = c.getresponse()
        data = r.read().decode("cp1252")
        c.close()
        return r, data

    def chat(self, msg, conv=None):
        return self.request("POST", "/chat", msg.encode("cp1252"),
                            {"X-Conversation": conv} if conv else {})

    def test_hello(self):
        r, body = self.request("GET", "/hello")
        self.assertEqual(r.status, 200)
        self.assertIn("claude95 gateway OK", body)

    def test_refuses_addresses_outside_allow(self):
        saved = claude95d.ALLOW
        claude95d.ALLOW = [claude95d.ipaddress.ip_network("10.0.0.0/8")]
        try:
            r, _ = self.request("GET", "/hello")
            self.assertEqual(r.status, 403)
        finally:
            claude95d.ALLOW = saved

    def test_conversation_continues(self):
        self.fake.replies = [response(text("Hello!")), response(text("You said hi."))]
        r, body = self.chat("hi")
        self.assertEqual((r.status, body), (200, "Hello!"))
        cid = r.getheader("X-Conversation")
        r, body = self.chat("what did I say?", cid)
        self.assertEqual(r.getheader("X-Conversation"), cid)
        self.assertEqual(len(self.fake.calls[1]["messages"]), 3)

    def test_reply_is_plain_windows_text(self):
        self.fake.replies = [response(text("## Title\n**bold** → done \U0001F600\n- item"))]
        _, body = self.chat("x")
        self.assertEqual(body, "Title\r\nbold -> done ?\r\n- item")

    def test_search_narration_dropped_and_sources_listed(self):
        cite = NS(url="https://example.com/a", title="Example")
        self.fake.replies = [response(text("Let me look."), NS(type="server_tool_use"),
                                      NS(type="web_search_tool_result"),
                                      text("It is 7.3.", [cite]), searches=1)]
        r, body = self.chat("latest kernel?")
        self.assertTrue(body.startswith("It is 7.3."))
        self.assertIn("Sources:\r\n1. Example - https://example.com/a", body)
        self.assertEqual(r.getheader("X-Searches"), "1")
        self.assertEqual(claude95d.usage["searches"], 1)

    def test_no_search_tool_when_out_of_searches(self):
        claude95d.usage["searches"] = claude95d.DAILY_SEARCHES
        self.fake.replies = [response(text("ok"))]
        self.chat("x")
        self.assertEqual(self.fake.calls[0]["tools"], [])

    def test_daily_message_limit(self):
        claude95d.usage["messages"] = claude95d.DAILY_MESSAGES
        r, _ = self.chat("x")
        self.assertEqual(r.status, 429)

    def test_api_error_rolls_back_history(self):
        err = anthropic.APIStatusError("boom", response=NS(status_code=529, headers={}, request=None), body=None)
        err.status_code = 529
        self.fake.replies = [response(text("first")), err]
        r, _ = self.chat("one")
        cid = r.getheader("X-Conversation")
        r, body = self.chat("two", cid)
        self.assertEqual(r.status, 502)
        self.assertEqual(len(claude95d.conversations[cid]["messages"]), 2)

    def test_refusal_rolls_back_history(self):
        self.fake.replies = [response(text(""), stop="refusal")]
        r, body = self.chat("x")
        self.assertIn("can't help", body)
        self.assertEqual(claude95d.conversations[r.getheader("X-Conversation")]["messages"], [])

    def test_rejects_empty_and_oversized(self):
        r, _ = self.request("POST", "/chat", b"", {"Content-Length": "0"})
        self.assertEqual(r.status, 400)
        r, _ = self.request("POST", "/chat", b"x" * (claude95d.MAX_MESSAGE + 1))
        self.assertEqual(r.status, 400)

    def assist(self, text, action="rewrite", instruction=None):
        headers = {"X-Action": action}
        if instruction:
            headers["X-Instruction"] = instruction
        return self.request("POST", "/assist", text.encode("cp1252"), headers)

    def test_assist_uses_action_prompt_without_search(self):
        self.fake.replies = [response(text("Better text."))]
        r, body = self.assist("rough text", "proofread")
        self.assertEqual((r.status, body), (200, "Better text."))
        call = self.fake.calls[0]
        self.assertIn("Fix the spelling", call["messages"][0]["content"])
        self.assertIn("<text>\nrough text\n</text>", call["messages"][0]["content"])
        self.assertEqual(call["tools"], [])
        self.assertIn("writing assistant", call["system"][0]["text"])

    def test_assist_custom_instruction_without_text(self):
        self.fake.replies = [response(text("Dear Sir,"))]
        r, body = self.assist("", "custom", "Write a short letter")
        self.assertEqual(r.status, 200)
        self.assertEqual(self.fake.calls[0]["messages"][0]["content"], "Write a short letter")
        self.assertNotEqual(self.fake.calls[0]["tools"], [])

    def test_assist_rejects_bad_requests(self):
        self.assertEqual(self.assist("x", "dance")[0].status, 400)
        self.assertEqual(self.assist("x", "custom")[0].status, 400)
        self.assertEqual(self.assist("", "rewrite")[0].status, 400)

    def test_assist_counts_toward_daily_limit(self):
        self.fake.replies = [response(text("ok"))]
        self.assist("x")
        self.assertEqual(claude95d.usage["messages"], 1)


if __name__ == "__main__":
    unittest.main()
