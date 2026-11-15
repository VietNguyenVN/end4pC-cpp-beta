#!/usr/bin/env python3
# Synced lyrics for end4pC's lyrics service. Sources: lrclib, BetterLyrics, NetEase and KuGou. "auto" tries them in
# that order and keeps the first that answers; a provider name asks only that one. Usage: lyrics.py TITLE ARTIST DURATION [PROVIDER]
# Prints one line per lyric as "seconds<TAB>text", then a last line "#ok", "#not_found" or "#no_info".
import sys
import urllib.request
import urllib.parse
import json
import re
import base64
import xml.etree.ElementTree as ET

def _parse_lrc(lrc_text: str) -> list:
    lines = []
    for raw in lrc_text.splitlines():
        raw = raw.strip()
        if not raw:
            continue
        try:
            tag_end = raw.index("]")
            time_str = raw[1:tag_end]
            text = raw[tag_end + 1:].strip()
            mins, secs = time_str.split(":")
            timestamp = int(mins) * 60 + float(secs)
            lines.append({"time": timestamp, "text": text})
        except Exception:
            continue
    return sorted(lines, key=lambda x: x["time"])

def _is_match(d: dict, title: str, artist: str) -> bool:
    if not d.get("syncedLyrics"):
        return False
    r_title  = (d.get("trackName")  or "").lower()
    r_artist = (d.get("artistName") or "").lower()
    t = title.lower()
    a = artist.lower()
    title_match = (t in r_title or r_title in t or
                   any(word in r_title for word in t.split() if len(word) > 3))
    artist_match = (a in r_artist or r_artist in a or
                    any(word in r_artist for word in a.split() if len(word) > 3))
    return title_match and artist_match

def fetch_lrclib(title: str, artist: str, duration: float) -> list:
    urls = [
        f"https://lrclib.net/api/get?track_name={urllib.parse.quote(title)}&artist_name={urllib.parse.quote(artist)}&duration={int(duration)}",
        f"https://lrclib.net/api/search?track_name={urllib.parse.quote(title)}&artist_name={urllib.parse.quote(artist)}",
        f"https://lrclib.net/api/search?q={urllib.parse.quote(title + ' ' + artist)}",
    ]
    for url in urls:
        try:
            with urllib.request.urlopen(url, timeout=15) as r:
                data = json.loads(r.read().decode())
            if isinstance(data, list):
                data = next((d for d in data if _is_match(d, title, artist)), None)
            if data and _is_match(data, title, artist):
                lines = _parse_lrc(data["syncedLyrics"])
                if lines:
                    return lines
        except Exception:
            continue
    return []

def _ttml_seconds(value: str) -> float:
    total = 0.0
    for part in value.replace("s", "").split(":"):
        total = total * 60 + float(part)
    return total

def _parse_ttml(ttml: str) -> list:
    root = ET.fromstring(ttml)
    lines = []
    for p in root.iter():
        if not p.tag.endswith("}p") and p.tag != "p":
            continue
        begin = p.get("begin")
        if begin is None:
            continue
        text = re.sub(r"\s+", " ", "".join(p.itertext())).strip()
        try:
            lines.append({"time": _ttml_seconds(begin), "text": text})
        except ValueError:
            continue
    return sorted(lines, key=lambda x: x["time"])

BOIDU_BASES = ["https://api.betterlyrics.org", "https://lyrics-api.boidu.dev"]

def fetch_boidu(title: str, artist: str, duration: float) -> list:
    query = f"/getLyrics?s={urllib.parse.quote(title)}&a={urllib.parse.quote(artist)}"
    if duration > 0:
        query += f"&d={int(duration)}"
    for base in BOIDU_BASES:
        try:
            req = urllib.request.Request(base + query, headers={"User-Agent": "Mozilla/5.0"})
            with urllib.request.urlopen(req, timeout=15) as r:
                data = json.loads(r.read().decode())
            lines = _parse_ttml(data.get("ttml") or "")
            if len(lines) >= 3:
                return lines
        except Exception:
            continue
    return []

def _loose_match(a: str, b: str) -> bool:
    a, b = a.lower(), b.lower()
    return bool(a and b) and (a in b or b in a or any(w in b for w in a.split() if len(w) > 3))

def _get_json(url: str, headers: dict = None):
    req = urllib.request.Request(url, headers=headers or {"User-Agent": "Mozilla/5.0"})
    with urllib.request.urlopen(req, timeout=15) as r:
        return json.loads(r.read().decode("utf-8", "replace"))

def fetch_netease(title: str, artist: str, duration: float) -> list:
    hdr = {"User-Agent": "Mozilla/5.0", "Referer": "https://music.163.com"}
    try:
        q = urllib.parse.quote(title + " " + artist)
        songs = _get_json(f"https://music.163.com/api/search/get?s={q}&type=1&limit=8", hdr).get("result", {}).get("songs", [])
        cands = [x for x in songs if _loose_match(title, x.get("name", "")) and
                 any(_loose_match(artist, a.get("name", "")) for a in x.get("artists", []))]
        if duration > 0:
            cands.sort(key=lambda x: abs(x.get("duration", 0) / 1000 - duration))
            cands = [x for x in cands if abs(x.get("duration", 0) / 1000 - duration) < 8]
        for song in cands[:2]:
            lrc = _get_json(f"https://music.163.com/api/song/lyric?id={song['id']}&lv=1", hdr).get("lrc", {}).get("lyric") or ""
            lines = [l for l in _parse_lrc(lrc) if l["text"]]
            if len(lines) >= 3:
                return lines
    except Exception:
        pass
    return []

def fetch_kugou(title: str, artist: str, duration: float) -> list:
    try:
        q = urllib.parse.quote(title + " " + artist)
        songs = _get_json(f"http://mobilecdn.kugou.com/api/v3/search/song?format=json&keyword={q}&page=1&pagesize=8").get("data", {}).get("info", [])
        cands = [x for x in songs if _loose_match(title, x.get("songname", "")) and _loose_match(artist, x.get("singername", ""))]
        if duration > 0:
            cands.sort(key=lambda x: abs(x.get("duration", 0) - duration))
            cands = [x for x in cands if abs(x.get("duration", 0) - duration) < 8]
        for song in cands[:2]:
            found = _get_json(f"http://lyrics.kugou.com/search?ver=1&man=yes&client=pc&keyword={q}&duration={int(song.get('duration', 0)) * 1000}&hash={song['hash']}")
            for c in found.get("candidates", [])[:2]:
                got = _get_json(f"http://lyrics.kugou.com/download?ver=1&client=pc&id={c['id']}&accesskey={c['accesskey']}&fmt=lrc&charset=utf8")
                lrc = base64.b64decode(got.get("content", "")).decode("utf-8", "replace").lstrip("\ufeff")
                lines = [l for l in _parse_lrc(lrc) if l["text"]]
                if len(lines) >= 3:
                    return lines
    except Exception:
        pass
    return []

PROVIDERS = [("lrclib", fetch_lrclib), ("betterlyrics", fetch_boidu), ("netease", fetch_netease), ("kugou", fetch_kugou)]

def main():
    if len(sys.argv) < 4:
        print("#no_info", flush=True)
        return
    title = sys.argv[1]
    artist = sys.argv[2]
    try:
        duration = float(sys.argv[3])
    except ValueError:
        duration = 0.0
    if not title or not artist:
        print("#no_info", flush=True)
        return
    wanted = sys.argv[4] if len(sys.argv) > 4 else "auto"
    lines = []
    for name, fetch in PROVIDERS:
        if wanted in ("auto", name):
            lines = fetch(title, artist, duration)
            if lines:
                break
    if not lines:
        print("#not_found", flush=True)
        return
    for line in lines:
        text = line["text"].replace("\t", " ").replace("\n", " ")
        print("%.3f\t%s" % (line["time"], text))
    print("#ok", flush=True)

if __name__ == "__main__":
    main()
