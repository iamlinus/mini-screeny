"""Football page: table position, next match and last result for the team in config.toml.

Source: the GraphQL API behind superettan.se / allsvenskan.se (https://gql.sportomedia.se/graphql),
the same data the official league sites show. No key. Polled at start and then once an hour.

The board has no clock, so everything time-related is computed here in Swedish time
("days to the next match", "lör 7 nov 15:00") and sent as ready-made text/numbers.
"""
import json, re, time, urllib.request
from datetime import datetime, timedelta, timezone
from zoneinfo import ZoneInfo

GQL = "https://gql.sportomedia.se/graphql"
LEAGUES = ["allsvenskan", "superettan"]       # whichever one the team is in this season
SE = ZoneInfo("Europe/Stockholm")
DAYS = ["mån", "tis", "ons", "tor", "fre", "lör", "sön"]
MONTHS = ["jan", "feb", "mar", "apr", "maj", "jun", "jul", "aug", "sep", "okt", "nov", "dec"]

Q_TABLE = """query S($l: String!, $y: Int!) {
  standingsForLeague(configLeagueName: $l, configSeasonStartYear: $y, type: "total") {
    standings { teamAbbrv teamName position stats { name value } } } }"""
Q_MATCHES = """query M($a: String!, $y: Int!) {
  matchesForTeam(abbrv: $a, configSeasonStartYear: $y) {
    matches { startDate status homeTeamName visitingTeamName homeTeamAbbrv visitingTeamAbbrv
              homeTeamScore visitingTeamScore leagueName } } }"""


def gql(query, variables):
    req = urllib.request.Request(GQL, data=json.dumps({"query": query, "variables": variables}).encode(),
                                 headers={"Content-Type": "application/json", "User-Agent": "mini-screeny-companion/1"})
    with urllib.request.urlopen(req, timeout=15) as r:
        body = json.load(r)
    if body.get("errors"):
        raise ValueError(body["errors"][0].get("message", "graphql error"))
    return body["data"]


_DROP = {"IF", "FF", "FK", "SK", "BOIS", "IK", "IFK", "GIF", "FC", "BK", "AIF", "FC."}


def short_name(name):
    """'Falkenbergs FF' -> 'Falkenberg', 'IFK Norrköping' -> 'Norrköping', 'Östers IF' -> 'Öster'."""
    words = [w for w in (name or "").split() if w.upper() not in _DROP] or (name or "?").split()
    out = " ".join(words)
    if len(words) == 1 and len(out) > 5 and out.endswith("s"):
        out = out[:-1]
    return out[:16]


def _when(dt):
    loc = dt.astimezone(SE)
    return f"{DAYS[loc.weekday()]} {loc.day} {MONTHS[loc.month - 1]} {loc:%H:%M}"


def _date(dt):
    loc = dt.astimezone(SE)
    return f"{loc.day} {MONTHS[loc.month - 1]}"


def _norm(s):
    return " ".join((s or "").lower().split())


def teams(year=None):
    """[(abbrv, name, league)] for every team in the leagues this season (or last season)."""
    year = year or datetime.now(SE).year
    for y in (year, year - 1):
        out = []
        for lg in LEAGUES:
            rows = (gql(Q_TABLE, {"l": lg, "y": y}).get("standingsForLeague") or {}).get("standings") or []
            out += [(r["teamAbbrv"], r.get("teamName") or r["teamAbbrv"], lg.capitalize()) for r in rows]
        if out:
            return out
    return []


_resolved = {}


def resolve(query):
    """config 'lag' (an abbreviation or a name, any case) -> league abbreviation, e.g. 'Helsingborgs IF' -> 'HIF'."""
    q = _norm(query)
    if q in _resolved:
        return _resolved[q]
    all_teams = teams()
    hit = (next((t for t in all_teams if _norm(t[0]) == q), None)
           or next((t for t in all_teams if _norm(t[1]) == q), None)
           or next((t for t in all_teams if q and q in _norm(t[1])), None)
           or next((t for t in all_teams if q and _norm(short_name(t[1])) == q), None))
    if not hit:
        raise ValueError(f"no team '{query}' in Allsvenskan/Superettan (list them: companion.py --lag)")
    _resolved[q] = hit[0]
    return hit[0]


def _parse(m, team):
    dt = datetime.fromisoformat(m["startDate"].replace("Z", "+00:00"))
    home = m["homeTeamAbbrv"] == team
    return {
        "dt": dt, "status": (m.get("status") or "").upper(), "home": home,
        "opp": short_name(m["visitingTeamName"] if home else m["homeTeamName"]),
        "gf": m["homeTeamScore"] if home else m["visitingTeamScore"],
        "ga": m["visitingTeamScore"] if home else m["homeTeamScore"],
        "league": m.get("leagueName") or "",
    }


def fetch(team_query, short=""):
    team = resolve(team_query)
    now = datetime.now(timezone.utc)
    year = now.astimezone(SE).year
    out = {"me": (short or team)[:8]}

    # table: this season, else last season (before the new season has started)
    for y in (year, year - 1):
        for lg in LEAGUES:
            rows = (gql(Q_TABLE, {"l": lg, "y": y}).get("standingsForLeague") or {}).get("standings") or []
            row = next((r for r in rows if r.get("teamAbbrv") == team), None)
            if row:
                stats = {s["name"]: s["value"] for s in row.get("stats") or []}
                out["table"] = {"pos": row["position"], "of": len(rows), "league": lg.capitalize(),
                                "pts": int(stats.get("pts", 0)), "gp": int(stats.get("gp", 0))}
                break
        if "table" in out:
            break

    matches = []
    for y in (year, year - 1, year + 1):
        try:
            ms = (gql(Q_MATCHES, {"a": team, "y": y}).get("matchesForTeam") or {}).get("matches") or []
        except Exception:
            ms = []
        matches += [_parse(m, team) for m in ms if m.get("startDate")]
    matches.sort(key=lambda m: m["dt"])

    done = [m for m in matches if m["status"].startswith("FINISHED")]
    if done:
        m = done[-1]
        out["last"] = {"opp": m["opp"], "home": m["home"], "gf": m["gf"], "ga": m["ga"], "date": _date(m["dt"])}

    # next match: the first one not finished, including one being played right now ("IDAG")
    upcoming = [m for m in matches if not m["status"].startswith("FINISHED") and m["dt"] > now - timedelta(hours=3)]
    if upcoming:
        m = upcoming[0]
        days = max(0, (m["dt"].astimezone(SE).date() - now.astimezone(SE).date()).days)
        out["next"] = {"opp": m["opp"], "home": m["home"], "days": days, "when": _when(m["dt"])}

    if len(out) == 1:
        return None
    return out
