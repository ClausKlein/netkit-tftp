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
touch ${TFTPDIR}/zero.data
chmod 400 ${TFTPDIR}/zero.data


# NOTE: we start server at bg
# download must fail, dir not world readable
bin/tftpd_test 1234 &
${TFTP} -m binary -c get rules.ninja zero.data || date && sleep 1

# upload should fail, file not world writeable
bin/tftpd_test 1234 &
${TFTP} -m binary -c put rules.ninja zero.data || date && sleep 1

# must fail no such file
chmod 777 ${TFTPDIR}
bin/tftpd_test 1234 &
${TFTP} -m binary -c get zero.dat || date && sleep 1


##############################################
# normal binary upload with dublicate ack's
bin/tftpd_test 1234 &
printf "verbose\ntrace\nbinary\nput rules.ninja\n" | bin/tftp 127.0.0.1 1234
diff ${TFTPDIR}/rules.ninja rules.ninja
sleep 5
##############################################
# test exact blocksize upload
dd if=/dev/random of=test1block.dat bs=512 count=1
dd if=bin/tftpd_test of=test1k.dat bs=1024 count=1
dd if=bin/tftpd_test of=test16k.dat bs=1024 count=16
bin/tftpd_test 1234 &
${TFTP} -m binary -c put test1block.dat
diff ${TFTPDIR}/test1block.dat test1block.dat
sleep 5
##############################################
bin/tftpd_test 1234 &
${TFTP} -m binary -c put test16k.dat
diff ${TFTPDIR}/test16k.dat test16k.dat
sleep 5
##############################################

# NOTE: we start server at bg
# download must fail
bin/tftpd_test 1234 &
touch ${TFTPDIR}/test16k.dat
chmod 666 ${TFTPDIR}/test16k.dat
${TFTP} -m binary -c get test16k.dat

# ascii upload must fail
${TFTP} -m ascii -c put rules.ninja &
bin/tftpd_test 1234 || date && sleep 1

# relative path upload must fail
${TFTP} -m binary -c put rules.ninja ../rules.ninja &
bin/tftpd_test 1234 || date && sleep 1

# relative path upload must fail
${TFTP} -m binary -c put rules.ninja ./../rules.ninja &
bin/tftpd_test 1234 || date && sleep 1

# invalid absolut path upload must fail
${TFTP} -m binary -c put rules.ninja ${PWD}/rules.ninja &
bin/tftpd_test 1234 || date && sleep 1

# relative path to nonexisting subdir must fail
${TFTP} -m binary -c put rules.ninja ./srv/tftp/rules.ninja &
bin/tftpd_test 1234 || date && sleep 1


##############################################
### absolut path upload
# dd if=bin/tftpd_test of=test32k.dat bs=1024 count=32
# bin/tftpd_test 1234 &
# ${TFTP} -m binary -c put test32k.dat ${TFTPDIR}/test32k.dat
# diff ${TFTPDIR}/test32k.dat test32k.dat
# sleep 5
##############################################


# test timeout
# port must be free
bin/tftpd_test 1234 ### || echo "OK"

# test help
bin/tftpd_test || echo "OK"

