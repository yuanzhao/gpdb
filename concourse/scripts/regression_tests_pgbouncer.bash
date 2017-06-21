#!/bin/bash -l

set -exo pipefail

CWDIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
source "${CWDIR}/common.bash"

function gen_env(){
	cat > /home/gpadmin/run_regression_test.sh <<-EOF
	set -exo pipefail

	trap look4diffs ERR
	dir=\${1}

	function look4diffs() {

	    diff_files=\`find \${dir}/gpdb_src/gpAux/extensions/gphdfs/regression -name regression.diffs\`

	    for diff_file in \${diff_files}; do
		if [ -f "\${diff_file}" ]; then
		    cat <<-FEOF

					======================================================================
					DIFF FILE: \${diff_file}
					----------------------------------------------------------------------

					\$(cat "\${diff_file}")

				FEOF    
	    done
	    exit 1
	}

	source /opt/gcc_env.sh
	source /usr/local/greenplum-db-devel/greenplum_path.sh

	cd "\${1}/gpdb_src/gpAux"
	source gpdemo/gpdemo-env.sh


	EOF

	chown -R gpadmin:gpadmin $(pwd)
	chown gpadmin:gpadmin /home/gpadmin/run_regression_test.sh
	chmod a+x /home/gpadmin/run_regression_test.sh
}

function install_pgbouncer(){
	pushd gpdb_src/gpAux/extensions/pgbouncer
	./autogen.sh
	./configure --prefix=/usr/local/greenplum-db-devel --with-libevent=libevent-prefix
	make; make install
	popd
}
function setup_pgbouncer(){
	cat > pg.ini <<-IEOF
	[databases]
	template1 = host=127.0.0.1 port=15432 dbname=template1
	postgres = host=127.0.0.1 port=15432 dbname=postgres
	[pgbouncer]
	listen_port = 6543
	listen_addr = 127.0.0.1
	auth_type = plain
	auth_file = users.txt
	logfile = pgbouncer.log
	pidfile = pgbouncer.pid
	admin_users = pgtest
	IEOF

	cat > users.txt <<-UEOF
	"pgtest" "changeme"
	UEOF
        echo "host     all         pgtest         127.0.0.1/28    md5" >> $MASTER_DATA_DIRECTORY/pg_hba.conf 	
	su gpadmin -c "pgbouncer -d pg.ini"
	
}
function install_ldap(){
	wget ftp://ftp.openldap.org/pub/OpenLDAP/openldap-release/openldap-2.4.45.tgz
	tar -xvf openldap-2.4.45.tgz
	pushd openldap-2.4.45
	./configure
	make depend
	make; make install
	popd
}

function setup_ldap(){
	cat > slapd.ldif <<-LEOF
	dn: cn=config
	objectClass: olcGlobal
	cn: config
	olcArgsFile: /usr/local/var/run/slapd.args
	olcPidFile: /usr/local/var/run/slapd.pid

	dn: cn=schema,cn=config
	objectClass: olcSchemaConfig
	cn: schema

	include: file:///usr/local/etc/openldap/schema/core.ldif

	dn: olcDatabase=frontend,cn=config
	objectClass: olcDatabaseConfig
	objectClass: olcFrontendConfig
	olcDatabase: frontend


	dn: olcDatabase=mdb,cn=config
	objectClass: olcDatabaseConfig
	objectClass: olcMdbConfig
	olcDatabase: mdb
	olcSuffix: dc=my-domain,dc=com
	olcRootDN: cn=Manager,dc=my-domain,dc=com
	olcRootPW: secret
	olcDbDirectory:    /usr/local/var/openldap-data
	olcDbIndex: objectClass eq	
	LEOF
}
function run_regression_test() {
	
	su - gpadmin -c "bash /home/gpadmin/run_regression_test.sh $(pwd)"
}

function setup_gpadmin_user() {
	./gpdb_src/concourse/scripts/setup_gpadmin_user.bash "$TARGET_OS"
}

function _main() {
	if [ -z "$TARGET_OS" ]; then
		echo "FATAL: TARGET_OS is not set"
		exit 1
	fi

	if [ "$TARGET_OS" != "centos" -a "$TARGET_OS" != "sles" ]; then
		echo "FATAL: TARGET_OS is set to an invalid value: $TARGET_OS"
		echo "Configure TARGET_OS to be centos or sles"
		exit 1
	fi

	time configure
	sed -i s/1024/unlimited/ /etc/security/limits.d/90-nproc.conf
	time install_gpdb
	time setup_gpadmin_user
	time make_cluster
	time gen_env
	time install_ldap
	time setup_ldap
	time install_pgbouncer 
	time setup_pgbouncer
	time run_regression_test
}

_main "$@"
