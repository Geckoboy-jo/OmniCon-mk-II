#!/usr/bin/env bash
set -euo pipefail

# Configure MySQL server on Oracle Linux VM for OmniCon.
# Run this ON THE VM as a sudo-capable user.
#
# Required environment variables:
#   ALLOWED_CIDR      e.g. 69.166.46.173/32
#   MYSQL_APP_PASSWORD strong random password
#
# Optional environment variables:
#   MYSQL_APP_USER    default: block_app
#   MYSQL_DB          default: omnicon_blocks

: "${ALLOWED_CIDR:?Set ALLOWED_CIDR}"
: "${MYSQL_APP_PASSWORD:?Set MYSQL_APP_PASSWORD}"

MYSQL_APP_USER="${MYSQL_APP_USER:-block_app}"
MYSQL_DB="${MYSQL_DB:-omnicon_blocks}"

sudo dnf -y update
sudo dnf -y install mysql-server firewalld
sudo systemctl enable --now mysqld firewalld

# Bind MySQL on all interfaces but rely on strict firewall + user host scoping.
sudo sed -i 's/^bind-address.*/bind-address = 0.0.0.0/' /etc/my.cnf.d/mysql-server.cnf || true
if ! grep -q '^bind-address' /etc/my.cnf.d/mysql-server.cnf; then
  echo 'bind-address = 0.0.0.0' | sudo tee -a /etc/my.cnf.d/mysql-server.cnf >/dev/null
fi
sudo systemctl restart mysqld

# Open SSH and MySQL only in firewall. MySQL is restricted to ALLOWED_CIDR.
sudo firewall-cmd --permanent --add-service=ssh
sudo firewall-cmd --permanent --add-rich-rule="rule family='ipv4' source address='${ALLOWED_CIDR}' port protocol='tcp' port='3306' accept"
sudo firewall-cmd --permanent --add-rich-rule="rule family='ipv4' port protocol='tcp' port='3306' drop"
sudo firewall-cmd --reload

# Create DB and least-privilege app user over the exact allowed host IP.
APP_HOST="${ALLOWED_CIDR%/*}"

sudo mysql <<SQL
CREATE DATABASE IF NOT EXISTS ${MYSQL_DB}
  CHARACTER SET utf8mb4
  COLLATE utf8mb4_0900_ai_ci;

CREATE USER IF NOT EXISTS '${MYSQL_APP_USER}'@'${APP_HOST}' IDENTIFIED BY '${MYSQL_APP_PASSWORD}'
  REQUIRE SSL
  PASSWORD EXPIRE INTERVAL 180 DAY
  FAILED_LOGIN_ATTEMPTS 5
  PASSWORD_LOCK_TIME 2;

ALTER USER '${MYSQL_APP_USER}'@'${APP_HOST}' IDENTIFIED BY '${MYSQL_APP_PASSWORD}'
  REQUIRE SSL
  PASSWORD EXPIRE INTERVAL 180 DAY
  FAILED_LOGIN_ATTEMPTS 5
  PASSWORD_LOCK_TIME 2;

GRANT SELECT, INSERT, UPDATE, DELETE
ON ${MYSQL_DB}.*
TO '${MYSQL_APP_USER}'@'${APP_HOST}';

FLUSH PRIVILEGES;
SQL

echo "MySQL configured."
echo "DB=${MYSQL_DB} USER=${MYSQL_APP_USER} HOST_SCOPE=${APP_HOST}"
