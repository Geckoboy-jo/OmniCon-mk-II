# Making the OmniCon database public

Goal: let any user connect to the `omnicon` database from any host, using the
app's existing password. Two things must be true:

1. **Network path open** — Oracle Cloud security list + the VM's own firewall
   must allow inbound TCP **13306** from the internet (`0.0.0.0/0`).
2. **Account granted for any host** — the MySQL account must exist as
   `'omnicon'@'%'` (accounts are `'user'@'host'` pairs).

> These steps run on the **DB server / Oracle Cloud Console**, not on a client
> device. A blocked client cannot grant itself access.

---

## Step 1 — Open the network in the Oracle Cloud Console

1. Open **Compute → Instances** and click the DB instance.
2. Under **Instance details → Primary VNIC**, click the **Subnet** link.
3. On the subnet page, click the **Security List** (or the **Network Security
   Group** if the instance uses one).
4. **Add Ingress Rule:**
   - **Stateless:** No
   - **Source Type:** CIDR
   - **Source CIDR:** `0.0.0.0/0`  ← this is what makes it public
   - **IP Protocol:** TCP
   - **Destination Port Range:** `13306`
   - **Description:** `OmniCon MySQL public`
5. Save.

> To restrict later, change the Source CIDR to specific IPs instead of
> `0.0.0.0/0`.

## Step 2 — Open the VM's own firewall

Connect to the instance (OCI **Cloud Shell** with `ssh`, or the instance's
**Console connection**), then:

```sh
# Ubuntu/Debian (ufw)
sudo ufw allow 13306/tcp

# OR Oracle Linux (firewalld)
sudo firewall-cmd --permanent --add-port=13306/tcp
sudo firewall-cmd --reload
```

## Step 3 — Make sure MariaDB listens on all interfaces

On the server, edit the MariaDB config (commonly
`/etc/mysql/mariadb.conf.d/50-server.cnf`) and set:

```
bind-address = 0.0.0.0
```

Then restart:

```sh
sudo systemctl restart mariadb
```

## Step 4 — Grant the account for any host (password-verified)

Copy this repo onto the server (or just the script), then run it. It prompts
for the app password, **verifies it by logging in first**, and only grants if
the password is correct:

```sh
./sql/grant_omnicon_remote.sh        # grants 'omnicon'@'%'
```

Or provide the password non-interactively:

```sh
OMNICON_DB_PASSWORD='Geckoboy07!1word' ./sql/grant_omnicon_remote.sh '%'
```

Equivalent raw SQL (as an admin MySQL user), if you prefer:

```sql
CREATE USER IF NOT EXISTS 'omnicon'@'%' IDENTIFIED BY 'Geckoboy07!1word';
GRANT SELECT, INSERT, UPDATE, DELETE ON omnicon.* TO 'omnicon'@'%';
FLUSH PRIVILEGES;
```

## Step 5 — Verify

From any client device:

```sh
mysql -h 147.224.62.87 -P 13306 -u omnicon -p omnicon -e 'SELECT 1;'
```

A successful `SELECT 1` means the database is now public. Then launch OmniCon
normally — the `[MySQL] Connect failed` message should be gone.

---

### Security note

Opening to `0.0.0.0/0` means **anyone on the internet who has the password can
read and modify the database.** For a class project this is usually fine, but
use a strong password and consider restricting the Source CIDR in Step 1 once
you know which IPs need access.
