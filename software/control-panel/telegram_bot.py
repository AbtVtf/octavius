"""
Octavius Telegram Bridge

Allows Octo to send and receive messages via Telegram.
Uses raw HTTP API — no extra dependencies needed.
"""

import requests
import threading
import time
import json

BOT_TOKEN = "8631102808:AAFLoFRzrNBfGT63ganYE9g656zFbWh7JiQ"
API_URL = f"https://api.telegram.org/bot{BOT_TOKEN}"
OWNER_CHAT_ID = None  # auto-detected from first message


def _api(method, **kwargs):
    try:
        resp = requests.post(f"{API_URL}/{method}", json=kwargs, timeout=30)
        data = resp.json()
        if data.get("ok"):
            return data.get("result")
        return None
    except:
        return None


def send_message(text, chat_id=None):
    """Send a message to the owner via Telegram."""
    cid = chat_id or OWNER_CHAT_ID
    if not cid:
        print("  [telegram] No chat ID yet — waiting for first message from owner")
        return False
    _api("sendMessage", chat_id=cid, text=text)
    return True


class TelegramPoller:
    """Polls Telegram for incoming messages and queues them."""

    def __init__(self, on_message=None, on_ip=None, on_mode=None):
        self.on_message = on_message
        self.on_ip = on_ip
        self.on_mode = on_mode
        self._running = False
        self._offset = 0

    def start(self):
        self._running = True
        t = threading.Thread(target=self._poll_loop, daemon=True)
        t.start()
        # Get bot info
        me = _api("getMe")
        if me:
            print(f"  [telegram bot ready: @{me.get('username', '?')}]")
        else:
            print(f"  [telegram bot: could not connect]")

    def stop(self):
        self._running = False

    def _poll_loop(self):
        global OWNER_CHAT_ID
        while self._running:
            try:
                result = _api("getUpdates", offset=self._offset, timeout=20)
                if not result:
                    time.sleep(5)
                    continue

                for update in result:
                    self._offset = update["update_id"] + 1
                    msg = update.get("message")
                    if not msg or not msg.get("text"):
                        continue

                    chat_id = msg["chat"]["id"]
                    text = msg["text"]
                    sender = msg.get("from", {}).get("first_name", "Unknown")

                    # Auto-detect owner from first message
                    if OWNER_CHAT_ID is None:
                        OWNER_CHAT_ID = chat_id
                        print(f"  [telegram] Owner detected: {sender} (chat_id={chat_id})")

                    # Only process messages from owner
                    if chat_id != OWNER_CHAT_ID:
                        _api("sendMessage", chat_id=chat_id, text="I only talk to my creator.")
                        continue

                    print(f"  [telegram] {sender}: {text}")

                    text_lower = text.strip().lower()

                    # Check for IP command: "ip 10.203.172.104"
                    if text_lower.startswith("ip "):
                        ip = text.strip().split()[1]
                        print(f"  [telegram] Watcher IP set to: {ip}")
                        _api("sendMessage", chat_id=chat_id, text=f"Got it — connecting to {ip}")
                        if self.on_ip:
                            self.on_ip(ip)
                        continue

                    # Check for mode commands
                    if text_lower in ("wake", "wake up", "normal", "normal mode"):
                        _api("sendMessage", chat_id=chat_id, text="Waking up!")
                        if self.on_mode:
                            self.on_mode("normal")
                        continue
                    elif text_lower in ("silent", "silent mode", "shut up", "mute"):
                        _api("sendMessage", chat_id=chat_id, text="Going silent.")
                        if self.on_mode:
                            self.on_mode("silent")
                        continue
                    elif text_lower in ("dnd", "do not disturb", "focus"):
                        _api("sendMessage", chat_id=chat_id, text="DND mode — thinking only.")
                        if self.on_mode:
                            self.on_mode("dnd")
                        continue
                    elif text_lower in ("chill", "chill mode", "relax"):
                        _api("sendMessage", chat_id=chat_id, text="Chilling...")
                        if self.on_mode:
                            self.on_mode("chill")
                        continue
                    elif text_lower in ("road", "road mode", "on the road", "wakeword"):
                        _api("sendMessage", chat_id=chat_id, text="Road mode — wake word only.")
                        if self.on_mode:
                            self.on_mode("road")
                        continue

                    if self.on_message:
                        self.on_message(text, chat_id)

            except Exception as e:
                print(f"  [telegram] poll error: {e}")
                time.sleep(10)
