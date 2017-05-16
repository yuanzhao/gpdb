#!/bin/bash -l
set -exo pipefail

function test_package(){

  mkdir -p $2
  SKIP=`awk '/^__END_HEADER__/ {print NR + 1; exit 0; }' $1`
  tail -n +${SKIP} $1 | tar zxf - -C $2
  escape_path="${2//\//\\/}"
  sed_pattern='s/`pwd`/'$escape_path'/g'
  echo $sed_pattern
  sed -i -e $sed_pattern $2/greenplum_$3_path.sh
  source $2/greenplum_$3_path.sh
  psql -h localhost -p 5432 -U gpadmin -c "select version();" postgres
}
set +x && echo $STAGING_SERVER_KEY | base64 -d >key && set -x
chmod 400 key
ssh -M -S /tmp/tunnelsock  -fNT -L 5432:localhost:5432 -i key -o StrictHostKeyChecking=no gpadmin@$STAGING_SERVER_IP
export EXIT_CODE=1
trap "{ ssh -S /tmp/tunnelsock -O exit -i key gpadmin@$STAGING_SERVER_IP; exit $EXIT_CODE; }" EXIT

unzip installer_gpdb_clients/greenplum-clients-*.zip
client_bin_file=`ls greenplum-clients-*.bin`
client_path="/usr/local/clients"
test_package "$client_bin_file" "$client_path" clients

unzip installer_gpdb_loaders/greenplum-loaders-*.zip
loader_bin_file=`ls greenplum-loaders-*.bin`
loader_path="/usr/local/loaders"
test_package "$loader_bin_file" "$loader_path" loaders
export EXIT_CODE=0