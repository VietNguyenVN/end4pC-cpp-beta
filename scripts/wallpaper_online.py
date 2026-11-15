#!/usr/bin/env python3
"""end4pC online wallpapers: Wallhaven, Unsplash and Pexels.

API keys live in the system keyring (libsecret: GNOME Keyring, KWallet, KeePassXC...) through `secret-tool`, one entry per
provider (attributes application=end4pC provider=<name>). They are never written to a file, never put in a command line
(`ps` shows arguments) and never printed: this script reads one only to put it in a request header.

Usage:
  wallpaper_online.py --status                          keyring=ok|missing|error, then provider=1|0 (a key is stored)
  wallpaper_online.py --key-set PROVIDER                the key on stdin
  wallpaper_online.py --key-clear PROVIDER
  wallpaper_online.py --search PROVIDER QUERY PAGE WIDTH THUMBDIR [SORT [SEED]]
        WIDTH is the screen width wanted (1920, 2560, 3840); SORT only matters for Wallhaven:
        toplist, hot, date_added (latest), random, views, favorites, relevance.
        tab separated lines on stdout. First: "pages<TAB>N<TAB>SEED" and then one line per picture
        (id, thumbnail file, full url, download location, width, height, author, title);
        or one line "error<TAB>auth(0|1)<TAB>message"
  wallpaper_online.py --download PROVIDER ID URL DOWNLOAD_LOCATION DESTINATION_WITHOUT_EXTENSION
        prints the path of the saved file
"""
import concurrent.futures
import json
import os
import subprocess
import sys
import urllib.error
import urllib.parse
import urllib.request

APP = "end4pC"
NEEDS_KEY = {"wallhaven": False, "unsplash": True, "pexels": True}
UA = "end4pC-wallpapers/1.0"
PER_PAGE = 24


# ---------------------------------------------------------------- keyring
def secret(*args, stdin=None):
    return subprocess.run(["secret-tool", *args], input=stdin, capture_output=True, text=True, timeout=20)


def key_get(provider):
    try:
        r = secret("lookup", "application", APP, "provider", provider)
    except (OSError, subprocess.SubprocessError):
        return ""
    return r.stdout.strip() if r.returncode == 0 else ""


def key_set(provider, key):
    r = secret("store", f"--label={APP} {provider} API key", "application", APP, "provider", provider, stdin=key)
    return r.returncode == 0


def status():
    try:
        secret("lookup", "application", APP, "provider", "probe")
    except FileNotFoundError:
        print("keyring=missing")
        return
    except (OSError, subprocess.SubprocessError):
        print("keyring=error")
        return
    print("keyring=ok")
    for p, need in NEEDS_KEY.items():
        print(f"{p}={1 if (not need or key_get(p)) else 0}")


# ---------------------------------------------------------------- http
class ApiError(Exception):
    def __init__(self, msg, auth=False):
        super().__init__(msg)
        self.auth = auth


def http_get(url, headers=None, binary=False):
    req = urllib.request.Request(url, headers={"User-Agent": UA, **(headers or {})})
    try:
        with urllib.request.urlopen(req, timeout=25) as r:
            data = r.read()
            return (data, r.headers.get("Content-Type", "")) if binary else json.loads(data.decode("utf-8", "replace"))
    except urllib.error.HTTPError as e:
        if e.code in (401, 403):
            raise ApiError("The service rejected the key.", auth=True)
        if e.code == 429:
            raise ApiError("Too many requests: the free limit was reached. Try again in a while.")
        raise ApiError(f"The service answered with an error ({e.code}).")
    except (urllib.error.URLError, TimeoutError, OSError):
        raise ApiError("No connection to the service.")
    except ValueError:
        raise ApiError("The service sent something unexpected.")


# ---------------------------------------------------------------- providers: each returns (items, pages)
def item(pid, thumb, full, title="", author="", w=0, h=0, dl=""):
    return {"id": str(pid), "thumb": thumb, "full": full, "title": title or "", "author": author or "", "w": w or 0, "h": h or 0, "dl": dl or ""}


WALLHAVEN_SORTS = ("toplist", "hot", "date_added", "random", "views", "favorites", "relevance")


def search_wallhaven(query, page, width, sort="toplist", seed=""):
    if sort not in WALLHAVEN_SORTS:
        sort = "toplist"
    q = {"categories": "111", "purity": "100", "page": page, "atleast": f"{width}x{round(width * 9 / 16)}", "ratios": "16x9,16x10", "sorting": sort}
    if query:
        q["q"] = query
    if sort == "toplist":
        q["topRange"] = "1y"
    if sort == "random" and seed:
        q["seed"] = seed  # Use the same shuffle seed for every page, or pages would repeat
    d = http_get("https://wallhaven.cc/api/v1/search?" + urllib.parse.urlencode(q))
    items = [item(x["id"], x["thumbs"]["large"], x["path"], "", "", x.get("dimension_x"), x.get("dimension_y")) for x in d.get("data", [])]
    meta = d.get("meta", {})
    return items, int(meta.get("last_page", 1) or 1), str(meta.get("seed") or "")


def search_unsplash(query, page, width, sort="", seed=""):
    hdr = {"Authorization": "Client-ID " + key_get("unsplash")}
    if query:
        d = http_get("https://api.unsplash.com/search/photos?" + urllib.parse.urlencode({"query": query, "page": page, "per_page": PER_PAGE, "orientation": "landscape"}), hdr)
        rows, pages = d.get("results", []), int(d.get("total_pages", 1) or 1)
    else:
        rows = http_get("https://api.unsplash.com/photos?" + urllib.parse.urlencode({"page": page, "per_page": PER_PAGE, "order_by": "popular"}), hdr)
        pages = 20
    h = round(width * 9 / 16)
    items = [item(x["id"], x["urls"]["small"], x["urls"]["raw"] + f"&w={width}&h={h}&fit=crop&fm=jpg&q=85", x.get("alt_description") or x.get("description"),
                  x.get("user", {}).get("name"), x.get("width"), x.get("height"), x.get("links", {}).get("download_location")) for x in rows]
    return items, pages, ""


def search_pexels(query, page, width, sort="", seed=""):
    hdr = {"Authorization": key_get("pexels")}
    if query:
        d = http_get("https://api.pexels.com/v1/search?" + urllib.parse.urlencode({"query": query, "page": page, "per_page": PER_PAGE, "orientation": "landscape"}), hdr)
    else:
        d = http_get("https://api.pexels.com/v1/curated?" + urllib.parse.urlencode({"page": page, "per_page": PER_PAGE}), hdr)
    h = round(width * 9 / 16)
    items = [item(x["id"], x["src"]["medium"], x["src"]["original"] + f"?auto=compress&cs=tinysrgb&w={width}&h={h}&fit=crop", x.get("alt"), x.get("photographer"), x.get("width"), x.get("height"))
             for x in d.get("photos", [])]
    pages = max(1, -(-int(d.get("total_results", 0) or 0) // PER_PAGE)) if query else 20
    return items, pages, ""


SEARCH = {"wallhaven": search_wallhaven, "unsplash": search_unsplash, "pexels": search_pexels}


def fetch_thumb(provider, it, thumbdir):
    path = os.path.join(thumbdir, f"{provider}_{it['id']}.jpg")
    if not os.path.exists(path):
        data, _ = http_get(it["thumb"], binary=True)
        with open(path + ".part", "wb") as f:
            f.write(data)
        os.replace(path + ".part", path)
    it["thumb"] = path
    return it


def prune(thumbdir, keep=500):
    try:
        files = sorted((os.path.join(thumbdir, n) for n in os.listdir(thumbdir)), key=os.path.getmtime, reverse=True)
        for f in files[keep:]:
            os.remove(f)
    except OSError:
        pass


def search(provider, query, page, width, thumbdir, sort="", seed=""):
    try:
        if provider not in SEARCH:
            raise ApiError("Unknown provider.")
        if NEEDS_KEY[provider] and not key_get(provider):
            raise ApiError("There is no key for this service.", auth=True)
        items, pages, seed_out = SEARCH[provider](query, page, width, sort, seed)
        os.makedirs(thumbdir, exist_ok=True)
        out = []
        with concurrent.futures.ThreadPoolExecutor(max_workers=8) as ex:
            futs = [ex.submit(fetch_thumb, provider, it, thumbdir) for it in items]
            for f in futs:
                try:
                    out.append(f.result())
                except Exception:
                    pass
        prune(thumbdir)
        clean = lambda v: str(v).replace("\t", " ").replace("\n", " ").replace("\r", " ")
        lines = [f"pages\t{pages}\t{clean(seed_out)}"]
        for it in out:
            lines.append("\t".join(clean(it[k]) for k in ("id", "thumb", "full", "dl", "w", "h", "author", "title")))
        print("\n".join(lines))
    except ApiError as e:
        print(f"error\t{1 if e.auth else 0}\t{e}")
    except Exception:  # Report unexpected errors as a message, never as a traceback
        print("error\t0\tSomething went wrong while asking the service.")


def download(provider, pid, url, dl, dest):
    try:
        if provider == "unsplash" and dl:  # Unsplash asks apps to report each download
            try:
                http_get(dl, {"Authorization": "Client-ID " + key_get("unsplash")})
            except ApiError:
                pass
        data, ctype = http_get(url, binary=True)
        ext = ".png" if "png" in ctype else ".webp" if "webp" in ctype else ".jpg"
        os.makedirs(os.path.dirname(dest), exist_ok=True)
        path = dest + ext
        with open(path + ".part", "wb") as f:
            f.write(data)
        os.replace(path + ".part", path)
        print(path)
        return 0
    except ApiError as e:
        print("error: " + str(e))
        return 1


def main(a):
    if a[:1] == ["--status"]:
        status()
    elif a[:1] == ["--key-set"] and len(a) > 1 and a[1] in NEEDS_KEY:
        key = sys.stdin.read().strip()
        if not key or len(key) > 300 or any(c.isspace() for c in key):
            print("error: that does not look like a key")
            return 1
        try:
            ok = key_set(a[1], key)
        except FileNotFoundError:
            print("error: secret-tool is not installed (package libsecret)")
            return 1
        except (OSError, subprocess.SubprocessError):
            print("error: the keyring did not answer")
            return 1
        print("ok" if ok else "error: the keyring refused to store it (is it unlocked?)")
        return 0 if ok else 1
    elif a[:1] == ["--key-clear"] and len(a) > 1 and a[1] in NEEDS_KEY:
        try:
            secret("clear", "application", APP, "provider", a[1])
        except (OSError, subprocess.SubprocessError):
            pass
        print("ok")
    elif a[:1] == ["--search"] and len(a) >= 6:
        search(a[1], a[2], int(a[3]) if a[3].isdigit() else 1, int(a[4]) if a[4].isdigit() else 1920, a[5], a[6] if len(a) > 6 else "", a[7] if len(a) > 7 else "")
    elif a[:1] == ["--download"] and len(a) >= 6:
        return download(a[1], a[2], a[3], a[4], a[5])
    else:
        print(__doc__)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
