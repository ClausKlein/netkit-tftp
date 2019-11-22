#!/bin/sh
TFTP=/usr/bin/tftp
TFTPDIR=/tmp/tftpboot

set -u
set -e
set -x

cd ${PWD}

rm -rf ${TFTPDIR}
mkdir ${TFTPDIR}
chmod 755 ${TFTPDIR}
touch ${TFTPDIR}/rules.ninja

# upload must fail, dir not world readable
${TFTP} 127.0.0.1 1234 -m binary -c put rules.ninja &
bin/tftpd_test 1234

# upload must fail, file not world writeable
chmod 777 ${TFTPDIR}
${TFTP} 127.0.0.1 1234 -m binary -c put rules.ninja &
bin/tftpd_test 1234

#TODO test exact blocksize upload
#TODO mkfile 500 test.txt

# no such file
bin/tftpd_test 1234 &
${TFTP} 127.0.0.1 1234 -m binary -c get test.txt

# normal binary upload
chmod 666 ${TFTPDIR}/rules.ninja
bin/tftpd_test 1234 &
${TFTP} 127.0.0.1 1234 -m binary -c put rules.ninja
diff ${TFTPDIR}/rules.ninja rules.ninja
sleep 5

# download must fail
bin/tftpd_test 1234 &
touch ${TFTPDIR}/test.txt
chmod 666 ${TFTPDIR}/test.txt
${TFTP} 127.0.0.1 1234 -m binary -c get test.txt

# ascii upload must fail
${TFTP} 127.0.0.1 1234 -m ascii -c put rules.ninja &

# relative path upload must fail
${TFTP} 127.0.0.1 1234 -m binary -c put rules.ninja ../rules.ninja &
${TFTP} 127.0.0.1 1234 -m binary -c put rules.ninja ./../rules.ninja &

# absolut path upload must fail
${TFTP} 127.0.0.1 1234 -m binary -c put rules.ninja ${PWD}/rules.ninja &

# test help
bin/tftpd_test || echo "OK"
# test timeout
bin/tftpd_test 1234 || echo "OK"

