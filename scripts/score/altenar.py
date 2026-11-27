"""Bookmaker odds and booking codes from the Altenar engine (many betting sites run on it), used by the Score page.

The site is chosen by its Altenar integration name (setting "Odds provider"), passed in SCORE_ODDS_INTEGRATION.

Unofficial public API: it can change without notice. Creating a code only reserves the slip
on the site; nothing is bet.

  altenar.py prices  '<json>'   latest odd for each leg's market
  altenar.py reserve '<json>'   reserve the slip and print its booking code

<json> is {"stake": 1, "legs": [{"matchId", "key", "kickoff", "home", "away"}]}; `key` is the model
market key (home, draw, 1x, o25, u35, btts_y, c85, ...).
"""
import gzip
import json
import math
import os
import re
import sys
import time
import unicodedata
import datetime as dt
from concurrent.futures import ThreadPoolExecutor
from difflib import SequenceMatcher
from urllib.request import Request, urlopen

CACHE = os.path.expanduser("~/.cache/end4pC/score")
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))  # (python -I does not add the script folder)
INTEGRATION = os.environ.get("SCORE_ODDS_INTEGRATION", "").strip()
SITE_ORIGIN = os.environ.get("SCORE_ODDS_SITE", "").strip().rstrip("/")


def country_code():
    """The country the slip is booked for: SCORE_ODDS_COUNTRY, or the country of the system locale (es_EC -> EC)."""
    given = os.environ.get("SCORE_ODDS_COUNTRY", "").strip().upper()
    if given:
        return given
    locale = os.environ.get("LC_ALL") or os.environ.get("LC_MESSAGES") or os.environ.get("LANG") or ""
    part = locale.split(".")[0].split("_")
    return part[1].upper() if len(part) > 1 and len(part[1]) == 2 else "US"
UA = "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/150.0 Safari/537.36"

PARAMS = {"culture": "es-ES", "timezoneOffset": 300, "integration": INTEGRATION, "deviceType": 1, "numFormat": "en-GB"}
QUERY = "&".join(f"{key}={value}" for key, value in PARAMS.items())
WEB = "https://sb2frontend-altenar2.biahosted.com/api/"
BETSLIP = "https://sb2betslip-altenar2.biahosted.com/api/"
SOCCER = 66
TTL = 10 * 60

STOP = {"fc", "cf", "sc", "ac", "cd", "sk", "fk", "club", "de", "la", "el", "cp", "afc", "ca", "cs", "ud", "rc",
        "as", "ss", "if", "bk", "sv", "us", "vs"}
NOT_FIRST_TEAM = re.compile(r"sub-?\d+|\bu\d\d\b|\(f\)|femen|reserv|juvenil|women|\bii\b")

# tipo de mercado de Altenar -> {tipo de cuota: clave del modelo}
PLAIN = {
    1: {1: "home", 2: "draw", 3: "away"},
    10: {9: "1x", 10: "12", 11: "x2"},
    11: {1: "dnb_h", 3: "dnb_a"},
    29: {74: "btts_y", 76: "btts_n"},
}
HANDICAP = 16  # Asian handicap: odd typeId 1714 backs the home team, 1715 the away team, line in the name
# mercados con línea: tipo de mercado -> (prefijo más de, prefijo menos de, líneas admitidas)
LINED = {
    18: ("o", "u", ("0.5", "1.5", "2.5", "3.5", "4.5")),
    166: ("c", "cu", ("7.5", "8.5", "9.5", "10.5", "11.5")),
}


def fetch(url, key, ttl=TTL):
    os.makedirs(CACHE, exist_ok=True)
    path = os.path.join(CACHE, key + ".json")
    if os.path.exists(path) and time.time() - os.path.getmtime(path) < ttl:
        try:
            with open(path, encoding="utf-8") as handle:
                return json.load(handle)
        except Exception:
            pass
    try:
        request = Request(url, headers={"User-Agent": UA, "Accept-Encoding": "gzip"})
        with urlopen(request, timeout=25) as response:
            raw = response.read()
            if response.headers.get("Content-Encoding") == "gzip":
                raw = gzip.decompress(raw)
        data = json.loads(raw.decode("utf-8"))
        with open(path + ".tmp", "w", encoding="utf-8") as handle:
            json.dump(data, handle)
        os.replace(path + ".tmp", path)
        return data
    except Exception:
        return None


ALIASES = {"lafc": "los angeles fc"}


def tokens(name):
    text = unicodedata.normalize("NFKD", ALIASES.get(name.lower(), name.lower()))
    text = "".join(ch for ch in text if not unicodedata.combining(ch))
    return [word for word in re.split(r"[^a-z0-9]+", text) if word and word not in STOP]


def similarity(first, second):
    a, b = tokens(first), tokens(second)
    if not a or not b:
        return 0.0
    ratio = SequenceMatcher(None, " ".join(a), " ".join(b)).ratio()
    shared = sum(1 for x in a if any(x == y or (min(len(x), len(y)) >= 4 and (x.startswith(y) or y.startswith(x))) for y in b))
    return max(ratio, 0.9 * shared / min(len(a), len(b)))


def match_events(matches, board):
    """Empareja cada partido de ESPN con su evento del sitio por hora de inicio y parecido de nombres."""
    teams = {team["id"]: team for team in board.get("competitors", [])}
    slots = {}
    for event in board.get("events", []):
        if len(event.get("competitorIds") or []) != 2 or NOT_FIRST_TEAM.search(event["name"].lower()):
            continue
        start = dt.datetime.fromisoformat(event["startDate"].replace("Z", "+00:00"))
        slots.setdefault(int(start.timestamp()) // 1800, []).append((start, event))
    found = {}
    for match in matches:
        kickoff = dt.datetime.fromisoformat(match["date"].replace("Z", "+00:00"))
        slot = int(kickoff.timestamp()) // 1800
        best, best_score = None, 0.0
        for start, event in slots.get(slot - 1, []) + slots.get(slot, []) + slots.get(slot + 1, []):
            if abs((start - kickoff).total_seconds()) > 20 * 60:
                continue
            home, away = (teams.get(team_id, {}).get("name", "") for team_id in event["competitorIds"])
            scores = similarity(match["home"]["name"], home), similarity(match["away"]["name"], away)
            if min(scores) >= 0.45 and sum(scores) / 2 > max(best_score, 0.6):
                best, best_score = event, sum(scores) / 2
        if best:
            found[match["id"]] = best
    return found


def event_prices(details):
    """Devuelve {clave del modelo: {"price", "odd", "market"}} con las cuotas abiertas de un evento."""
    odds = {odd["id"]: odd for odd in details.get("odds", [])}
    prices = {}
    for market in details.get("markets", []):
        type_id = market.get("typeId")
        rows = market.get("desktopOddIds") or []
        listed = [odds[odd_id] for row in rows for odd_id in row if odd_id in odds]
        base = {key: market[key] for key in ("typeId", "sportMarketId", "id", "name") if key in market}
        for odd in listed:
            if odd.get("oddStatus") != 0 or odd.get("price", 0) <= 1:
                continue
            if type_id in PLAIN:
                key = PLAIN[type_id].get(odd["typeId"])
                shape = dict(base, oddIds=[item["id"] for item in listed])
            elif type_id == HANDICAP and odd["typeId"] in (1714, 1715):
                side = "h" if odd["typeId"] == 1714 else "a"
                shown = re.search(r"\(([+-]\d+(?:\.\d+)?)\)", odd.get("name") or "")
                key = f"ah_{side}_{shown.group(1)}" if shown else None
                pair = [item["id"] for item in listed if item.get("sv") == odd.get("sv")]
                shape = dict(base, oddIds=pair, sv=odd.get("sv"), sn=odd.get("sv"))
            elif type_id in LINED and odd.get("sv") in LINED[type_id][2]:
                over, under, _ = LINED[type_id]
                prefix = over if odd["typeId"] == 12 else (under if odd["typeId"] == 13 else None)
                key = prefix + odd["sv"].replace(".", "") if prefix else None
                pair = [item["id"] for item in listed if item.get("sv") == odd["sv"]]
                shape = dict(base, oddIds=pair, sv=odd["sv"], sn=odd["sv"])
            else:
                key = None
            if key and key not in prices:
                prices[key] = {"price": odd["price"], "odd": odd, "market": shape}
    return prices


def load_day(matches):
    """Cuotas del sitio para una lista de partidos. Devuelve {id del partido: contexto} o None si falla."""
    if not INTEGRATION:
        return None
    board = fetch(f"{WEB}Widget/GetUpcoming?{QUERY}&eventCount=0&sportId={SOCCER}&champIds=0", f"odds_{INTEGRATION}_board")
    if not board or not board.get("events"):
        return None
    sports = {item["id"]: item for item in board.get("sports", [])}
    categories = {item["id"]: item for item in board.get("categories", [])}
    champs = {item["id"]: item for item in board.get("champs", [])}
    teams = {item["id"]: item for item in board.get("competitors", [])}
    events = match_events(matches, board)

    def load(item):
        match_id, event = item
        details = fetch(f"{WEB}Widget/GetEventDetails?{QUERY}&eventId={event['id']}", f"odds_{INTEGRATION}_event_{event['id']}")
        if not details:
            return match_id, None
        sport = dict(sports.get(event["sportId"], {}), catIds=[event["catId"]])
        category = dict(categories.get(event["catId"], {}), champIds=[event["champId"]])
        return match_id, {
            "event": event,
            "sport": sport,
            "category": category,
            "champ": champs.get(event["champId"], {}),
            "competitors": [teams[team_id] for team_id in event["competitorIds"] if team_id in teams],
            "prices": event_prices(details),
        }

    with ThreadPoolExecutor(max_workers=12) as pool:
        return {match_id: context for match_id, context in pool.map(load, events.items()) if context}


def selection(context, key):
    entry = context["prices"][key]
    event = context["event"]
    return {
        "odd": dict(entry["odd"], lineDir=1, priceDir=1, intEventId=event["id"], shouldUpdate=False),
        "event": event,
        "status": 0,
        "isBanker": False,
        "isEnabled": True,
        "sport": context["sport"],
        "category": context["category"],
        "competitors": context["competitors"],
        "championship": dict(context["champ"], category=context["category"], sport=context["sport"]),
        "market": entry["market"],
        "widgetInfo": {"widget": 20, "page": 7, "tabIndex": None, "tipsterId": None, "suggestionType": None},
        "incompatibleOddIds": [],
        "isVoidBetBuilder": False,
    }


def reserve(selections, stake=1):
    """Guarda el cupón en the bookmaker (múltiple si hay dos o más) y devuelve el código de reserva. No apuesta nada."""
    betslip = {
        "stakes": [{"type": 3, "value": stake, "isEnabled": True, "useEachWay": False, "preciseValue": stake,
                    "isHighlighted": False}],
        "selections": selections,
        "betType": 1 if len(selections) > 1 else 0,
        "stakeAdjustment": {"type": 0, "value": 0},
        "fullCoverData": {"type": 1, "label": None},
    }
    body = dict(PARAMS, countryCode=country_code(), betslip=json.dumps(betslip, ensure_ascii=False))
    request = Request(BETSLIP + "Betslip/reserveBet", data=json.dumps(body).encode("utf-8"), method="POST",
                      headers={"User-Agent": UA, "Content-Type": "application/json", "Origin": SITE_ORIGIN or "https://sb2frontend-altenar2.biahosted.com",
                               "Referer": (SITE_ORIGIN or "https://sb2frontend-altenar2.biahosted.com") + "/"})
    with urlopen(request, timeout=25) as response:
        answer = json.loads(response.read().decode("utf-8"))
    if answer.get("Error") or not answer.get("Result"):
        raise RuntimeError(f"The site rejected the slip: {answer.get('Error')}")
    return answer["Result"]["ReservationKey"]


def find(code):
    """Reads the slip saved under an the bookmaker booking code."""
    url = f"{BETSLIP}Betslip/FindReservedBet?{QUERY}&key={code}"
    with urlopen(Request(url, headers={"User-Agent": UA}), timeout=25) as response:
        answer = json.loads(response.read().decode("utf-8"))
    if answer.get("Error") or not answer.get("Result"):
        raise RuntimeError("The site does not know that code")
    return json.loads(answer["Result"]["Betslip"])


def leg_of(chosen):
    """What a selection of a slip bets on, in terms the live alerts understand."""
    market, odd = chosen.get("market", {}), chosen.get("odd", {})
    type_id, name = market.get("typeId"), (odd.get("name") or "").strip()
    number = re.search(r"(\d+(?:[.,]\d+)?)", name)
    line = float(number.group(1).replace(",", ".")) if number else None
    lower = name.lower()
    direction = "under" if ("menos" in lower or "under" in lower) else "over"
    leg = {"kind": "other", "line": None, "direction": direction, "pick": "", "label": f"{market.get('name', '')}: {name}"}
    if type_id == 1:
        leg.update(kind="result", pick={"1": "home", "x": "draw", "2": "away"}.get(name.lower(), ""))
    elif type_id == 10:
        leg.update(kind="result", pick=name.lower().replace(" ", ""))
    elif type_id == 18:
        leg.update(kind="goals", line=line)
    elif type_id == 166:
        leg.update(kind="corners", line=line)
    elif type_id == HANDICAP:
        shown = re.search(r"\(([+-]\d+(?:\.\d+)?)\)", name)
        leg.update(kind="handicap", line=float(shown.group(1)) if shown else None,
                   pick="home" if odd.get("typeId") == 1714 else "away")
    elif type_id == 29:
        leg.update(kind="btts", pick="yes" if lower.startswith(("s", "y")) else "no")
    return leg


def code_command(code):
    """The legs of a booking code, each tied to the ESPN match it belongs to (when it can be found)."""
    slip = find(code)
    with open(os.path.join(CACHE, "matches.json"), encoding="utf-8") as handle:
        known = json.load(handle).get("matches", [])
    legs = []
    for chosen in slip.get("selections", []):
        event = chosen.get("event", {})
        parts = re.split(r"\s+vs\.?\s+", event.get("name", ""), maxsplit=1)
        names = parts if len(parts) == 2 else []
        start = dt.datetime.fromisoformat(event["startDate"].replace("Z", "+00:00"))
        best, best_score = None, 0.0
        for match in known if names else []:
            kickoff = dt.datetime.fromisoformat(match["date"].replace("Z", "+00:00"))
            if abs((kickoff - start).total_seconds()) > 20 * 60:
                continue
            scores = similarity(match["home"]["name"], names[0]), similarity(match["away"]["name"], names[1])
            if min(scores) >= 0.45 and sum(scores) / 2 > max(best_score, 0.6):
                best, best_score = match, sum(scores) / 2
        leg = leg_of(chosen)
        leg.update({
            "event": event.get("name", ""), "start": event["startDate"], "price": chosen.get("odd", {}).get("price", 0),
            "matchId": best["id"] if best else "", "league": best["league"]["id"] if best else "",
            "home": best["home"]["name"] if best else (names[0] if names else ""),
            "away": best["away"]["name"] if best else (names[1] if names else ""),
        })
        legs.append(leg)
    stakes = slip.get("stakes") or [{}]
    print(json.dumps({"code": code, "stake": stakes[0].get("value") or 1, "legs": legs}))


def matches_of(legs):
    seen = {}
    for leg in legs:
        seen.setdefault(leg["matchId"], {"id": leg["matchId"], "date": leg["kickoff"],
                                         "home": {"name": leg["home"]}, "away": {"name": leg["away"]}})
    return list(seen.values())


# --- Suggested parlay (ported from the Windows app) ---------------------------

PARLAY_KEYS = {"result": ("home", "draw", "away"), "goals": ("o15", "o25"), "corners": ("c75", "c85", "c95", "c105")}


MIN_PRICE = 1.10  # a leg that pays less than this is not worth putting in a parlay


def type_candidate(match, kind, contexts=None):
    """The likeliest option of a kind for a match whose odds are at least MIN_PRICE. With the site's odds (contexts), only options the
    site lists count and the price is its own; without them the model's fair price (1 / probability) stands in."""
    probs = match.get("probs")
    if not probs or match["state"] != "pre":
        return None
    prices = (contexts or {}).get(match["id"], {}).get("prices") if contexts else None
    options = []
    for key in PARLAY_KEYS[kind]:
        if contexts:
            entry = (prices or {}).get(key)
            if not entry:
                continue  # the site does not list it
            price = entry["price"]
        else:
            price = 1 / probs[key] if probs[key] > 0 else 0
        if price >= MIN_PRICE:
            options.append((probs[key], key))
    if not options:
        return None
    prob, pick = max(options)
    return {"match": match, "type": kind, "pick": pick, "prob": prob}


def build_parlay(matches, legs, kinds, contexts=None):
    kinds = [kind for kind in ("result", "goals", "corners") if kind in kinds]
    by_type = {}
    for kind in kinds:
        found = [type_candidate(match, kind, contexts) for match in matches]
        by_type[kind] = sorted((c for c in found if c), key=lambda c: -c["prob"])
    order = sorted(kinds, key=lambda kind: -(by_type[kind][0]["prob"] if by_type[kind] else 0))
    quota = {kind: legs // len(order) + (index < legs % len(order)) for index, kind in enumerate(order)}
    used, chosen = set(), []
    for kind in order:
        taken = 0
        for candidate in by_type[kind]:
            if taken >= quota[kind]:
                break
            if candidate["match"]["id"] in used:
                continue
            used.add(candidate["match"]["id"])
            chosen.append(candidate)
            taken += 1
    if len(chosen) < legs:
        rest = sorted((c for kind in order for c in by_type[kind]), key=lambda c: -c["prob"])
        for candidate in rest:
            if len(chosen) >= legs:
                break
            if candidate["match"]["id"] in used:
                continue
            used.add(candidate["match"]["id"])
            chosen.append(candidate)
    chosen.sort(key=lambda c: -c["prob"])
    combined = math.prod(c["prob"] for c in chosen) if chosen else 0.0
    return {"legs": chosen, "combined": combined, "odds": 1 / combined if combined > 0 else 0.0}


TARGET_KEYS = {
    "result": ("home", "draw", "away", "1x", "12", "x2"),
    "goals": ("o15", "o25"),
    "corners": ("c75", "c85", "c95", "c105"),
}
STEP = 0.004
MARGIN = 1.06        # margen típico de la casa: probabilidad implícita = 1 / cuota / MARGIN
MAX_DISAGREEMENT = 1.3  # si el modelo ve 30% más probabilidad que la casa, casi seguro se equivoca el modelo


def candidates(matches, contexts, kinds):
    """Selecciones elegibles: partidos con datos suficientes y donde el modelo no contradice mucho a la casa."""
    found = []
    for match in matches:
        context = contexts.get(match["id"])
        probs = match.get("probs")
        if not context or not probs or match["state"] != "pre" or match["lowData"]:
            continue
        for kind in kinds:
            for key in TARGET_KEYS[kind]:
                entry = context["prices"].get(key)
                if not entry or probs.get(key, 0) <= 0:
                    continue
                implied = 1 / entry["price"] / MARGIN
                if probs[key] <= implied * MAX_DISAGREEMENT:
                    found.append({"match": match, "type": kind, "pick": key, "prob": probs[key], "price": entry["price"]})
    return found


def target_parlay(options, target):
    """La combinación más probable (según el modelo) de 2 o más partidos distintos cuya cuota llega al objetivo."""
    groups = {}
    for option in options:
        groups.setdefault(option["match"]["id"], []).append(option)
    goal = math.ceil(math.log(target) / STEP)
    # estado: (cuota acumulada en pasos, selecciones 0/1/2+) -> (log de probabilidad, selecciones)
    best = {(0, 0): (0.0, ())}
    for items in groups.values():
        updated = dict(best)
        for (steps, count), (logp, chosen) in best.items():
            for option in items:
                state = (min(goal, steps + int(math.log(option["price"]) / STEP)), min(2, count + 1))
                value = logp + math.log(option["prob"])
                if state not in updated or value > updated[state][0]:
                    updated[state] = (value, chosen + (option,))
        best = updated
    winner = best.get((goal, 2))
    if not winner:
        return {"legs": [], "combined": 0.0, "odds": 0.0}
    legs = sorted(winner[1], key=lambda leg: -leg["prob"])
    return {"legs": legs, "combined": math.prod(leg["prob"] for leg in legs),
            "odds": math.prod(leg["price"] for leg in legs)}


def load_matches():
    """Upcoming matches from the shell's own cache, with the market keys the picks and the bookmaker use."""
    import score as football
    with open(os.path.join(CACHE, "matches.json"), encoding="utf-8") as handle:
        matches = json.load(handle).get("matches", [])
    for match in matches:
        if match.get("probs"):
            match["probs"] = dict(football.norm_probs(match["probs"]), scores=match["probs"].get("scores"))
    return [m for m in matches if m["state"] == "pre" and m.get("probs")]


def prices_of(contexts):
    return {match_id: {key: entry["price"] for key, entry in context["prices"].items()}
            for match_id, context in contexts.items()}


def picks_command():
    """The algorithm's pick for every upcoming match, using the bookmaker's odds where it has them."""
    import score as football
    matches = load_matches()
    contexts = load_day(matches) or {}
    prices = prices_of(contexts)
    raw = {m["id"]: m for m in json.load(open(os.path.join(CACHE, "matches.json"), encoding="utf-8"))["matches"]}
    picks, safe, options = {}, {}, {}
    for match in matches:
        original = dict(raw[match["id"]])
        known = prices.get(match["id"])
        picks[match["id"]] = football.best_pick(original, known)
        safe[match["id"]] = football.safest_pick(original, known)
        # Every option it chose from: key -> [probability, price, real price?]
        options[match["id"]] = {o["key"]: [round(o["prob"], 4), o["price"], o["real"]] for o in football.pick_options(original, known)}
    out = {"picks": picks, "safe": safe, "options": options, "rule": football.RULE, "odds": len(contexts), "matches": len(matches)}
    with open(os.path.join(CACHE, "picks.json") + ".tmp", "w", encoding="utf-8") as handle:
        json.dump(out, handle)
    os.replace(os.path.join(CACHE, "picks.json") + ".tmp", os.path.join(CACHE, "picks.json"))
    print(json.dumps(out))


def value_command():
    """Where the bookmaker pays more than the market (DraftKings, via ESPN) says it is worth. See value.py."""
    import score as football
    import value
    matches = load_matches()
    contexts = load_day(matches) or {}
    prices = prices_of(contexts)

    def fair_of(match):
        data = football.fetch(f"{football.SITE}/{match['league']['id']}/summary?event={match['id']}", 20 * 60, f"value_{match['id']}")
        return match, value.market_fair(data or {})

    found, compared = [], 0
    with ThreadPoolExecutor(max_workers=12) as pool:
        for match, fair in pool.map(fair_of, [m for m in matches if m["id"] in prices]):
            for edge in value.edges(fair, prices[match["id"]]):
                compared += 1
                found.append(dict(edge, matchId=match["id"], home=match["home"]["name"], away=match["away"]["name"],
                                  date=match["date"], league=match["league"]["name"],
                                  label=value.label(edge["key"], match["home"]["name"], match["away"]["name"])))
    found.sort(key=lambda e: -e["edge"])
    mean = sum(e["edge"] for e in found) / len(found) if found else 0
    print(json.dumps({"compared": compared, "meanEdge": round(mean, 4), "positive": [e for e in found if e["edge"] > 0], "all": found}))


def parlay_command(request):
    import score as football
    ids = set(request["matchIds"])
    matches = [m for m in load_matches() if m["id"] in ids]
    kinds = [k for k in request.get("kinds", ["result", "goals", "corners"]) if k in PARLAY_KEYS]
    contexts = load_day(matches) or {}
    mode, value = request["mode"], request["value"]
    if mode == "target":
        if not contexts:
            raise RuntimeError("Odds from the site are needed to build a parlay by odds")
        parlay = target_parlay(candidates(matches, contexts, kinds), value)
    else:
        parlay = build_parlay(matches, value, kinds, contexts or None)
        for leg in parlay["legs"]:
            entry = contexts.get(leg["match"]["id"], {}).get("prices", {}).get(leg["pick"])
            if entry:
                leg["price"] = entry["price"]
    legs = parlay["legs"]
    priced = bool(legs) and all("price" in leg for leg in legs)
    if priced:
        parlay["odds"] = math.prod(leg["price"] for leg in legs)
    print(json.dumps({
        "priced": priced, "combined": parlay["combined"], "odds": parlay["odds"], "hasOdds": bool(contexts),
        "legs": [{
            "matchId": leg["match"]["id"], "key": leg["pick"], "type": leg["type"], "prob": round(leg["prob"], 4),
            "price": leg.get("price"), "league": leg["match"]["league"]["name"],
            "home": leg["match"]["home"]["name"], "away": leg["match"]["away"]["name"], "kickoff": leg["match"]["date"],
            "label": football.pick_label(leg["pick"], leg["match"]["home"]["name"], leg["match"]["away"]["name"]),
        } for leg in legs],
    }))


def handicap_command(match_id):
    """The Asian handicap worth backing for a match: the model plus how the last meetings ended."""
    import score as football
    import handicap
    matches = load_matches()
    match = next((m for m in matches if m["id"] == match_id), None)
    if not match:
        raise RuntimeError("Match not found")
    games = football.h2h_games(match["league"]["id"], match["home"]["id"], match["away"]["id"], match["id"], 10)
    lines = []
    context = (load_day([match]) or {}).get(match_id)
    for key, entry in (context["prices"] if context else {}).items():
        if key.startswith("ah_"):
            _, side, number = key.split("_")
            lines.append({"side": "home" if side == "h" else "away", "line": float(number), "price": entry["price"], "key": key})
    print(json.dumps({"suggestion": handicap.suggest(match, lines, games), "meetings": len(games), "onSite": bool(lines)}))


def main():
    command = sys.argv[1]
    if not INTEGRATION and command != "parlay":
        raise RuntimeError("No odds provider is set")
    if command == "handicap":
        return handicap_command(sys.argv[2])
    if command == "picks":
        return picks_command()
    if command == "code":
        return code_command(sys.argv[2])
    if command == "value":
        return value_command()
    request = json.loads(sys.argv[2])
    if command == "parlay":
        return parlay_command(request)
    legs = request["legs"]
    contexts = load_day(matches_of(legs)) or {}
    usable = [leg for leg in legs if leg["matchId"] in contexts and leg["key"] in contexts[leg["matchId"]]["prices"]]
    if command == "prices":
        print(json.dumps({leg["matchId"]: contexts[leg["matchId"]]["prices"][leg["key"]]["price"] for leg in usable}))
        return
    if not usable:
        raise RuntimeError("None of these picks was found on the site. Check the Odds provider in Settings.")
    selections = [selection(contexts[leg["matchId"]], leg["key"]) for leg in usable]
    code = reserve(selections, request.get("stake") or 1)
    print(json.dumps({"code": code, "used": len(usable), "missing": len(legs) - len(usable)}))


def as_tsv(command, data):
    """The answers as tab separated lines (the Score page has no JSON parser). E message | P match price | C code used missing |
    S priced combined odds hasOdds, then L match key type prob price league home away kickoff label per leg."""
    def cell(value):
        return "" if value is None else str(value).replace("\t", " ").replace("\n", " ")
    if "error" in data:
        return "E\t" + cell(data["error"]) + "\n"
    if command == "prices":
        return "".join(f"P\t{cell(k)}\t{cell(v)}\n" for k, v in data.items())
    if command == "parlay":
        out = [f"S\t{int(bool(data['priced']))}\t{data['combined']}\t{data['odds']}\t{int(bool(data['hasOdds']))}"]
        for leg in data["legs"]:
            out.append("L\t" + "\t".join(cell(leg.get(k)) for k in ("matchId", "key", "type", "prob", "price", "league", "home", "away", "kickoff", "label")))
        return "\n".join(out) + "\n"
    if "code" in data:
        return f"C\t{cell(data['code'])}\t{data.get('used', 0)}\t{data.get('missing', 0)}\n"
    return ""


if __name__ == "__main__":
    import io
    import contextlib
    want_tsv = "--tsv" in sys.argv
    if want_tsv:
        sys.argv.remove("--tsv")
    buffer = io.StringIO()
    code = 0
    try:
        with contextlib.redirect_stdout(buffer):
            main()
    except Exception as error:
        buffer = io.StringIO(json.dumps({"error": str(error)}))
        code = 1
    text = buffer.getvalue()
    if want_tsv:
        try:
            text = as_tsv(sys.argv[1] if len(sys.argv) > 1 else "", json.loads(text))
        except Exception:
            text = "E\t" + text.strip()[:200] + "\n"
    sys.stdout.write(text)
    sys.exit(code)
