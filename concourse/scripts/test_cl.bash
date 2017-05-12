#!/bin/bash -l
function extract_package(){

  SKIP=`awk '/^__END_HEADER__/ {print NR + 1; exit 0; }' $1`
  tail -n +${SKIP} $1 | tar zxf - -C $2
}
unzip installer_rhel6_gpdb_clients/greenplum-clients-*.zip
client_bin_file=`ls greenplum-clients-*.bin`
client_path="/usr/local/clients"
mkdir -p $client_path
extract_package "$client_bin_file" "$client_path"

cd $client_path
echo `ls`
source  greenplum_clients_path.sh
cd -

psql -h 35.166.116.126 -p 5432 -U gpadmin -c "select version();" postgres

unzip installer_rhel6_gpdb_loaders/greenplum-loaders-*.zip
loader_bin_file=`ls greenplum-loaders-*.bin`
loader_path="/usr/local/loaders"
mkdir -p $loader_path
extract_package "$loader_bin_file" "$loader_path"

cd $loader_path
echo `ls`
source  greenplum_clients_path.sh
cd -

psql -h SMOKE_TEST_SERVER -p 5432 -U gpadmin -c "select version();" postgres

