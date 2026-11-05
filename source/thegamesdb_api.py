import os
import requests

def thegamesdb_get_cover_url(game_name, api_key):
    """
    Search TheGamesDB for a game and return the cover image URL (if found).
    Requires a free API key from TheGamesDB.
    """
    search_url = "https://api.thegamesdb.net/v1.1/Games/ByGameName"
    params = {
        "apikey": api_key,
        "name": game_name,
        "fields": "players,overview,genres,boxart"
    }
    resp = requests.get(search_url, params=params)
    if resp.status_code != 200:
        return None
    data = resp.json()
    games = data.get("data", {}).get("games", [])
    if not games:
        return None
    game = games[0]
    boxarts = game.get("boxarts", {})
    # Prefer "front" boxart
    front = boxarts.get("front", [])
    if front:
        base_url = data.get("data", {}).get("base_url", {}).get("original", "")
        if base_url:
            return base_url + front[0]["filename"]
    # Fallback: any boxart
    for key in ("front", "back", "spine"):
        arts = boxarts.get(key, [])
        if arts:
            base_url = data.get("data", {}).get("base_url", {}).get("original", "")
            if base_url:
                return base_url + arts[0]["filename"]
    return None
