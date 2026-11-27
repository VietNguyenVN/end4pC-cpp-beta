#!/usr/bin/env python3
"""Score (Tools page): football matches from ESPN's public scoreboard and a statistical model of the probabilities.

  score.py --board [--cached] [--force]   a tab separated list of the matches for the page (--cached: only what is on disk, no network; --force: ignore a recent copy)

The numbers are estimates, not advice."""
import json
import math
import os
import sys
import time
import datetime as dt
from concurrent.futures import ThreadPoolExecutor
from urllib.request import Request, urlopen

UA = "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/150.0 Safari/537.36"
SITE = "https://site.api.espn.com/apis/site/v2/sports/soccer"
CORE = "https://sports.core.api.espn.com/v2/sports/soccer/leagues"
CACHE = os.path.expanduser("~/.cache/end4pC/score")

LEAGUES = [
    {"id": "esp.1", "name": "LaLiga", "country": "ES", "goals": 1.30, "domestic": True},
    {"id": "eng.1", "name": "Premier League", "country": "EN", "goals": 1.45, "domestic": True},
    {"id": "ita.1", "name": "Serie A", "country": "IT", "goals": 1.30, "domestic": True},
    {"id": "fra.1", "name": "Ligue 1", "country": "FR", "goals": 1.40, "domestic": True},
    {"id": "por.1", "name": "Primeira Liga", "country": "PT", "goals": 1.30, "domestic": True},
    {"id": "tur.1", "name": "Süper Lig", "country": "TR", "goals": 1.45, "domestic": True},
    {"id": "ned.1", "name": "Eredivisie", "country": "NL", "goals": 1.65, "domestic": True},
    {"id": "ger.1", "name": "Bundesliga", "country": "DE", "goals": 1.60, "domestic": True},
    {"id": "mex.1", "name": "Liga MX", "country": "MX", "goals": 1.40, "domestic": True},
    {"id": "arg.1", "name": "Liga Profesional", "country": "AR", "goals": 1.10, "domestic": True},
    {"id": "bra.1", "name": "Brasileirão", "country": "BR", "goals": 1.25, "domestic": True},
    {"id": "ksa.1", "name": "Saudi Pro League", "country": "SA", "goals": 1.45, "domestic": True},
    {"id": "ecu.1", "name": "LigaPro Ecuador", "country": "EC", "goals": 1.30, "domestic": True},
    {"id": "col.1", "name": "Liga BetPlay", "country": "CO", "goals": 1.15, "domestic": True},
    {"id": "chi.1", "name": "Primera División Chile", "country": "CL", "goals": 1.35, "domestic": True},
    {"id": "usa.1", "name": "MLS", "country": "US", "goals": 1.50, "domestic": True},
    {"id": "uefa.champions", "name": "Champions League", "country": "EU", "goals": 1.45, "domestic": False},
    {"id": "uefa.europa", "name": "Europa League", "country": "EU", "goals": 1.40, "domestic": False},
    {"id": "uefa.europa.conf", "name": "Conference League", "country": "EU", "goals": 1.45, "domestic": False},
    {"id": "conmebol.libertadores", "name": "Copa Libertadores", "country": "SA", "goals": 1.25, "domestic": False},
    {"id": "conmebol.sudamericana", "name": "Copa Sudamericana", "country": "SA", "goals": 1.20, "domestic": False},
]
BY_ID = {league["id"]: league for league in LEAGUES}
CORNERS_PER_TEAM = 5.0
SHRINK = 5.0
HOME_BOOST = 1.10
AWAY_FACTOR = 0.92
DAYS_AHEAD = 8


def fetch(url, ttl, key):
    os.makedirs(CACHE, exist_ok=True)
    path = os.path.join(CACHE, key + ".json")
    if os.path.exists(path) and time.time() - os.path.getmtime(path) < ttl:
        try:
            with open(path, encoding="utf-8") as handle:
                return json.load(handle)
        except Exception:
            pass
    try:
        with urlopen(Request(url, headers={"User-Agent": UA}), timeout=15) as response:
            data = json.loads(response.read().decode("utf-8"))
        tmp = path + ".tmp"
        with open(tmp, "w", encoding="utf-8") as handle:
            json.dump(data, handle)
        os.replace(tmp, path)
        return data
    except Exception:
        if os.path.exists(path):
            try:
                with open(path, encoding="utf-8") as handle:
                    return json.load(handle)
            except Exception:
                return None
        return None


LOGOS = os.path.join(CACHE, "logos")


def logo_file(team_id):
    return os.path.join(LOGOS, f"{team_id}.png")


def fetch_logo(team_id, url):
    """Downloads a crest once; later runs find it on disk. Returns the local path, or None if it is not there."""
    path = logo_file(team_id)
    if os.path.exists(path) and os.path.getsize(path) > 0:
        return path
    if not url:
        return None
    try:
        os.makedirs(LOGOS, exist_ok=True)
        with urlopen(Request(url, headers={"User-Agent": UA}), timeout=15) as response:
            blob = response.read()
        if not blob:
            return None
        with open(path + ".tmp", "wb") as handle:
            handle.write(blob)
        os.replace(path + ".tmp", path)
        return path
    except Exception:
        return None


def localize_logos(matches):
    """Points every crest at its local copy (downloading the missing ones in parallel). A crest that could not be
    downloaded keeps its web address, so it still shows, just slower."""
    wanted = {}
    for match in matches:
        for side in ("home", "away"):
            team = match[side]
            if team.get("logo", "").startswith("http"):
                wanted[team["id"]] = team["logo"]
    with ThreadPoolExecutor(max_workers=16) as pool:
        local = dict(zip(wanted, pool.map(lambda item: fetch_logo(*item), wanted.items())))
    for match in matches:
        for side in ("home", "away"):
            path = local.get(match[side]["id"])
            if path:
                match[side]["logo"] = "file://" + path


def poisson_pmf(mean, kmax):
    out = [math.exp(-mean)]
    for k in range(1, kmax + 1):
        out.append(out[-1] * mean / k)
    return out


def goals_probs(lh, la):
    ph = poisson_pmf(lh, 10)
    pa = poisson_pmf(la, 10)
    rho = -0.08
    matrix = [[ph[i] * pa[j] for j in range(11)] for i in range(11)]
    matrix[0][0] *= 1 - lh * la * rho
    matrix[0][1] *= 1 + lh * rho
    matrix[1][0] *= 1 + la * rho
    matrix[1][1] *= 1 - rho
    total = sum(sum(row) for row in matrix)
    home = draw = away = over15 = over25 = 0.0
    for i in range(11):
        for j in range(11):
            p = matrix[i][j] / total
            if i > j:
                home += p
            elif i == j:
                draw += p
            else:
                away += p
            if i + j >= 2:
                over15 += p
            if i + j >= 3:
                over25 += p
    return home, draw, away, over15, over25


def full_probs(lh, la):
    """Same Dixon-Coles-style matrix as goals_probs, with the extra markets."""
    ph = poisson_pmf(lh, 10)
    pa = poisson_pmf(la, 10)
    rho = -0.08
    matrix = [[ph[i] * pa[j] for j in range(11)] for i in range(11)]
    matrix[0][0] *= 1 - lh * la * rho
    matrix[0][1] *= 1 + lh * rho
    matrix[1][0] *= 1 + la * rho
    matrix[1][1] *= 1 - rho
    total = sum(sum(row) for row in matrix)
    out = {k: 0.0 for k in ("home", "draw", "away", "o15", "o25", "o35", "btts")}
    scores = []
    for i in range(11):
        for j in range(11):
            p = matrix[i][j] / total
            scores.append((p, i, j))
            if i > j:
                out["home"] += p
            elif i == j:
                out["draw"] += p
            else:
                out["away"] += p
            if i + j >= 2:
                out["o15"] += p
            if i + j >= 3:
                out["o25"] += p
            if i + j >= 4:
                out["o35"] += p
            if i > 0 and j > 0:
                out["btts"] += p
    out["dc1x"] = out["home"] + out["draw"]
    out["dcx2"] = out["draw"] + out["away"]
    out["dc12"] = out["home"] + out["away"]
    scores.sort(reverse=True)
    out["scores"] = [{"score": f"{i}-{j}", "p": round(p, 4)} for p, i, j in scores[:3]]
    return out


def negbin_over(mean, line, dispersion=1.35):
    r = mean / (dispersion - 1)
    p = r / (r + mean)
    cdf = 0.0
    for k in range(int(math.floor(line)) + 1):
        logp = math.lgamma(k + r) - math.lgamma(r) - math.lgamma(k + 1) + r * math.log(p) + k * math.log(1 - p)
        cdf += math.exp(logp)
    return max(0.0, min(1.0, 1 - cdf))


def shrink(per_game, games, prior):
    return (per_game * games + prior * SHRINK) / (games + SHRINK)


def parse_stats(data):
    flat = {}
    for category in (data or {}).get("splits", {}).get("categories", []):
        for stat in category.get("stats", []):
            flat[stat.get("name")] = stat.get("value")
    return flat


def team_stats(league_id, season, team_id):
    url = f"{CORE}/{league_id}/seasons/{season}/types/1/teams/{team_id}/statistics"
    flat = parse_stats(fetch(url, 12 * 3600, f"stats_{league_id}_{season}_{team_id}"))
    games = float(flat.get("appearances") or 0)
    if games <= 0:
        return None
    return {
        "games": games,
        "gf": float(flat.get("totalGoals") or 0) / games,
        "ga": float(flat.get("goalsConceded") or 0) / games,
        "cf": float(flat.get("wonCorners") or 0) / games,
        "ca": float(flat.get("lostCorners") or 0) / games,
    }


def domestic_index():
    def load(league):
        data = fetch(f"{SITE}/{league['id']}/teams?limit=100", 7 * 24 * 3600, f"teams_{league['id']}")
        try:
            return league["id"], [str(item["team"]["id"]) for item in data["sports"][0]["leagues"][0]["teams"]]
        except Exception:
            return league["id"], []

    index = {}
    with ThreadPoolExecutor(max_workers=8) as pool:
        for league_id, teams in pool.map(load, [l for l in LEAGUES if l["domestic"]]):
            for team_id in teams:
                index[team_id] = league_id
    return index


def build_matches(today):
    jobs = []
    for league in LEAGUES:
        # From yesterday on: a match that kicked off late yesterday can still be in play
        for offset in range(-1, DAYS_AHEAD + 1):
            day = today + dt.timedelta(days=offset)
            jobs.append((league, day.strftime("%Y%m%d")))

    def load(job):
        league, stamp = job
        data = fetch(f"{SITE}/{league['id']}/scoreboard?dates={stamp}", 20 * 60, f"board_{league['id']}_{stamp}")
        return league, data

    events = {}
    seasons = {}
    with ThreadPoolExecutor(max_workers=24) as pool:
        for league, data in pool.map(load, jobs):
            if not data:
                continue
            try:
                seasons[league["id"]] = data["leagues"][0]["season"]["year"]
            except Exception:
                pass
            for event in data.get("events", []):
                try:
                    state = event["status"]["type"]["state"]
                    local_day = parse_iso(event["date"]).astimezone().date()
                except Exception:
                    continue
                if local_day >= today or state == "in":
                    events[(league["id"], event["id"])] = (league, event)
    return events, seasons


def positions_data():
    def load(league):
        data = fetch(f"https://site.api.espn.com/apis/v2/sports/soccer/{league['id']}/standings", 3 * 3600, f"standings_{league['id']}")
        ranks = {}
        for child in (data or {}).get("children", []):
            for entry in child.get("standings", {}).get("entries", []):
                flat = {stat.get("name"): stat.get("value") for stat in entry.get("stats", [])}
                rank = int(flat.get("rank") or 0)
                if rank > 0:
                    ranks[str(entry.get("team", {}).get("id", ""))] = rank
        return league["id"], ranks

    with ThreadPoolExecutor(max_workers=8) as pool:
        result = dict(pool.map(load, LEAGUES))
    return result


def positions():
    json.dump(positions_data(), sys.stdout)


def form_data(league_id, team_ids, count=6):
    domestic = domestic_index()
    result = {}

    def load(team_id):
        home_league = domestic.get(team_id) or league_id
        data = fetch(f"{SITE}/{home_league}/teams/{team_id}/schedule", 3 * 3600, f"schedule_{home_league}_{team_id}")
        games = []
        for event in (data or {}).get("events", []):
            try:
                competition = event["competitions"][0]
                if competition.get("status", {}).get("type", {}).get("state") != "post":
                    continue
                sides = {c["team"]["id"]: c for c in competition["competitors"]}
                mine = sides.get(team_id)
                other = [c for key, c in sides.items() if key != team_id]
                if not mine or not other:
                    continue

                def score(competitor):
                    value = competitor.get("score")
                    if isinstance(value, dict):
                        value = value.get("value", value.get("displayValue"))
                    return int(float(value or 0))

                mine_score, other_score = score(mine), score(other[0])
                games.append({
                    "date": event["date"],
                    "r": "W" if mine_score > other_score else ("L" if mine_score < other_score else "D"),
                    "vs": other[0]["team"].get("abbreviation", ""),
                    "vsName": other[0]["team"].get("displayName", ""),
                    "ha": mine.get("homeAway", ""),
                    "score": f"{mine_score}-{other_score}",
                })
            except Exception:
                continue
        games.sort(key=lambda g: g["date"])
        return team_id, games[-count:]

    with ThreadPoolExecutor(max_workers=4) as pool:
        for team_id, games in pool.map(load, team_ids):
            result[team_id] = games
    return result


def team_form(league_id, team_ids, count=6):
    json.dump(form_data(league_id, team_ids, count), sys.stdout)


def watch(jobs):
    """Live state of a few matches: one small request each. jobs = [{"id", "league"}]."""
    def load(job):
        return job["id"], fetch(f"{SITE}/{job['league']}/summary?event={job['id']}", -1, f"watch_{job['id']}")

    out = {}
    with ThreadPoolExecutor(max_workers=8) as pool:
        for match_id, data in pool.map(load, jobs):
            try:
                competition = data["header"]["competitions"][0]
                status = competition["status"]["type"]
                sides = {c["homeAway"]: c for c in competition["competitors"]}
                team_side = {c["team"]["id"]: c["homeAway"] for c in competition["competitors"]}
                corners = {"home": None, "away": None}
                for team in data.get("boxscore", {}).get("teams", []):
                    for stat in team.get("statistics", []):
                        if stat.get("name") == "wonCorners":
                            corners[team["homeAway"]] = to_int(stat.get("displayValue"))
                goals = []
                for detail in competition.get("details") or []:
                    if not detail.get("scoringPlay"):
                        continue
                    goals.append({
                        "minute": (detail.get("clock") or {}).get("displayValue", ""),
                        "side": team_side.get(str((detail.get("team") or {}).get("id", "")), ""),
                        "player": ((detail.get("athletesInvolved") or [{}])[0] or {}).get("displayName", ""),
                        "own": bool(detail.get("ownGoal")),
                        "penalty": bool(detail.get("penaltyKick")),
                    })
                out[match_id] = {
                    "state": status.get("state", "pre"),
                    "detail": status.get("shortDetail", ""),
                    "home": {"name": sides["home"]["team"].get("displayName", ""), "score": to_int(sides["home"].get("score")) or 0},
                    "away": {"name": sides["away"]["team"].get("displayName", ""), "score": to_int(sides["away"].get("score")) or 0},
                    "corners": corners,
                    "goals": goals,
                }
            except Exception:
                continue
    json.dump(out, sys.stdout)


def h2h_games(league_id, home_id, away_id, skip_id="", count=6):
    """The last finished meetings of two teams, newest first (see head_to_head)."""
    domestic = domestic_index()
    leagues = []
    for candidate in (league_id, domestic.get(home_id), domestic.get(away_id), "uefa.champions", "uefa.europa", "uefa.europa.conf"):
        if candidate and candidate not in leagues:
            leagues.append(candidate)
    year = dt.datetime.now().year
    jobs = [(league, season) for league in leagues for season in range(year, year - 7, -1)]

    def load(job):
        league, season = job
        data = fetch(f"{SITE}/{league}/teams/{home_id}/schedule?season={season}", 6 * 3600, f"h2h_{league}_{home_id}_{season}")
        return league, (data or {}).get("events", [])

    def score(competitor):
        value = competitor.get("score")
        if isinstance(value, dict):
            value = value.get("value", value.get("displayValue"))
        return int(float(value or 0))

    games = {}
    with ThreadPoolExecutor(max_workers=12) as pool:
        for league, events in pool.map(load, jobs):
            for event in events:
                try:
                    competition = event["competitions"][0]
                    if competition.get("status", {}).get("type", {}).get("state") != "post":
                        continue
                    sides = {c["team"]["id"]: c for c in competition["competitors"]}
                    if away_id not in sides or home_id not in sides or event["id"] == skip_id:
                        continue
                    games[event["id"]] = {
                        "date": event["date"],
                        "competition": event.get("seasonType", {}).get("name", "") or BY_ID.get(league, {}).get("name", ""),
                        "home": {"id": competition["competitors"][0]["team"]["id"] if competition["competitors"][0].get("homeAway") == "home" else competition["competitors"][1]["team"]["id"]},
                        "sides": {tid: {"name": c["team"].get("displayName", ""), "score": score(c), "homeAway": c.get("homeAway", "")} for tid, c in sides.items()},
                    }
                except Exception:
                    continue
    ordered = sorted(games.values(), key=lambda g: g["date"], reverse=True)[:count]
    out = []
    for game in ordered:
        home_side = next((tid for tid, v in game["sides"].items() if v["homeAway"] == "home"), home_id)
        away_side = next((tid for tid in game["sides"] if tid != home_side), away_id)
        out.append({
            "date": game["date"],
            "competition": game["competition"],
            "home": {"id": home_side, **game["sides"][home_side]},
            "away": {"id": away_side, **game["sides"][away_side]},
        })
    return out


def head_to_head(league_id, home_id, away_id, skip_id="", count=6):
    """The last finished meetings of two teams, newest first.

    ESPN only lists a team's games per competition and season, so the home team's calendar is read for
    the match's own competition, both teams' domestic leagues and the European cups, over the last seasons.
    """
    json.dump({"games": h2h_games(league_id, home_id, away_id, skip_id, count)}, sys.stdout)


def standings(league_id):
    data = fetch(f"https://site.api.espn.com/apis/v2/sports/soccer/{league_id}/standings", 3 * 3600, f"standings_{league_id}")
    groups = []
    for child in (data or {}).get("children", []):
        rows = []
        for entry in child.get("standings", {}).get("entries", []):
            flat = {stat.get("name"): stat.get("value") for stat in entry.get("stats", [])}
            team = entry.get("team", {})
            logos = team.get("logos") or []
            rows.append({
                "rank": int(flat.get("rank") or 0),
                "id": str(team.get("id", "")),
                "name": team.get("displayName", ""),
                "abbr": team.get("abbreviation", ""),
                "logo": logos[0].get("href", "") if logos else "",
                "p": int(flat.get("gamesPlayed") or 0),
                "w": int(flat.get("wins") or 0),
                "d": int(flat.get("ties") or 0),
                "l": int(flat.get("losses") or 0),
                "gf": int(flat.get("pointsFor") or 0),
                "ga": int(flat.get("pointsAgainst") or 0),
                "gd": int(flat.get("pointDifferential") or 0),
                "pts": int(flat.get("points") or 0),
            })
        rows.sort(key=lambda row: row["rank"] or 999)
        groups.append({"name": child.get("name", ""), "rows": rows})
    json.dump({"league": league_id, "groups": groups}, sys.stdout)


def parse_iso(value):
    return dt.datetime.fromisoformat(value.replace("Z", "+00:00"))


def match_corners(league_id, event_id):
    data = fetch(f"{SITE}/{league_id}/summary?event={event_id}", -1, f"summary_{event_id}")
    result = {"home": None, "away": None}
    try:
        for team in data["boxscore"]["teams"]:
            for stat in team.get("statistics", []):
                if stat.get("name") == "wonCorners":
                    result[team["homeAway"]] = int(float(stat.get("displayValue") or 0))
    except Exception:
        pass
    return result


def live_update():
    path = os.path.join(CACHE, "matches.json")
    try:
        with open(path, encoding="utf-8") as handle:
            cached = json.load(handle)
    except Exception:
        json.dump({"updates": []}, sys.stdout)
        return
    now = dt.datetime.now(dt.timezone.utc)
    pending = set()
    for match in cached.get("matches", []):
        kickoff = parse_iso(match["date"])
        if match["state"] == "in" or (match["state"] == "pre" and now - dt.timedelta(hours=4) < kickoff <= now + dt.timedelta(minutes=2)):
            pending.add(match["league"]["id"])
    index = {m["id"]: m for m in cached.get("matches", [])}
    updates = []
    stamps = [now.strftime("%Y%m%d"), (now - dt.timedelta(days=1)).strftime("%Y%m%d")]

    def load(job):
        league_id, stamp = job
        return league_id, fetch(f"{SITE}/{league_id}/scoreboard?dates={stamp}", -1, f"live_{league_id}_{stamp}")

    jobs = [(league_id, stamp) for league_id in sorted(pending) for stamp in stamps]
    with ThreadPoolExecutor(max_workers=8) as pool:
        for league_id, data in pool.map(load, jobs):
            for event in (data or {}).get("events", []):
                known = index.get(event["id"])
                if not known:
                    continue
                competition = event["competitions"][0]
                sides = {c["homeAway"]: c for c in competition["competitors"]}
                status = event.get("status", {}).get("type", {})
                known["state"] = status.get("state", known["state"])
                known["detail"] = status.get("shortDetail", "")
                known["home"]["score"] = sides["home"].get("score")
                known["away"]["score"] = sides["away"].get("score")
                updates.append({"id": event["id"], "league": league_id})

    seen = set()
    results = []
    for update in updates:
        if update["id"] in seen:
            continue
        seen.add(update["id"])
        match = index[update["id"]]
        corners = match_corners(update["league"], update["id"]) if match["state"] == "in" else {"home": None, "away": None}
        results.append({
            "id": match["id"],
            "state": match["state"],
            "detail": match["detail"],
            "home": {"score": match["home"]["score"]},
            "away": {"score": match["away"]["score"]},
            "corners": corners,
        })

    if results:
        tmp = path + ".tmp"
        with open(tmp, "w", encoding="utf-8") as handle:
            json.dump(cached, handle)
        os.replace(tmp, path)
    json.dump({"updates": results}, sys.stdout)


def main():
    today = dt.datetime.now().astimezone().date()
    events, seasons = build_matches(today)
    domestic = domestic_index()

    needed = {}
    for (league_id, _), (_, event) in events.items():
        for competitor in event["competitions"][0]["competitors"]:
            team_id = str(competitor["team"]["id"])
            season = seasons.get(league_id)
            if season is None:
                continue
            needed[(league_id, season, team_id)] = True
            home_league = domestic.get(team_id)
            if home_league and home_league != league_id:
                needed[(home_league, seasons.get(home_league, season), team_id)] = True

    stats = {}

    def load_stats(key):
        return key, team_stats(*key)

    with ThreadPoolExecutor(max_workers=16) as pool:
        for key, value in pool.map(load_stats, list(needed.keys())):
            stats[key] = value

    def best_stats(league_id, team_id):
        season = seasons.get(league_id)
        own = stats.get((league_id, season, team_id))
        home_league = domestic.get(team_id)
        domestic_stats = stats.get((home_league, seasons.get(home_league, season), team_id)) if home_league else None
        if domestic_stats and (not own or domestic_stats["games"] >= own["games"]):
            return domestic_stats, BY_ID[home_league]["goals"]
        return own, BY_ID[league_id]["goals"]

    matches = []
    for (league_id, event_id), (league, event) in events.items():
        competition = event["competitions"][0]
        sides = {c["homeAway"]: c for c in competition["competitors"]}
        if "home" not in sides or "away" not in sides:
            continue
        status = event.get("status", {}).get("type", {})
        row = {
            "id": event_id,
            "league": {"id": league_id, "name": league["name"], "country": league["country"]},
            "date": event["date"],
            "state": status.get("state", "pre"),
            "detail": status.get("shortDetail", ""),
        }
        for side in ("home", "away"):
            team = sides[side]["team"]
            row[side] = {
                "id": str(team["id"]),
                "name": team.get("displayName", ""),
                "abbr": team.get("abbreviation", ""),
                "logo": team.get("logo", ""),
                "score": sides[side].get("score"),
            }
        home_stats, home_avg = best_stats(league_id, row["home"]["id"])
        away_stats, away_avg = best_stats(league_id, row["away"]["id"])
        row["sample"] = {
            "home": int(home_stats["games"]) if home_stats else 0,
            "away": int(away_stats["games"]) if away_stats else 0,
        }
        row["lowData"] = min(row["sample"]["home"], row["sample"]["away"]) < 4
        if home_stats and away_stats:
            avg = league["goals"]
            att_h = shrink(home_stats["gf"], home_stats["games"], home_avg) / home_avg
            def_h = shrink(home_stats["ga"], home_stats["games"], home_avg) / home_avg
            att_a = shrink(away_stats["gf"], away_stats["games"], away_avg) / away_avg
            def_a = shrink(away_stats["ga"], away_stats["games"], away_avg) / away_avg
            lam_h = min(3.8, max(0.35, avg * att_h * def_a * HOME_BOOST))
            lam_a = min(3.8, max(0.35, avg * att_a * def_h * AWAY_FACTOR))
            home, draw, away, o15, o25 = goals_probs(lam_h, lam_a)
            cf_h = shrink(home_stats["cf"], home_stats["games"], CORNERS_PER_TEAM) / CORNERS_PER_TEAM
            ca_h = shrink(home_stats["ca"], home_stats["games"], CORNERS_PER_TEAM) / CORNERS_PER_TEAM
            cf_a = shrink(away_stats["cf"], away_stats["games"], CORNERS_PER_TEAM) / CORNERS_PER_TEAM
            ca_a = shrink(away_stats["ca"], away_stats["games"], CORNERS_PER_TEAM) / CORNERS_PER_TEAM
            exp_c = min(14.0, max(6.0, CORNERS_PER_TEAM * 1.06 * cf_h * ca_a + CORNERS_PER_TEAM * 0.94 * cf_a * ca_h))
            extra = full_probs(lam_h, lam_a)
            row["probs"] = {
                "home": round(home, 4), "draw": round(draw, 4), "away": round(away, 4),
                "o15": round(o15, 4), "o25": round(o25, 4),
                "o35": round(extra["o35"], 4), "btts": round(extra["btts"], 4),
                "dc1x": round(extra["dc1x"], 4), "dcx2": round(extra["dcx2"], 4), "dc12": round(extra["dc12"], 4),
                "c75": round(negbin_over(exp_c, 7.5), 4),
                "c85": round(negbin_over(exp_c, 8.5), 4),
                "c95": round(negbin_over(exp_c, 9.5), 4),
                "c105": round(negbin_over(exp_c, 10.5), 4),
                "scores": extra["scores"],
            }
            row["season"] = {
                side_name: {
                    "games": int(st["games"]),
                    "gf": round(st["gf"], 2), "ga": round(st["ga"], 2),
                    "cf": round(st["cf"], 1), "ca": round(st["ca"], 1),
                }
                for side_name, st in (("home", home_stats), ("away", away_stats))
            }
            row["expected"] = {"home": round(lam_h, 2), "away": round(lam_a, 2), "corners": round(exp_c, 1)}
            row["pick"] = best_pick(row)
            row["safe"] = safest_pick(row)
        matches.append(row)

    matches.sort(key=lambda m: m["date"])
    localize_logos(matches)
    out = {
        "generated": dt.datetime.now(dt.timezone.utc).isoformat(),
        "leagues": [{"id": l["id"], "name": l["name"], "country": l["country"]} for l in LEAGUES],
        "matches": matches,
    }
    os.makedirs(CACHE, exist_ok=True)
    path = os.path.join(CACHE, "matches.json")
    with open(path + ".tmp", "w", encoding="utf-8") as handle:
        json.dump(out, handle)
    os.replace(path + ".tmp", path)
    json.dump(out, sys.stdout)


# --- The algorithm's pick (port of the Windows app's engine.best_pick) -----
# Among the options the model finds likely (>= PICK_MIN), the one that pays best. Without a real
# bookmaker price the model's own fair price is used. Options where the model sees far more
# probability than the bookmaker are dropped: in those the model is almost always the one that is wrong.

PICK_KEYS = ("home", "draw", "away", "o15", "o25", "btts_y", "c75", "c85", "c95")
# Rule v2 (24 Oct 2026). The first week of history (v1: PICK_MIN 0.60, MAX_DISAGREEMENT 1.3) showed that picks below
# 65% hit 53% while the model claimed 62%, and that the more the model disagreed with the bookmaker the worse it did.
RULE = "v2"
PICK_MIN = 0.65
HOUSE_MARGIN = 1.06
MAX_DISAGREEMENT = 1.10


def norm_probs(p):
    """The shell's probability names -> the market keys the bookmaker and the picks use."""
    out = {
        "home": p["home"], "draw": p["draw"], "away": p["away"],
        "1x": p["dc1x"], "x2": p["dcx2"], "12": p["dc12"],
        "o15": p["o15"], "o25": p["o25"], "o35": p["o35"],
        "u25": 1 - p["o25"], "u35": 1 - p["o35"],
        "btts_y": p["btts"], "btts_n": 1 - p["btts"],
    }
    for key in ("c75", "c85", "c95", "c105"):
        if key in p:
            out[key] = p[key]
    return out


def pick_label(key, home, away):
    fixed = {
        "draw": "Draw", "o15": "Over 1.5 goals", "o25": "Over 2.5 goals", "o35": "Over 3.5 goals",
        "u25": "Under 2.5 goals", "u35": "Under 3.5 goals", "btts_y": "Both teams score",
        "btts_n": "Not both teams score", "c75": "Over 7.5 corners", "c85": "Over 8.5 corners",
        "c95": "Over 9.5 corners", "c105": "Over 10.5 corners",
    }
    if key in fixed:
        return fixed[key]
    return {"home": f"{home} wins", "away": f"{away} wins", "1x": f"{home} or draw",
            "x2": f"Draw or {away}", "12": f"{home} or {away}"}[key]


def pick_options(match, prices=None):
    """Every pickable market with its probability and price (the bookmaker's when known, the fair price otherwise)."""
    probs = norm_probs(match["probs"])
    options = []
    for key in PICK_KEYS:
        prob, price = probs.get(key, 0), (prices or {}).get(key)
        if prob <= 0 or (price and prob > MAX_DISAGREEMENT / price / HOUSE_MARGIN):
            continue
        options.append({"key": key, "prob": prob, "price": price or round(1 / prob / HOUSE_MARGIN, 2), "real": bool(price)})
    if not options:
        key = max(PICK_KEYS, key=lambda item: probs.get(item, 0))
        options = [{"key": key, "prob": probs[key], "price": round(1 / probs[key] / HOUSE_MARGIN, 2), "real": False}]
    return options


def finish_pick(match, option):
    chosen = dict(option)
    chosen["prob"] = round(chosen["prob"], 4)
    chosen["label"] = pick_label(chosen["key"], match["home"]["name"], match["away"]["name"])
    return chosen


def best_pick(match, prices=None):
    """Among the options the model finds likely, the one that pays best. None when nothing reaches PICK_MIN:
    a match with no convincing option simply gets no pick."""
    likely = [o for o in pick_options(match, prices) if o["prob"] >= PICK_MIN]
    return finish_pick(match, max(likely, key=lambda o: o["price"])) if likely else None


def safest_pick(match, prices=None):
    """The most probable of the same options. It pays little, but it is the one least likely to fail."""
    return finish_pick(match, max(pick_options(match, prices), key=lambda o: (o["prob"], o["price"])))


# --- Track record of the algorithm's pick -----------------------------------
# Each match's pick is saved before it starts and compared with the final score afterwards.
# Predictions are only ever saved before kickoff: they can't be rebuilt for past matches,
# because the model's inputs (season stats) have moved on since.

HISTORY = os.path.expanduser("~/.local/share/end4pC/tools/score-history.json")
PICKS_FILE = os.path.join(CACHE, "picks.json")


def to_int(value):
    if isinstance(value, dict):
        value = value.get("value", value.get("displayValue"))
    try:
        return int(float(value))
    except (TypeError, ValueError):
        return None


def pick_hit(key, result):
    """True/False if the pick was right; None while the data it needs (corners) is missing."""
    home, away = result["home"], result["away"]
    total = home + away
    if key.startswith("c") and key[1:].isdigit():
        if result.get("corners") is None:
            return None
        return result["corners"] > float(key[1:-1] + "." + key[-1])
    return {
        "home": home > away, "draw": home == away, "away": home < away,
        "1x": home >= away, "x2": away >= home, "12": home != away,
        "o15": total > 1.5, "o25": total > 2.5, "o35": total > 3.5,
        "u25": total < 2.5, "u35": total < 3.5,
        "btts_y": home > 0 and away > 0, "btts_n": not (home > 0 and away > 0),
    }[key]


def match_corners(league_id, event_id):
    data = fetch(f"{SITE}/{league_id}/summary?event={event_id}", -1, f"summary_{event_id}")
    result = {"home": None, "away": None}
    try:
        for team in data["boxscore"]["teams"]:
            for stat in team.get("statistics", []):
                if stat.get("name") == "wonCorners":
                    result[team["homeAway"]] = to_int(stat.get("displayValue"))
    except Exception:
        pass
    return result


def record_history(history, matches, picks, safes=None, options=None):
    changed = False
    for match in matches:
        entry = history.get(match["id"])
        if match["state"] == "pre" and match.get("probs"):
            pick = picks[match["id"]] if match["id"] in picks else match.get("pick")
            if pick is None:
                # The rule leaves this match without a pick: a pick saved earlier for it, not yet played, goes away
                if entry and entry["result"] is None:
                    del history[match["id"]]
                    changed = True
                continue
            if entry and entry["pick"].get("real") and not pick.get("real"):
                continue  # don't replace a pick made with real prices by one without them
            fresh = {
                "date": match["date"], "league": match["league"]["id"], "leagueName": match["league"]["name"],
                "home": match["home"]["name"], "away": match["away"]["name"],
                "probs": {key: match["probs"][key] for key in ("home", "draw", "away")},
                "pick": pick, "result": None, "rule": RULE,
                # What the page showed next to the pick, and every option it chose from, to judge them later
                "safe": (safes or {}).get(match["id"]) or match.get("safe"),
                "options": (options or {}).get(match["id"]),
            }
            if fresh != entry:
                history[match["id"]] = fresh
                changed = True
        elif entry and entry["result"] is None and match["state"] == "post":
            home, away = to_int(match["home"].get("score")), to_int(match["away"].get("score"))
            if home is not None and away is not None:
                entry["result"] = {"home": home, "away": away}
                changed = True
    return changed


def settle_history(history):
    """Final scores (and corners for corner picks) of saved picks whose match should be over."""
    now = dt.datetime.now(dt.timezone.utc)
    groups = {}
    for match_id, entry in history.items():
        if entry["result"] is not None:
            continue
        kickoff = parse_iso(entry["date"])
        if kickoff > now - dt.timedelta(hours=2):
            continue
        for offset in (-1, 0):
            stamp = (kickoff + dt.timedelta(days=offset)).strftime("%Y%m%d")
            groups.setdefault((entry["league"], stamp), set()).add(match_id)

    def load(job):
        (league_id, stamp), ids = job
        data = fetch(f"{SITE}/{league_id}/scoreboard?dates={stamp}", 600, f"settle_{league_id}_{stamp}")
        found = {}
        for event in (data or {}).get("events", []):
            try:
                if event["id"] not in ids or event["status"]["type"]["state"] != "post":
                    continue
                sides = {c["homeAway"]: c for c in event["competitions"][0]["competitors"]}
                home, away = to_int(sides["home"].get("score")), to_int(sides["away"].get("score"))
                if home is not None and away is not None:
                    found[event["id"]] = {"home": home, "away": away}
            except Exception:
                continue
        return found

    changed = False
    with ThreadPoolExecutor(max_workers=8) as pool:
        for found in pool.map(load, list(groups.items())[:150]):
            for match_id, result in found.items():
                history[match_id]["result"] = result
                changed = True
        wanted = [i for i, e in history.items() if e["result"] is not None and e["pick"]["key"].startswith("c")
                  and e["result"].get("corners") is None]

        def corners(match_id):
            count = match_corners(history[match_id]["league"], match_id)
            return match_id, (count["home"] + count["away"] if None not in count.values() else None)

        for match_id, total in pool.map(corners, wanted[:60]):
            if total is not None:
                history[match_id]["result"]["corners"] = total
                changed = True
    return changed


def history_summary(history):
    done = sorted((e for e in history.values() if e["result"]), key=lambda e: e["date"])
    hits = [(pick_hit(e["pick"]["key"], e["result"]), e) for e in done]
    hits = [h for h, _ in hits if h is not None]
    likely = [(max(("home", "draw", "away"), key=lambda k: e["probs"][k]), e) for e in done]
    hits_1x2 = [pick_hit(k, e["result"]) for k, e in likely]
    # The first week used rule v1 (entries without a "rule"); the stats of each rule are kept apart
    by_rule = {}
    for entry in done:
        hit = pick_hit(entry["pick"]["key"], entry["result"])
        if hit is None:
            continue
        slot = by_rule.setdefault(entry.get("rule", "v1"), {"n": 0, "hits": 0})
        slot["n"] += 1
        slot["hits"] += hit
    safe_hits = [pick_hit(e["safe"]["key"], e["result"]) for e in done if e.get("safe")]
    safe_hits = [h for h in safe_hits if h is not None]
    return {
        "n": len(hits), "hits": sum(hits),
        "n1x2": len(done), "hits1x2": sum(hits_1x2),
        "nRecent": len(hits[-30:]), "hitsRecent": sum(hits[-30:]),
        "byRule": by_rule, "safeN": len(safe_hits), "safeHits": sum(safe_hits),
    }


def history_data():
    def read(path, default):
        try:
            with open(path, encoding="utf-8") as handle:
                return json.load(handle)
        except Exception:
            return default

    # The track record of the Quickshell page, if there is one, is the base; what was saved here goes over it
    history = read(os.path.expanduser("~/.config/pCshell/football-history.json"), {})
    history.update(read(HISTORY, {}))
    matches = read(os.path.join(CACHE, "matches.json"), {}).get("matches", [])
    stored = read(PICKS_FILE, {})
    picks = stored.get("picks", {})
    changed = record_history(history, matches, picks, stored.get("safe", {}), stored.get("options", {}))
    changed = settle_history(history) or changed
    if changed:
        os.makedirs(os.path.dirname(HISTORY), exist_ok=True)
        with open(HISTORY + ".tmp", "w", encoding="utf-8") as handle:
            json.dump(history, handle, ensure_ascii=False)
        os.replace(HISTORY + ".tmp", HISTORY)

    now = dt.datetime.now(dt.timezone.utc).isoformat()

    def row(match_id, entry):
        result, pick = entry["result"], entry["pick"]
        return {
            "id": match_id, "date": entry["date"], "league": entry.get("leagueName", ""),
            "home": entry["home"], "away": entry["away"],
            "label": pick["label"], "key": pick["key"], "prob": pick["prob"],
            "price": pick.get("price"), "real": pick.get("real", False), "result": result,
            "hit": pick_hit(pick["key"], result) if result else None,
        }

    past = sorted(((i, e) for i, e in history.items() if e["result"] or e["date"] < now), key=lambda x: x[1]["date"], reverse=True)[:300]
    coming = sorted(((i, e) for i, e in history.items() if not e["result"] and e["date"] >= now), key=lambda x: x[1]["date"])[:60]
    return {"summary": history_summary(history), "entries": [row(i, e) for i, e in past] + [row(i, e) for i, e in coming]}


def history_command():
    json.dump(history_data(), sys.stdout)


def history_tsv():
    """The track record for the page: one 'S' line with the totals, then one 'E' line per saved pick."""
    data = history_data()
    sm = data["summary"]
    out = ["S\t" + "\t".join(str(sm[k]) for k in ("n", "hits", "n1x2", "hits1x2", "nRecent", "hitsRecent", "safeN", "safeHits"))]
    for e in data["entries"]:
        result = e["result"] or {}
        hit = "" if e["hit"] is None else ("1" if e["hit"] else "0")
        out.append("E\t" + "\t".join(str(v).replace("\t", " ") for v in (
            e["id"], e["date"], e["league"], e["home"], e["away"], e["label"], e["key"], e["prob"],
            e["price"] if e["price"] else "", 1 if e["real"] else 0,
            result.get("home", ""), result.get("away", ""), hit)))
    sys.stdout.write("\n".join(out) + "\n")


# --- The list of the Score page ------------------------------------------------
STALE_SECONDS = 30 * 60


def odds_picks():
    """With an odds provider set (--odds NAME), asks altenar.py for the algorithm's picks priced with the site's real odds.
    They are saved in picks.json, which the board and the track record read. Failing quietly leaves the model's own prices."""
    name = sys.argv[sys.argv.index("--odds") + 1] if "--odds" in sys.argv and sys.argv.index("--odds") + 1 < len(sys.argv) else ""
    if not name:
        return
    import subprocess
    env = dict(os.environ, SCORE_ODDS_INTEGRATION=name)
    site = sys.argv[sys.argv.index("--site") + 1] if "--site" in sys.argv and sys.argv.index("--site") + 1 < len(sys.argv) else ""
    if site:
        env["SCORE_ODDS_SITE"] = site
    try:
        subprocess.run([sys.executable, "-I", os.path.join(os.path.dirname(os.path.abspath(__file__)), "altenar.py"), "picks"],
                       env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=120)
    except Exception:
        pass


def board(cached_only, force=False):
    """The matches as tab separated lines, the format the page reads (it has no JSON parser).
    First line: '#' and the time the data was made. Then one line per match, fields in FIELDS order."""
    path = os.path.join(CACHE, "matches.json")
    fresh = not force and os.path.exists(path) and time.time() - os.path.getmtime(path) < STALE_SECONDS
    if not cached_only and not fresh:
        import contextlib
        with open(os.devnull, "w") as sink, contextlib.redirect_stdout(sink):
            main()
            odds_picks()
            try:
                history_data()  # saves the picks of the matches still to play and settles the ones that ended
            except Exception:
                pass
    try:
        with open(path, encoding="utf-8") as handle:
            data = json.load(handle)
    except Exception:
        return
    def clean(value):
        return str(value if value is not None else "").replace("\t", " ").replace("\n", " ")
    try:
        with open(PICKS_FILE, encoding="utf-8") as handle:
            stored = json.load(handle)
    except Exception:
        stored = {}
    try:
        ranks = positions_data()  # the place of every team in its league's table (cached for hours)
    except Exception:
        ranks = {}
    out = ["#\t" + clean(data.get("generated", "")) + "\t" + str(int(os.path.getmtime(path)))]
    for m in data.get("matches", []):
        probs = m.get("probs") or {}
        pick = stored.get("picks", {}).get(m["id"], m.get("pick")) or {}
        safe = stored.get("safe", {}).get(m["id"], m.get("safe")) or {}
        fields = [
            m["id"], m["league"]["id"], m["league"]["name"], m["league"]["country"], m["date"], m["state"], m.get("detail", ""),
            m["home"]["id"], m["home"]["name"], m["home"]["abbr"], m["home"]["logo"].replace("file://", ""), m["home"].get("score"),
            m["away"]["id"], m["away"]["name"], m["away"]["abbr"], m["away"]["logo"].replace("file://", ""), m["away"].get("score"),
            probs.get("home", ""), probs.get("draw", ""), probs.get("away", ""),
            pick.get("label", ""), pick.get("prob", ""), pick.get("key", ""),
            safe.get("label", ""), safe.get("prob", ""), safe.get("key", ""),
            pick.get("price") or "", 1 if pick.get("real") else 0, safe.get("price") or "", 1 if safe.get("real") else 0,
            (ranks.get(m["league"]["id"]) or {}).get(m["home"]["id"], ""), (ranks.get(m["league"]["id"]) or {}).get(m["away"]["id"], ""),
        ]
        out.append("\t".join(clean(f) for f in fields))
    sys.stdout.write("\n".join(out) + "\n")


# --- The detail of one match ---------------------------------------------------
def detail(match_id, local_only):
    """Tab separated records for the detail view; the first field of every line says what it is:
    P key value (a chance) | X home away corners (expected) | S side games gf ga cf ca (season averages) | G score chance (likely scores)
    K label chance key (the algorithm's pick) | F side result against ha score date (form) | H date competition home hs away as (meetings) | R side rank"""
    path = os.path.join(CACHE, "matches.json")
    try:
        with open(path, encoding="utf-8") as handle:
            matches = json.load(handle).get("matches", [])
    except Exception:
        return
    match = next((m for m in matches if m["id"] == match_id), None)
    if not match:
        return
    out = []
    for key, value in (match.get("probs") or {}).items():
        if key != "scores":
            out.append(f"P\t{key}\t{value}")
    expected = match.get("expected") or {}
    if expected:
        out.append(f"X\t{expected.get('home','')}\t{expected.get('away','')}\t{expected.get('corners','')}")
    for side, st in (match.get("season") or {}).items():
        out.append(f"S\t{side}\t{st['games']}\t{st['gf']}\t{st['ga']}\t{st['cf']}\t{st['ca']}")
    for sc in (match.get("probs") or {}).get("scores", []):
        out.append(f"G\t{sc['score']}\t{sc['p']}")
    pick = match.get("pick")
    if pick:
        out.append(f"K\t{pick.get('label','')}\t{pick.get('prob','')}\t{pick.get('key','')}")
    if not local_only:
        home_id, away_id, league_id = match["home"]["id"], match["away"]["id"], match["league"]["id"]
        with ThreadPoolExecutor(max_workers=3) as pool:
            form_job = pool.submit(form_data, league_id, [home_id, away_id])
            meetings_job = pool.submit(h2h_games, league_id, home_id, away_id, match_id, 6)
            ranks_job = pool.submit(positions_data)
            form, meetings, ranks = form_job.result(), meetings_job.result(), ranks_job.result()
        for side, team_id in (("home", home_id), ("away", away_id)):
            for game in form.get(team_id, []):
                out.append("F\t" + "\t".join([side, game["r"], game["vsName"], game["ha"], game["score"], game["date"]]))
            rank = (ranks.get(league_id) or {}).get(team_id)
            if rank:
                out.append(f"R\t{side}\t{rank}")
        for game in meetings:
            out.append("H\t" + "\t".join([game["date"], game["competition"], game["home"]["name"], str(game["home"]["score"]), game["away"]["name"], str(game["away"]["score"])]))
    sys.stdout.write("\n".join(out) + "\n")


if __name__ == "__main__":
    if "--record" in sys.argv:
        history_tsv()
    elif "--detail" in sys.argv:
        detail(sys.argv[sys.argv.index("--detail") + 1], "--local" in sys.argv)
    elif "--board" in sys.argv:
        board("--cached" in sys.argv, "--force" in sys.argv)
    elif "--form" in sys.argv:
        position = sys.argv.index("--form")
        team_form(sys.argv[position + 1], sys.argv[position + 2].split(","))
    elif "--history" in sys.argv:
        history_command()
    elif "--watch" in sys.argv:
        watch(json.loads(sys.argv[sys.argv.index("--watch") + 1]))
    elif "--h2h" in sys.argv:
        position = sys.argv.index("--h2h")
        head_to_head(sys.argv[position + 1], sys.argv[position + 2], sys.argv[position + 3],
                     sys.argv[position + 4] if len(sys.argv) > position + 4 and not sys.argv[position + 4].startswith("--") else "")
    elif "--positions" in sys.argv:
        positions()
    elif "--standings" in sys.argv:
        standings(sys.argv[sys.argv.index("--standings") + 1])
    elif "--live" in sys.argv:
        live_update()
    else:
        main()
