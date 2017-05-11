#!/bin/bash -l
function extract_package(){
	
SKIP=`awk '/^__END_HEADER__/ {print NR + 1; exit 0; }' $1`
tail -n +${SKIP} $1 | tar zxf - -C $2
}
unzip installer_rhel6_gpdb_clients/greenplum-clients-*.zip

extract_package($(ls installer_rhel6_gpdb_clients/greenplum-clients-*.bin), '~/clients')

cd ~/clients
source greenplum-path.sh

psql -h SMOKE_TEST_SERVER -C "select version();" postgres


unzip installer_rhel6_gpdb_loaders/greenplum-loaders-*.zip

extract_package($(ls installer_rhel6_gpdb_loaders/greenplum-loaders-*.bin), '~/loaders')
