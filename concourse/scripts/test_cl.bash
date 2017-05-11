#!/bin/bash -l
function extract_package(){

SKIP=`awk '/^__END_HEADER__/ {print NR + 1; exit 0; }' $1`
tail -n +${SKIP} $1 | tar zxf - -C $2
}
unzip installer_rhel6_gpdb_clients/greenplum-clients-*.zip
client_bin_file=`ls greenplum-clients-*.bin`
extract_package "$client_bin_file" "~/clients"

pushd
cd ~/clients
source greenplum-path.sh
popd

psql -h SMOKE_TEST_SERVER -p 5432 -U gpadmin -C "select version();" postgres
unzip installer_rhel6_gpdb_loaders/greenplum-loaders-*.zip
loader_bin_file=`ls greenplum-loaders-*.bin`
extract_package "$loader_bin_file" "~/loaders"
