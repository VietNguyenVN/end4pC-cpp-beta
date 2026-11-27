"""Value bets: where the bookmaker pays more than the market says a result is worth.

The market price is DraftKings', which ESPN publishes for most matches (1X2 and total goals at the main line).
Its margin is removed to get fair probabilities; value = the bookmaker price x fair probability - 1.
Our own model is NOT used here: on the history so far, the bets it called "value" lost money.
"""


def decimal(american):
    american = float(american)
    return 1 + american / 100 if american > 0 else 1 + 100 / -american


def fair(prices):
    """Removes the bookmaker margin with the power method: probabilities 1/price raised to an exponent chosen so
    they sum to 1. Unlike dividing by the total, it does not inflate long shots, where the margin is mostly hidden."""
    inverse = {k: 1 / p for k, p in prices.items()}
    low, high = 1.0, 8.0
    for _ in range(60):
        mid = (low + high) / 2
        if sum(v ** mid for v in inverse.values()) > 1:
            low = mid
        else:
            high = mid
    exponent = (low + high) / 2
    return {k: v ** exponent for k, v in inverse.items()}


def market_fair(summary):
    """{key: fair probability} from the match summary's DraftKings odds, using the keys of the picks."""
    entries = (summary.get("pickcenter") or summary.get("odds") or [])
    if not entries:
        return {}
    entry = entries[0]
    out = {}
    try:
        three = {
            "home": decimal(entry["homeTeamOdds"]["moneyLine"]),
            "draw": decimal(entry["drawOdds"]["moneyLine"]),
            "away": decimal(entry["awayTeamOdds"]["moneyLine"]),
        }
        out.update(fair(three))
    except Exception:
        pass
    try:
        line = float(entry["overUnder"])
        pair = fair({"over": decimal(entry["overOdds"]), "under": decimal(entry["underOdds"])})
        tag = str(line).replace(".", "")
        out[f"o{tag}"] = pair["over"]
        out[f"u{tag}"] = pair["under"]
    except Exception:
        pass
    return out


def edges(fair_probs, site_prices):
    """Value of every market both sides cover: [{key, price, fair, edge}], best first."""
    found = []
    for key, probability in fair_probs.items():
        price = site_prices.get(key)
        if price and price > 1:
            found.append({"key": key, "price": price, "fair": round(probability, 4), "edge": round(price * probability - 1, 4)})
    return sorted(found, key=lambda e: -e["edge"])


def label(key, home, away):
    if key in ("home", "away"):
        return f"{home if key == 'home' else away} wins"
    if key == "draw":
        return "Draw"
    side = {"o": "Over", "u": "Under"}.get(key[:1], "")
    digits = key[1:]
    if side and digits.isdigit() and len(digits) >= 2:
        return f"{side} {digits[:-1]}.{digits[-1]} goals"
    return key
