#!/bin/sh
TFTP="/usr/bin/tftp -v -4 127.0.0.1 1234"
TFTPDIR=/tmp/tftpboot

set -u
set -e
set -x

cd ${PWD}

rm -rf ${TFTPDIR}
mkdir ${TFTPDIR}
chmod 755 ${TFTPDIR}
touch ${TFTPDIR}/test.data
chmod 600 ${TFTPDIR}/test.data


#TODO test exact blocksize upload
#TODO mkfile 500 test.txt


# NOTE: we start server at bg
# download must fail, dir not world readable
bin/tftpd_test 1234 &
${TFTP} -m binary -c get rules.ninja test.data || date && sleep 1

# upload should fail, file not world writeable
chmod 777 ${TFTPDIR}
# bin/tftpd_test 1234 &
# ${TFTP} -m binary -c put rules.ninja test.data || date && sleep 1

# must fail no such file
bin/tftpd_test 1234 &
${TFTP} -m binary -c get test.data || date && sleep 1


#######################################
# NOTE: we start client at bg
# normal binary upload
${TFTP} -m binary -c put rules.ninja &
bin/tftpd_test 1234
diff ${TFTPDIR}/rules.ninja rules.ninja
#######################################


# NOTE: we start server at bg
# download must fail
bin/tftpd_test 1234 &
touch ${TFTPDIR}/test.data
chmod 666 ${TFTPDIR}/test.data
${TFTP} -m binary -c get test.data || date && sleep 1

# ascii upload must fail
bin/tftpd_test 1234 &
${TFTP} -m ascii -c put rules.ninja || date && sleep 1

# relative path upload must fail
bin/tftpd_test 1234 &
${TFTP} -m binary -c put rules.ninja ../rules.ninja || date && sleep 1

# relative path upload must fail
bin/tftpd_test 1234 &
${TFTP} -m binary -c put rules.ninja ./../rules.ninja || date && sleep 1

# invalid absolut path upload must fail
bin/tftpd_test 1234 &
${TFTP} -m binary -c put rules.ninja ${PWD}/rules.ninja || date && sleep 1

# relative path to nonexisting subdir must fail
bin/tftpd_test 1234 &
${TFTP} -m binary -c put rules.ninja ./srv/tftp/rules.ninja || date && sleep 1


#######################################
# absolut path upload
bin/tftpd_test 1234 &
${TFTP} -m binary -c put rules.ninja ${TFTPDIR}/rules.ninja
diff ${TFTPDIR}/rules.ninja rules.ninja
sleep 5
#######################################


# test timeout
# port must be free
bin/tftpd_test 1234 ### || echo "OK"

# test help
bin/tftpd_test || echo "OK"

