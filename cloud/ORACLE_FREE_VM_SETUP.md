# Oracle Free VM Setup for OmniCon MySQL

This guide gets you a free Oracle Cloud VM with MySQL reachable only from your app IP.

## 1) One-time prerequisites (your laptop)

- Public client IP detected: `69.166.46.173`
- Recommended allowlist CIDR: `69.166.46.173/32`
- SSH key generated:
  - Private key: `~/.ssh/omnicon_oracle_free`
  - Public key: `~/.ssh/omnicon_oracle_free.pub`

## 2) In Oracle Console, collect IDs

1. Create/select compartment for this project.
2. Copy these values:
   - Compartment OCID (or tenancy OCID for root compartment)
   - Region code (example: `us-phoenix-1`)

Note: You can skip subnet creation entirely. The script can create VCN/subnet and lock security rules automatically.

## 3) In Oracle Cloud Shell, install tools

```bash
sudo dnf -y install jq
# If oci CLI is not already available in Cloud Shell:
# Follow Oracle's quick setup or run: oci setup config
```

## 4) Provision VM (Cloud Shell)

```bash
cd OmniCon-mk-II/cloud
chmod +x provision_oracle_vm.sh

# If using Cloud Shell, create key file there from your local public key text.
# Paste the full content of ~/.ssh/omnicon_oracle_free.pub between quotes:
echo "ssh-ed25519 REPLACE_WITH_YOUR_PUBLIC_KEY omnicon-cloud" > "$HOME/omnicon_oracle_free.pub"

export REGION="us-sanjose-1"
export COMPARTMENT_OCID="ocid1.tenancy.oc1..REPLACE_OR_USE_COMPARTMENT_OCID"
export SSH_PUB_KEY_PATH="$HOME/omnicon_oracle_free.pub"
export ALLOWED_CIDR="69.166.46.173/32"

# Optional: if you already have a public subnet, set SUBNET_OCID.
# export SUBNET_OCID="ocid1.subnet.oc1..REPLACE"

./provision_oracle_vm.sh
```

Save output values:
- `INSTANCE_OCID=...`
- `PUBLIC_IP=...`

## 5) Configure MySQL on VM

From your local machine:

```bash
scp -i ~/.ssh/omnicon_oracle_free cloud/configure_mysql_vm.sh opc@<PUBLIC_IP>:~/
ssh -i ~/.ssh/omnicon_oracle_free opc@<PUBLIC_IP>
chmod +x ~/configure_mysql_vm.sh

export ALLOWED_CIDR="69.166.46.173/32"
export MYSQL_APP_PASSWORD="REPLACE_WITH_STRONG_PASSWORD"
./configure_mysql_vm.sh
```

## 6) Import schema from project

From your local machine:

```bash
scp -i ~/.ssh/omnicon_oracle_free sql/secure_block_db.sql opc@<PUBLIC_IP>:~/
ssh -i ~/.ssh/omnicon_oracle_free opc@<PUBLIC_IP>

# Optional: if secure_block_db.sql contains a different host pattern,
# use only the table-creation section or update host/user entries before import.
sudo mysql < ~/secure_block_db.sql
```

## 7) App connection values

- Host: `<PUBLIC_IP>`
- Port: `3306`
- Database: `omnicon_blocks`
- User: `block_app`
- SSL: required/enabled

## Notes

- This is low-cost/free-tier oriented and depends on regional free capacity.
- Use Oracle block volumes or scheduled dumps for backups.
- Keep allowlist at `/32` whenever possible.
