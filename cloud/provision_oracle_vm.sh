#!/usr/bin/env bash
set -euo pipefail

# Provision an Oracle Always Free VM suitable for hosting MySQL for OmniCon.
# Run this from Oracle Cloud Shell. If SUBNET_OCID is not provided, networking is created.
#
# Required environment variables:
#   REGION            e.g. us-phoenix-1
#   COMPARTMENT_OCID  e.g. ocid1.compartment.oc1..xxxx (root tenancy OCID also works)
#   SSH_PUB_KEY_PATH  e.g. ~/.ssh/omnicon_oracle_free.pub
#   ALLOWED_CIDR      e.g. 69.166.46.173/32
#
# Optional environment variables:
#   SUBNET_OCID       existing public subnet OCID (skip network creation if provided)
#   AD_NAME           e.g. Uocm:PHX-AD-1 (if unset, first AD is used)
#   INSTANCE_NAME     default: omnicon-mysql-free
#   SHAPE             default: VM.Standard.E2.1.Micro
#   IMAGE_OCID        if unset, script picks latest Oracle Linux image in region
#   VCN_CIDR          default: 10.0.0.0/16
#   SUBNET_CIDR       default: 10.0.1.0/24

: "${REGION:?Set REGION}"
: "${COMPARTMENT_OCID:?Set COMPARTMENT_OCID}"
: "${SSH_PUB_KEY_PATH:?Set SSH_PUB_KEY_PATH}"
: "${ALLOWED_CIDR:?Set ALLOWED_CIDR}"

INSTANCE_NAME="${INSTANCE_NAME:-omnicon-mysql-free}"
SHAPE="${SHAPE:-VM.Standard.E2.1.Micro}"
VCN_CIDR="${VCN_CIDR:-10.0.0.0/16}"
SUBNET_CIDR="${SUBNET_CIDR:-10.0.1.0/24}"

if ! command -v jq >/dev/null 2>&1; then
  echo "jq is required. Install jq and retry."
  exit 1
fi

if [[ -z "${AD_NAME:-}" ]]; then
  AD_NAME="$(oci iam availability-domain list \
    --region "$REGION" \
    --compartment-id "$COMPARTMENT_OCID" \
    --query 'data[0].name' --raw-output)"
fi

if [[ -z "${SUBNET_OCID:-}" ]]; then
  VCN_NAME="${INSTANCE_NAME}-vcn"
  IGW_NAME="${INSTANCE_NAME}-igw"
  RT_NAME="${INSTANCE_NAME}-rt"
  SL_NAME="${INSTANCE_NAME}-sl"
  SUBNET_NAME="${INSTANCE_NAME}-public-subnet"

  VCN_JSON="$(oci network vcn create \
    --region "$REGION" \
    --compartment-id "$COMPARTMENT_OCID" \
    --display-name "$VCN_NAME" \
    --cidr-block "$VCN_CIDR" \
    --wait-for-state AVAILABLE)"
  VCN_OCID="$(printf '%s' "$VCN_JSON" | jq -r '.data.id')"

  IGW_OCID="$(oci network internet-gateway create \
    --region "$REGION" \
    --compartment-id "$COMPARTMENT_OCID" \
    --vcn-id "$VCN_OCID" \
    --display-name "$IGW_NAME" \
    --is-enabled true \
    --query 'data.id' --raw-output)"

  RT_OCID="$(oci network route-table create \
    --region "$REGION" \
    --compartment-id "$COMPARTMENT_OCID" \
    --vcn-id "$VCN_OCID" \
    --display-name "$RT_NAME" \
    --route-rules "[{\"destination\":\"0.0.0.0/0\",\"destinationType\":\"CIDR_BLOCK\",\"networkEntityId\":\"${IGW_OCID}\"}]" \
    --query 'data.id' --raw-output)"

  SL_OCID="$(oci network security-list create \
    --region "$REGION" \
    --compartment-id "$COMPARTMENT_OCID" \
    --vcn-id "$VCN_OCID" \
    --display-name "$SL_NAME" \
    --egress-security-rules '[{"destination":"0.0.0.0/0","protocol":"all"}]' \
    --ingress-security-rules "[
      {\"source\":\"${ALLOWED_CIDR}\",\"protocol\":\"6\",\"tcpOptions\":{\"destinationPortRange\":{\"min\":22,\"max\":22}}},
      {\"source\":\"${ALLOWED_CIDR}\",\"protocol\":\"6\",\"tcpOptions\":{\"destinationPortRange\":{\"min\":3306,\"max\":3306}}}
    ]" \
    --query 'data.id' --raw-output)"

  SUBNET_OCID="$(oci network subnet create \
    --region "$REGION" \
    --compartment-id "$COMPARTMENT_OCID" \
    --vcn-id "$VCN_OCID" \
    --display-name "$SUBNET_NAME" \
    --cidr-block "$SUBNET_CIDR" \
    --route-table-id "$RT_OCID" \
    --security-list-ids "[\"${SL_OCID}\"]" \
    --prohibit-public-ip-on-vnic false \
    --wait-for-state AVAILABLE \
    --query 'data.id' --raw-output)"
fi

if [[ -z "${IMAGE_OCID:-}" ]]; then
  IMAGE_OCID="$(oci compute image list \
    --region "$REGION" \
    --compartment-id "$COMPARTMENT_OCID" \
    --operating-system "Oracle Linux" \
    --shape "$SHAPE" \
    --sort-by TIMECREATED --sort-order DESC \
    --query 'data[0].id' --raw-output)"
fi

INSTANCE_JSON="$(oci compute instance launch \
  --region "$REGION" \
  --compartment-id "$COMPARTMENT_OCID" \
  --availability-domain "$AD_NAME" \
  --display-name "$INSTANCE_NAME" \
  --shape "$SHAPE" \
  --subnet-id "$SUBNET_OCID" \
  --assign-public-ip true \
  --image-id "$IMAGE_OCID" \
  --metadata "{\"ssh_authorized_keys\":\"$(cat "$SSH_PUB_KEY_PATH")\"}" \
  --wait-for-state RUNNING)"

INSTANCE_OCID="$(printf '%s' "$INSTANCE_JSON" | jq -r '.data.id')"
VNIC_OCID="$(oci compute instance list-vnics \
  --region "$REGION" \
  --instance-id "$INSTANCE_OCID" \
  --query 'data[0].id' --raw-output)"
PUBLIC_IP="$(oci network vnic get \
  --region "$REGION" \
  --vnic-id "$VNIC_OCID" \
  --query 'data."public-ip"' --raw-output)"

echo "INSTANCE_OCID=$INSTANCE_OCID"
echo "SUBNET_OCID=$SUBNET_OCID"
echo "PUBLIC_IP=$PUBLIC_IP"
