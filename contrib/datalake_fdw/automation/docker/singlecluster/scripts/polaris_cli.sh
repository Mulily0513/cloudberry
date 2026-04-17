#!/bin/sh
set -e


API_HOST=${API_HOST:-http://singlecluster-polaris-1:8181}
REALM=${REALM:-POLARIS}
CATALOG_NAME=${CATALOG_NAME:-polaris_default_catalog}
NS_NAME=${NS_NAME:-public}
ROOT_CLIENT_ID=${ROOT_CLIENT_ID:-root}
ROOT_CLIENT_SECRET=${ROOT_CLIENT_SECRET:-s3cr3t}
S3_LOCATION=${S3_LOCATION:-s3://warehouse}
S3_ROLE_ARN=${S3_ROLE_ARN:-arn:aws:iam::000000000000:role/dummy}
S3_USER_ARN=${S3_USER_ARN:-arn:aws:iam::000000000000:user/dummy}
S3_REGION=${S3_REGION:-us-west-2}
S3_PATH_STYLE=${S3_PATH_STYLE:-true}
S3_ENDPOINT=${S3_ENDPOINT:-http://lakehouse:9100}
S3_ACCESS_KEY=${S3_ACCESS_KEY:-admin}
S3_SECRET_KEY=${S3_SECRET_KEY:-password}
ROLE_NAME=${ROLE_NAME:-service_admin}
CATALOG_ROLE_NAME=${CATALOG_ROLE_NAME:-catalog_admin}
# Polaris catalog-level privilege that allows purge (CATALOG_MANAGE_CONTENT covers DROP with purge)
GRANT_PRIVILEGE=${GRANT_PRIVILEGE:-CATALOG_MANAGE_CONTENT}

need() { command -v "$1" >/dev/null 2>&1 || { echo "Missing command: $1"; exit 1; }; }
need curl

# Use jq if available, otherwise fall back to grep/sed
if command -v jq >/dev/null 2>&1; then
  HAS_JQ=true
else
  HAS_JQ=false
fi

# Extract a simple top-level string field from JSON
# Usage: json_field "access_token" < json_input
json_field() {
  if [ "$HAS_JQ" = true ]; then
    jq -r ".$1"
  else
    sed -n 's/.*"'"$1"'"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p'
  fi
}

# Extract array of names from identifiers JSON (e.g. .identifiers[].name)
json_table_names() {
  if [ "$HAS_JQ" = true ]; then
    jq -r '.identifiers[]?.name // empty'
  else
    grep -o '"name"[[:space:]]*:[[:space:]]*"[^"]*"' | sed 's/"name"[[:space:]]*:[[:space:]]*"//;s/"//'
  fi
}

# Extract namespace names from namespaces JSON
json_namespace_names() {
  if [ "$HAS_JQ" = true ]; then
    jq -r '.namespaces[]? | if type == "array" then .[0] else .namespace[0] end'
  else
    # namespaces API returns {"namespaces":[["ns1"],["ns2"]]}
    grep -o '\["[^"]*"\]' | sed 's/\[\"//;s/\"\]//' | sort -u
  fi
}

# Pretty print JSON (best-effort without jq)
json_pretty() {
  if [ "$HAS_JQ" = true ]; then
    jq .
  else
    cat
  fi
}

get_token() {
  curl -s -X POST "$API_HOST/api/catalog/v1/oauth/tokens" \
    -d grant_type=client_credentials \
    -d client_id="$ROOT_CLIENT_ID" \
    -d client_secret="$ROOT_CLIENT_SECRET" \
    -d scope=PRINCIPAL_ROLE:ALL | json_field access_token
}

create_catalog() {
  TOKEN="$1"
  PAYLOAD=$(cat <<JSON
{
  "catalog": {
    "name": "$CATALOG_NAME",
    "type": "INTERNAL",
    "readOnly": false,
    "properties": {
      "default-base-location": "$S3_LOCATION",
      "polaris.config.drop-with-purge.enabled": "true",
      "s3.endpoint": "$S3_ENDPOINT",
      "s3.access-key-id": "$S3_ACCESS_KEY",
      "s3.secret-access-key": "$S3_SECRET_KEY",
      "s3.path-style-access": "true",
      "client.region": "$S3_REGION"
    },
    "storageConfigInfo": {
      "storageType": "S3",
      "allowedLocations": ["$S3_LOCATION"],
      "roleArn": "$S3_ROLE_ARN",
      "userArn": "$S3_USER_ARN",
      "region": "$S3_REGION",
      "pathStyleAccess": $S3_PATH_STYLE
    }
  }
}
JSON
)
  CODE=$(curl -s -o /tmp/cat_resp.json -w "%{http_code}" -X POST "$API_HOST/api/management/v1/catalogs" \
    -H "Authorization: Bearer $TOKEN" \
    -H "Polaris-Realm: $REALM" \
    -H "Content-Type: application/json" \
    -d "$PAYLOAD")
  if [ "$CODE" = "201" ] || [ "$CODE" = "409" ]; then
    echo "catalog $CATALOG_NAME ok (code $CODE)"
  else
    echo "catalog create failed code=$CODE"; cat /tmp/cat_resp.json; exit 1
  fi
}

grant_drop_with_purge() {
  TOKEN="$1"
  # Grant CATALOG_MANAGE_CONTENT to catalog role (allows DROP TABLE with purge)
  PAYLOAD=$(cat <<JSON
{
  "type": "catalog",
  "privilege": "$GRANT_PRIVILEGE"
}
JSON
)
  CODE=$(curl -s -o /tmp/grant_resp.json -w "%{http_code}" -X PUT \
    "$API_HOST/api/management/v1/catalogs/$CATALOG_NAME/catalog-roles/$CATALOG_ROLE_NAME/grants" \
    -H "Authorization: Bearer $TOKEN" \
    -H "Polaris-Realm: $REALM" \
    -H "Content-Type: application/json" \
    -d "$PAYLOAD")
  if [ "$CODE" -ge 200 ] && [ "$CODE" -lt 300 ]; then
    echo "granted $GRANT_PRIVILEGE to catalog-role $CATALOG_ROLE_NAME on catalog $CATALOG_NAME (code $CODE)"
  else
    echo "grant failed code=$CODE"; cat /tmp/grant_resp.json; exit 1
  fi
}

list_catalog_roles() {
  TOKEN="$1"
  curl -s -H "Authorization: Bearer $TOKEN" -H "Polaris-Realm: $REALM" \
    "$API_HOST/api/management/v1/catalogs/$CATALOG_NAME/catalog-roles" | json_pretty
}

list_grants() {
  TOKEN="$1"
  curl -s -H "Authorization: Bearer $TOKEN" -H "Polaris-Realm: $REALM" \
    "$API_HOST/api/management/v1/catalogs/$CATALOG_NAME/catalog-roles/$CATALOG_ROLE_NAME/grants" | json_pretty
}

create_namespace() {
  TOKEN="$1"

  create_catalog "$TOKEN"
  CODE=$(curl -s -o /tmp/ns_resp.json -w "%{http_code}" -X POST "$API_HOST/api/catalog/v1/$CATALOG_NAME/namespaces" \
    -H "Authorization: Bearer $TOKEN" \
    -H "Content-Type: application/json" \
    -H "Polaris-Realm: $REALM" \
    -d '{"namespace":["'"$NS_NAME"'"],"properties":{}}')
  if [ "$CODE" -ge 400 ]; then
    echo "namespace create failed code=$CODE"; cat /tmp/ns_resp.json; exit 1
  else
    echo "namespace $NS_NAME ok (code $CODE)"
  fi
}

list_catalogs() {
  TOKEN="$1"
  curl -s -H "Authorization: Bearer $TOKEN" -H "Polaris-Realm: $REALM" \
    "$API_HOST/api/management/v1/catalogs" | json_pretty
}

list_namespaces() {
  TOKEN="$1"
  curl -s -H "Authorization: Bearer $TOKEN" -H "Polaris-Realm: $REALM" \
    "$API_HOST/api/catalog/v1/$CATALOG_NAME/namespaces" | json_pretty
}

delete_catalog() {
  TOKEN="$1"
  FORCE="${2:-false}"
  
  if [ "$FORCE" = "true" ]; then
    # List and delete all tables in all namespaces first
    NAMESPACES=$(curl -s -H "Authorization: Bearer $TOKEN" -H "Polaris-Realm: $REALM" \
      "$API_HOST/api/catalog/v1/$CATALOG_NAME/namespaces" | json_namespace_names)

    for NS in $NAMESPACES; do
      [ -z "$NS" ] && continue
      echo "Cleaning namespace: $NS"
      TABLES=$(curl -s -H "Authorization: Bearer $TOKEN" -H "Polaris-Realm: $REALM" \
        "$API_HOST/api/catalog/v1/$CATALOG_NAME/namespaces/$NS/tables" | json_table_names)
      
      for TABLE in $TABLES; do
        [ -z "$TABLE" ] && continue
        echo "  Dropping table: $NS.$TABLE"
        DEL_CODE=$(curl -s -o /tmp/table_del.json -w "%{http_code}" -X DELETE \
          "$API_HOST/api/catalog/v1/$CATALOG_NAME/namespaces/$NS/tables/$TABLE" \
          -H "Authorization: Bearer $TOKEN" -H "Polaris-Realm: $REALM")
        [ "$DEL_CODE" -ge 400 ] && echo "    Failed: code=$DEL_CODE" && cat /tmp/table_del.json
      done
      
      # Delete the namespace itself
      echo "  Dropping namespace: $NS"
      NS_CODE=$(curl -s -o /tmp/ns_del.json -w "%{http_code}" -X DELETE \
        "$API_HOST/api/catalog/v1/$CATALOG_NAME/namespaces/$NS" \
        -H "Authorization: Bearer $TOKEN" -H "Polaris-Realm: $REALM")
      [ "$NS_CODE" -ge 400 ] && echo "    Failed: code=$NS_CODE" && cat /tmp/ns_del.json
    done
  fi
  
  CODE=$(curl -s -o /tmp/del_resp.json -w "%{http_code}" -X DELETE "$API_HOST/api/management/v1/catalogs/$CATALOG_NAME" \
    -H "Authorization: Bearer $TOKEN" \
    -H "Polaris-Realm: $REALM")
  if [ "$CODE" = "204" ] || [ "$CODE" = "404" ]; then
    echo "catalog $CATALOG_NAME deleted (code $CODE)"
  else
    echo "catalog delete failed code=$CODE"; cat /tmp/del_resp.json; exit 1
  fi
}

case "$1" in
  create-catalog)
    TOKEN=$(get_token); create_catalog "$TOKEN"
    ;;
  create-namespace)
    TOKEN=$(get_token); create_namespace "$TOKEN"
    ;;
  list-catalogs)
    TOKEN=$(get_token); list_catalogs "$TOKEN"
    ;;
  list-namespaces)
    TOKEN=$(get_token); list_namespaces "$TOKEN"
    ;;
  delete-catalog)
    TOKEN=$(get_token); delete_catalog "$TOKEN" "false"
    ;;
  delete-catalog-force)
    TOKEN=$(get_token); delete_catalog "$TOKEN" "true"
    ;;
  grant-drop-with-purge)
    TOKEN=$(get_token); grant_drop_with_purge "$TOKEN"
    ;;
  list-catalog-roles)
    TOKEN=$(get_token); list_catalog_roles "$TOKEN"
    ;;
  list-grants)
    TOKEN=$(get_token); list_grants "$TOKEN"
    ;;
  *)
    echo "use: $0 {create-catalog|create-namespace|list-catalogs|list-namespaces|delete-catalog|delete-catalog-force}"
    echo "           {grant-drop-with-purge|list-catalog-roles|list-grants}"
    echo "overwrite: API_HOST REALM CATALOG_NAME NS_NAME ROOT_CLIENT_ID ROOT_CLIENT_SECRET"
    echo "                 S3_LOCATION S3_ROLE_ARN S3_USER_ARN S3_REGION S3_PATH_STYLE"
    echo "                 S3_ENDPOINT S3_ACCESS_KEY S3_SECRET_KEY"
    echo "                 CATALOG_ROLE_NAME GRANT_PRIVILEGE"
    exit 1
    ;;
esac