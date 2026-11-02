#!/usr/bin/env bash
# Grant the OmniCon app account remote access to the `omnicon` database —
# but ONLY after verifying the password actually authenticates.
#
# Why: a client that fails with
#   ERROR 2013 (HY000): Lost connection to MySQL server at 'reading
#   authorization packet', system error: 104
# while other devices connect fine is being dropped because its public IP is
# not in the account's allowed host set (accounts are 'user'@'host' pairs).
#
# This script never grants access for an unverified password. It:
#   1. reads the app password from the environment (or prompts, hidden),
#   2. verifies it by logging in through the server's local socket as the
#      existing 'omnicon'@'localhost' account (or another existing host entry),
#   3. only if that login succeeds, creates/updates 'omnicon'@'<host>' with
#      the SAME password and grants it privileges.
#
# Run on the DB server (or a host that can reach it with admin rights):
#   ./sql/grant_omnicon_remote.sh                 # grant to '%' (any host)
#   ./sql/grant_omnicon_remote.sh 203.0.113.10    # grant to one client IP
#
# Required environment / input:
#   OMNICON_DB_PASSWORD  the app's DB password (prompted if unset)
#   ADMIN                admin login, default "root" (uses local socket auth)
#   DB                   database name, default "omnicon"

set -euo pipefail

GRANT_HOST="${1:-%}"
ADMIN="${ADMIN:-root}"
DB="${DB:-omnicon}"
APP_USER="omnicon"

log() { printf '[grant] %s\n' "$*"; }
die() { printf '[grant] ERROR: %s\n' "$*" >&2; exit 1; }

command -v mysql >/dev/null 2>&1 || die "mysql client not found."

# 1. Obtain the app password without echoing it.
if [ -z "${OMNICON_DB_PASSWORD:-}" ]; then
    read -r -s -p "App DB password for '$APP_USER': " OMNICON_DB_PASSWORD
    echo
fi
[ -n "$OMNICON_DB_PASSWORD" ] || die "empty password."

# Use a temp defaults file so the password never appears on the command line
# (where it would be visible in `ps`). Cleaned up on exit.
CRED=$(mktemp)
chmod 600 "$CRED"
trap 'rm -f "$CRED"' EXIT
printf '[client]\npassword=%s\n' "$OMNICON_DB_PASSWORD" > "$CRED"

# 2. Verify the password is correct BEFORE granting anything.
#    We authenticate as the existing account over the local socket. If the
#    password is wrong, this fails and we stop — no grant is made.
log "Verifying the password for '$APP_USER' before granting..."
if ! mysql --defaults-extra-file="$CRED" -u "$APP_USER" -e 'SELECT 1;' >/dev/null 2>&1; then
    die "Password verification failed for '$APP_USER'. Refusing to grant access. \
The password must match the existing '$APP_USER' account."
fi
log "Password verified."

# 3. Grant only now that the password is confirmed correct.
log "Granting '$APP_USER'@'$GRANT_HOST' access to '$DB' with the verified password..."
mysql --defaults-extra-file="$CRED" -u "$ADMIN" <<SQL
CREATE USER IF NOT EXISTS '$APP_USER'@'$GRANT_HOST' IDENTIFIED BY '$OMNICON_DB_PASSWORD';
ALTER USER '$APP_USER'@'$GRANT_HOST' IDENTIFIED BY '$OMNICON_DB_PASSWORD';
GRANT SELECT, INSERT, UPDATE, DELETE ON \`$DB\`.* TO '$APP_USER'@'$GRANT_HOST';
FLUSH PRIVILEGES;
SQL

log "Done. '$APP_USER'@'$GRANT_HOST' can now connect to '$DB'."
log "Also ensure the server bind-address is 0.0.0.0 and the firewall/NSG allows TCP 13306."
