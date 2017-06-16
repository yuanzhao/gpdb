set -euo pipefail

#store postgresql user info into pgbouncer

#config pgbouncer

override_psql_user(){
        cat > users.txt <<-UEOF
        "pgtest" "changeme"
        UEOF
}
override_pg_ini(){
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
        idfile = pgbouncer.pid
        admin_users = pgtest
        IEOF
}

function test_success_remote_login(){

	PGPORT=$1 PGPASSWORD=password1 $0/bin/psql -U ldapuser1 -t -c'select 1=1;'
}

function test_success_local_plain(){

	PGPORT=$1 PGPASSWORD=plain1 $0/bin/psql -U user1 -t -c'select 1=1;'
}

function test_fail_remotelogin(){

	PGPORT=$1 PGPASSWORD=xxxplain1 $0/bin/psql -U user1 -t -c'select 1=1;'
}

function test_wrong_passwd_remote(){
	###
	client recevie wrong password compared with the one is stored in LDAP server, use ldapusr1
	###
	PGPORT={1} PGPASSWORD=wwwwww {0}/bin/psql -U ldapusr1   -t -c 'select 1=1;'
	}
function test_user_not_in_remote(){
	###
	user is not stored in LDAP server although it has correct config in pgbouncer and GPDB
	###
	PGPORT={1} PGPASSWORD=wwwwww {0}/bin/psql -U ldapusr5  -t -c 'select 1=1;'
}	

function test_invalid_extserver(){
	###
	LDAP server in users.txt is wrong
	###
	PGPORT={1} PGPASSWORD=wwwwww {0}/bin/psql -U ldapusr3   -t -c 'select 1=1;'
}
function test_connection_refuse(){
	###
	LDAP server is not running
	###
	PGPORT={1} PGPASSWORD=wwwwww {0}/bin/psql -U ldapusr4   -t -c 'select 1=1;'
}
function test_wrong_passwd_pgbouncer(){
	###
	client get wrong password
	###
	PGPORT={1} PGPASSWORD=wwwwww {0}/bin/psql -U usr1   -t -c 'select 1=1;'
}
function test_user_not_in_pgbouncer(){
	###
	user has no config in users.txt
	###
	PGPORT={1} PGPASSWORD=wwwwww {0}/bin/psql -U usr4   -t -c 'select 1=1;'
}
function test_user_not_in_gpdb(){
	###
	user not exist in GPDB
	###
	PGPORT={1} PGPASSWORD=wwwwww {0}/bin/psql -U usr3   -t -c 'select 1=1;'
}
_main(){

}
