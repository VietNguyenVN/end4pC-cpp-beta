"""Asian handicap suggestion, built from the model and from how the two teams' previous meetings ended.

For a team T and a line L, the bet wins when (goals of T - goals of the rival + L) > 0, loses when < 0 and is
returned on 0. Quarter lines (-0.25, +0.75...) split the stake in two neighbouring lines.

How likely a line is to be covered mixes two sources:
  - the model's score matrix (Poisson with the Dixon-Coles correction), which is the prior
  - the real margins of the last meetings, each one counting like one more game of evidence
With K = 6 the model weighs as much as six meetings: a long history moves the answer, a short one barely does.
"""
import math

K = 6.0            # how many meetings the model is worth
MIN_COVER = 0.72   # a line is only suggested when it is covered at least this often
RHO = -0.08
STEP = 0.25
HOUSE_MARGIN = 1.06


def margin_distribution(lam_home, lam_away, kmax=10):
    """P(home goals - away goals = m) from the Dixon-Coles score matrix."""
    def pmf(mean):
        out = [math.exp(-mean)]
        for k in range(1, kmax + 1):
            out.append(out[-1] * mean / k)
        return out

    ph, pa = pmf(lam_home), pmf(lam_away)
    matrix = [[ph[i] * pa[j] for j in range(kmax + 1)] for i in range(kmax + 1)]
    matrix[0][0] *= max(0.0, 1 - lam_home * lam_away * RHO)
    matrix[0][1] *= max(0.0, 1 + lam_home * RHO)
    matrix[1][0] *= max(0.0, 1 + lam_away * RHO)
    matrix[1][1] *= max(0.0, 1 - RHO)
    total = sum(sum(row) for row in matrix)
    out = {}
    for i in range(kmax + 1):
        for j in range(kmax + 1):
            out[i - j] = out.get(i - j, 0.0) + matrix[i][j] / total
    return out


def sub_lines(line):
    """A quarter line is two bets of half the stake on the lines around it."""
    twice = round(line * 4)
    if twice % 2 == 0:
        return [(line, 1.0)]
    return [(line - STEP, 0.5), (line + STEP, 0.5)]


def outcome_weights(margin, line):
    """(win, lose) weight of one result for a bet on this line: pushes count for neither."""
    win = lose = 0.0
    for sub, weight in sub_lines(line):
        r = margin + sub
        if r > 1e-9:
            win += weight
        elif r < -1e-9:
            lose += weight
    return win, lose


def cover_rate(weights, line):
    """Share of decided stakes that win, over a {margin: weight} table."""
    win = lose = 0.0
    for margin, weight in weights.items():
        w, l = outcome_weights(margin, line)
        win += weight * w
        lose += weight * l
    return (win / (win + lose)) if win + lose > 0 else 0.0


def history_counts(margins, line):
    win = lose = 0.0
    for margin in margins:
        w, l = outcome_weights(margin, line)
        win += w
        lose += l
    return win, lose


def cover_probability(model_weights, margins, line):
    """Model and previous meetings together: the model is the prior, the meetings are the evidence."""
    p_model = cover_rate(model_weights, line)
    win, lose = history_counts(margins, line)
    return (K * p_model + win) / (K + win + lose), p_model, win, lose


def oriented(weights, side):
    """The same table seen from the away team: margins flip sign."""
    return weights if side == "home" else {-m: w for m, w in weights.items()}


def fmt_line(line):
    if abs(line) < 1e-9:
        return "0"
    return f"{line:+.2f}".rstrip("0").rstrip(".")


def suggest(match, lines, games):
    """The line to back, from the candidates the bookmaker offers (or a standard grid without it).

    lines: [{"side": "home"|"away", "line": float, "price": float, "key": str}]
    games: previous meetings as returned by score.h2h_games.
    Returns None when no line is covered often enough.
    """
    expected = match.get("expected") or {}
    if "home" not in expected or "away" not in expected:
        return None
    model = margin_distribution(float(expected["home"]), float(expected["away"]))
    home_id = match["home"]["id"]
    margins_home = []
    for game in games:
        mine = game["home"] if game["home"]["id"] == home_id else game["away"]
        theirs = game["away"] if mine is game["home"] else game["home"]
        margins_home.append(mine["score"] - theirs["score"])

    candidates = lines or [
        {"side": side, "line": step * STEP, "price": None, "key": f"ah_{side[0]}_{fmt_line(step * STEP)}"}
        for side in ("home", "away") for step in range(-10, 11)
    ]
    scored = []
    for entry in candidates:
        side, line = entry["side"], entry["line"]
        weights = oriented(model, side)
        margins = margins_home if side == "home" else [-m for m in margins_home]
        probability, p_model, win, lose = cover_probability(weights, margins, line)
        price = entry.get("price")
        scored.append({
            "key": entry["key"], "side": side, "line": line, "p": round(probability, 4), "pModel": round(p_model, 4),
            "price": price, "real": bool(price), "fair": round(1 / probability / HOUSE_MARGIN, 2) if probability > 0 else None,
            "covered": win, "missed": lose, "meetings": len(margins),
            "label": f"{match[side].get('abbr') or match[side]['name']} {fmt_line(line)}",
        })
    safe = [c for c in scored if c["p"] >= MIN_COVER]
    if not safe:
        return None
    best = max(safe, key=lambda c: (c["price"] or c["fair"] or 0, c["p"]))
    best["alternatives"] = sorted((c for c in safe if c is not best), key=lambda c: -(c["price"] or c["fair"] or 0))[:3]
    return best
