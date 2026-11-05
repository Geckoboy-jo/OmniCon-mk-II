import os
import requests

def igdb_get_cover_url(game_name, client_id, access_token):
    """
    Search IGDB for a game and return the cover image URL (if found).
    Requires a valid Twitch/IGDB client_id and access_token.
    """
    url = "https://api.igdb.com/v4/games"
    headers = {
        "Client-ID": client_id,
        "Authorization": f"Bearer {access_token}",
    }
    # IGDB query: search by name, return cover id
    data = f'search "{game_name}"; fields name,cover; limit 1;'
    resp = requests.post(url, headers=headers, data=data)
    if resp.status_code != 200:
        return None
    results = resp.json()
    if not results or not results[0].get("cover"):
        return None
    cover_id = results[0]["cover"]
    # Now fetch the cover image URL
    url = "https://api.igdb.com/v4/covers"
    data = f'fields url; where id = {cover_id};'
    resp = requests.post(url, headers=headers, data=data)
    if resp.status_code != 200:
        return None
    covers = resp.json()
    if not covers:
        return None
    # IGDB returns URLs with //images.igdb.com/..., prepend https:
    return "https:" + covers[0]["url"]
