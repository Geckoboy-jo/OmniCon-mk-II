# OmniCon-mk-II
Private repo

## Archive.org download cookies

The ROM downloader supports a Netscape-format cookie jar for Archive.org
downloads. Export cookies for `archive.org` from a browser into
`saves/archive_cookies.txt`, then launch OmniCon normally. The cookie file is
ignored by Git.

Export all cookies for the Archive.org page, not just a single preference or
donation cookie. The jar must include the browser's active Archive.org
session/authentication cookie; otherwise restricted downloads return HTTP 401.
OmniCon keeps refreshed cookies in the separate ignored file
`saves/archive_cookies.runtime.txt` and never rewrites the exported source jar.

To use a different cookie-jar location, set `OMNICON_COOKIE_FILE` before
starting the program:

```sh
export OMNICON_COOKIE_FILE=/path/to/archive-cookies.txt
```
