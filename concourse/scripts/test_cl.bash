#!/bin/bash -l
set -exo pipefail

function extract_package(){

  mkdir -p $2
  SKIP=`awk '/^__END_HEADER__/ {print NR + 1; exit 0; }' $1`
  tail -n +${SKIP} $1 | tar zxf - -C $2
  escape_path="${2//\//\\/}"
  sed -i -e "s/\\`pwd\\`/$escape_path/g"
}
unzip installer_rhel6_gpdb_clients/greenplum-clients-*.zip
client_bin_file=`ls greenplum-clients-*.bin`
client_path="/usr/local/clients"

extract_package "$client_bin_file" "$client_path"

source  $client_path/greenplum_clients_path.sh

echo $STAGING_SERVER_KEY | base64 -d >key
chmod 400 key
ssh -M -S /tmp/tunnelsock  -fNT -L 5432:localhost:5432 -i key -o StrictHostKeyChecking=no gpadmin@$STAGING_SERVER_IP
psql -h 35.166.116.126 -p 5432 -U gpadmin -c "select version();" postgres
ssh -S /tmp/tunnelsock -O exit -i key gpadmin@$STAGING_SERVER_IP
# unzip installer_rhel6_gpdb_loaders/greenplum-loaders-*.zip
# loader_bin_file=`ls greenplum-loaders-*.bin`
# loader_path="/usr/local/loaders"
# extract_package "$loader_bin_file" "$loader_path"

# source  $loader_path/greenplum_clients_path.sh

# psql -h SMOKE_TEST_SERVER -p 5432 -U gpadmin -c "select version();" postgres

